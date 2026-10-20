#include "core/ocr/OcrSelection.h"

#include <limits>

namespace ocr {

namespace {
// CJK (and Japanese/Korean + fullwidth) text is written without inter-word spaces, so
// two words are joined with no separator when either side is such a character. Latin
// words keep the single space between them.
bool isCjk(QChar c)
{
    const ushort u = c.unicode();
    return (u >= 0x2E80 && u <= 0x9FFF)     // CJK radicals … CJK Unified Ideographs
        || (u >= 0x3000 && u <= 0x30FF)     // CJK symbols/punctuation, Hiragana, Katakana
        || (u >= 0xAC00 && u <= 0xD7AF)     // Hangul syllables
        || (u >= 0xF900 && u <= 0xFAFF)     // CJK compatibility ideographs
        || (u >= 0xFF00 && u <= 0xFFEF);    // halfwidth/fullwidth forms
}
} // namespace

int wordCount(const platform::OcrResult& r)
{
    int n = 0;
    for (const auto& line : r.lines)
        n += int(line.words.size());
    return n;
}

int wordAt(const platform::OcrResult& r, const QPoint& imgPt)
{
    int flat = 0;
    for (const auto& line : r.lines) {
        for (const auto& w : line.words) {
            if (w.box.contains(imgPt))
                return flat;
            ++flat;
        }
    }
    return -1;
}

int nearestWord(const platform::OcrResult& r, const QPoint& imgPt)
{
    int    best  = -1;
    qint64 bestD = std::numeric_limits<qint64>::max();
    int    flat  = 0;
    for (const auto& line : r.lines) {
        for (const auto& w : line.words) {
            const QPoint c  = w.box.center();
            const qint64 dx = qint64(c.x()) - imgPt.x();
            const qint64 dy = qint64(c.y()) - imgPt.y();
            const qint64 d  = dx * dx + dy * dy;
            if (d < bestD) {
                bestD = d;
                best  = flat;
            }
            ++flat;
        }
    }
    return best;
}

Span spanBetween(const platform::OcrResult& r, const QPoint& anchor, const QPoint& cursor)
{
    const int ia = nearestWord(r, anchor);
    const int ib = nearestWord(r, cursor);
    if (ia < 0 || ib < 0)
        return {};
    return { qMin(ia, ib), qMax(ia, ib) };
}

Span all(const platform::OcrResult& r)
{
    const int n = wordCount(r);
    if (n == 0)
        return {};
    return { 0, n - 1 };
}

QList<QRect> boxesForSpan(const platform::OcrResult& r, const Span& span)
{
    QList<QRect> out;
    if (span.isEmpty())
        return out;
    int flat = 0;
    for (const auto& line : r.lines) {
        for (const auto& w : line.words) {
            if (flat >= span.first && flat <= span.last)
                out.append(w.box);
            ++flat;
        }
    }
    return out;
}

QString textForSpan(const platform::OcrResult& r, const Span& span, bool keepLineBreaks)
{
    if (span.isEmpty())
        return {};
    QString out;
    int     flat     = 0;
    bool    emitted  = false;
    int     prevLine = -1;
    QString prevText;
    for (int l = 0; l < r.lines.size(); ++l) {
        for (int w = 0; w < r.lines[l].words.size(); ++w) {
            if (flat >= span.first && flat <= span.last) {
                const QString& t = r.lines[l].words[w].text;
                if (emitted) {
                    if (l != prevLine && keepLineBreaks) {
                        out += QLatin1Char('\n');
                    } else {
                        // Join within a line (or across lines when not keeping breaks):
                        // a single space between Latin words, nothing next to CJK.
                        const QChar a = prevText.isEmpty() ? QChar() : prevText.back();
                        const QChar b = t.isEmpty()        ? QChar() : t.front();
                        if (!isCjk(a) && !isCjk(b))
                            out += QLatin1Char(' ');
                    }
                }
                out     += t;
                emitted  = true;
                prevLine = l;
                prevText = t;
            }
            ++flat;
        }
    }
    return out;
}

} // namespace ocr
