#pragma once

#include "platform/platform.h"

#include <QList>
#include <QPoint>
#include <QRect>
#include <QString>

// Pure reading-order text-selection math over an OCR result. No Qt GUI/widgets, so
// it is headless-unit-tested (test/tst_ocr_selection.cpp) and reused by both the
// snip and pin OCR hosts via ui/OcrTextLayer.
//
// Words are addressed by a flat index in reading order: lines top-to-bottom as the
// engine returned them, words left-to-right within each line. All coordinates are
// image pixels (the space of platform::OcrResult::imageSize).
namespace ocr {

// A contiguous run of words in reading order, inclusive [first,last].
struct Span {
    int first = -1;
    int last  = -1;
    bool isEmpty() const { return first < 0 || last < first; }
    int  count()   const { return isEmpty() ? 0 : last - first + 1; }
};

// Total number of words across all lines.
int wordCount(const platform::OcrResult& r);

// Flat index of the word whose box contains `imgPt`, or -1 if the point is not
// inside any word box.
int wordAt(const platform::OcrResult& r, const QPoint& imgPt);

// Flat index of the word nearest `imgPt` (by box centre), or -1 when there are no
// words. Used so a drag that strays into the gaps still extends the selection.
int nearestWord(const platform::OcrResult& r, const QPoint& imgPt);

// Selection from the word nearest `anchor` to the word nearest `cursor`, normalized
// so first<=last. Empty when there are no words.
Span spanBetween(const platform::OcrResult& r, const QPoint& anchor, const QPoint& cursor);

// The whole document (every word), or empty when there are none.
Span all(const platform::OcrResult& r);

// Word boxes covered by `span` (image px) — for painting the selection highlight.
QList<QRect> boxesForSpan(const platform::OcrResult& r, const Span& span);

// Text of `span`. Words within a line join with a single space. Where the span
// crosses a line boundary, the join is '\n' when `keepLineBreaks`, else a space.
QString textForSpan(const platform::OcrResult& r, const Span& span, bool keepLineBreaks);

} // namespace ocr
