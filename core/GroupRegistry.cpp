#include "core/GroupRegistry.h"

#include <QJsonArray>
#include <QJsonObject>

GroupRegistry::GroupRegistry(QObject* parent) : QObject(parent) {}

int GroupRegistry::indexOf(int id) const
{
    for (int i = 0; i < m_groups.size(); ++i)
        if (m_groups[i].id == id)
            return i;
    return -1;
}

int GroupRegistry::create(const QString& name)
{
    Group g{m_nextId++, name};
    m_groups.append(g);
    emit changed();
    return g.id;
}

bool GroupRegistry::rename(int id, const QString& name)
{
    const int idx = indexOf(id);
    if (idx < 0)
        return false;
    m_groups[idx].name = name;
    emit changed();
    return true;
}

bool GroupRegistry::remove(int id)
{
    const int idx = indexOf(id);
    if (idx < 0)
        return false;
    // Emit while the group is still present + shown, so the app can discard its live
    // pins (delete, not put-away) before we drop it. No hidden() here on purpose:
    // the pins are being destroyed, not serialized back to disk.
    emit removed(id);
    m_groups.remove(idx);
    m_shownIds.removeAll(id);
    emit changed();
    return true;
}

void GroupRegistry::setShown(int id, bool show)
{
    if (id == 0 || indexOf(id) < 0)
        return;
    if (show == isShown(id))
        return;
    if (show) {
        m_shownIds.append(id);
        emit shown(id);
    } else {
        m_shownIds.removeAll(id);
        emit hidden(id);
    }
    emit changed();
}

QString GroupRegistry::name(int id) const
{
    const int idx = indexOf(id);
    return idx < 0 ? QString() : m_groups[idx].name;
}

QJsonObject GroupRegistry::toJson() const
{
    QJsonArray arr;
    for (const Group& g : m_groups) {
        QJsonObject o;
        o[QStringLiteral("id")]   = g.id;
        o[QStringLiteral("name")] = g.name;
        arr.append(o);
    }
    QJsonArray shownArr;
    for (int id : m_shownIds)
        shownArr.append(id);
    QJsonObject root;
    root[QStringLiteral("nextId")]   = m_nextId;
    root[QStringLiteral("shownIds")] = shownArr;
    root[QStringLiteral("groups")]   = arr;
    return root;
}

void GroupRegistry::fromJson(const QJsonObject& o)
{
    m_groups.clear();
    int maxId = 0;
    const QJsonArray arr = o.value(QStringLiteral("groups")).toArray();
    for (const QJsonValue& v : arr) {
        const QJsonObject go = v.toObject();
        Group g{go.value(QStringLiteral("id")).toInt(), go.value(QStringLiteral("name")).toString()};
        if (g.id > 0)
            m_groups.append(g);
        maxId = qMax(maxId, g.id);
    }
    m_nextId = qMax(o.value(QStringLiteral("nextId")).toInt(1), maxId + 1);

    // Shown set. A groups.json written before multi-show carries a single "activeId"
    // scalar instead — read it as a one-group shown set so an existing config keeps
    // showing what it showed. Unknown/duplicate ids are dropped either way.
    m_shownIds.clear();
    QVector<int> candidates;
    if (o.contains(QStringLiteral("shownIds"))) {
        const QJsonArray shownArr = o.value(QStringLiteral("shownIds")).toArray();
        for (const QJsonValue& v : shownArr)
            candidates.append(v.toInt());
    } else {
        candidates.append(o.value(QStringLiteral("activeId")).toInt(0));
    }
    for (int id : candidates)
        if (id != 0 && indexOf(id) >= 0 && !m_shownIds.contains(id))
            m_shownIds.append(id);
}
