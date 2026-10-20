#include "core/AreaHistory.h"

AreaHistory::AreaHistory(int max)
    : m_max(qMax(0, max))
{
}

void AreaHistory::setMax(int n)
{
    m_max = qMax(0, n);
    while (int(m_areas.size()) > m_max)
        m_areas.removeLast();          // evict oldest
    m_cursor = -1;
}

void AreaHistory::record(const QRect& r)
{
    if (m_max <= 0 || r.isEmpty())
        return;
    if (!m_areas.isEmpty() && m_areas.first() == r)
        return;                        // identical to the current newest — skip
    m_areas.prepend(r);
    while (int(m_areas.size()) > m_max)
        m_areas.removeLast();          // evict oldest past max
    m_cursor = -1;
}

QRect AreaHistory::step(int dir)
{
    if (m_areas.isEmpty() || m_max <= 0)
        return QRect();
    const int n = int(m_areas.size());
    int target;
    if (dir < 0) {                     // walk toward older
        target = m_cursor + 1;
        if (target >= n)
            target = m_loop ? 0 : -1;      // wrap to newest, or clamp at the oldest end
    } else {                           // walk toward newer
        target = m_cursor - 1;
        if (target < 0)
            target = m_loop ? n - 1 : -1;  // wrap to oldest, or clamp at the newest end
    }
    if (target < 0)
        return QRect();                // at an end with loop off — leave the cursor put
    m_cursor = target;
    return m_areas[target];
}

void AreaHistory::clear()
{
    m_areas.clear();
    m_cursor = -1;
}
