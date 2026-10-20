#pragma once

#include <QImage>
#include <QList>
#include <QObject>
#include <QRect>
#include <QString>
#include <QVector>
#include <functional>

#include "core/ScreenComposite.h"   // ScreenShot

class QThread;

// A bounded ring of the last N *successful* snips (Features.md §1). Each recorded
// snip persists the cropped final PNG + a small thumbnail AND the full-screen
// freeze it was cut from (one PNG per monitor) plus a JSON sidecar (selection rect
// + per-monitor geo/dpr), so the list survives restarts and the in-snip `,`/`.`
// replay can restore the whole past desktop for a re-snip. The oldest entry is
// evicted once the ring is full. A cursor (prev()/next()) backs the `,`/`.` replay.
// Pure logic (Qt Gui only, no widgets/Win32) → headlessly unit-tested (tst_history).
// Stage 6.
class History : public QObject {
    Q_OBJECT
public:
    // One monitor of a recorded snip's full-screen freeze (see Entry::screens).
    struct FrozenScreen {
        QRect   geo;                // global logical geometry at capture time
        qreal   dpr = 1.0;          // device-pixel ratio of this monitor
        QString imagePath;          // snip_<id>_full<k>.png (device pixels; loaded lazily)
    };

    struct Entry {
        QString id;        // sortable timestamp id (also the on-disk basename)
        QString fullPath;  // cropped final PNG (full resolution)
        QString thumbPath; // thumbnail PNG
        QImage  thumbnail; // loaded thumbnail (for the history strip UI)
        QRect   selection; // finalized capture area, global logical (for `,`/`.` replay)
        QVector<FrozenScreen> screens;  // full-screen freeze, one per monitor
    };

    // `dir` is the history folder (created on demand). `max` ≤ 0 disables history.
    explicit History(const QString& dir, int max = 30, QObject* parent = nullptr);
    ~History() override;                 // flushes pending background writes

    void setMax(int n);                 // re-clamps the ring + evicts excess on disk
    int  max() const { return m_max; }

    // Record a successful snip: writes the cropped image + thumbnail, the full-screen
    // freeze (`shots`, one PNG per monitor) + a sidecar (`sel` + per-monitor geo/dpr),
    // prepends it as the newest entry, evicts the oldest past max, and resets the replay
    // cursor. Ignores a null crop or max ≤ 0. Returns the new entry's id ("" if skipped).
    QString record(const QImage& crop, const QList<ScreenShot>& shots = {},
                   const QRect& sel = {});

    int count() const { return int(m_entries.size()); }
    const QVector<Entry>& entries() const { return m_entries; }  // newest first
    QImage fullAt(int index) const;     // load the cropped image (null if out of range)

    // `,`/`.` replay support: the full-screen freeze + finalized area of an entry, so
    // the overlay can restore that past desktop and re-select its area for a re-snip.
    QList<ScreenShot> screensAt(int index) const;   // loads _full<k>.png (screen=nullptr)
    QRect             selectionAt(int index) const; // finalized capture area (global logical)

    // `,`/`.` replay cursor. Newest first: prev() walks toward older, next() toward
    // newer. Returns the full image at the new cursor (null at the ends / when empty).
    QImage prev();
    QImage next();
    int    cursor() const { return m_cursor; }
    void   resetCursor() { m_cursor = -1; }

    void clear();                       // wipe the ring + on-disk files

    // Block until all queued background disk writes/deletes have drained. Mainly for
    // tests + a clean shutdown flush; fast no-op when the queue is already empty.
    void waitForWrites();

signals:
    void changed();                     // ring contents changed (recorded/evicted/cleared)

private:
    void load();                        // discover existing entries from disk
    void enforceMax();                  // evict oldest beyond m_max (disk + memory)
    void removeFiles(const Entry& e);   // delete an entry's crop/thumb/freeze/sidecar files
    void writeSidecar(const Entry& e) const;   // persist selection + full-screen geometry
    void readSidecar(Entry& e) const;          // load selection + full-screen geometry
    void enqueue(std::function<void()> fn);    // run fn on the serial writer thread (FIFO)
    static QImage makeThumb(const QImage& full);

    QString m_dir;
    int     m_max;
    QVector<Entry> m_entries;           // index 0 = newest
    int     m_cursor = -1;              // -1 = no replay position yet
    int     m_seq = 0;                  // disambiguates records within the same ms

    // Serial background disk I/O: the slow full-screen PNG encodes + sidecar writes (and
    // file deletes) run here so a copy/save/pin returns to the UI immediately. One thread
    // keeps them ordered (a write always precedes the matching evict-delete).
    QThread* m_writerThread = nullptr;
    QObject* m_writer = nullptr;         // context object living on m_writerThread
};
