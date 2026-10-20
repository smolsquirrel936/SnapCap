#include "ui/FontPickerDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QListWidget>
#include <QSpinBox>
#include <QPushButton>
#include <QFontDatabase>
#include <QMouseEvent>
#include <QRawFont>
#include <QHash>

#include "core/FontNameTable.h"
#include "core/i18n/I18n.h"
#include "ui/PrefsStyle.h"
#include "ui/WheelGuard.h"

QString FontPickerDialog::displayName(const QString& family)
{
    static QHash<QString, QString> cache;
    auto it = cache.constFind(family);
    if (it != cache.constEnd())
        return it.value();

    // Read the font's own 'name' table and prefer its native (CJK) family name.
    const QByteArray tbl = QRawFont::fromFont(QFont(family)).fontTable("name");
    const QString native = core::parseNativeFamilyName(tbl);
    const QString name = native.isEmpty() ? family : native;
    cache.insert(family, name);
    return name;
}

FontPickerDialog::FontPickerDialog(const QFont& initial, QWidget* parent, int maxSize)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("KeyBindingsDialog"));   // reuse the KB QSS block
    setWindowTitle(TR("prefs.fontpicker.title"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    // --- Family / Style / Size columns --------------------------------------
    auto* cols = new QGridLayout;
    cols->setHorizontalSpacing(12);

    cols->addWidget(new QLabel(TR("prefs.fontpicker.family")), 0, 0);
    cols->addWidget(new QLabel(TR("prefs.fontpicker.style")),  0, 1);
    cols->addWidget(new QLabel(TR("prefs.fontpicker.size")),   0, 2);

    m_families = new QListWidget;
    m_families->setSortingEnabled(true);
    m_styles = new QListWidget;
    m_size = new QSpinBox;
    m_size->setRange(6, qMax(6, maxSize));   // setValue below then clamps to this

    cols->addWidget(m_families, 1, 0);
    cols->addWidget(m_styles,   1, 1);
    cols->addWidget(m_size,     1, 2, Qt::AlignTop);
    cols->setColumnStretch(0, 3);
    cols->setColumnStretch(1, 2);
    cols->setColumnStretch(2, 1);
    root->addLayout(cols, 1);

    // --- Preview -------------------------------------------------------------
    m_preview = new QLabel(TR("prefs.fontpicker.sample"));
    m_preview->setObjectName(QStringLiteral("fontPreview"));
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setFrameShape(QFrame::StyledPanel);
    m_preview->setMinimumHeight(90);
    root->addWidget(m_preview);

    // --- Buttons -------------------------------------------------------------
    auto* bar = new QHBoxLayout;
    bar->addStretch();
    auto* okBtn = new QPushButton(TR("prefs.fontpicker.ok"));
    auto* cancelBtn = new QPushButton(TR("prefs.fontpicker.cancel"));
    okBtn->setDefault(true);
    connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    bar->addWidget(okBtn);
    bar->addWidget(cancelBtn);
    root->addLayout(bar);

    populateFamilies(initial.family());
    reloadStyles(QFontDatabase::styleString(initial));
    m_size->setValue(initial.pointSize() > 0 ? initial.pointSize() : 9);

    connect(m_families, &QListWidget::currentRowChanged, this, [this] {
        reloadStyles(currentStyle());   // keep the style name across family switches
        updatePreview();
    });
    connect(m_styles, &QListWidget::currentRowChanged, this, [this] { updatePreview(); });
    connect(m_size, qOverload<int>(&QSpinBox::valueChanged), this, [this] { updatePreview(); });

    updatePreview();

    WheelGuard::install(this);           // wheel adjusts the size box only once clicked
    setStyleSheet(PrefsStyle::load());   // match the Preferences theme (font-scaled)
    resize(640, 460);
}

// A press that reaches the dialog itself landed on blank space (the lists, the
// size box and the buttons accept their own), so drop focus — same "click
// elsewhere to unfocus" a Preferences page gets from its QScrollArea, and it
// disarms the wheel guard on the size box.
void FontPickerDialog::mousePressEvent(QMouseEvent* e)
{
    if (QWidget* fw = focusWidget())
        fw->clearFocus();
    QDialog::mousePressEvent(e);
}

void FontPickerDialog::populateFamilies(const QString& selectFamily)
{
    m_families->clear();
    int selectRow = -1;
    for (const QString& family : QFontDatabase::families()) {
        if (family.startsWith(QLatin1Char('@')))   // vertical-writing duplicates
            continue;
        auto* item = new QListWidgetItem(displayName(family));
        item->setData(Qt::UserRole, family);        // real Qt family for QFont
        item->setFont(QFont(family, 11));           // render the name in its own glyphs
        m_families->addItem(item);
    }
    // Sorting is on, so resolve the selection after all items are in place.
    for (int i = 0; i < m_families->count(); ++i) {
        if (m_families->item(i)->data(Qt::UserRole).toString() == selectFamily) {
            selectRow = i;
            break;
        }
    }
    if (selectRow < 0 && m_families->count() > 0)
        selectRow = 0;
    m_families->setCurrentRow(selectRow);
    if (selectRow >= 0)
        m_families->scrollToItem(m_families->item(selectRow), QAbstractItemView::PositionAtCenter);
}

void FontPickerDialog::reloadStyles(const QString& selectStyle)
{
    const QSignalBlocker block(m_styles);   // don't fire updatePreview mid-rebuild
    m_styles->clear();
    QStringList styles = QFontDatabase::styles(currentFamily());
    if (styles.isEmpty())
        styles << QStringLiteral("Regular");
    m_styles->addItems(styles);

    int row = styles.indexOf(selectStyle);
    if (row < 0)
        row = 0;
    m_styles->setCurrentRow(row);
}

QString FontPickerDialog::currentFamily() const
{
    auto* item = m_families->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

QString FontPickerDialog::currentStyle() const
{
    auto* item = m_styles->currentItem();
    return item ? item->text() : QString();
}

void FontPickerDialog::updatePreview()
{
    if (m_preview)
        m_preview->setFont(selectedFont());
}

QFont FontPickerDialog::selectedFont() const
{
    return QFontDatabase::font(currentFamily(), currentStyle(), m_size->value());
}

QFont FontPickerDialog::getFont(bool* ok, const QFont& initial, QWidget* parent, int maxSize)
{
    FontPickerDialog dlg(initial, parent, maxSize);
    const bool accepted = dlg.exec() == QDialog::Accepted;
    if (ok)
        *ok = accepted;
    return accepted ? dlg.selectedFont() : initial;
}
