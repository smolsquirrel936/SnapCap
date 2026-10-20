#pragma once

#include <QIcon>
#include <QObject>

class QSystemTrayIcon;
class QMenu;
class QAction;
class ActionRegistry;
class Settings;
class GroupRegistry;

// System-tray presence. Menu items + click actions dispatch ActionIds through
// the ActionRegistry. Labels come from i18n keys and rebuild on locale change.
// The icon follows interface/trayIconMode: "color" = brand-blue "S", otherwise
// a monochrome "S" that adapts to the system light/dark colour scheme; both
// update live on the Settings change / colour-scheme change.
class TrayIcon : public QObject {
    Q_OBJECT
public:
    TrayIcon(ActionRegistry* actions, Settings* settings, GroupRegistry* groups,
             QObject* parent = nullptr);
    void show();
    // Show a balloon/toast from the tray icon (e.g. after a quick-save).
    void notify(const QString& title, const QString& body);

signals:
    void manageGroupsRequested();      // Group → Manage... clicked

private:
    void retranslate();
    void applyIconMode();          // set the tray icon from interface/trayIconMode
    QIcon iconForMode() const;
    void rebuildGroupMenu();       // repopulate the Group submenu from the registry

    ActionRegistry* m_actions = nullptr;
    Settings* m_settings = nullptr;
    GroupRegistry* m_groups = nullptr;
    QSystemTrayIcon* m_tray = nullptr;
    QMenu* m_menu = nullptr;
    QMenu* m_groupMenu = nullptr;
    QAction* m_snip = nullptr;
    QAction* m_paste = nullptr;
    QAction* m_closeAll = nullptr;
    QAction* m_destroyAll = nullptr;
    QAction* m_prefs = nullptr;
    QAction* m_quit = nullptr;
};
