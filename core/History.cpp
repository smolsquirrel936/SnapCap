#include "core/History.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPair>
#include <QThread>

namespace {
constexpr int kThumbMax = 96;          // longest-side px for the persisted thumbnail
const QString kThumbSuffix = QStringLiteral("_thumb");
const QString kFullSuffix  = QStringLiteral("_full");   // per-monitor freeze images

QString thumbForFull(const QString& fullPath)
{
    const QFileInfo fi(fullPath);
    return fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + kThumbSuffix
         + QStringLiteral(".png");
}

QString sidecarForFull(const QString& fullPath)
{
    const QFileInfo fi(fullPath);
    return fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + QStringLiteral(".json");
}

QString fullScreenPath(const QString& dir, const QString& id, int k)
{
    return QDir(dir).filePath(QStringLiteral("snip_%1%2%3.png").arg(id, kFullSuffix).arg(k));
}
} // namespace

History::History(const QString& dir, int max, QObject* parent)
    : QObject(parent), m_dir(dir), m_max(qMax(0, max))
{
    // A dedicated serial thread for the heavy disk I/O (see m_writerThread doc).
    m_writer = new QObject;             // no parent: it lives on the worker thread
    m_writerThread = new QThread(this);
    m_writer->moveToThread(m_writerThread);
    m_writerThread->start();
    load();
}

History::~History()
{
    if (m_writerThread) {
        m_writerThread->quit();         // drains events already queued, then exits the loop
        m_writerThread->wait();
    }
    delete m_writer;                    // safe: the worker thread has fully stopped
}

void History::enqueue(std::function<void()> fn)
{
    // Queued functor call → runs on m_writerThread in FIFO order. Falls back to running
    // inline if the worker isn't up (shouldn't happen once constructed).
    if (m_writer)
        QMetaObject::invokeMethod(m_writer, std::move(fn), Qt::QueuedConnection);
    else
        fn();
}

void History::waitForWrites()
{
    if (m_writer)                       // empty task behind the queue → returns once drained
        QMetaObject::invokeMethod(m_writer, [] {}, Qt::BlockingQueuedConnection);
}

QImage History::makeThumb(const QImage& full)
{
    if (full.isNull())
        return full;
    const int longest = qMax(full.width(), full.height());
    if (longest <= kThumbMax)
        return full;
    return full.scaled(QSize(kThumbMax, kThumbMax), Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

void History::load()
{
    m_entries.clear();
    QDir dir(m_dir);
    if (!dir.exists())
        return;
    // Full images are "snip_<id>.png"; thumbnails are "snip_<id>_thumb.png". Sort
    // newest-first by name (the id is a zero-padded timestamp, so name order = time).
    const auto names = dir.entryList({QStringLiteral("snip_*.png")}, QDir::Files, QDir::Name);
    for (const QString& name : names) {
        if (name.contains(kThumbSuffix) || name.contains(kFullSuffix))
            continue;                  // thumbnails + per-monitor freeze images aren't entries
        Entry e;
        const QFileInfo fi(name);
        e.id = fi.completeBaseName().mid(QStringLiteral("snip_").size());
        e.fullPath = dir.filePath(name);
        e.thumbPath = thumbForFull(e.fullPath);
        e.thumbnail.load(e.thumbPath);
        readSidecar(e);                // selection rect + full-screen freeze (if present)
        m_entries.prepend(e);          // newest first
    }
    enforceMax();
}

QString History::record(const QImage& crop, const QList<ScreenShot>& shots, const QRect& sel)
{
    if (crop.isNull() || m_max <= 0)
        return QString();
    if (!QDir().mkpath(m_dir))
        return QString();

    // Millisecond timestamp keeps ids monotonic + name-sortable; a per-session
    // sequence suffix disambiguates two records that land in the same millisecond.
    const QString id = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmmsszzz"))
                     + QStringLiteral("_%1").arg(m_seq++, 6, 10, QChar('0'));
    Entry e;
    e.id = id;
    e.fullPath  = QDir(m_dir).filePath(QStringLiteral("snip_%1.png").arg(id));
    e.thumbPath = thumbForFull(e.fullPath);
    e.thumbnail = makeThumb(crop);
    e.selection = sel;

    // The crop + thumbnail are small — write them synchronously so fullAt()/pick-thumbnail
    // are always ready right after record().
    if (!crop.save(e.fullPath, "PNG"))
        return QString();
    e.thumbnail.save(e.thumbPath, "PNG");

    // Full-screen freeze: one PNG per monitor + a JSON sidecar of the geometry so a later
    // `,`/`.` replay can restore the whole past desktop and its capture area. Encoding a
    // full desktop is slow, so gather the images + metadata now (paths are deterministic)
    // but hand the actual encode+write to the background writer (keeps copy/save/pin snappy).
    QVector<QPair<QString, QImage>> pending;
    for (int k = 0; k < shots.size(); ++k) {
        const ScreenShot& sh = shots.at(k);
        if (sh.shot.isNull())
            continue;
        FrozenScreen fs;
        fs.geo = sh.geo;
        fs.dpr = sh.dpr;
        fs.imagePath = fullScreenPath(m_dir, id, k);
        e.screens.append(fs);
        pending.append({fs.imagePath, sh.shot});
    }
    const Entry snapshot = e;           // captured for the sidecar write
    enqueue([this, pending, snapshot] {
        for (const auto& pr : pending)
            pr.second.save(pr.first, "PNG");
        writeSidecar(snapshot);
    });

    m_entries.prepend(e);
    enforceMax();
    m_cursor = -1;
    emit changed();
    return id;
}

void History::writeSidecar(const Entry& e) const
{
    QJsonObject root;
    const QJsonArray selArr{e.selection.x(), e.selection.y(),
                            e.selection.width(), e.selection.height()};
    root[QStringLiteral("selection")] = selArr;
    QJsonArray screens;
    for (const FrozenScreen& fs : e.screens) {
        QJsonObject o;
        o[QStringLiteral("geo")] = QJsonArray{fs.geo.x(), fs.geo.y(),
                                              fs.geo.width(), fs.geo.height()};
        o[QStringLiteral("dpr")] = fs.dpr;
        o[QStringLiteral("image")] = QFileInfo(fs.imagePath).fileName();
        screens.append(o);
    }
    root[QStringLiteral("screens")] = screens;
    QFile f(sidecarForFull(e.fullPath));
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

void History::readSidecar(Entry& e) const
{
    QFile f(sidecarForFull(e.fullPath));
    if (!f.open(QIODevice::ReadOnly))
        return;                        // pre-full-screen entry (crop-only) — leave empty
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonArray sel = root.value(QStringLiteral("selection")).toArray();
    if (sel.size() == 4)
        e.selection = QRect(sel[0].toInt(), sel[1].toInt(), sel[2].toInt(), sel[3].toInt());
    const QDir dir(QFileInfo(e.fullPath).absolutePath());
    const QJsonArray screens = root.value(QStringLiteral("screens")).toArray();
    for (const QJsonValue& v : screens) {
        const QJsonObject o = v.toObject();
        const QJsonArray g = o.value(QStringLiteral("geo")).toArray();
        FrozenScreen fs;
        if (g.size() == 4)
            fs.geo = QRect(g[0].toInt(), g[1].toInt(), g[2].toInt(), g[3].toInt());
        fs.dpr = o.value(QStringLiteral("dpr")).toDouble(1.0);
        fs.imagePath = dir.filePath(o.value(QStringLiteral("image")).toString());
        e.screens.append(fs);
    }
}

void History::removeFiles(const Entry& e)
{
    // Delete on the writer queue too, so an evict-delete can never overtake a still-pending
    // full-screen write for the same entry (FIFO: write(X) was enqueued before delete(X)).
    QStringList paths{e.fullPath, e.thumbPath, sidecarForFull(e.fullPath)};
    for (const FrozenScreen& fs : e.screens)
        paths << fs.imagePath;
    enqueue([paths] {
        for (const QString& p : paths)
            QFile::remove(p);
    });
}

void History::enforceMax()
{
    bool removed = false;
    while (int(m_entries.size()) > m_max) {
        removeFiles(m_entries.takeLast());
        removed = true;
    }
    if (m_cursor >= m_entries.size())
        m_cursor = m_entries.size() - 1;
    if (removed)
        emit changed();
}

void History::setMax(int n)
{
    m_max = qMax(0, n);
    enforceMax();
}

QImage History::fullAt(int index) const
{
    if (index < 0 || index >= m_entries.size())
        return QImage();
    QImage img;
    img.load(m_entries[index].fullPath);
    return img;
}

QList<ScreenShot> History::screensAt(int index) const
{
    QList<ScreenShot> out;
    if (index < 0 || index >= m_entries.size())
        return out;
    for (const FrozenScreen& fs : m_entries[index].screens) {
        ScreenShot sh;                 // screen stays null: the freeze is display-agnostic
        sh.geo = fs.geo;
        sh.dpr = fs.dpr;
        sh.shot.load(fs.imagePath);
        sh.shot.setDevicePixelRatio(1.0);
        out.append(sh);
    }
    return out;
}

QRect History::selectionAt(int index) const
{
    if (index < 0 || index >= m_entries.size())
        return QRect();
    return m_entries[index].selection;
}

QImage History::prev()
{
    if (m_entries.isEmpty() || m_cursor + 1 >= m_entries.size())
        return QImage();               // empty, or already at the oldest end
    ++m_cursor;                        // walk toward older
    return fullAt(m_cursor);
}

QImage History::next()
{
    if (m_entries.isEmpty() || m_cursor <= 0) {
        if (m_cursor == 0) m_cursor = -1;   // stepping newer past the newest clears it
        return QImage();
    }
    --m_cursor;                        // walk toward newer
    return fullAt(m_cursor);
}

void History::clear()
{
    for (const Entry& e : m_entries)
        removeFiles(e);
    m_entries.clear();
    m_cursor = -1;
    emit changed();
}
