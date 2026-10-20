#pragma once

#include <QList>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRegion>
#include <QSize>

// One monitor in the snip coordinate model. `captureGeometry` (native physical
// pixels) is the canonical space the whole snip pipeline works in — the space
// where physically-adjacent monitors pack with no gap even under mixed DPI.
// `logicalGeometry` (Qt logical coords) is used only to place the per-monitor
// overlay window.
struct ScreenLayoutEntry {
    QRect logicalGeometry;     // Qt logical geometry (QScreen::geometry) — window placement
    QRect captureGeometry;     // native physical px — canonical
    QSize capturedPixelSize;   // actual frozen-shot size (== captureGeometry.size() barring rounding)
    qreal scaleX = 1.0;        // capture px per logical px (derived from capturedPixelSize)
    qreal scaleY = 1.0;
};

// Pure geometry (no Qt Widgets / no Win32) so it is headless-unit-testable.
// Converts between each monitor's local-logical view coordinates (relative to
// its logicalGeometry top-left) and the shared capture-pixel space.
class ScreenLayout {
public:
    ScreenLayout() = default;
    explicit ScreenLayout(QList<ScreenLayoutEntry> screens);

    const QList<ScreenLayoutEntry>& screens() const { return m_screens; }
    int count() const { return int(m_screens.size()); }
    bool isEmpty() const { return m_screens.isEmpty(); }

    QRect   captureBounds() const;          // union of every captureGeometry (SelectionModel bounds)
    QRegion coveredCaptureRegion() const;   // real screens only — no gap fill (gap awareness)

    // A point in screen i's local-logical coords → capture px.
    QPoint localLogicalToCapture(int i, QPointF local) const;
    // A capture-px rect → screen i's local-logical rect (relative to logicalGeometry
    // top-left). Maps both exclusive edges then subtracts, so fractional-DPI seams
    // never produce an inclusive-edge off-by-one; rounds once at the pixel boundary.
    QRect   captureRectToLocalLogical(int i, const QRect& cap) const;

    // Index of the screen whose captureGeometry contains `cap`; nearest by centre if
    // none contains it. -1 only when the layout is empty.
    int indexForCapture(QPoint cap) const;
    // Index of the screen whose logicalGeometry contains `g`; nearest by centre otherwise.
    int indexForLogical(QPoint g) const;

    // capture px per logical px at a capture point (its screen's scaleX). 1.0 if empty.
    qreal scaleAt(QPoint cap) const;

    // Bridges to/from Qt global-logical space, used where the pipeline still talks to
    // logical-only APIs (window/element detection, pin placement, the frozen cursor).
    QPoint captureToGlobalLogical(QPoint cap) const;
    QPoint globalLogicalToCapture(QPointF g) const;
    QRect  captureRectToGlobalLogical(const QRect& cap) const;   // maps both exclusive edges
    QRect  globalLogicalRectToCapture(const QRect& g) const;

private:
    QList<ScreenLayoutEntry> m_screens;
};
