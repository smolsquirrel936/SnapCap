#include "platform/win/WinMonitorTable.h"

#include "platform/platform.h"

#include <QGuiApplication>
#include <QScreen>
#include <cmath>
#include <cstdlib>

namespace winmon {

namespace {

BOOL CALLBACK collectMonitors(HMONITOR h, HDC, LPRECT, LPARAM lp)
{
    auto* v = reinterpret_cast<QVector<RECT>*>(lp);
    MONITORINFOEXW mi;
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(h, reinterpret_cast<LPMONITORINFO>(&mi)))
        v->append(mi.rcMonitor);
    return TRUE;
}

bool rectContains(const RECT& r, long x, long y)
{
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

const Mon* monForLogical(const QVector<Mon>& t, const QPoint& l)
{
    for (const Mon& m : t)
        if (m.logGeo.contains(l))
            return &m;
    const Mon* best = nullptr;
    qreal bestD = -1;
    for (const Mon& m : t) {
        const QPoint c = m.logGeo.center();
        const qreal d = QPointF(c - l).manhattanLength();
        if (bestD < 0 || d < bestD) { bestD = d; best = &m; }
    }
    return best;
}

const Mon* monForPhysical(const QVector<Mon>& t, long x, long y)
{
    for (const Mon& m : t)
        if (rectContains(m.phys, x, y))
            return &m;
    const Mon* best = nullptr;
    qreal bestD = -1;
    for (const Mon& m : t) {
        const long cx = (m.phys.left + m.phys.right) / 2;
        const long cy = (m.phys.top + m.phys.bottom) / 2;
        const qreal d = std::abs(cx - x) + std::abs(cy - y);
        if (bestD < 0 || d < bestD) { bestD = d; best = &m; }
    }
    return best;
}

} // namespace

QVector<Mon> buildTable()
{
    QVector<RECT> phys;
    EnumDisplayMonitors(nullptr, nullptr, collectMonitors, reinterpret_cast<LPARAM>(&phys));

    QVector<bool> used(phys.size(), false);
    QVector<Mon> table;
    for (QScreen* s : QGuiApplication::screens()) {
        Mon m;
        m.logGeo = s->geometry();
        m.dpr = s->devicePixelRatio();
        if (m.dpr <= 0.0)
            m.dpr = 1.0;

        // Pair this QScreen to its physical monitor by matching physical SIZE.
        // `QScreen::name()` on Windows is a friendly string ("MAG 272URDF"), not
        // the GDI device name ("\\.\DISPLAY1"), so name-pairing never matches. But
        // physical size == logical size * dpr is reliable even when the *origin*
        // isn't, so a size match yields the true physical origin (rcMonitor). The
        // origin estimate only breaks ties between identically-sized monitors.
        const long W    = long(std::llround(m.logGeo.width()  * m.dpr));
        const long H    = long(std::llround(m.logGeo.height() * m.dpr));
        const long estL = long(std::llround(m.logGeo.left()   * m.dpr));
        const long estT = long(std::llround(m.logGeo.top()    * m.dpr));
        int best = -1;
        qint64 bestScore = -1;
        for (int i = 0; i < phys.size(); ++i) {
            if (used[i])
                continue;
            const long pw = phys[i].right - phys[i].left;
            const long ph = phys[i].bottom - phys[i].top;
            const qint64 sizeDiff   = qint64(std::abs(pw - W)) + std::abs(ph - H);
            const qint64 originDist = qint64(std::abs(phys[i].left - estL))
                                    + std::abs(phys[i].top - estT);
            const qint64 score = sizeDiff * 1000000 + originDist;   // size dominates
            if (bestScore < 0 || score < bestScore) {
                bestScore = score;
                best = i;
            }
        }
        if (best >= 0 && (bestScore / 1000000) <= 4) {     // physical size lines up
            m.phys = phys[best];
            used[best] = true;
        } else {
            // Last resort (no monitor matched): assume physical == logical * dpr.
            m.phys.left   = estL;
            m.phys.top    = estT;
            m.phys.right  = estL + W;
            m.phys.bottom = estT + H;
        }
        table.append(m);
    }
    return table;
}

POINT logicalToPhysical(const QVector<Mon>& t, const QPoint& l)
{
    const Mon* m = monForLogical(t, l);
    if (!m)
        return POINT{l.x(), l.y()};
    return POINT{
        LONG(m->phys.left + std::llround((l.x() - m->logGeo.left()) * m->dpr)),
        LONG(m->phys.top  + std::llround((l.y() - m->logGeo.top())  * m->dpr))};
}

QPoint physicalToLogical(const QVector<Mon>& t, long x, long y)
{
    const Mon* m = monForPhysical(t, x, y);
    if (!m)
        return QPoint(int(x), int(y));
    return QPoint(
        int(m->logGeo.left() + std::llround((x - m->phys.left) / m->dpr)),
        int(m->logGeo.top()  + std::llround((y - m->phys.top)  / m->dpr)));
}

QRect physRectToLogical(const QVector<Mon>& t, const RECT& r)
{
    const QPoint tl = physicalToLogical(t, r.left, r.top);
    // The bottom-right corner is EXCLUSIVE, so it can sit exactly on a monitor
    // boundary (x == phys.right is outside this monitor). Looking it up as a plain
    // point then resolves it to the NEIGHBOURING monitor and mixes two different
    // origins/scales into one rect — e.g. a 1918px-tall rect ending at x=3840 came
    // back 1975 logical px tall instead of 1279. Pick the monitor from the last
    // INCLUSIVE pixel, then map the exclusive edges through that same monitor.
    const Mon* m = monForPhysical(t, r.right - 1, r.bottom - 1);
    const QPoint br = m
        ? QPoint(int(m->logGeo.left() + std::llround((r.right  - m->phys.left) / m->dpr)),
                 int(m->logGeo.top()  + std::llround((r.bottom - m->phys.top)  / m->dpr)))
        : QPoint(int(r.right), int(r.bottom));
    return QRect(tl, QSize(br.x() - tl.x(), br.y() - tl.y()));
}

} // namespace winmon

// --- Native monitor geometry for the snip coordinate model -------------------
namespace platform {

QList<MonitorInfo> monitorLayout()
{
    QList<MonitorInfo> out;
    for (const winmon::Mon& m : winmon::buildTable()) {
        MonitorInfo info;
        info.logicalGeometry = m.logGeo;
        info.captureGeometry = QRect(int(m.phys.left), int(m.phys.top),
                                     int(m.phys.right - m.phys.left),
                                     int(m.phys.bottom - m.phys.top));
        out.append(info);
    }
    return out;
}

} // namespace platform
