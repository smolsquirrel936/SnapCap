#include "ui/OcrTextLayer.h"

#include <QPainter>

void OcrTextLayer::setResult(const platform::OcrResult& r)
{
    m_result = r;
    clearSelection();
    m_selecting = false;
}

void OcrTextLayer::clear()
{
    m_result = {};
    clearSelection();
    m_selecting = false;
}

void OcrTextLayer::beginSelect(const QPoint& imgPt)
{
    m_anchor    = imgPt;
    m_selecting = true;
    m_span      = ocr::spanBetween(m_result, imgPt, imgPt);
}

void OcrTextLayer::extendSelect(const QPoint& imgPt)
{
    if (!m_selecting)
        return;
    m_span = ocr::spanBetween(m_result, m_anchor, imgPt);
}

void OcrTextLayer::selectAll()
{
    m_span = ocr::all(m_result);
}

void OcrTextLayer::clearSelection()
{
    m_span = {};
}

QString OcrTextLayer::selectedText() const
{
    return ocr::textForSpan(m_result, m_span, m_keepBreaks);
}

void OcrTextLayer::paint(QPainter& p, const MapRect& mapRect, const QColor& accent) const
{
    if (m_result.lines.isEmpty())
        return;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(Qt::NoPen);

    // Plate + selection are drawn per LINE as one continuous bar (the union of the line's
    // word boxes), not per word: Tesseract's word boxes don't tile a line — especially for
    // CJK, where they leave gaps — so per-word fills look broken. The union reads as a clean
    // "selectable text" bar and a gapless highlight.
    const QColor plate(150, 150, 150, 70);
    QColor hi = accent;
    hi.setAlpha(110);

    int flat = 0;
    for (const auto& line : m_result.lines) {
        QRect plateBox, selBox;
        for (const auto& w : line.words) {
            plateBox = plateBox.united(w.box);
            if (!m_span.isEmpty() && flat >= m_span.first && flat <= m_span.last)
                selBox = selBox.united(w.box);
            ++flat;
        }
        if (!plateBox.isEmpty())
            p.fillRect(mapRect(plateBox), plate);
        if (!selBox.isEmpty())
            p.fillRect(mapRect(selBox), hi);
    }

    p.restore();
}
