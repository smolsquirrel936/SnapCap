#include "core/SessionStore.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

namespace {
constexpr int kFormatVersion = 1;

QJsonObject rectToJson(const QRect& r)
{
    QJsonObject o;
    o[QStringLiteral("x")] = r.x();
    o[QStringLiteral("y")] = r.y();
    o[QStringLiteral("w")] = r.width();
    o[QStringLiteral("h")] = r.height();
    return o;
}

QRect rectFromJson(const QJsonObject& o)
{
    if (o.isEmpty())
        return QRect();
    return QRect(o.value(QStringLiteral("x")).toInt(), o.value(QStringLiteral("y")).toInt(),
                 o.value(QStringLiteral("w")).toInt(), o.value(QStringLiteral("h")).toInt());
}
} // namespace

SessionStore::SessionStore(const QString& dir) : m_dir(dir) {}

QString SessionStore::manifestPath() const
{
    return QDir(m_dir).filePath(QStringLiteral("session.json"));
}

QString SessionStore::imagePath(int index) const
{
    return QDir(m_dir).filePath(QStringLiteral("pin_%1.png").arg(index));
}

bool SessionStore::hasSession() const
{
    return QFile::exists(manifestPath());
}

void SessionStore::clear() const
{
    QDir dir(m_dir);
    if (!dir.exists())
        return;
    for (const QString& name : dir.entryList({QStringLiteral("pin_*.png")}, QDir::Files))
        dir.remove(name);
    dir.remove(QStringLiteral("session.json"));
}

bool SessionStore::save(const QVector<PinSnapshot>& pins) const
{
    if (!QDir().mkpath(m_dir))
        return false;

    // Start from a clean slate so stale sidecar PNGs never resurface on restore.
    clear();

    QJsonArray arr;
    bool ok = true;
    for (int i = 0; i < pins.size(); ++i) {
        const PinSnapshot& p = pins[i];
        const QString imgRel = QStringLiteral("pin_%1.png").arg(i);
        if (!p.image.isNull() && !p.image.save(imagePath(i), "PNG"))
            ok = false;

        QJsonObject o;
        o[QStringLiteral("image")]     = imgRel;
        o[QStringLiteral("transform")] = p.transform;
        o[QStringLiteral("geometry")]  = rectToJson(p.geometry);
        if (!p.annotations.isEmpty())
            o[QStringLiteral("annotations")] = p.annotations;
        arr.append(o);
    }

    QJsonObject root;
    root[QStringLiteral("version")] = kFormatVersion;
    root[QStringLiteral("pins")]    = arr;

    QFile f(manifestPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return ok;
}

QVector<PinSnapshot> SessionStore::load() const
{
    QVector<PinSnapshot> out;
    QFile f(manifestPath());
    if (!f.open(QIODevice::ReadOnly))
        return out;

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return out;

    const QJsonArray arr = doc.object().value(QStringLiteral("pins")).toArray();
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        PinSnapshot p;
        const QString imgRel = o.value(QStringLiteral("image")).toString();
        if (!imgRel.isEmpty())
            p.image.load(QDir(m_dir).filePath(imgRel));
        p.transform   = o.value(QStringLiteral("transform")).toObject();
        p.geometry    = rectFromJson(o.value(QStringLiteral("geometry")).toObject());
        p.annotations = o.value(QStringLiteral("annotations")).toObject();
        out.append(p);
    }
    return out;
}
