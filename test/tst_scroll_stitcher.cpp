#include <QtTest>
#include <QPainter>

#include "core/scroll/ScrollStitcher.h"

using scroll::ScrollStitcher;
using Result = scroll::ScrollStitcher::Result;
using Side = scroll::ScrollStitcher::Side;

// Vertical scroll stitching (headless).
//
// Every case works the same way: a synthetic tall "page" is sliced into overlapping
// region-sized frames at known offsets, fed in, and the reassembled canvas is compared
// against the page it came from — so a wrong offset cannot pass as a right one.
class TstScrollStitcher : public QObject
{
    Q_OBJECT
private slots:
    void firstFrameSeedsCanvas();
    void reconstructsPageFromSlices();
    void reconstructsSparseTextPage();
    void uniformContentDoesNotFabricateScroll();
    void repeatedFrameIsUnchanged();
    void scrollingUpPrepends();
    void revisitingCapturedAreaMovesLiveOffset();
    void jumpBeyondOverlapIsRejected();
    void movingScrollbarThumbStillMatches();
    void staticChromeBandStillMatches();
    void stickySidebarColumnsStillMatch();
    void wideBlankMarginsDoNotDominate();
    void largeScrollBackKeepsLivePosition();
    void forwardJumpPastFrontierIsRefused();
    void trimAboveKeepsLiveFrameAndStitchesOn();
    void trimBelowKeepsLiveFrameAndStitchesOn();
    void trimIsNoOpWhenThatSideIsEmpty();
    void heightCapRefusesGrowth();
    void wrongSizedFrameIsRejected();
    void nullFrameIsRejected();

private:
    // A page whose every row is distinct, so no two rows can be confused and a
    // misplacement shows up as a pixel difference. Deliberately NOT an arithmetic
    // pattern like (y*13)&0xff: those repeat every 256 rows, which makes any page
    // taller than that genuinely ambiguous to match and tests nothing.
    static QImage makePage(int w, int h)
    {
        QImage img(w, h, QImage::Format_RGB32);
        quint32 state = 12345;
        auto next = [&state] { state = state * 1664525u + 1013904223u; return state; };
        for (int y = 0; y < h; ++y) {
            const quint32 rowSeed = next();
            QRgb* p = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = 0; x < w; ++x) {
                const quint32 v = rowSeed ^ (quint32(x) * 2654435761u);
                p[x] = qRgb(v & 0xff, (v >> 8) & 0xff, (v >> 16) & 0xff);
            }
        }
        return img;
    }
    // Closer to what this actually runs on: a white page with sparse, distinct dark
    // "text" rows. Most rows are identical to each other, so only the true offset can
    // line the text up — which is the property the matcher depends on.
    static QImage makeTextPage(int w, int h)
    {
        QImage img(w, h, QImage::Format_RGB32);
        img.fill(qRgb(255, 255, 255));
        quint32 state = 999;
        auto next = [&state] { state = state * 1664525u + 1013904223u; return state; };
        for (int y = 12; y < h; y += 20) {
            const quint32 seed = next();
            for (int row = y; row < qMin(h, y + 3); ++row) {
                QRgb* p = reinterpret_cast<QRgb*>(img.scanLine(row));
                for (int x = 8; x < w - 8; ++x)
                    if (mix(seed, quint32(x)) & 1)
                        p[x] = qRgb(30, 30, 30);
            }
        }
        return img;
    }
    // Avalanche mixer. A bare `seed ^ (x * k)` will not do here: picking one bit out of
    // it yields only two possible patterns, so the "distinct" text bands would repeat
    // and the page would be ambiguous to match — the fixture, not the code, failing.
    static quint32 mix(quint32 a, quint32 b)
    {
        quint32 v = a + b * 2654435761u;
        v ^= v >> 15;
        v *= 2246822519u;
        v ^= v >> 13;
        return v;
    }
    static QImage slice(const QImage& page, int top, int h)
    {
        return page.copy(0, top, page.width(), h);
    }
    // Paints a scrollbar-thumb-like bar into the right-hand columns of a frame.
    static QImage withThumb(const QImage& frame, int thumbTop, int thumbH)
    {
        QImage out = frame.copy();
        const int w = out.width();
        for (int y = thumbTop; y < qMin(out.height(), thumbTop + thumbH); ++y) {
            QRgb* p = reinterpret_cast<QRgb*>(out.scanLine(y));
            for (int x = w - 12; x < w; ++x)
                p[x] = qRgb(160, 160, 160);
        }
        return out;
    }
};

void TstScrollStitcher::firstFrameSeedsCanvas()
{
    const QImage page = makePage(120, 900);
    ScrollStitcher s;
    QCOMPARE(s.addFrame(slice(page, 0, 300)), Result::Appended);
    QCOMPARE(s.canvas().height(), 300);
    QCOMPARE(s.liveOffset(), 0);
    QCOMPARE(s.frameCount(), 1);
    QCOMPARE(s.canvas(), slice(page, 0, 300));
}

void TstScrollStitcher::reconstructsPageFromSlices()
{
    const QImage page = makePage(120, 900);
    ScrollStitcher s;
    for (int top = 0; top <= 600; top += 100)
        QCOMPARE(s.addFrame(slice(page, top, 300)), Result::Appended);

    QCOMPARE(s.canvas().height(), 900);
    QCOMPARE(s.liveOffset(), 600);
    QCOMPARE(s.frameCount(), 7);
    QCOMPARE(s.canvas(), page);          // pixel-exact reassembly
}

void TstScrollStitcher::repeatedFrameIsUnchanged()
{
    const QImage page = makePage(120, 900);
    ScrollStitcher s;
    s.addFrame(slice(page, 200, 300));
    QCOMPARE(s.addFrame(slice(page, 200, 300)), Result::Unchanged);
    QCOMPARE(s.canvas().height(), 300);
    QCOMPARE(s.liveOffset(), 0);
}

void TstScrollStitcher::scrollingUpPrepends()
{
    const QImage page = makePage(120, 900);
    ScrollStitcher s;
    QCOMPARE(s.addFrame(slice(page, 300, 300)), Result::Appended);
    QCOMPARE(s.addFrame(slice(page, 100, 300)), Result::Prepended);

    QCOMPARE(s.canvas().height(), 500);          // page rows [100, 600)
    QCOMPARE(s.liveOffset(), 0);
    QCOMPARE(s.canvas(), slice(page, 100, 500));
}

void TstScrollStitcher::revisitingCapturedAreaMovesLiveOffset()
{
    const QImage page = makePage(120, 900);
    ScrollStitcher s;
    s.addFrame(slice(page, 0, 300));
    QCOMPARE(s.addFrame(slice(page, 200, 300)), Result::Appended);
    QCOMPARE(s.canvas().height(), 500);
    QCOMPARE(s.liveOffset(), 200);

    // Back up into content already captured: nothing to add, but the live position moved.
    QCOMPARE(s.addFrame(slice(page, 100, 300)), Result::Unchanged);
    QCOMPARE(s.canvas().height(), 500);
    QCOMPARE(s.liveOffset(), 100);
    QCOMPARE(s.canvas(), slice(page, 0, 500));
}

void TstScrollStitcher::jumpBeyondOverlapIsRejected()
{
    const QImage page = makePage(120, 900);
    ScrollStitcher s;
    s.addFrame(slice(page, 0, 300));
    // Scrolled a full frame and more: the two share no content at all.
    QCOMPARE(s.addFrame(slice(page, 400, 300)), Result::NoOverlap);
    QCOMPARE(s.canvas().height(), 300);          // dropped, canvas untouched
    QCOMPARE(s.liveOffset(), 0);
    QCOMPARE(s.frameCount(), 1);
    // ...and recovery works: scrolling back to an overlapping position resumes.
    QCOMPARE(s.addFrame(slice(page, 150, 300)), Result::Appended);
    QCOMPARE(s.canvas(), slice(page, 0, 450));
}

void TstScrollStitcher::reconstructsSparseTextPage()
{
    // The realistic case: most rows are blank and identical, so a wrong offset can
    // still match on whitespace. Only the true one lines the distinct text rows up.
    const QImage page = makeTextPage(400, 900);
    ScrollStitcher s;
    for (int top = 0; top <= 600; top += 100)
        QCOMPARE(s.addFrame(slice(page, top, 300)), Result::Appended);
    QCOMPARE(s.canvas().height(), 900);
    QCOMPARE(s.canvas(), page);
}

void TstScrollStitcher::uniformContentDoesNotFabricateScroll()
{
    // Nothing on screen but a flat colour: every offset matches equally well, so the
    // offset is genuinely unknowable. The tie-break must choose "nothing moved" rather
    // than invent rows that were never seen.
    QImage blank(400, 300, QImage::Format_RGB32);
    blank.fill(qRgb(250, 250, 250));
    ScrollStitcher s;
    QCOMPARE(s.addFrame(blank), Result::Appended);
    QCOMPARE(s.addFrame(blank), Result::Unchanged);
    QCOMPARE(s.canvas().height(), 300);
}

void TstScrollStitcher::movingScrollbarThumbStillMatches()
{
    // The region includes a scrollbar whose thumb moves with the content: a band of
    // rows differs in 12 of 400 columns. Exact row equality would reject this.
    const QImage page = makePage(400, 900);
    ScrollStitcher s;
    QCOMPARE(s.addFrame(withThumb(slice(page, 0, 300), 20, 40)), Result::Appended);
    QCOMPARE(s.addFrame(withThumb(slice(page, 100, 300), 60, 40)), Result::Appended);
    QCOMPARE(s.addFrame(withThumb(slice(page, 200, 300), 100, 40)), Result::Appended);
    QCOMPARE(s.canvas().height(), 500);
    QCOMPARE(s.liveOffset(), 200);
}

void TstScrollStitcher::staticChromeBandStillMatches()
{
    // The real shape of a full-window capture: the top band is the app's chrome (tabs,
    // address bar) and does NOT scroll, while the body below it does. No single offset
    // explains the whole frame — the body matches at the scroll distance, the chrome
    // only at zero — so a "most rows must agree" rule rejects both and the session
    // stalls on the very first scroll. The body is what has to win.
    const QImage page = makeTextPage(600, 1600);
    const QImage chrome = makePage(600, 90);       // fixed, never scrolls

    auto frameAt = [&](int top) {
        QImage f(600, 500, QImage::Format_RGB32);
        QPainter p(&f);
        p.drawImage(0, 0, page, 0, top, 600, 500);     // body: scrolls
        p.drawImage(0, 0, chrome);                     // chrome: pinned on top
        p.end();
        return f;
    };

    ScrollStitcher s;
    QCOMPARE(s.addFrame(frameAt(0)), Result::Appended);
    QCOMPARE(s.addFrame(frameAt(120)), Result::Appended);
    QCOMPARE(s.addFrame(frameAt(240)), Result::Appended);
    QCOMPARE(s.canvas().height(), 740);            // 500 + 120 + 120
}

void TstScrollStitcher::stickySidebarColumnsStillMatch()
{
    // A Wikipedia-shaped page captured at full monitor width: a sticky navigation
    // sidebar on the left and a sticky table of contents on the right, with the article
    // scrolling between them. The static parts are COLUMNS this time, not a band across
    // the top — so at the article's true offset every row is spoiled by the two sidebars,
    // and at zero every row is spoiled by the article. Whole-row matching can satisfy
    // neither. What has to be recognised is that one wide band of columns translated
    // together.
    const int W = 1000, kLeft = 180, kRight = 220;      // article: the middle 600 px
    const QImage article = makeTextPage(W, 1600);
    const QImage leftBar  = makePage(kLeft, 500);       // sticky: identical every frame
    const QImage rightBar = makePage(kRight, 500);

    auto frameAt = [&](int top) {
        QImage f(W, 500, QImage::Format_RGB32);
        QPainter p(&f);
        p.drawImage(0, 0, article, 0, top, W, 500);     // article: scrolls
        p.drawImage(0, 0, leftBar);                     // sidebars: pinned
        p.drawImage(W - kRight, 0, rightBar);
        p.end();
        return f;
    };

    ScrollStitcher s;
    QCOMPARE(s.addFrame(frameAt(0)), Result::Appended);
    QCOMPARE(s.addFrame(frameAt(140)), Result::Appended);
    QCOMPARE(s.addFrame(frameAt(280)), Result::Appended);
    QCOMPARE(s.canvas().height(), 780);                 // 500 + 140 + 140
}

void TstScrollStitcher::wideBlankMarginsDoNotDominate()
{
    // What a real article looks like captured at full monitor width: a column of text in
    // the middle, wide blank margins either side. Those margins are IDENTICAL on every
    // row, so they agree at every possible offset — and a shortlist that ranks an offset
    // by its best-matching column strip hands them a perfect score, burying the true
    // offset under hundreds of meaningless ones so it is never even scored. Observed on
    // a real page: all 8 strips agreed on all 459 overlapping rows at the true offset,
    // and a bogus offset still outranked it on the blank right margin alone.
    // Margins of 15% a side: wide enough that whole strips are blank (which is what
    // misled the shortlist), narrow enough not to satisfy stage 2's column-band rule on
    // their own — that is a separate limitation, recorded in Unimplemented-Features.md §3c.
    const int W = 1600, kTextL = 240, kTextR = 1360;
    QImage page(W, 2400, QImage::Format_RGB32);
    page.fill(qRgb(255, 255, 255));
    quint32 state = 4242;
    for (int y = 14; y < 2400; y += 22) {
        const quint32 seed = (state = state * 1664525u + 1013904223u);
        for (int row = y; row < qMin(2400, y + 3); ++row) {
            QRgb* p = reinterpret_cast<QRgb*>(page.scanLine(row));
            for (int x = kTextL; x < kTextR; ++x)
                if (mix(seed, quint32(x)) & 1)
                    p[x] = qRgb(30, 30, 30);
        }
    }

    ScrollStitcher s;
    for (int top = 0; top <= 900; top += 300)
        QCOMPARE(s.addFrame(slice(page, top, 800)), Result::Appended);
    QCOMPARE(s.canvas().height(), 1700);
    QCOMPARE(s.liveOffset(), 900);
    QCOMPARE(s.canvas(), slice(page, 0, 1700));      // and placed exactly, not approximately
}

void TstScrollStitcher::largeScrollBackKeepsLivePosition()
{
    // Scrolling BACK is where the live position goes wrong in practice: a flick upward
    // moves the page further in one debounce window than a frame-to-frame match can
    // describe, and the frame lands wholly inside content already captured — so the
    // image is untouched (Unchanged) and only liveOffset, i.e. the strip's marker, is
    // wrong. It reads too LOW, because a match short of the truth is the only kind the
    // search range can produce.
    const QImage page = makeTextPage(400, 1600);
    ScrollStitcher s;
    for (int top = 0; top <= 600; top += 100)
        s.addFrame(slice(page, top, 300));
    QCOMPARE(s.canvas().height(), 900);
    QCOMPARE(s.liveOffset(), 600);

    // A modest step back, well inside what a frame-to-frame match can see.
    QCOMPARE(s.addFrame(slice(page, 450, 300)), Result::Unchanged);
    QCOMPARE(s.liveOffset(), 450);

    // A flick: 1.5 screens back at once. This is the reported case.
    s.addFrame(slice(page, 0, 300));
    QCOMPARE(s.liveOffset(), 0);
    QCOMPARE(s.canvas().height(), 900);      // nothing new — it is all already captured
}

void TstScrollStitcher::forwardJumpPastFrontierIsRefused()
{
    // Scrolling DOWN too far in one go — an auto-scroll step that overshoots — lands the
    // frame at the frontier, mostly on content never captured. Searching the whole canvas
    // for it is exactly how a frame ends up matched against similar-looking content
    // ABOVE, jumping the marker backwards. Relocation must decline anything that is not
    // wholly inside what has already been captured, leaving the honest warning instead.
    const QImage page = makeTextPage(400, 2400);
    ScrollStitcher s;
    for (int top = 0; top <= 600; top += 100)
        s.addFrame(slice(page, top, 300));
    QCOMPARE(s.canvas().height(), 900);
    QCOMPARE(s.liveOffset(), 600);

    // 280 px on from the live frame: past what an incremental match can span, and the
    // frame runs 280 px beyond the captured content.
    QCOMPARE(s.addFrame(slice(page, 880, 300)), Result::NoOverlap);
    QCOMPARE(s.canvas().height(), 900);      // nothing invented
    QCOMPARE(s.liveOffset(), 600);           // ...and the marker did not move
}

// Trimming cuts one side away and leaves the session able to carry on: the live frame is
// still the matching reference, so the very next frame stitches onto the shortened canvas
// exactly as it would have onto the long one.
void TstScrollStitcher::trimAboveKeepsLiveFrameAndStitchesOn()
{
    const QImage page = makePage(120, 1200);
    ScrollStitcher s;
    for (int top = 0; top <= 600; top += 100)
        s.addFrame(slice(page, top, 300));
    QCOMPARE(s.canvas().height(), 900);          // page rows [0, 900), live at 600
    QCOMPARE(s.liveOffset(), 600);

    QCOMPARE(s.trim(Side::Above), 600);
    QCOMPARE(s.canvas().height(), 300);
    QCOMPARE(s.liveOffset(), 0);                 // the live frame is now the first row
    QCOMPARE(s.canvas(), slice(page, 600, 300));

    // Scrolling on still works, and lands where it would have without the cut.
    QCOMPARE(s.addFrame(slice(page, 700, 300)), Result::Appended);
    QCOMPARE(s.canvas(), slice(page, 600, 400));
    QCOMPARE(s.liveOffset(), 100);
}

void TstScrollStitcher::trimBelowKeepsLiveFrameAndStitchesOn()
{
    const QImage page = makePage(120, 1200);
    ScrollStitcher s;
    for (int top = 0; top <= 600; top += 100)
        s.addFrame(slice(page, top, 300));

    // The live frame ends at the canvas's own end, so there is nothing below it yet.
    QCOMPARE(s.trim(Side::Below), 0);
    QCOMPARE(s.canvas().height(), 900);

    QCOMPARE(s.addFrame(slice(page, 500, 300)), Result::Unchanged);   // scroll back a step
    QCOMPARE(s.liveOffset(), 500);
    QCOMPARE(s.trim(Side::Below), 100);          // rows [800, 900) go
    QCOMPARE(s.canvas().height(), 800);
    QCOMPARE(s.liveOffset(), 500);               // the live frame did not move
    QCOMPARE(s.canvas(), slice(page, 0, 800));

    // ...and the trimmed-away ground simply re-captures on the way back down.
    QCOMPARE(s.addFrame(slice(page, 600, 300)), Result::Appended);
    QCOMPARE(s.canvas(), slice(page, 0, 900));
}

void TstScrollStitcher::trimIsNoOpWhenThatSideIsEmpty()
{
    const QImage page = makePage(120, 900);
    ScrollStitcher s;
    s.addFrame(slice(page, 0, 300));             // one frame: it IS the whole canvas

    QCOMPARE(s.trim(Side::Above), 0);
    QCOMPARE(s.trim(Side::Below), 0);
    QCOMPARE(s.canvas(), slice(page, 0, 300));
    QCOMPARE(s.liveOffset(), 0);
}

void TstScrollStitcher::heightCapRefusesGrowth()
{
    const QImage page = makePage(120, 900);
    ScrollStitcher s(350);
    QCOMPARE(s.addFrame(slice(page, 0, 300)), Result::Appended);
    // Would make the canvas 400 px tall, past the 350 cap.
    QCOMPARE(s.addFrame(slice(page, 100, 300)), Result::Capped);
    QCOMPARE(s.canvas().height(), 300);
    QCOMPARE(s.liveOffset(), 0);
    QCOMPARE(s.frameCount(), 1);
    // A frame that fits is still accepted after a capped one.
    QCOMPARE(s.addFrame(slice(page, 50, 300)), Result::Appended);
    QCOMPARE(s.canvas().height(), 350);
}

void TstScrollStitcher::wrongSizedFrameIsRejected()
{
    const QImage page = makePage(120, 900);
    ScrollStitcher s;
    s.addFrame(slice(page, 0, 300));
    QImage narrow = makePage(100, 300);
    QCOMPARE(s.addFrame(narrow), Result::Rejected);
    QCOMPARE(s.canvas().height(), 300);
}

void TstScrollStitcher::nullFrameIsRejected()
{
    ScrollStitcher s;
    QCOMPARE(s.addFrame(QImage()), Result::Rejected);
    QVERIFY(s.isEmpty());
}

QTEST_GUILESS_MAIN(TstScrollStitcher)
#include "tst_scroll_stitcher.moc"
