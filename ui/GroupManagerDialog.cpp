#include "ui/GroupManagerDialog.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QInputDialog>
#include <QMessageBox>
#include <QLineEdit>
#include <QShowEvent>
#include <QPalette>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QApplication>
#include <QStyle>
#include <QPainter>
#include <QBrush>

#include "core/GroupRegistry.h"
#include "core/i18n/I18n.h"

namespace {
// Item roles: the id is UserRole (used by selection); the pin-count string is drawn
// separately, right-aligned, by the delegate below.
constexpr int kCountRole = Qt::UserRole + 1;

// Draws each row as two columns: the group name left-aligned, the "(n)" count right-aligned.
// A plain QListWidgetItem string can't right-align part of itself, hence this delegate. The
// base delegate paints the background + name with the style's own selection-aware colour (e.g.
// the Windows 11 style keeps dark text on its LIGHT selection band). The count is overlaid in
// the item's normal text colour — never HighlightedText, which that light band would hide.
class GroupRowDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* p, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        QStyledItemDelegate::paint(p, option, index);   // background + selection + name (left)

        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);          // applies ForegroundRole → gray "(Ungrouped)"
        QColor pen;
        const QVariant fg = index.data(Qt::ForegroundRole);
        if (fg.isValid()) {
            pen = fg.value<QBrush>().color();
        } else {
            const QPalette::ColorGroup cg = (opt.state & QStyle::State_Enabled)
                                                ? QPalette::Normal : QPalette::Disabled;
            pen = opt.palette.color(cg, QPalette::Text);
        }
        p->save();
        p->setPen(pen);
        p->drawText(opt.rect.adjusted(6, 0, -6, 0), Qt::AlignRight | Qt::AlignVCenter,
                    index.data(kCountRole).toString());
        p->restore();
    }
};
} // namespace

GroupManagerDialog::GroupManagerDialog(GroupRegistry* groups, std::function<int(int)> pinCount,
                                       QWidget* parent)
    : QDialog(parent), m_groups(groups), m_pinCount(std::move(pinCount))
{
    // Non-modal: the user must be able to right-click pins / use the tray to change group
    // membership while this window is open, and watch the counts update live.
    setModal(false);

    m_list = new QListWidget(this);
    m_list->setItemDelegate(new GroupRowDelegate(m_list));
    m_new    = new QPushButton(this);
    m_rename = new QPushButton(this);
    m_delete = new QPushButton(this);
    m_close  = new QPushButton(this);

    auto* buttons = new QVBoxLayout;
    buttons->addWidget(m_new);
    buttons->addWidget(m_rename);
    buttons->addWidget(m_delete);
    buttons->addStretch();
    buttons->addWidget(m_close);

    auto* row = new QHBoxLayout(this);
    row->addWidget(m_list, 1);
    row->addLayout(buttons);

    connect(m_new,    &QPushButton::clicked, this, &GroupManagerDialog::onNew);
    connect(m_rename, &QPushButton::clicked, this, &GroupManagerDialog::onRename);
    connect(m_delete, &QPushButton::clicked, this, &GroupManagerDialog::onDelete);
    connect(m_close,  &QPushButton::clicked, this, &QDialog::accept);
    connect(m_list, &QListWidget::itemSelectionChanged, this, [this] {
        const bool sel = selectedId() >= 1;   // only real groups (id >= 1), not "(Ungrouped)"
        m_rename->setEnabled(sel);
        m_delete->setEnabled(sel);
    });
    if (m_groups) {
        connect(m_groups, &GroupRegistry::changed, this, &GroupManagerDialog::refresh);
        connect(&I18n::instance(), &I18n::localeChanged, this, &GroupManagerDialog::retranslate);
    }

    retranslate();
    refresh();
    resize(360, 260);
}

void GroupManagerDialog::showEvent(QShowEvent* e)
{
    QDialog::showEvent(e);
    refresh();   // counts can have changed since the last time it was open
}

void GroupManagerDialog::retranslate()
{
    auto& tr = I18n::instance();
    setWindowTitle(tr.t(QStringLiteral("group.manage.title")));
    m_new->setText(tr.t(QStringLiteral("group.manage.new")));
    m_rename->setText(tr.t(QStringLiteral("group.manage.rename")));
    m_delete->setText(tr.t(QStringLiteral("group.manage.delete")));
    m_close->setText(tr.t(QStringLiteral("group.manage.close")));
    refresh();   // "{name} ({count})" rows use a translated pattern
}

int GroupManagerDialog::selectedId() const
{
    QListWidgetItem* it = m_list->currentItem();
    return (it && m_list->currentRow() >= 0) ? it->data(Qt::UserRole).toInt() : -1;
}

void GroupManagerDialog::refresh()
{
    if (!m_groups)
        return;
    const int keep = selectedId();
    m_list->clear();
    auto& tr = I18n::instance();
    auto countText = [&tr](int n) {
        return tr.t(QStringLiteral("group.manage.count"), {{QStringLiteral("count"), n}});
    };

    // A fixed "(Ungrouped)" row at the top: always present and greyed. It is selectable (so
    // clicking it deselects any real group and greys out Rename/Delete) but its id 0 is never
    // a real group, so those buttons stay disabled. Shows how many pins belong to no group.
    {
        auto* ung = new QListWidgetItem(tr.t(QStringLiteral("group.manage.ungrouped")), m_list);
        ung->setData(Qt::UserRole, 0);
        ung->setData(kCountRole, countText(m_pinCount ? m_pinCount(0) : 0));
        ung->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
        if (keep == 0)
            m_list->setCurrentItem(ung);
    }

    for (const GroupRegistry::Group& g : m_groups->groups()) {
        auto* it = new QListWidgetItem(g.name, m_list);
        it->setData(Qt::UserRole, g.id);
        it->setData(kCountRole, countText(m_pinCount ? m_pinCount(g.id) : 0));
        if (g.id == keep)
            m_list->setCurrentItem(it);
    }
    const bool sel = selectedId() >= 1;   // only real groups enable Rename/Delete
    m_rename->setEnabled(sel);
    m_delete->setEnabled(sel);
}

void GroupManagerDialog::onNew()
{
    auto& tr = I18n::instance();
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr.t(QStringLiteral("group.manage.new_title")),
        tr.t(QStringLiteral("group.manage.name_label")), QLineEdit::Normal, QString(), &ok);
    if (ok && !name.trimmed().isEmpty())
        m_groups->create(name.trimmed());
}

void GroupManagerDialog::onRename()
{
    const int id = selectedId();
    if (id < 1)                // never the "(Ungrouped)" row (id 0) or an empty selection
        return;
    auto& tr = I18n::instance();
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr.t(QStringLiteral("group.manage.rename_title")),
        tr.t(QStringLiteral("group.manage.name_label")), QLineEdit::Normal,
        m_groups->name(id), &ok);
    if (ok && !name.trimmed().isEmpty())
        m_groups->rename(id, name.trimmed());
}

void GroupManagerDialog::onDelete()
{
    const int id = selectedId();
    if (id < 1)                // never the "(Ungrouped)" row (id 0) or an empty selection
        return;
    auto& tr = I18n::instance();
    QMessageBox box(QMessageBox::Warning, tr.t(QStringLiteral("group.manage.delete_title")),
                    tr.t(QStringLiteral("group.manage.delete_text"),
                         {{QStringLiteral("name"), m_groups->name(id)}}),
                    QMessageBox::Yes | QMessageBox::No, this);
    box.setDefaultButton(QMessageBox::No);
    if (box.exec() == QMessageBox::Yes)
        m_groups->remove(id);
}
