#pragma once

#include <QDialog>
#include <functional>

class GroupRegistry;
class QListWidget;
class QPushButton;

// The "Manage groups" window (opened from the tray → Group → Manage...). Lists the
// named pin groups with their pin counts and offers New / Rename / Delete. All group
// data lives in the core GroupRegistry (this only drives it); deleting a group is
// destructive (discards its pins) and is confirmed. Pin counts come from an injected
// callback because the active group's pins are live windows while the rest are on disk
// — only the app layer can tell. See claude-plan/Architecture-FileMap.md.
class GroupManagerDialog : public QDialog {
    Q_OBJECT
public:
    GroupManagerDialog(GroupRegistry* groups, std::function<int(int)> pinCount,
                       QWidget* parent = nullptr);

    // Re-read the pin counts. GroupRegistry::changed drives this for name/list/active
    // changes; the app calls it directly when pin *membership* changes (assign, close),
    // which the registry doesn't know about.
    void refresh();

protected:
    void showEvent(QShowEvent* e) override;   // recompute counts every time it opens

private:
    void retranslate();
    int  selectedId() const;               // -1 if nothing selected
    void onNew();
    void onRename();
    void onDelete();

    GroupRegistry* m_groups = nullptr;
    std::function<int(int)> m_pinCount;
    QListWidget* m_list = nullptr;
    QPushButton* m_new = nullptr;
    QPushButton* m_rename = nullptr;
    QPushButton* m_delete = nullptr;
    QPushButton* m_close = nullptr;
};
