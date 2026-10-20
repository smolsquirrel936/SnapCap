#pragma once

#include <QRect>
#include <QVector>

// A bounded, in-memory ring of recent successful-snip **areas** (selection rects),
// backing the in-snip `R` / `Shift+R` reuse-area replay (Features.md §1). Unlike
// core/History (which persists full images to disk), this only remembers geometry
// and lives for the session — the list resets on restart. Pure logic (no Qt widgets/
// Win32) → headlessly unit-tested (tst_area_history). Stage 6.
class AreaHistory {
public:
    // `max` ≤ 0 disables the ring. `loop` wraps the replay cursor at the ends.
    explicit AreaHistory(int max = 8);

    void setMax(int n);                 // re-clamps + evicts excess, resets the cursor
    int  max() const { return m_max; }
    void setLoop(bool on) { m_loop = on; }
    bool loop() const { return m_loop; }

    // Record a successful snip's area: prepends it as the newest entry, evicts the
    // oldest past max, and resets the replay cursor. Ignores a null rect or max ≤ 0;
    // a rect identical to the current newest is skipped (no duplicate churn).
    void record(const QRect& r);

    int count() const { return int(m_areas.size()); }
    const QVector<QRect>& areas() const { return m_areas; }   // newest first

    // Replay cursor. Newest first: dir < 0 walks toward older, dir > 0 toward newer.
    // Returns the area at the new cursor, or a null QRect at an end when loop is off
    // (when loop is on, it wraps around). Null when empty.
    QRect step(int dir);
    int   cursor() const { return m_cursor; }
    void  resetCursor() { m_cursor = -1; }

    void clear();

private:
    QVector<QRect> m_areas;             // index 0 = newest
    int  m_max;
    bool m_loop = false;
    int  m_cursor = -1;                 // -1 = no replay position yet
};
