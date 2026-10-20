#pragma once

#include "core/ocr/OcrSelection.h"
#include "platform/platform.h"

#include <QColor>
#include <QPoint>
#include <QRectF>
#include <QString>

#include <functional>

class QPainter;

// The shared OCR text-selection layer, reused by both the snip overlay and the pin
// editor. It holds one recognized result plus the current selected word-span, does
// the drag-selection interaction (delegating the reading-order math to core/ocr),
// and paints the "selectable text" chrome (a subtle grey box per word, an accent
// highlight over the selection — matching the reference OCR look).
//
// It is coordinate-system agnostic: all its state is in the recognized image's pixel
// space (platform::OcrResult boxes). The host supplies a `mapRect` functor that maps
// an image-pixel QRect to device/widget coordinates for painting, and feeds mouse
// input already converted to image pixels. That is what lets the capture-px snip host
// and the composed-px pin host share it unchanged.
class OcrTextLayer {
public:
    // Maps an image-pixel rect to the host's paint coordinates.
    using MapRect = std::function<QRectF(const QRect&)>;

    void setResult(const platform::OcrResult& r);   // clears any selection
    void clear();
    bool isEmpty() const { return m_result.lines.isEmpty(); }
    const platform::OcrResult& result() const { return m_result; }

    // Drag selection — points are in image (recognized-crop) pixels.
    void beginSelect(const QPoint& imgPt);           // set anchor + start selecting
    void extendSelect(const QPoint& imgPt);          // update the span to imgPt
    void endSelect() { m_selecting = false; }
    bool selecting() const { return m_selecting; }

    void selectAll();
    void clearSelection();
    bool hasSelection() const { return !m_span.isEmpty(); }

    void setKeepLineBreaks(bool keep) { m_keepBreaks = keep; }
    bool keepLineBreaks() const { return m_keepBreaks; }

    // Selected text (empty when nothing is selected), lines joined per keepLineBreaks.
    QString selectedText() const;

    // Paint every word box (subtle grey) and the selected span (accent highlight),
    // mapping image-pixel boxes through `mapRect`.
    void paint(QPainter& p, const MapRect& mapRect, const QColor& accent) const;

private:
    platform::OcrResult m_result;
    ocr::Span m_span;
    QPoint    m_anchor;
    bool      m_selecting  = false;
    bool      m_keepBreaks = true;
};
