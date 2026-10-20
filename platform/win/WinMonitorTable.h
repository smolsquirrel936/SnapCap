#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>

#include <QPoint>
#include <QRect>
#include <QVector>

// Shared QScreen<->HMONITOR pairing + per-monitor logical<->physical mapping.
//
// Qt lays screens out in its own logical coordinate space (origin + DPR) while
// Win32 reports physical pixels. We pair each QScreen with its HMONITOR by
// physical SIZE (QScreen::name() on Windows is a friendly string, not the GDI
// device name, so name-pairing never matches) to get a complete, per-monitor-
// DPI-correct affine map. Used by WinElementDetect (element/window hit-testing)
// and platform::monitorLayout() (the mixed-DPI snip coordinate model).
namespace winmon {

struct Mon {
    QRect  logGeo;       // Qt logical geometry
    RECT   phys{};       // physical rcMonitor
    double dpr = 1.0;
};

// Build the table for the current monitor set (order matches QGuiApplication::screens()).
QVector<Mon> buildTable();

POINT  logicalToPhysical(const QVector<Mon>& t, const QPoint& l);
QPoint physicalToLogical(const QVector<Mon>& t, long x, long y);
QRect  physRectToLogical(const QVector<Mon>& t, const RECT& r);

} // namespace winmon
