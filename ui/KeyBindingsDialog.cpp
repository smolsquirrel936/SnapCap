#include "ui/KeyBindingsDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QTableWidgetItem>
#include <QPushButton>
#include <QLabel>
#include <QApplication>
#include <QClipboard>
#include <QFont>
#include <QPainter>
#include <QMouseEvent>
#include <QEvent>
#include <QStyledItemDelegate>
#include <QStyle>

#include "core/i18n/I18n.h"
#include "ui/PrefsStyle.h"

namespace {
// One reference row: a section heading (cmd == nullptr, key holds the title i18n
// key) or a binding (literal shortcut notation + i18n key for the command prose).
struct Row { const char* key; const char* cmd; };

const Row kRows[] = {
    // --- Snip only ----------------------------------------------------------
    {"prefs.kb.section.snip", nullptr},
    {"Tab",                 "prefs.kb.snip.detect_toggle"},
    {"` or !",              "prefs.kb.snip.cursor_toggle"},
    {"Ctrl + A",            "prefs.kb.snip.fullscreen"},
    {"R",                   "prefs.kb.snip.repeat_last"},
    {", (Comma)",           "prefs.kb.snip.prev_record"},
    {". (Period)",          "prefs.kb.snip.next_record"},
    {"F5",                  "prefs.kb.snip.refresh"},
    {"Ctrl + Arrow keys",   "prefs.kb.snip.enlarge"},
    {"Shift + Arrow keys",  "prefs.kb.snip.shrink"},
    {"Ctrl + T",            "prefs.kb.snip.pin"},
    {"Enter",               "prefs.kb.snip.copy_quit"},
    {"Esc",                 "prefs.kb.snip.abort"},
    {"Mouse right-click",   "prefs.kb.snip.redecide"},

    // --- Paste only ---------------------------------------------------------
    {"prefs.kb.section.paste", nullptr},
    {"1",                   "prefs.kb.paste.rotate_cw"},
    {"2",                   "prefs.kb.paste.rotate_ccw"},
    {"3",                   "prefs.kb.paste.flip_h"},
    {"4",                   "prefs.kb.paste.flip_v"},
    {"5",                   "prefs.kb.paste.grayscale"},
    {"6",                   "prefs.kb.paste.invert"},
    {"Esc",                 "prefs.kb.paste.hide_or_close"},
    {"Shift + Esc",         "prefs.kb.paste.destroy"},
    {"Ctrl + W",            "prefs.kb.paste.close"},
    {"Ctrl + A",            "prefs.kb.paste.select_all"},
    {"Ctrl + Shift + C",    "prefs.kb.paste.copy_text"},
    {"Ctrl + V",            "prefs.kb.paste.replace"},
    {"Ctrl + Shift + P",    "prefs.kb.paste.preferences"},

    // --- Common -------------------------------------------------------------
    {"prefs.kb.section.common", nullptr},
    {"Alt",                 "prefs.kb.common.magnifier"},
    {"Shift",               "prefs.kb.common.formats"},
    {"C",                   "prefs.kb.common.copy_color"},
    {"W, S, A, D",          "prefs.kb.common.move_cursor"},
    {"Arrow keys",          "prefs.kb.common.move_window"},
    {"Space",               "prefs.kb.common.paint_bar"},
    {"Ctrl + Z",            "prefs.kb.common.undo"},
    {"Ctrl + Shift + Z",    "prefs.kb.common.remove_all"},
    {"Ctrl + C",            "prefs.kb.common.copy"},
    {"Ctrl + Y",            "prefs.kb.common.redo"},
    {"Ctrl + S",            "prefs.kb.common.save"},
    {"Ctrl + Shift + S",    "prefs.kb.common.quick_save"},
    {"Mouse right-click",   "prefs.kb.common.deselect"},
    {"Mouse scroll or 1/2 or [/]", "prefs.kb.common.pen_width"},
};

bool isSection(const Row& r) { return r.cmd == nullptr; }

// Paints cells with no per-cell highlight: strips the hover/selected/focus
// states so the style never fills a hovered or current cell. The only hover cue
// is HoverTable's full-row hollow rectangle drawn on top.
class PlainDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override
    {
        QStyleOptionViewItem o = opt;
        o.state &= ~(QStyle::State_MouseOver | QStyle::State_Selected | QStyle::State_HasFocus);
        QStyledItemDelegate::paint(p, o, idx);
    }
};

// A QTableWidget that draws a full-row hollow rectangle under the cursor. QSS
// :hover only styles the single hovered cell, so the row-wide outline is painted
// manually over the viewport. No selection — this is a read-only reference.
// Overriding the protected event virtuals needs no Q_OBJECT/moc.
class HoverTable : public QTableWidget {
public:
    HoverTable()
    {
        setMouseTracking(true);
        viewport()->setMouseTracking(true);
    }

protected:
    void mouseMoveEvent(QMouseEvent* e) override
    {
        // Deliberately not forwarded to the base view: that would set the
        // internal hover/current cell and let the style paint a solid highlight
        // over it. We only want our own full-row hollow rectangle.
        setHoverRow(rowAt(e->position().toPoint().y()));
        e->accept();
    }
    void leaveEvent(QEvent* e) override
    {
        setHoverRow(-1);
        QTableWidget::leaveEvent(e);
    }
    void paintEvent(QPaintEvent* e) override
    {
        QTableWidget::paintEvent(e);
        // Skip when nothing is hovered or the row is a section heading (spanned).
        if (m_hoverRow < 0 || columnSpan(m_hoverRow, 0) > 1)
            return;
        QRect r = visualRect(model()->index(m_hoverRow, 0));
        if (!r.isValid())
            return;
        r.setLeft(2);
        r.setRight(viewport()->width() - 3);
        QPainter p(viewport());
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor(0x2d, 0x7d, 0xff), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(r).adjusted(0.5, 1.5, -0.5, -1.5), 5, 5);
    }

private:
    void setHoverRow(int row)
    {
        if (row != m_hoverRow) {
            m_hoverRow = row;
            viewport()->update();
        }
    }
    int m_hoverRow = -1;
};
} // namespace

KeyBindingsDialog::KeyBindingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("KeyBindingsDialog"));
    setWindowTitle(I18n::instance().t(QStringLiteral("prefs.kb.title")));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    m_table = new HoverTable;
    m_table->setColumnCount(2);
    m_table->horizontalHeader()->hide();
    m_table->verticalHeader()->hide();
    m_table->setShowGrid(false);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);   // hover-only highlight
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setFocusPolicy(Qt::NoFocus);
    m_table->setAlternatingRowColors(true);
    m_table->setWordWrap(false);
    m_table->setItemDelegate(new PlainDelegate(m_table));   // no per-cell highlight
    root->addWidget(m_table, 1);

    populate();

    auto* bar = new QHBoxLayout;
    bar->addStretch();
    auto* copyBtn = new QPushButton(I18n::instance().t(QStringLiteral("prefs.kb.copy")));
    connect(copyBtn, &QPushButton::clicked, this, &KeyBindingsDialog::copyToClipboard);
    bar->addWidget(copyBtn);
    root->addLayout(bar);

    // Reuse the Preferences theme so this popup matches the dialog that spawns it
    // (font-scaled to the current interface font).
    setStyleSheet(PrefsStyle::load());

    resize(800, 1500);
}

void KeyBindingsDialog::populate()
{
    auto& tr = I18n::instance();
    const int n = int(sizeof(kRows) / sizeof(kRows[0]));
    m_table->setRowCount(n);

    for (int i = 0; i < n; ++i) {
        const Row& r = kRows[i];
        if (isSection(r)) {
            auto* item = new QTableWidgetItem(tr.t(QString::fromLatin1(r.key)));
            QFont f = item->font();
            f.setBold(true);
            const qreal ps = f.pointSizeF();
            f.setPointSizeF(ps > 0 ? ps + 3 : 15);   // paragraph titles stand out
            item->setFont(f);
            m_table->setItem(i, 0, item);
            m_table->setSpan(i, 0, 1, 2);
        } else {
            auto* keyItem = new QTableWidgetItem(QString::fromLatin1(r.key));
            keyItem->setTextAlignment(Qt::AlignCenter);
            m_table->setItem(i, 0, keyItem);
            m_table->setItem(i, 1, new QTableWidgetItem(tr.t(QString::fromLatin1(r.cmd))));
        }
    }

    m_table->setColumnWidth(0, 200);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    // Uniform, compact rows (resizeRowsToContents mixes with setSpan/padding to
    // give some single-line rows double height). Long wrapping lines still get
    // room via the taller default section size.
    m_table->verticalHeader()->setDefaultSectionSize(40);
}

void KeyBindingsDialog::copyToClipboard() const
{
    auto& tr = I18n::instance();
    QString out;
    for (const Row& r : kRows) {
        if (isSection(r)) {
            out += QLatin1Char('\n') + tr.t(QString::fromLatin1(r.key)) + QLatin1Char('\n');
        } else {
            out += QString::fromLatin1(r.key) + QLatin1Char('\t')
                 + tr.t(QString::fromLatin1(r.cmd)) + QLatin1Char('\n');
        }
    }
    QApplication::clipboard()->setText(out.trimmed());
}
