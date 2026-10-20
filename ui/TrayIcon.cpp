#include "ui/TrayIcon.h"

#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QFont>

#include "core/action/ActionRegistry.h"
#include "core/i18n/I18n.h"
#include "core/Settings.h"
#include "core/GroupRegistry.h"

namespace {
// The app glyph: a rounded square "S" filled with `fill` and a white letter.
// (Real SVG asset comes later.)
QIcon makeIcon(const QColor& fill)
{
    QPixmap pm(64, 64);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawRoundedRect(QRectF(4, 4, 56, 56), 12, 12);
    p.setPen(Qt::white);
    QFont f = p.font();
    f.setBold(true);
    f.setPointSize(28);
    p.setFont(f);
    p.drawText(pm.rect(), Qt::AlignCenter, QStringLiteral("S"));
    p.end();
    return QIcon(pm);
}
} // namespace

TrayIcon::TrayIcon(ActionRegistry* actions, Settings* settings, GroupRegistry* groups,
                   QObject* parent)
    : QObject(parent), m_actions(actions), m_settings(settings), m_groups(groups)
{
    m_tray = new QSystemTrayIcon(this);
    applyIconMode();
    m_menu = new QMenu();

    m_snip  = m_menu->addAction(QString());
    m_paste = m_menu->addAction(QString());
    m_menu->addSeparator();
    m_closeAll   = m_menu->addAction(QString());
    m_destroyAll = m_menu->addAction(QString());
    m_menu->addSeparator();
    // Group submenu — repopulated from the registry each time it opens.
    m_groupMenu = m_menu->addMenu(QString());
    connect(m_groupMenu, &QMenu::aboutToShow, this, &TrayIcon::rebuildGroupMenu);
    m_menu->addSeparator();
    m_prefs = m_menu->addAction(QString());
    m_quit  = m_menu->addAction(QString());
    m_tray->setContextMenu(m_menu);

    connect(m_snip,  &QAction::triggered, this, [this] { m_actions->dispatch(ActionId::Snip); });
    connect(m_paste, &QAction::triggered, this, [this] { m_actions->dispatch(ActionId::Paste); });
    connect(m_closeAll,   &QAction::triggered, this, [this] { m_actions->dispatch(ActionId::CloseAllPins); });
    connect(m_destroyAll, &QAction::triggered, this, [this] { m_actions->dispatch(ActionId::DestroyAllPins); });
    connect(m_prefs, &QAction::triggered, this, [this] { m_actions->dispatch(ActionId::OpenPreferences); });
    connect(m_quit,  &QAction::triggered, this, [this] { m_actions->dispatch(ActionId::Quit); });

    // Left-click = Snip, middle-click = Paste (per Features.md §5).
    connect(m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger)
                    m_actions->dispatch(ActionId::Snip);
                else if (reason == QSystemTrayIcon::MiddleClick)
                    m_actions->dispatch(ActionId::Paste);
            });

    connect(&I18n::instance(), &I18n::localeChanged, this, &TrayIcon::retranslate);
    retranslate();

    // Live icon updates when the tray mode or the custom colour changes.
    if (m_settings) {
        connect(m_settings, &Settings::changed, this, [this](const QString& key) {
            if (key == QStringLiteral("interface/trayIconMode")
                || key == QStringLiteral("interface/trayIconColor"))
                applyIconMode();
        });
    }
}

void TrayIcon::show()
{
    m_tray->show();
}

void TrayIcon::notify(const QString& title, const QString& body)
{
    if (m_tray)
        m_tray->showMessage(title, body, QSystemTrayIcon::Information);
}

QIcon TrayIcon::iconForMode() const
{
    const QString mode = m_settings
        ? m_settings->value(QStringLiteral("interface/trayIconMode"),
                            QStringLiteral("color")).toString()
        : QStringLiteral("color");
    // "custom" = user-picked colour; anything else = the brand blue.
    QColor fill(0x2d, 0x7d, 0xff);
    if (mode == QStringLiteral("custom") && m_settings) {
        const QColor c(m_settings->value(QStringLiteral("interface/trayIconColor"),
                                         QStringLiteral("#2D7DFF")).toString());
        if (c.isValid())
            fill = c;
    }
    return makeIcon(fill);
}

void TrayIcon::applyIconMode()
{
    m_tray->setIcon(iconForMode());
}

void TrayIcon::rebuildGroupMenu()
{
    auto& tr = I18n::instance();
    m_groupMenu->clear();
    if (m_groups) {
        const auto groups = m_groups->groups();
        if (groups.isEmpty()) {
            QAction* none = m_groupMenu->addAction(tr.t(QStringLiteral("menu.group_none")));
            none->setEnabled(false);
        }
        for (const GroupRegistry::Group& g : groups) {
            QAction* a = m_groupMenu->addAction(g.name);
            a->setCheckable(true);
            a->setChecked(m_groups->isShown(g.id));
            const int id = g.id;
            // Independent toggles, not radios: any number of groups can be shown at once,
            // so each item only shows/hides its own group.
            connect(a, &QAction::triggered, this,
                    [this, id](bool checked) { m_groups->setShown(id, checked); });
        }
        m_groupMenu->addSeparator();
    }
    QAction* manage = m_groupMenu->addAction(tr.t(QStringLiteral("menu.group_manage")));
    connect(manage, &QAction::triggered, this, [this] { emit manageGroupsRequested(); });
}

void TrayIcon::retranslate()
{
    auto& tr = I18n::instance();
    m_snip->setText(tr.t(QStringLiteral("menu.snip")));
    m_paste->setText(tr.t(QStringLiteral("menu.paste")));
    m_closeAll->setText(tr.t(QStringLiteral("menu.close_all")));
    m_destroyAll->setText(tr.t(QStringLiteral("menu.destroy_all")));
    m_groupMenu->setTitle(tr.t(QStringLiteral("menu.group")));
    m_prefs->setText(tr.t(QStringLiteral("menu.preferences")));
    m_quit->setText(tr.t(QStringLiteral("menu.quit")));
    m_tray->setToolTip(tr.t(QStringLiteral("app.name")));
}
