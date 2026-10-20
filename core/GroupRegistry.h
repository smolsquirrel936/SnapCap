#pragma once

#include <QObject>
#include <QString>
#include <QVector>

class QJsonObject;

// The set of named pin groups plus which of them are currently "shown". Any number may be
// shown at once (including none), and the shown set is what the app turns into live pin
// windows. Pure bookkeeping (Qt Core only, no widgets/disk) → unit-tested
// (tst_group_registry); the app layer reacts to the signals to actually move/serialize pin
// windows and to delete a group's on-disk pins. A pin carries its single group id in
// PinModel::group() (0 = ungrouped, always visible). See claude-plan/Architecture-FileMap.md.
class GroupRegistry : public QObject {
    Q_OBJECT
public:
    struct Group {
        int id = 0;
        QString name;
    };

    explicit GroupRegistry(QObject* parent = nullptr);

    // Create a new (empty) group; returns its fresh id. Does NOT make it active.
    int  create(const QString& name);
    bool rename(int id, const QString& name);
    // Remove the group. Emits removed(id) FIRST (while it is still present and possibly
    // shown, so the app can discard its live pins), then drops it and un-shows it.
    bool remove(int id);

    // Show or hide one group; the others are untouched. Unknown ids (and 0 — the ungrouped
    // set is always visible and is not a group) are ignored, as is a no-op call. Emits
    // shown(id) / hidden(id) so the app performs the window work.
    void setShown(int id, bool show);

    bool isShown(int id) const { return m_shownIds.contains(id); }
    // The shown groups, in the order they were shown. Never contains 0.
    QVector<int> shownIds() const { return m_shownIds; }
    QString name(int id) const;                 // "" if unknown
    bool contains(int id) const { return indexOf(id) >= 0; }
    QVector<Group> groups() const { return m_groups; }
    int  count() const { return int(m_groups.size()); }

    QJsonObject toJson() const;
    void fromJson(const QJsonObject& o);        // data only — emits no signals

signals:
    void changed();                             // list/name/shown-set changed → refresh menus
    void shown(int id);                         // app: load id's pins from disk into live windows
    void hidden(int id);                        // app: serialize id's live pins back to disk
    void removed(int id);                       // app: discard id's live pins + on-disk store

private:
    int indexOf(int id) const;

    QVector<Group> m_groups;
    QVector<int> m_shownIds;                    // ids of every currently-shown group
    int m_nextId = 1;
};
