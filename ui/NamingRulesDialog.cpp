#include "ui/NamingRulesDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QTableWidgetItem>
#include <QPushButton>
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
// One reference row: a bold spanned heading (cmd == nullptr, key holds the
// heading's i18n key) or a token line (literal token notation in col 0 + i18n
// key for the description prose in col 1). Mirrors KeyBindingsDialog's model.
struct Row { const char* key; const char* cmd; };

const Row kRows[] = {
    // --- File-name rules (bold intro lines, no sub-rows) --------------------
    {"prefs.nr.suffixes_line", nullptr},
    {"prefs.nr.illegal_line",  nullptr},

    // --- Environment variables: %......% ------------------------------------
    {"prefs.nr.section.env", nullptr},
    {"%os%",           "prefs.nr.env.os"},
    {"%computername%", "prefs.nr.env.computername"},
    {"%username%",     "prefs.nr.env.username"},
    {"...",            "prefs.nr.env.more"},

    // --- Special variables --------------------------------------------------
    {"prefs.nr.section.special", nullptr},
    {"%title%",    "prefs.nr.special.title"},
    {"%title:50%", "prefs.nr.special.title_limited"},

    // --- Dates and times: $......$ ------------------------------------------
    {"prefs.nr.section.datetime", nullptr},
    {"d",    "prefs.nr.dt.d"},
    {"dd",   "prefs.nr.dt.dd"},
    {"ddd",  "prefs.nr.dt.ddd"},
    {"dddd", "prefs.nr.dt.dddd"},
    {"M",    "prefs.nr.dt.M"},
    {"MM",   "prefs.nr.dt.MM"},
    {"MMM",  "prefs.nr.dt.MMM"},
    {"MMMM", "prefs.nr.dt.MMMM"},
    {"yy",   "prefs.nr.dt.yy"},
    {"yyyy", "prefs.nr.dt.yyyy"},
    {"H",    "prefs.nr.dt.H"},
    {"HH",   "prefs.nr.dt.HH"},
    {"m",    "prefs.nr.dt.m"},
    {"mm",   "prefs.nr.dt.mm"},
    {"s",    "prefs.nr.dt.s"},
    {"ss",   "prefs.nr.dt.ss"},
    {"z",    "prefs.nr.dt.z"},
    {"zzz",  "prefs.nr.dt.zzz"},
    {"t",    "prefs.nr.dt.t"},
};

bool isSection(const Row& r) { return r.cmd == nullptr; }

// Strips per-cell hover/selected/focus states so the style never fills a cell;
// the only hover cue is HoverTable's full-row hollow rectangle. (== KB dialog.)
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

// A read-only QTableWidget that paints a full-row hollow rounded rectangle under
// the cursor (QSS :hover only styles one cell). Identical to KeyBindingsDialog's
// HoverTable — kept local so the two popups stay visually in lockstep.
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
        if (m_hoverRow < 0 || columnSpan(m_hoverRow, 0) > 1)   // skip spanned headings
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

NamingRulesDialog::NamingRulesDialog(QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("KeyBindingsDialog"));   // reuse the KB QSS block
    setWindowTitle(I18n::instance().t(QStringLiteral("prefs.output.naming_rules")));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    m_table = new HoverTable;
    m_table->setColumnCount(2);
    m_table->horizontalHeader()->hide();
    m_table->verticalHeader()->hide();
    m_table->setShowGrid(false);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setFocusPolicy(Qt::NoFocus);
    m_table->setAlternatingRowColors(true);
    m_table->setWordWrap(false);
    m_table->setItemDelegate(new PlainDelegate(m_table));
    root->addWidget(m_table, 1);

    populate();

    auto* bar = new QHBoxLayout;
    bar->addStretch();
    auto* copyBtn = new QPushButton(I18n::instance().t(QStringLiteral("prefs.kb.copy")));
    connect(copyBtn, &QPushButton::clicked, this, &NamingRulesDialog::copyToClipboard);
    bar->addWidget(copyBtn);
    root->addLayout(bar);

    // Reuse the Preferences theme so this popup matches the dialog that spawns it
    // (font-scaled to the current interface font).
    setStyleSheet(PrefsStyle::load());

    resize(680, 900);
}

void NamingRulesDialog::populate()
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
            f.setPointSizeF(ps > 0 ? ps + 3 : 15);
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
    m_table->verticalHeader()->setDefaultSectionSize(36);
}

void NamingRulesDialog::copyToClipboard() const
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
