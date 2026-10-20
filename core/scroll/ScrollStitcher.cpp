#include "core/scroll/ScrollStitcher.h"

#include <QHash>
#include <QPair>
#include <QSet>
#include <QtGlobal>
#include <algorithm>
#include <cstring>

namespace scroll {

namespace {

// A row must overlap at least this much for an offset to be considered at all, so a
// sliver of coincidentally-matching pixels can never pass for a scroll.
int minOverlapRows(int frameHeight) { return qMax(40, frameHeight / 8); }

// Per-channel difference below which two pixels count as equal. Absorbs the odd
// re-rendered antialiased edge without letting real content differences through.
constexpr int kChannelTolerance = 8;
// A row still counts as matching if at most this fraction of its pixels differ. This
// is what a moving scrollbar thumb costs: a dozen columns out of a thousand. The
// absolute floor keeps that true for a narrow region, where a ~16 px thumb would
// otherwise exceed the fraction and invalidate every row it touches.
constexpr double kRowDiffFraction = 0.05;
constexpr int kRowDiffFloorPixels = 24;
// ...or, failing that, the row still counts as matching when a WIDE CONTIGUOUS BAND of
// its pixels agrees. This is the column-wise twin of the run rule below, and it is what
// makes a page with sticky sidebars work: a navigation column on the left and a table of
// contents on the right stay put while the article between them scrolls, so at the
// article's true offset every row is spoiled by both sidebars, and at zero every row is
// spoiled by the article. Neither can pass a "few pixels may differ" test. What is
// actually true — and what this asks — is that one wide band of columns moved together.
constexpr double kMinColRunFraction = 0.35;
constexpr int kMinColRunFloorPixels = 64;
// How many hash-shortlisted offsets get the (much costlier) pixel scoring.
constexpr int kCandidates = 8;

// The measure both stages use: an offset is judged by the LONGEST CONTIGUOUS RUN of
// matching rows it produces, not by how many rows match overall — and the run must be at
// least minOverlapRows long to be believed. That is what makes a real window work: a
// captured region usually contains parts that do not scroll (an app's tabs and address
// bar, a pinned header, a status bar), and no single offset can explain those *and* the
// body. Counting all rows, the body and the chrome each veto the other's offset until
// every candidate looks bad. The scrolling viewport is by nature one contiguous band, so
// the longest run finds it and ignores the rest. The kMinColRun rule above is the same
// idea turned 90°, for parts that do not scroll because they are pinned COLUMNS.

// The shortlist is built per COLUMN STRIP, not per whole row. Hashing a whole row makes
// the fast stage blind in exactly the case stage 2 was taught to handle: with a sticky
// sidebar, no whole row is ever identical at the offset the article moved by, so the
// true offset scores zero here and never reaches stage 2 to be judged properly.
// Per-strip hashes ask the weaker, correct question — did SOME vertical band repeat.
constexpr int kStrips = 8;
// A canvas strip-hash seen more often than this is furniture — a blank row, a repeated
// rule — and says nothing about WHERE a frame sits, so the relocation vote ignores it.
constexpr int kMaxRepeats = 24;
// Fraction of a frame's rows that must match the canvas for a RELOCATION to be believed.
// Deliberately far stricter than the incremental path's minOverlapRows: a relocation may
// land anywhere in the image, so it has to be nearly certain, whereas an incremental
// match is already confined to a neighbouring offset.
constexpr double kRelocateMinRun = 0.80;
// ...and the share of the OVERLAP an incremental match must run for. Lower than the
// relocation bar, because a page's non-scrolling furniture (a header band) legitimately
// costs an offset part of its overlap — but high enough that a coincidental match at some
// unrelated offset cannot pass.
constexpr double kMinRunOfOverlap = 0.50;

// Fold the strip index into its hash so one map can index all strips.
quint64 stripKey(quint64 hash, int strip)
{
    return hash ^ (quint64(strip + 1) * 0x9E3779B97F4A7C15ULL);
}

// FNV-1a over each strip of each row. Indexed [row * kStrips + strip].
QVector<quint64> stripHashes(const QImage& img)
{
    const int w = img.width(), h = img.height();
    QVector<quint64> out(size_t(h) * kStrips);
    for (int y = 0; y < h; ++y) {
        const uchar* p = img.constScanLine(y);
        for (int s = 0; s < kStrips; ++s) {
            const int x0 = int(qint64(w) * s / kStrips);
            const int x1 = int(qint64(w) * (s + 1) / kStrips);
            quint64 v = 0xcbf29ce484222325ULL;
            for (int i = x0 * 4; i < x1 * 4; ++i) {
                v ^= p[i];
                v *= 0x100000001b3ULL;
            }
            out[size_t(y) * kStrips + s] = v;
        }
    }
    return out;
}

// Rows of `frame` that overlap the reference when frame row j sits at reference row
// j + dy: [j0, j1).
void overlapRange(int h, int dy, int* j0, int* j1)
{
    *j0 = qMax(0, -dy);
    *j1 = qMin(h, h - dy);
}

} // namespace

ScrollStitcher::ScrollStitcher(int maxHeight)
    : m_maxHeight(maxHeight)
{
}

void ScrollStitcher::reset()
{
    m_canvas = QImage();
    m_lastFrame = QImage();
    m_lastHashes.clear();
    m_canvasHashes.clear();
    m_liveOffset = 0;
    m_frames = 0;
}

int ScrollStitcher::trim(Side side)
{
    if (m_canvas.isNull() || m_lastFrame.isNull())
        return 0;
    const int canvasH = m_canvas.height();
    const int liveEnd = m_liveOffset + m_lastFrame.height();

    // Both cases keep the live frame whole, so the canvas can never end up shorter than a
    // frame — which is the invariant addFrame() relies on when it assumes a frame cannot
    // overhang both ends at once.
    const int cut = (side == Side::Above) ? m_liveOffset : canvasH - liveEnd;
    if (cut <= 0)
        return 0;

    if (side == Side::Above) {
        m_canvas = m_canvas.copy(0, cut, m_canvas.width(), canvasH - cut);
        m_canvasHashes = m_canvasHashes.mid(qsizetype(cut) * kStrips);
        m_liveOffset = 0;                      // the live frame is now the first row
    } else {
        m_canvas = m_canvas.copy(0, 0, m_canvas.width(), liveEnd);
        m_canvasHashes.resize(qsizetype(liveEnd) * kStrips);
    }
    return cut;
}

// One tolerant row comparison, shared by both scorers: equal enough if few pixels differ
// (rendering noise, a scrollbar thumb) OR a wide contiguous band agrees (a scrolling
// column between pinned ones).
static bool rowsMatch(const QRgb* a, const QRgb* b, int w, int maxBadPixels, int minColRun)
{
    if (std::memcmp(a, b, size_t(w) * 4) == 0)
        return true;
    int bad = 0, colRun = 0, bestColRun = 0;
    for (int x = 0; x < w; ++x) {
        const bool ok = qAbs(qRed(a[x])   - qRed(b[x]))   <= kChannelTolerance
                     && qAbs(qGreen(a[x]) - qGreen(b[x])) <= kChannelTolerance
                     && qAbs(qBlue(a[x])  - qBlue(b[x]))  <= kChannelTolerance;
        if (ok) bestColRun = qMax(bestColRun, ++colRun);
        else  { colRun = 0; ++bad; }
    }
    return bad <= maxBadPixels || bestColRun >= minColRun;
}

int ScrollStitcher::pixelRunAgainstCanvas(const QImage& frame, int top) const
{
    const int h = frame.height(), w = frame.width();
    const int j0 = qMax(0, -top);
    const int j1 = qMin(h, m_canvas.height() - top);
    if (j1 <= j0)
        return 0;
    const int maxBadPixels = qMin(w, qMax(kRowDiffFloorPixels, int(w * kRowDiffFraction)));
    const int minColRun = qMin(w, qMax(kMinColRunFloorPixels, int(w * kMinColRunFraction)));
    int run = 0, best = 0;
    for (int j = j0; j < j1; ++j) {
        const auto* a = reinterpret_cast<const QRgb*>(frame.constScanLine(j));
        const auto* b = reinterpret_cast<const QRgb*>(m_canvas.constScanLine(top + j));
        run = rowsMatch(a, b, w, maxBadPixels, minColRun) ? run + 1 : 0;
        best = qMax(best, run);
    }
    return best;
}

int ScrollStitcher::relocate(const QImage& frame, const QVector<quint64>& hashes) const
{
    const int h = frame.height();
    const int canvasH = m_canvas.height();
    const int minOverlap = minOverlapRows(h);
    if (canvasH <= 0 || m_canvasHashes.size() != size_t(canvasH) * kStrips)
        return kNoPosition;

    // Index the canvas by strip hash. Rows that repeat everywhere (blank ones) carry no
    // positional information and would swamp the vote, so they are dropped outright.
    QHash<quint64, QVector<int>> index;
    index.reserve(canvasH);
    for (int c = 0; c < canvasH; ++c)
        for (int s = 0; s < kStrips; ++s)
            index[stripKey(m_canvasHashes[size_t(c) * kStrips + s], s)].append(c);
    for (auto it = index.begin(); it != index.end();)
        it = (it.value().size() > kMaxRepeats) ? index.erase(it) : ++it;

    // Every surviving (row, strip) votes for the offset that would put it where it was
    // seen before. The true position collects a vote from most of the frame.
    QHash<int, int> votes;
    for (int j = 0; j < h; ++j) {
        for (int s = 0; s < kStrips; ++s) {
            const auto it = index.constFind(stripKey(hashes[size_t(j) * kStrips + s], s));
            if (it == index.constEnd())
                continue;
            for (int c : it.value())
                ++votes[c - j];
        }
    }
    if (votes.isEmpty())
        return kNoPosition;

    // Try the strongest few in pixel space — the vote narrows, the pixels decide, exactly
    // as in the incremental path. Ties go to the smaller move, for the same reason.
    QVector<QPair<int, int>> ranked;      // (votes, top)
    ranked.reserve(votes.size());
    for (auto it = votes.constBegin(); it != votes.constEnd(); ++it)
        ranked.append(qMakePair(it.value(), it.key()));
    std::sort(ranked.begin(), ranked.end(), [this](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first > b.first;
        return qAbs(a.second - m_liveOffset) < qAbs(b.second - m_liveOffset);
    });

    for (int i = 0; i < ranked.size() && i < kCandidates; ++i) {
        const int top = ranked[i].second;
        // Relocation re-finds a frame inside content already captured; it must NEVER
        // extend the capture. Only the incremental match may grow the canvas, because
        // only it is bounded — it can be wrong by at most an overlap. A relocation is
        // free to land anywhere, so allowing it to grow lets one bad guess write frames
        // at an arbitrary offset. Requiring full containment removes that entirely: a
        // wrong relocation can then cost at most a wrong marker, never a wrong image.
        if (top < 0 || top + h > canvasH)
            continue;
        // ...and the bar is far higher than the incremental path's. A frame genuinely
        // revisiting captured ground matches the canvas almost row for row, so demanding
        // most of it costs true positives nothing and starves false ones — which is what
        // let a downward overshoot get placed against similar-looking content ABOVE.
        if (pixelRunAgainstCanvas(frame, top) >= int(h * kRelocateMinRun))
            return top;
    }
    return kNoPosition;
}

int ScrollStitcher::pixelRun(const QImage& frame, int dy) const
{
    const int h = frame.height(), w = frame.width();
    int j0 = 0, j1 = 0;
    overlapRange(h, dy, &j0, &j1);
    if (j1 <= j0)
        return 0;

    const int maxBadPixels = qMin(w, qMax(kRowDiffFloorPixels, int(w * kRowDiffFraction)));
    const int minColRun = qMin(w, qMax(kMinColRunFloorPixels, int(w * kMinColRunFraction)));
    int run = 0, best = 0;
    for (int j = j0; j < j1; ++j) {
        const auto* a = reinterpret_cast<const QRgb*>(frame.constScanLine(j));
        const auto* b = reinterpret_cast<const QRgb*>(m_lastFrame.constScanLine(j + dy));
        run = rowsMatch(a, b, w, maxBadPixels, minColRun) ? run + 1 : 0;
        best = qMax(best, run);
    }
    return best;
}

ScrollStitcher::Match ScrollStitcher::findOffset(const QImage& frame,
                                                const QVector<quint64>& hashes) const
{
    const int h = frame.height();
    const int minOverlap = minOverlapRows(h);
    if (h < minOverlap)
        return {};

    // Stage 1 — rank offsets by TOTAL agreement: how many (row, strip) pairs repeat.
    //
    // It used to take the longest run of the single best strip, and that let one blank
    // column decide everything. A near-constant strip — a page margin — has the same hash
    // on almost every row, so it matches at every offset; taking the best strip handed a
    // perfect score to hundreds of meaningless offsets and crowded the true one out of
    // the shortlist entirely, so stage 2 never even scored it. Measured on a real page:
    // at the true offset all 8 strips agreed on all 459 overlapping rows, while a bogus
    // offset scored higher on the blank right margin alone. Summing over strips makes
    // that impossible — seven agreeing strips outweigh one — and needs no judgement about
    // which strips are "informative", which is a threshold that would need tuning per
    // page. Contiguity is not lost, only deferred: stage 2 still demands a run.
    // Scored as a FRACTION of the pairs on offer, never a raw count: a smaller offset
    // overlaps more rows and so has more chances to agree, which by itself ranks every
    // near-zero offset above the truth.
    struct Cand { int dy; double score; };
    QVector<Cand> cands;
    cands.reserve(2 * h);
    for (int dy = -(h - minOverlap); dy <= h - minOverlap; ++dy) {
        int j0 = 0, j1 = 0;
        overlapRange(h, dy, &j0, &j1);
        if (j1 - j0 < minOverlap)
            continue;
        int agree = 0;
        for (int s = 0; s < kStrips; ++s)
            for (int j = j0; j < j1; ++j)
                if (hashes[size_t(j) * kStrips + s]
                    == m_lastHashes[size_t(j + dy) * kStrips + s])
                    ++agree;
        cands.append({dy, double(agree) / (double(j1 - j0) * kStrips)});
    }
    if (cands.isEmpty())
        return {};

    // Stage 2 — re-score the most promising offsets in pixel space, with tolerance.
    // dy == 0 is always scored: it is the "nothing moved" answer, and it must be able
    // to win outright on content too uniform to place any other way.
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
        if (a.score != b.score) return a.score > b.score;
        return qAbs(a.dy) < qAbs(b.dy);          // deterministic, and prefers the smaller move
    });
    QVector<int> shortlist;
    shortlist.append(0);
    for (const Cand& c : cands) {
        if (shortlist.size() >= kCandidates + 1)
            break;
        if (!shortlist.contains(c.dy))
            shortlist.append(c.dy);
    }

    int bestDy = 0;
    int bestRun = -1;
    for (int dy : shortlist) {
        const int run = pixelRun(frame, dy);
        // Ties resolve to the SMALLER |dy|. On near-uniform content every offset scores
        // alike, and choosing the largest would fabricate content that was never seen.
        if (run > bestRun || (run == bestRun && qAbs(dy) < qAbs(bestDy))) {
            bestRun = run;
            bestDy = dy;
        }
    }

    // The run must be a real share of the OVERLAP, not merely longer than an absolute
    // floor. The floor alone is far too easy to clear by accident: at a large offset the
    // overlap is small, so a coincidental band of matching text or whitespace can satisfy
    // it and a wrong offset gets accepted — observed in the field as a downward scroll
    // matched as an upward one, prepending rows that were never there. When the true
    // offset is out of range (the page moved further than an overlap can span), no
    // candidate deserves to win, and saying so produces the honest "scroll back" warning.
    int j0 = 0, j1 = 0;
    overlapRange(h, bestDy, &j0, &j1);
    const int required = qMax(minOverlap, int((j1 - j0) * kMinRunOfOverlap));
    return {bestDy, bestRun >= required};
}

ScrollStitcher::Result ScrollStitcher::addFrame(const QImage& frame)
{
    if (frame.isNull() || frame.width() <= 0 || frame.height() <= 0)
        return Result::Rejected;

    const QImage f = frame.convertToFormat(QImage::Format_RGB32);
    const int w = f.width(), h = f.height();

    if (m_canvas.isNull()) {
        if (h > m_maxHeight)
            return Result::Capped;
        m_canvas = f.copy();
        m_lastFrame = f.copy();
        m_lastHashes = stripHashes(f);
        m_canvasHashes = m_lastHashes;
        m_liveOffset = 0;
        m_frames = 1;
        return Result::Appended;
    }
    if (f.size() != m_lastFrame.size())
        return Result::Rejected;

    const QVector<quint64> hashes = stripHashes(f);
    const Match m = findOffset(f, hashes);
    int newTop;
    if (m.ok) {
        newTop = m_liveOffset + m.dy;
    } else {
        // Nothing describes this frame in terms of the previous one — the page moved
        // further in one go than an overlap can span, which is what a flick back up looks
        // like. Everything it shows may still be in the canvas, so look for it there
        // outright. This is the only path that can recover a large jump; without it the
        // frame is dropped and the live position silently keeps its stale, lower value.
        newTop = relocate(f, hashes);
        if (newTop == kNoPosition)
            return Result::NoOverlap;
    }

    const int oldH = m_canvas.height();
    const int growTop = qMax(0, -newTop);
    const int growBottom = qMax(0, newTop + h - oldH);
    const int newH = oldH + growTop + growBottom;
    if (newH > m_maxHeight)
        return Result::Capped;

    if (growTop > 0 || growBottom > 0) {
        QImage bigger(w, newH, QImage::Format_RGB32);
        bigger.fill(Qt::black);
        for (int y = 0; y < oldH; ++y)
            std::memcpy(bigger.scanLine(y + growTop), m_canvas.constScanLine(y), size_t(w) * 4);
        m_canvas = bigger;
        newTop += growTop;          // the old content moved down with it
    }

    // Only the newly exposed rows are written: where the frame overlaps content already
    // captured, the EARLIER capture wins. That keeps a sticky header/footer confined to
    // the ends of the result instead of smearing a copy of it through the middle.
    // Only one side can ever grow — the canvas is never shorter than a frame, so a frame
    // cannot overhang both ends at once.
    // The canvas hash table grows with it, taken straight from the frame's own hashes —
    // the new rows are copies of those rows, so there is nothing to recompute.
    if (growTop > 0) {
        for (int y = 0; y < growTop; ++y)                       // newTop is 0 here
            std::memcpy(m_canvas.scanLine(y), f.constScanLine(y), size_t(w) * 4);
        m_canvasHashes = hashes.mid(0, growTop * kStrips) + m_canvasHashes;
    } else if (growBottom > 0) {
        for (int y = oldH; y < newH; ++y)
            std::memcpy(m_canvas.scanLine(y), f.constScanLine(y - newTop), size_t(w) * 4);
        m_canvasHashes += hashes.mid((oldH - newTop) * kStrips, growBottom * kStrips);
    }

    m_liveOffset = newTop;
    m_lastFrame = f.copy();
    m_lastHashes = hashes;
    ++m_frames;
    return growTop > 0 ? Result::Prepended
         : growBottom > 0 ? Result::Appended
                          : Result::Unchanged;
}

} // namespace scroll
