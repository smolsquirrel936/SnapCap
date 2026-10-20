#include "ui/PreferencesDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QComboBox>
#include <QStyle>
#include <QStyleHints>
#include <QTimer>
#include <QCheckBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QStandardItemModel>
#include <QSlider>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QAction>
#include <QCursor>
#include <QListWidget>
#include <QMenu>
#include <QInputDialog>
#include <QStackedWidget>
#include <QScrollArea>
#include <QFrame>
#include <QFileDialog>
#include <QMessageBox>
#include <QKeySequence>
#include <QShowEvent>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QDesktopServices>
#include <QUrl>
#include <QCoreApplication>
#include <QApplication>
#include <QFontInfo>
#include <QScreen>
#include <QPixmap>
#include <QPainter>
#include <QPolygonF>
#include <QFont>
#include <QFontMetrics>
#include <QIcon>
#include <QStylePainter>
#include <QStyleOptionButton>
#include <QStyleOptionComboBox>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>

#include "core/Settings.h"
#include "core/ClipboardContent.h"
#include "core/FilenameTemplate.h"
#include "core/Logging.h"
#include "core/action/ActionRegistry.h"
#include "core/i18n/I18n.h"
#include "core/recording/RecordingSpec.h"
#include "ui/ColorDialog.h"
#include "ui/CustomSnipDialog.h"
#include "ui/FontPickerDialog.h"
#include "ui/HotkeyEdit.h"
#include "ui/KeyBindingsDialog.h"
#include "ui/NamingRulesDialog.h"
#include "ui/OcrBar.h"
#include "ui/PrefsStyle.h"
#include "ui/WheelGuard.h"
#include "platform/platform.h"

#include <algorithm>  // std::find_if
#include <iterator>   // std::size

namespace {
// The interface point size every authored size in this file is drawn against. The
// window (applyFontScaledSize) and the control widths (setScaledWidth) both scale by
// the ratio of the live font to this, so a larger interface font grows the whole page
// in proportion. NOTE: 10, not the 9 the QSS was written against — two different
// reference points that must not be "unified".
constexpr double kDesignPt = 10.0;

// Control widths in logical px at kDesignPt. kControlW is the shared default every
// setting row gets; kNarrowControlW is for short fixed vocabularies (Language, GPU
// acceleration, Log level, Theme), whose 240 px box was mostly empty space.
constexpr int kControlW = 240;
constexpr int kNarrowControlW = 133;

void applyOneScaledWidth(QWidget* w, int minPx, int maxPx, double s)
{
    w->setMinimumWidth(minPx > 0 ? int(minPx * s) : 0);
    w->setMaximumWidth(maxPx > 0 ? int(maxPx * s) : QWIDGETSIZE_MAX);
}

// A push button that elides its label instead of forcing the button wider. Used by
// the Interface-font row, which auto-fits to the chosen family name but is capped —
// a long CJK family would otherwise crush the label column. The style option is
// re-used rather than painted by hand so the QSS bevel still applies.
class ElidedButton : public QPushButton {
public:
    using QPushButton::QPushButton;

protected:
    void paintEvent(QPaintEvent*) override
    {
        QStylePainter p(this);
        QStyleOptionButton opt;
        initStyleOption(&opt);
        const QRect r = style()->subElementRect(QStyle::SE_PushButtonContents, &opt, this);
        opt.text = fontMetrics().elidedText(text(), Qt::ElideRight, r.width());
        p.drawControl(QStyle::CE_PushButton, opt);
    }
};

// Centres the rows of a combo's popup. The popup is drawn by a delegate reading the
// MODEL, so a stylesheet cannot reach it and per-item Qt::TextAlignmentRole would have
// to be re-set every time an item is added or retranslated. Overriding the option
// instead covers items added at any point in the dialog's life.
class CenteredItemDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

protected:
    void initStyleOption(QStyleOptionViewItem* opt, const QModelIndex& idx) const override
    {
        QStyledItemDelegate::initStyleOption(opt, idx);
        opt->displayAlignment = Qt::AlignCenter;
    }
};

// A combo whose collapsed text is centred to match its popup. Qt hardcodes
// Qt::AlignLeft for the closed label (QCommonStyle's CE_ComboBoxLabel) and QSS
// `text-align` is ignored on QComboBox, so the label has to be painted here — the
// BOX itself still goes through the style, keeping the QSS border/background/arrow.
// Painting it also lets the text elide, which CE_ComboBoxLabel does not do: it clips
// mid-glyph instead, which the narrow (kNarrowControlW) rows can actually reach.
// drawItemText + the enabled flag is what keeps a disabled combo on the stylesheet's
// `QComboBox:disabled` colour, which QStyleSheetStyle resolves into the palette's
// Disabled group at polish time.
class CenteredComboBox : public QComboBox {
public:
    explicit CenteredComboBox(QWidget* parent = nullptr) : QComboBox(parent)
    {
        setItemDelegate(new CenteredItemDelegate(this));
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QStylePainter p(this);
        QStyleOptionComboBox opt;
        initStyleOption(&opt);
        p.drawComplexControl(QStyle::CC_ComboBox, opt);
        if (opt.currentText.isEmpty())
            return;
        // NOT centred in SC_ComboBoxEditField: that rect reserves the 18 px ::drop-down
        // zone on the right, so centring inside it lands ~9 px LEFT of the box's real
        // middle — which reads as off-centre against the popup, whose rows centre on the
        // full width. (At kNarrowControlW that 9 px is plainly visible.) This stylesheet
        // paints nothing in that zone anyway: `QComboBox::drop-down` sets only a width and
        // there is no `::down-arrow` rule, so QStyleSheetStyle draws no arrow. Mirror the
        // field's own left inset on the right instead and centre on the true middle, which
        // also buys the label ~16 px more room before it has to elide.
        const QRect field = style()->subControlRect(QStyle::CC_ComboBox, &opt,
                                                    QStyle::SC_ComboBoxEditField, this);
        const int inset = qMax(1, field.left() - rect().left());
        const QRect r(rect().left() + inset, field.top(),
                      qMax(0, width() - 2 * inset), field.height());
        style()->drawItemText(&p, r, Qt::AlignCenter, opt.palette,
                              opt.state & QStyle::State_Enabled,
                              fontMetrics().elidedText(opt.currentText, Qt::ElideRight, r.width()),
                              QPalette::Text);
    }
};

// The Control-page hotkey rows. Every row maps to a live ActionRegistry action and
// mirrors res/keymap.default.yaml. `section` groups rows under the Control page's
// four inline headers (indices into kSectionTitles).
enum CtrlSection { Core = 0, View = 1, Tools = 2, Misc = 3 };
struct GlobalDefault { const char* name; const char* seq; const char* labelKey; int section; };
const GlobalDefault kGlobalDefaults[] = {
    {"snip",         "F1",       "prefs.hotkey.snip",              Core},
    {"snipCopy",     "Ctrl+F1",  "prefs.hotkey.snip_copy",         Core},
    {"snipActiveWindow", "",     "prefs.hotkey.snip_active_window",Core},
    {"quickSnip",    "",         "prefs.hotkey.quick_snip",        Core},
    {"customSnip",   "Shift+F1", "prefs.hotkey.custom_snip",       Core},
    {"paste",        "F3",       "prefs.hotkey.paste",             Core},
    {"hideAll",      "Shift+F3", "prefs.hotkey.hide_all",          View},
    {"closeAll",     "",         "prefs.hotkey.close_all",         View},
    {"destroyAll",   "",         "prefs.hotkey.destroy_all",       View},
    {"pickColor",    "",         "prefs.hotkey.pick_color",        Tools},
    {"scanBarcode",  "",         "prefs.hotkey.scan_barcode",      Tools},
    {"clickThrough", "",         "prefs.hotkey.click_through",     Misc},
};

// The four Control-page section headers, in display order (index == CtrlSection).
const char* kSectionTitles[] = {
    "prefs.control.section.core",
    "prefs.control.section.view",
    "prefs.control.section.tools",
    "prefs.control.section.misc",
};

// Validator badge: filled green circle with a white check, or red with a cross.
QPixmap makeStatusPixmap(bool ok)
{
    QPixmap pm(18, 18);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(ok ? QColor(0x22, 0x9f, 0x5a) : QColor(0xd0, 0x40, 0x40));
    p.drawEllipse(QRectF(0, 0, 18, 18));
    QPen pen(Qt::white);
    pen.setWidthF(2.0);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    if (ok) {
        p.drawPolyline(QPolygonF({QPointF(5, 9.5), QPointF(8, 12.5), QPointF(13, 6)}));
    } else {
        p.drawLine(QPointF(6, 6), QPointF(12, 12));
        p.drawLine(QPointF(12, 6), QPointF(6, 12));
    }
    p.end();
    return pm;
}

// Small blue "i" badge used as a hint marker (see makeInfoLabel).
QPixmap makeInfoPixmap()
{
    QPixmap pm(16, 16);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x8a, 0x92, 0xa3));
    p.drawEllipse(QRectF(0, 0, 16, 16));
    p.setPen(Qt::white);
    QFont f = p.font();
    f.setBold(true);
    f.setPixelSize(11);
    p.setFont(f);
    p.drawText(pm.rect(), Qt::AlignCenter, QStringLiteral("i"));
    p.end();
    return pm;
}

// A "(i)" hint icon whose tooltip is an i18n string (set once at build time).
// Used across the Output page.
static QLabel* makeInfoLabel(const QString& tipKey)
{
    auto* lbl = new QLabel;
    lbl->setPixmap(makeInfoPixmap());
    lbl->setToolTip(I18n::instance().t(tipKey));
    return lbl;
}

// Factory default for the manual-save filename template ("$...$" date/time spans).
static QString defaultFileNameTemplate()
{
    return QStringLiteral("SnapCap_$yyyy-MM-dd_HH-mm-ss$.png");
}

// Factory default quick-save path template: Desktop + the filename template.
static QString defaultQuickSavePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    return QDir(dir).filePath(defaultFileNameTemplate());
}

// Factory default auto-save path template: Pictures\SnapCap_AutoSave + the filename template.
static QString defaultAutoSavePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    return QDir(dir).filePath(QStringLiteral("SnapCap_AutoSave/") + defaultFileNameTemplate());
}

// Brand mark for the header — same blue rounded-square "S" as TrayIcon::makeIcon().
QPixmap makeBrandPixmap(int size)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x2d, 0x7d, 0xff));
    const qreal r = size * 0.22;
    p.drawRoundedRect(QRectF(0, 0, size, size), r, r);
    p.setPen(Qt::white);
    QFont f = p.font();
    f.setBold(true);
    f.setPointSizeF(size * 0.5);
    p.setFont(f);
    p.drawText(pm.rect(), Qt::AlignCenter, QStringLiteral("S"));
    p.end();
    return pm;
}
} // namespace

PreferencesDialog::PreferencesDialog(Settings* settings, ActionRegistry* actions,
                                     platform::IGlobalHotkey* probe,
                                     platform::IAutostart* autostart, QWidget* parent)
    : QDialog(parent), m_settings(settings), m_actions(actions), m_probe(probe),
      m_autostart(autostart)
{
    setObjectName(QStringLiteral("PreferencesDialog"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(buildHeader());

    // --- body: sidebar | (sub-category column) | content stack ---------------
    auto* body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);

    auto* navCol = new QWidget;
    navCol->setFixedWidth(200);
    auto* navLay = new QVBoxLayout(navCol);
    navLay->setContentsMargins(12, 16, 12, 16);
    navLay->setSpacing(6);
    auto* navHeader = new QLabel;
    navHeader->setObjectName(QStringLiteral("PrefsNavHeader"));
    m_labels.append({navHeader, QStringLiteral("prefs.nav_header")});
    navLay->addWidget(navHeader);
    m_sidebar = new QListWidget;
    m_sidebar->setObjectName(QStringLiteral("PrefsSidebar"));
    m_sidebar->setFrameShape(QFrame::NoFrame);
    navLay->addWidget(m_sidebar, 1);
    body->addWidget(navCol);

    m_subList = new QListWidget;
    m_subList->setObjectName(QStringLiteral("PrefsSubcats"));
    m_subList->setFrameShape(QFrame::NoFrame);
    m_subList->setFixedWidth(160);
    m_subList->hide();   // dormant until a category declares >1 sub-category
    body->addWidget(m_subList);

    m_stack = new QStackedWidget;
    auto* scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("PrefsContent"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(m_stack);
    // The content stack is sized to each page's own height (see
    // adjustStackHeightForCurrentPage), so on short pages the viewport shows below
    // it. Name the viewport so the QSS can paint it the panel colour, letting the
    // right-panel background fill top-to-bottom instead of stopping at the last item.
    scroll->viewport()->setObjectName(QStringLiteral("PrefsViewport"));
    body->addWidget(scroll, 1);

    root->addLayout(body, 1);

    // --- categories (one page each; sub-category column stays hidden) ---------
    addCategory(buildGeneralTab(),   QStringLiteral("prefs.tab.general"));
    addCategory(buildInterfaceTab(), QStringLiteral("prefs.tab.interface"));
    addCategory(buildSnipTab(),      QStringLiteral("prefs.tab.snip"));
    addCategory(buildPasteTab(),     QStringLiteral("prefs.tab.paste"));
    addCategory(buildOutputTab(),    QStringLiteral("prefs.tab.output"));
    addCategory(buildControlTab(),   QStringLiteral("prefs.tab.control"));
    addCategory(buildOcrTab(),       QStringLiteral("prefs.tab.ocr"));
    addCategory(buildRecordingTab(), QStringLiteral("prefs.tab.recording"));
    addCategory(buildUpdateTab(),    QStringLiteral("prefs.tab.update"));
    addCategory(buildAboutTab(),     QStringLiteral("prefs.tab.about"));

    connect(m_sidebar, &QListWidget::currentRowChanged, this, &PreferencesDialog::onCategorySelected);
    connect(m_subList, &QListWidget::currentRowChanged, this, &PreferencesDialog::onSubCategorySelected);

    loadStyleSheet();
    connect(&I18n::instance(), &I18n::localeChanged, this, &PreferencesDialog::retranslate);
    retranslate();

    // Re-scale the theme text when the interface font changes (or on a full reset).
    // Application applies QApplication::setFont() first (its Settings::changed handler
    // is connected at startup), so the base font is already updated here.
    connect(m_settings, &Settings::changed, this, [this](const QString& key) {
        const bool fontKey = (key == QStringLiteral("interface/font")) || key.isEmpty();
        if (!fontKey && key != QStringLiteral("interface/accentColor")
            && key != QStringLiteral("interface/theme"))
            return;
        loadStyleSheet();   // re-theme live: font scaling AND the accent color
        if (!fontKey)
            return;         // a colour change moves nothing, so leave the geometry alone
        // A bigger font makes every label bigger, so the window has to grow with it or
        // controls clip and a horizontal scrollbar appears. Then redo the page height:
        // the content stack is FIXED-height, so without this the rows overlap until the
        // window is resized by hand. (resize() above fires resizeEvent, which also calls
        // this — but not when the size is already clamped to the screen, hence this call.)
        applyScaledWidths();   // before the height: wider controls can re-wrap a description
        applyFontScaledSize();
        adjustStackHeightForCurrentPage();
    });

    // On "Auto", the scheme can change without any setting changing — the user flips
    // Windows between light and dark. Qt reports that here, and the stylesheet has to
    // be resolved again against the other palette. (Forcing Light/Dark also emits this,
    // which is harmless: loadStyleSheet() is idempotent.)
    //
    // ⚠️ The re-apply is DEFERRED to the next event-loop turn, and that is load-bearing.
    // This signal arrives while Qt is still working through the OS theme change, and
    // re-styling from inside it left part of the tree — the #PageTitle and #SectionTitle
    // labels — holding the colour from the *previous* scheme, while cards, sidebar and
    // setting labels had already flipped. Switching theme by hand never showed it,
    // because that path re-applies in a later cycle anyway. Running after Qt has
    // finished makes the whole tree agree.
    connect(QApplication::styleHints(), &QStyleHints::colorSchemeChanged,
            this, [this](Qt::ColorScheme s) {
                QTimer::singleShot(0, this, [this, s] {
                    loadStyleSheet();

                    // Not every control here is styled by the QSS. Radio indicators and
                    // spin boxes are drawn by the NATIVE style from its own cached
                    // brushes, so re-applying the stylesheet cannot reach them: after an
                    // OS-driven change the checked radio kept its light-mode dot on a
                    // dark card. Setting the scheme explicitly never showed this, because
                    // that makes Qt rebuild the palette and re-polish the tree itself.
                    // Re-polishing by hand gives the OS path the same treatment.
                    style()->unpolish(this);
                    style()->polish(this);
                    for (QWidget* w : findChildren<QWidget*>()) {
                        style()->unpolish(w);
                        style()->polish(w);
                    }

                    qInfo("[prefs] OS scheme -> %s; stylesheet resolved as %s",
                          s == Qt::ColorScheme::Dark ? "dark" : "light",
                          PrefsStyle::effectiveScheme() == PrefsStyle::Scheme::Dark ? "dark" : "light");
                });
            });

    // Every page exists by now: make the wheel leave spin/combo boxes alone until
    // one is clicked, so scrolling a page can't silently change a setting.
    WheelGuard::install(this);

    m_sidebar->setCurrentRow(0);
    applyFontScaledSize();
}

// --- shell ------------------------------------------------------------------

QWidget* PreferencesDialog::buildHeader()
{
    auto* header = new QWidget;
    header->setObjectName(QStringLiteral("PrefsHeader"));
    auto* h = new QHBoxLayout(header);
    h->setContentsMargins(20, 14, 20, 14);
    h->setSpacing(8);

    auto* logo = new QLabel;
    logo->setPixmap(makeBrandPixmap(22));
    auto* name = new QLabel(QStringLiteral("SnapCap"));
    name->setObjectName(QStringLiteral("PrefsBrandName"));

    h->addWidget(logo);
    h->addWidget(name);
    h->addStretch();
    return header;
}

void PreferencesDialog::loadStyleSheet()
{
    setStyleSheet(PrefsStyle::load());   // font-scaled theme; empty = no-op
}

void PreferencesDialog::addCategory(QWidget* page, const QString& titleKey, const QStringList& subKeys)
{
    const int idx = m_stack->count();
    m_stack->addWidget(page);

    auto* item = new QListWidgetItem(m_sidebar);   // text set by retranslate()
    m_sidebarItems.append({item, titleKey});

    Category c;
    c.subKeys = subKeys;
    c.stackIndices.append(idx);
    // Future: additional sub-category pages append more indices here.
    m_categories.append(c);
}

void PreferencesDialog::onCategorySelected(int row)
{
    if (row < 0 || row >= m_categories.size())
        return;
    m_currentCategory = row;
    const Category& c = m_categories[row];

    const QSignalBlocker block(m_subList);
    m_subList->clear();
    if (c.subKeys.size() > 1) {
        for (const QString& key : c.subKeys)
            new QListWidgetItem(I18n::instance().t(key), m_subList);
        m_subList->setCurrentRow(0);
        m_subList->show();
    } else {
        m_subList->hide();
    }
    m_stack->setCurrentIndex(c.stackIndices.first());
    adjustStackHeightForCurrentPage();
}

void PreferencesDialog::onSubCategorySelected(int row)
{
    if (m_currentCategory < 0 || m_currentCategory >= m_categories.size())
        return;
    const Category& c = m_categories[m_currentCategory];
    if (row < 0 || row >= c.stackIndices.size())
        return;
    m_stack->setCurrentIndex(c.stackIndices[row]);
    adjustStackHeightForCurrentPage();
}

// The live interface font as a multiple of kDesignPt — the one ratio every authored
// size in this dialog is scaled by (the window in applyFontScaledSize, the control
// widths in setScaledWidth).
// It reads the REQUESTED point size, not QFontInfo's resolved one: resolving
// round-trips through the font engine at the screen's DPI and comes back rounded —
// 10 pt on a 150% display resolves to 9.75 — which silently shrank the window below
// its authored width. QFontInfo is the fallback only for a font specified in pixels,
// which has no point size of its own.
double PreferencesDialog::fontScale() const
{
    const QFont appFont = QApplication::font();
    const double pt = appFont.pointSizeF() > 0.0 ? appFont.pointSizeF()
                                                 : QFontInfo(appFont).pointSizeF();
    const double s = pt / kDesignPt;
    return s > 0.0 ? s : 1.0;
}

void PreferencesDialog::setScaledWidth(QWidget* w, int minPx, int maxPx)
{
    if (!w)
        return;
    // One record per widget: addSettingRow registers the shared default first, and a
    // page that wants a different width calls this again on the same control.
    auto it = std::find_if(m_scaledWidths.begin(), m_scaledWidths.end(),
                           [w](const ScaledWidth& sw) { return sw.w == w; });
    if (it != m_scaledWidths.end())
        *it = {w, minPx, maxPx};
    else
        m_scaledWidths.append({w, minPx, maxPx});
    applyOneScaledWidth(w, minPx, maxPx, fontScale());
}

void PreferencesDialog::applyScaledWidths()
{
    const double s = fontScale();
    for (const ScaledWidth& sw : m_scaledWidths)
        applyOneScaledWidth(sw.w, sw.minPx, sw.maxPx, s);
}

// The dialog window itself stays a fixed size; the scrollable content stack (m_stack,
// inside the QScrollArea) is sized per tab so a short page (About) isn't forced to the
// tallest page's (Snip) height. But it's clamped to at least the viewport height so the
// page — and thus the panel background — always fills top-to-bottom: on a short tab the
// extra space sits below the content (blank panel colour), while a tall tab still
// scrolls. This is what keeps the right panel from ending abruptly at the last item.
// This window's dimensions are authored against the 10 pt base UI font, so at a larger
// interface font every label, row and control grows while the window would not — which
// clips the right-hand controls and raises a horizontal scrollbar. Scale the authored
// numbers by the same ratio, clamped to the screen so a big font can't produce a window
// that will not fit. The base is 10 (today's default) rather than the 9 the QSS was
// written against — two different reference points that must not be "unified".
void PreferencesDialog::applyFontScaledSize()
{
    // Logical px: Qt applies the display scale itself, so these are the sizes on a 100%
    // screen and already 1.5x that on a 150% one. Only the FONT is scaled for here.
    constexpr int kBaseW = 900, kBaseH = 880;    // comfortable default at kDesignPt
    constexpr int kMinW  = 680, kMinH  = 480;    // below this the rows stop working

    const double s = fontScale();

    // Available (not full) geometry, so the taskbar doesn't end up under the window.
    const QScreen* sc = screen();
    const QSize cap = sc ? sc->availableGeometry().size() : QSize(kBaseW, kBaseH);

    setMinimumSize(qMin(int(kMinW * s), cap.width()), qMin(int(kMinH * s), cap.height()));
    resize(qMin(int(kBaseW * s), cap.width()), qMin(int(kBaseH * s), cap.height()));
}

void PreferencesDialog::adjustStackHeightForCurrentPage()
{
    QWidget* page = m_stack->currentWidget();
    if (!page)
        return;
    // page->sizeHint().height() alone ignores the page's real (viewport) width, so
    // word-wrapped description labels report a different line count — and thus a
    // different height — per tab than they actually render at. Use the layout's
    // height-for-width at the stack's real width so every tab's trailing gap before
    // its bottom button row ends up the same.
    const int w = m_stack->width() > 0 ? m_stack->width() : page->sizeHint().width();
    QLayout* lay = page->layout();
    int h = (lay && lay->hasHeightForWidth()) ? lay->totalHeightForWidth(w)
                                              : page->sizeHint().height();
    // Never shorter than the scroll viewport (m_stack's parent), so the page fills the
    // whole panel and no exposed viewport shows below a short page's last item.
    if (QWidget* viewport = m_stack->parentWidget())
        h = qMax(h, viewport->height());
    m_stack->setFixedHeight(h);
}

// --- page/row builders ------------------------------------------------------

void PreferencesDialog::addPageHeader(QVBoxLayout* page, const QString& titleKey, const QString& subtitleKey)
{
    auto* title = new QLabel;
    title->setObjectName(QStringLiteral("PageTitle"));
    m_labels.append({title, titleKey});
    page->addWidget(title);

    if (!subtitleKey.isEmpty()) {
        auto* sub = new QLabel;
        sub->setObjectName(QStringLiteral("PageSubtitle"));
        sub->setWordWrap(true);
        m_labels.append({sub, subtitleKey});
        page->addWidget(sub);
    }
    page->addSpacing(10);
}

void PreferencesDialog::addSectionHeader(QVBoxLayout* page, const QString& titleKey, const QString& descKey)
{
    page->addSpacing(4);
    auto* title = new QLabel;
    title->setObjectName(QStringLiteral("SectionTitle"));
    m_labels.append({title, titleKey});
    page->addWidget(title);

    if (!descKey.isEmpty()) {
        auto* desc = new QLabel;
        desc->setObjectName(QStringLiteral("SectionDesc"));
        desc->setWordWrap(true);
        m_labels.append({desc, descKey});
        page->addWidget(desc);
    }
    page->addSpacing(6);
}

QVBoxLayout* PreferencesDialog::beginCard(QVBoxLayout* page)
{
    auto* card = new QFrame;
    card->setProperty("card", true);
    auto* inner = new QVBoxLayout(card);
    inner->setContentsMargins(16, 4, 16, 4);
    inner->setSpacing(0);
    page->addWidget(card);
    return inner;
}

void PreferencesDialog::addRowDivider(QVBoxLayout* card)
{
    auto* line = new QFrame;
    line->setObjectName(QStringLiteral("RowDivider"));
    line->setFixedHeight(1);
    card->addWidget(line);
}

QWidget* PreferencesDialog::addSettingRow(QVBoxLayout* card, const QString& labelKey,
                                          const QString& descKey, QWidget* control, bool divider,
                                          bool stretchControl, bool valueTakesRow)
{
    auto* row = new QWidget;
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 12, 0, 12);
    h->setSpacing(16);

    auto* left = new QVBoxLayout;
    left->setSpacing(2);
    auto* lbl = new QLabel;
    lbl->setObjectName(QStringLiteral("SettingLabel"));
    m_labels.append({lbl, labelKey});
    left->addWidget(lbl);
    if (!descKey.isEmpty()) {
        auto* desc = new QLabel;
        desc->setObjectName(QStringLiteral("SettingDesc"));
        desc->setWordWrap(true);
        m_labels.append({desc, descKey});
        left->addWidget(desc);
    }
    // valueTakesRow: let the value take the row's spare width (the label column
    // shrinks to its text) — used by the path previews so long paths wrap only at
    // the row edge instead of in a narrow right-hand column. Otherwise the label
    // column takes the stretch and the control keeps its natural (min) width.
    h->addLayout(left, valueTakesRow ? 0 : 1);

    if (control) {
        if (stretchControl && !valueTakesRow)
            setScaledWidth(control, kControlW);
        h->addWidget(control, valueTakesRow ? 1 : 0, Qt::AlignVCenter);
    }

    card->addWidget(row);
    if (divider)
        addRowDivider(card);
    return control;
}

QCheckBox* PreferencesDialog::addCheckRow(QVBoxLayout* card, const QString& key, bool def,
                                          const QString& labelKey, const QString& descKey, bool divider)
{
    auto* row = new QWidget;
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 12, 0, 12);
    h->setSpacing(12);

    auto* cb = new QCheckBox;
    if (!key.isEmpty()) {
        cb->setChecked(m_settings->value(key, def).toBool());
        connect(cb, &QCheckBox::toggled, this, [this, key](bool on) { m_settings->setValue(key, on); });
    }
    h->addWidget(cb, 0, Qt::AlignTop);

    auto* left = new QVBoxLayout;
    left->setSpacing(2);
    auto* lbl = new QLabel;
    lbl->setObjectName(QStringLiteral("SettingLabel"));
    m_labels.append({lbl, labelKey});
    left->addWidget(lbl);
    if (!descKey.isEmpty()) {
        auto* desc = new QLabel;
        desc->setObjectName(QStringLiteral("SettingDesc"));
        desc->setWordWrap(true);
        m_labels.append({desc, descKey});
        left->addWidget(desc);
    }
    h->addLayout(left, 1);

    card->addWidget(row);
    if (divider)
        addRowDivider(card);
    return cb;
}

QString PreferencesDialog::pickFolder(const QString& current)
{
    return QFileDialog::getExistingDirectory(this, QString(), current);
}

// --- pages ------------------------------------------------------------------

QWidget* PreferencesDialog::buildGeneralTab()
{
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(24, 20, 24, 20);
    addPageHeader(v, QStringLiteral("prefs.tab.general"), QStringLiteral("prefs.subtitle.general"));
    auto* card = beginCard(v);

    // Language (live switch).
    m_langCombo = new CenteredComboBox;
    for (const auto& [code, name] : I18n::instance().availableLocales())
        m_langCombo->addItem(name, code);
    const int li = m_langCombo->findData(I18n::instance().locale());
    if (li >= 0) m_langCombo->setCurrentIndex(li);
    connect(m_langCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        const QString code = m_langCombo->currentData().toString();
        m_settings->setValue(QStringLiteral("general/locale"), code);
        I18n::instance().setLocale(code);     // → localeChanged → retranslate()
    });
    addSettingRow(card, QStringLiteral("prefs.language"), QStringLiteral("prefs.desc.language"), m_langCombo);
    setScaledWidth(m_langCombo, kNarrowControlW, kNarrowControlW);

    // Run on system startup / As administrator — wired to the OS autostart entry
    // (registry Run value or an elevated Scheduled Task). Persistence + OS
    // reconciliation live in applyAutostart(); the empty key keeps addCheckRow from
    // auto-persisting. Both stay disabled when no platform backend is available.
    m_runStartup = addCheckRow(card, QString(), false,
                               QStringLiteral("prefs.run_on_startup"), QStringLiteral("prefs.desc.run_on_startup"));
    m_asAdmin = addCheckRow(card, QString(), false,
                            QStringLiteral("prefs.as_administrator"), QStringLiteral("prefs.desc.as_administrator"));
    connect(m_runStartup, &QCheckBox::toggled, this, [this](bool on) {
        if (!on) { QSignalBlocker b(m_asAdmin); m_asAdmin->setChecked(false); }
        m_asAdmin->setEnabled(on && m_autostart);   // admin only meaningful with startup on
        applyAutostart();
    });
    connect(m_asAdmin, &QCheckBox::toggled, this, [this](bool) { applyAutostart(); });
    refreshAutostartControls();                     // reflect the real OS state on open

    // GPU acceleration — the ONLY consumer is core/render's *blur* dispatch, which
    // is why the row description says "blur" and not "acceleration": mosaic is
    // CPU-only by measurement and ignores this entirely. "Auto" probes for a usable
    // GPU and falls back to the CPU blur on failure, so it is safe as the default.
    m_gpuAccel = new CenteredComboBox;
    static const struct { const char* code; const char* key; } kGpuModes[] = {
        {"auto", "prefs.gpu.auto"},
        {"cpu",  "prefs.gpu.cpu"},
    };
    for (const auto& g : kGpuModes)
        m_gpuAccel->addItem(I18n::instance().t(QLatin1String(g.key)), QLatin1String(g.code));
    {
        const QString cur = m_settings->value(QStringLiteral("general/gpu_acceleration"),
                                              QStringLiteral("auto")).toString();
        const int idx = m_gpuAccel->findData(cur);
        m_gpuAccel->setCurrentIndex(idx >= 0 ? idx : m_gpuAccel->findData(QStringLiteral("auto")));
    }
    connect(m_gpuAccel, &QComboBox::currentIndexChanged, this, [this](int) {
        m_settings->setValue(QStringLiteral("general/gpu_acceleration"),
                             m_gpuAccel->currentData().toString());
    });
    // Label key is `gpu_accel`, not `gpu`: `prefs.gpu` is the parent map holding the
    // two item labels, and YAML cannot make one key both a scalar and a map. Same
    // shape as the existing `prefs.log_level` + `prefs.log.*` pair.
    addSettingRow(card, QStringLiteral("prefs.gpu_accel"), QStringLiteral("prefs.desc.gpu"), m_gpuAccel);
    setScaledWidth(m_gpuAccel, kNarrowControlW, kNarrowControlW);

    // Log level — drives core/Logging file verbosity (live + persisted).
    m_logLevel = new CenteredComboBox;
    static const struct { const char* code; const char* key; } kLogLevels[] = {
        {"off",     "prefs.log.off"},
        {"errors",  "prefs.log.errors"},
        {"normal",  "prefs.log.normal"},
        {"verbose", "prefs.log.verbose"},
    };
    for (const auto& l : kLogLevels)
        m_logLevel->addItem(I18n::instance().t(QLatin1String(l.key)), QLatin1String(l.code));
    {
        const QString cur = m_settings->value(QStringLiteral("log/level"), QStringLiteral("normal")).toString();
        const int idx = m_logLevel->findData(cur);
        m_logLevel->setCurrentIndex(idx >= 0 ? idx : m_logLevel->findData(QStringLiteral("normal")));
    }
    connect(m_logLevel, &QComboBox::currentIndexChanged, this, [this](int) {
        const QString code = m_logLevel->currentData().toString();
        m_settings->setValue(QStringLiteral("log/level"), code);
        Logging::setLevel(Logging::levelFromString(code));
    });
    addSettingRow(card, QStringLiteral("prefs.log_level"), QStringLiteral("prefs.desc.log_level"), m_logLevel);
    setScaledWidth(m_logLevel, kNarrowControlW, kNarrowControlW);

    // Log path — editable line + Browse, mirroring Config path. Relocating moves
    // the current log file and redirects logging live (core/Logging::relocate).
    m_logPathEdit = new QLineEdit(m_settings->logFilePath());
    auto* logBrowseBtn = new QPushButton;
    logBrowseBtn->setProperty("i18nKey", QStringLiteral("prefs.browse"));
    const auto applyLogPath = [this](const QString& chosen) {
        const QString cur = m_settings->logFilePath();
        if (chosen.isEmpty()
            || QFileInfo(chosen).absoluteFilePath() == QFileInfo(cur).absoluteFilePath()) {
            m_logPathEdit->setText(cur);
            return;
        }
        const QString absNew = QFileInfo(chosen).absoluteFilePath();
        if (Logging::relocate(absNew)) {
            // Keep it a bare filename when it lives next to config.ini (portable).
            const QString cfgDir = QFileInfo(m_settings->filePath()).absolutePath();
            const bool inCfgDir = QFileInfo(absNew).absolutePath() == QDir(cfgDir).absolutePath();
            m_settings->setValue(QStringLiteral("log/file"),
                                 inCfgDir ? QFileInfo(absNew).fileName() : absNew);
            m_logPathEdit->setText(m_settings->logFilePath());
            QMessageBox::information(this, windowTitle(),
                                     I18n::instance().t(QStringLiteral("prefs.log_moved")));
        } else {
            QMessageBox::warning(this, windowTitle(),
                                 I18n::instance().t(QStringLiteral("prefs.log_move_failed")));
            m_logPathEdit->setText(cur);
        }
    };
    connect(logBrowseBtn, &QPushButton::clicked, this, [this, applyLogPath] {
        const QString chosen = QFileDialog::getSaveFileName(
            this, I18n::instance().t(QStringLiteral("prefs.log_path")),
            m_settings->logFilePath(),
            I18n::instance().t(QStringLiteral("prefs.log_filter")));
        applyLogPath(chosen);
    });
    connect(m_logPathEdit, &QLineEdit::editingFinished, this,
            [this, applyLogPath] { applyLogPath(m_logPathEdit->text().trimmed()); });
    auto* logWrap = new QWidget;
    auto* logRow = new QHBoxLayout(logWrap);
    logRow->setContentsMargins(0, 0, 0, 0);
    logRow->addWidget(m_logPathEdit);
    logRow->addWidget(logBrowseBtn);
    // stretchControl=false so addSettingRow sizes the WRAPPER to its contents; the width
    // is put on the line edit instead, matching the bare kControlW field of Output →
    // Manual Save → File name. Registering the wrapper would size field+button together
    // and leave the field visibly narrower than every other path row.
    addSettingRow(card, QStringLiteral("prefs.log_path"), QStringLiteral("prefs.desc.log_path"),
                  logWrap, /*divider=*/true, /*stretchControl=*/false);
    setScaledWidth(m_logPathEdit, kControlW);
    m_resetButtons.append(logBrowseBtn);   // reuse the i18nKey retranslate loop

    // Config path — editable line + Browse (navigate folder + type filename).
    m_configPathEdit = new QLineEdit(m_settings->filePath());
    auto* browseBtn = new QPushButton;
    browseBtn->setProperty("i18nKey", QStringLiteral("prefs.browse"));
    const auto applyPath = [this](const QString& chosen) {
        if (chosen.isEmpty()
            || QFileInfo(chosen).absoluteFilePath() == QFileInfo(m_settings->filePath()).absoluteFilePath()) {
            m_configPathEdit->setText(m_settings->filePath());
            return;
        }
        if (m_settings->relocate(chosen)) {
            m_configPathEdit->setText(m_settings->filePath());
            QMessageBox::information(this, windowTitle(),
                                     I18n::instance().t(QStringLiteral("prefs.config_moved")));
        } else {
            QMessageBox::warning(this, windowTitle(),
                                 I18n::instance().t(QStringLiteral("prefs.config_move_failed")));
            m_configPathEdit->setText(m_settings->filePath());
        }
    };
    connect(browseBtn, &QPushButton::clicked, this, [this, applyPath] {
        const QString chosen = QFileDialog::getSaveFileName(
            this, I18n::instance().t(QStringLiteral("prefs.config_path")),
            m_settings->filePath(),
            I18n::instance().t(QStringLiteral("prefs.config_filter")));
        applyPath(chosen);
    });
    connect(m_configPathEdit, &QLineEdit::editingFinished, this,
            [this, applyPath] { applyPath(m_configPathEdit->text().trimmed()); });
    auto* pathWrap = new QWidget;
    auto* pathRow = new QHBoxLayout(pathWrap);
    pathRow->setContentsMargins(0, 0, 0, 0);
    pathRow->addWidget(m_configPathEdit);
    pathRow->addWidget(browseBtn);
    addSettingRow(card, QStringLiteral("prefs.config_path"), QStringLiteral("prefs.desc.config_path"),
                  pathWrap, /*divider=*/false, /*stretchControl=*/false);   // width on the edit — see Log path
    setScaledWidth(m_configPathEdit, kControlW);
    m_resetButtons.append(browseBtn);   // reuse the i18nKey retranslate loop

    // Restore Defaults (bottom-right, outside the card).
    v->addSpacing(20);
    m_restoreBtn = new QPushButton;
    m_restoreBtn->setProperty("i18nKey", QStringLiteral("prefs.restore_defaults"));
    connect(m_restoreBtn, &QPushButton::clicked, this, &PreferencesDialog::restoreDefaults);
    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnRow->addWidget(m_restoreBtn);
    v->addLayout(btnRow);
    v->addStretch();   // slack sits below the buttons as panel background, not in the cards
    m_resetButtons.append(m_restoreBtn);
    return page;
}

QWidget* PreferencesDialog::buildInterfaceTab()
{
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(24, 20, 24, 20);
    addPageHeader(v, QStringLiteral("prefs.tab.interface"), QStringLiteral("prefs.subtitle.interface"));

    // --- subcategory: General (subtitle + card) -------------------------------
    addSectionHeader(v, QStringLiteral("prefs.iface.section.general"),
                     QStringLiteral("prefs.iface.section_desc.general"));
    auto* gen = beginCard(v);

    // Interface font — button opens the font picker; applied app-wide by
    // Application::applyInterfaceFont (interface/font).
    m_ifaceFontBtn = new ElidedButton;
    updateFontButton();
    connect(m_ifaceFontBtn, &QPushButton::clicked, this, [this] {
        bool ok = false;
        // Capped at 20 pt: this font scales the entire UI, and applyFontScaledSize()
        // grows the window with it — past 20 the window is larger than a typical screen
        // and every page falls back to scrolling. The paste conversion font, which only
        // rasterises text into an image, keeps the picker's full range.
        constexpr int kMaxInterfaceFontPt = 20;
        const QFont f = FontPickerDialog::getFont(&ok, currentIfaceFont(), this,
                                                  kMaxInterfaceFontPt);
        if (ok) { m_settings->setValue(QStringLiteral("interface/font"), f.toString()); updateFontButton(); }
    });
    addSettingRow(gen, QStringLiteral("prefs.iface.font"), QStringLiteral("prefs.desc.iface_font"),
                  m_ifaceFontBtn);
    // Auto-fits the family name it shows, between kNarrowControlW and kControlW — a
    // short family keeps the button from shrinking to a stub that reads as a different
    // kind of control, and past the cap ElidedButton trims the text rather than
    // widening (the tooltip in updateFontButton() keeps the full name reachable).
    setScaledWidth(m_ifaceFontBtn, kNarrowControlW, kControlW);

    // Theme — Auto / Light / Dark (interface/theme). Two consumers: PrefsStyle picks
    // the palette its @tokens resolve against, and Application::applyColorScheme pushes
    // the same choice into Qt so unstyled windows and the native title bars follow.
    // "Auto" defers to the OS and tracks it live.
    m_themeMode = new CenteredComboBox;
    static const struct { const char* code; const char* key; } kThemes[] = {
        {"auto",  "prefs.iface.theme_opt.auto"},
        {"light", "prefs.iface.theme_opt.light"},
        {"dark",  "prefs.iface.theme_opt.dark"},
    };
    for (const auto& t : kThemes)
        m_themeMode->addItem(I18n::instance().t(QLatin1String(t.key)), QLatin1String(t.code));
    {
        const QString cur = m_settings->value(QStringLiteral("interface/theme"),
                                              QStringLiteral("auto")).toString();
        const int idx = m_themeMode->findData(cur);
        m_themeMode->setCurrentIndex(idx >= 0 ? idx : m_themeMode->findData(QStringLiteral("auto")));
    }
    connect(m_themeMode, &QComboBox::currentIndexChanged, this, [this](int) {
        m_settings->setValue(QStringLiteral("interface/theme"), m_themeMode->currentData().toString());
    });
    // Label key is `theme_mode`, not `theme`: `prefs.iface.theme_opt` holds the item
    // labels, and one key cannot be both a scalar and a map.
    addSettingRow(gen, QStringLiteral("prefs.iface.theme_mode"), QStringLiteral("prefs.desc.theme_mode"),
                  m_themeMode);
    setScaledWidth(m_themeMode, kNarrowControlW, kNarrowControlW);

    // Accent color — button opens the project ColorDialog; applied as the UI accent
    // via PrefsStyle / PaintBar / SnipOverlay (interface/accentColor).
    m_accentColorBtn = new QPushButton;
    updateAccentButton();
    connect(m_accentColorBtn, &QPushButton::clicked, this, [this] {
        const QColor c = ColorDialog::getColor(currentAccentColor(), m_settings, this);
        if (c.isValid()) { m_settings->setValue(QStringLiteral("interface/accentColor"), c.name()); updateAccentButton(); }
    });
    addSettingRow(gen, QStringLiteral("prefs.iface.accent_color"), QStringLiteral("prefs.desc.accent_color"),
                  m_accentColorBtn);
    setScaledWidth(m_accentColorBtn, kNarrowControlW, kNarrowControlW);   // aligns with Theme above

    // Tray icon — Auto / Colored / Custom (last row: no divider).
    addSettingRow(gen, QStringLiteral("prefs.iface.tray_icon"), QStringLiteral("prefs.desc.tray_icon"),
                  buildTrayIconControl(), /*divider=*/false);

    // --- subcategory: Magnifier (subtitle + card) -----------------------------
    addSectionHeader(v, QStringLiteral("prefs.iface.section.magnifier"),
                     QStringLiteral("prefs.iface.section_desc.magnifier"));
    auto* mag = beginCard(v);

    // Color values — checkbox list of formats. The divider under this row is what
    // separates the format selection from the HEX styling toggles below.
    addSettingRow(mag, QStringLiteral("prefs.iface.color_values"), QStringLiteral("prefs.desc.color_values"),
                  buildColorFormatsControl(), /*divider=*/true);

    m_magNoHash = addCheckRow(mag, QStringLiteral("interface/magnifier/noHash"), false,
                              QStringLiteral("prefs.iface.no_hash"), QStringLiteral("prefs.desc.no_hash"));
    m_magHexUpper = addCheckRow(mag, QStringLiteral("interface/magnifier/hexUppercase"), false,
                                QStringLiteral("prefs.iface.hex_uppercase"), QStringLiteral("prefs.desc.hex_uppercase"));
    m_magUsageTips = addCheckRow(mag, QStringLiteral("interface/magnifier/usageTips"), true,
                                 QStringLiteral("prefs.iface.usage_tips"), QStringLiteral("prefs.desc.usage_tips"));
    m_magGrid = addCheckRow(mag, QStringLiteral("interface/magnifier/gridLines"), true,
                            QStringLiteral("prefs.iface.grid_lines"), QStringLiteral("prefs.desc.grid_lines"));
    m_magCoords = addCheckRow(mag, QStringLiteral("interface/magnifier/coordinates"), true,
                              QStringLiteral("prefs.iface.coordinates"), QStringLiteral("prefs.desc.coordinates"));

    // Loupe size as a percentage of the default 180px box (last row: no divider).
    m_magSize = new QSpinBox;
    m_magSize->setRange(50, 200);
    m_magSize->setSingleStep(25);
    m_magSize->setSuffix(QStringLiteral(" %"));
    m_magSize->setValue(m_settings->value(QStringLiteral("interface/magnifier/sizePercent"), 100).toInt());
    connect(m_magSize, &QSpinBox::valueChanged, this, [this](int val) {
        m_settings->setValue(QStringLiteral("interface/magnifier/sizePercent"), val);
    });
    addSettingRow(mag, QStringLiteral("prefs.iface.mag_size"), QStringLiteral("prefs.desc.mag_size"),
                  m_magSize, /*divider=*/false);
    setScaledWidth(m_magSize, kNarrowControlW, kNarrowControlW);

    // --- Restore Defaults (scoped to THIS page's interface/* settings) --------
    v->addSpacing(20);
    m_ifaceRestoreBtn = new QPushButton;
    m_ifaceRestoreBtn->setProperty("i18nKey", QStringLiteral("prefs.restore_defaults"));
    connect(m_ifaceRestoreBtn, &QPushButton::clicked, this, &PreferencesDialog::restoreInterfaceDefaults);
    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnRow->addWidget(m_ifaceRestoreBtn);
    v->addLayout(btnRow);
    v->addStretch();   // slack sits below the buttons as panel background, not in the cards
    m_resetButtons.append(m_ifaceRestoreBtn);
    return page;
}

// Magnifier "Color values": a grid of format checkboxes (3 columns). Any toggle
// re-serializes the enabled formats to a comma list in Settings.
QWidget* PreferencesDialog::buildColorFormatsControl()
{
    struct Fmt { const char* code; const char* key; };
    static const Fmt kFmts[] = {
        {"rgb", "prefs.iface.fmt.rgb"},  {"hex", "prefs.iface.fmt.hex"},  {"hsl",  "prefs.iface.fmt.hsl"},
        {"hsv", "prefs.iface.fmt.hsv"},  {"hsi", "prefs.iface.fmt.hsi"},  {"cmyk", "prefs.iface.fmt.cmyk"},
        {"dec", "prefs.iface.fmt.dec"},
    };
    const QStringList on = m_settings->value(QStringLiteral("interface/magnifier/formats"),
                                             QStringLiteral("rgb,hex"))
                               .toString().split(QLatin1Char(','), Qt::SkipEmptyParts);

    auto* wrap = new QWidget;
    auto* grid = new QGridLayout(wrap);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(18);
    grid->setVerticalSpacing(6);

    int i = 0;
    for (const auto& f : kFmts) {
        auto* cb = new QCheckBox;
        cb->setChecked(on.contains(QString::fromLatin1(f.code)));
        connect(cb, &QCheckBox::toggled, this, [this] { saveColorFormats(); });
        grid->addWidget(cb, i / 3, i % 3);
        m_colorFormats.append({cb, QString::fromLatin1(f.code)});
        m_buttonI18n.append({cb, QString::fromLatin1(f.key)});
        ++i;
    }
    return wrap;
}

void PreferencesDialog::saveColorFormats()
{
    QStringList on;
    for (const auto& [cb, code] : m_colorFormats)
        if (cb->isChecked()) on << code;
    m_settings->setValue(QStringLiteral("interface/magnifier/formats"), on.join(QLatin1Char(',')));
}

static void setSwatchIcon(QPushButton* b, const QColor& c);  // defined below

QWidget* PreferencesDialog::buildTrayIconControl()
{
    struct Mode { const char* mode; const char* key; };
    static const Mode kModes[] = {
        {"color",  "prefs.iface.tray.color"},
        {"custom", "prefs.iface.tray.custom"},
    };
    QString cur = m_settings->value(QStringLiteral("interface/trayIconMode"),
                                    QStringLiteral("color")).toString();
    // Migrate the retired "auto" (and any unset/unknown value) to "color".
    if (cur != QStringLiteral("custom"))
        cur = QStringLiteral("color");

    auto* wrap = new QWidget;
    auto* h = new QHBoxLayout(wrap);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(14);
    m_trayGroup = new QButtonGroup(this);

    QRadioButton* customRadio = nullptr;
    for (const auto& m : kModes) {
        const QString mode = QString::fromLatin1(m.mode);
        auto* rb = new QRadioButton;
        rb->setChecked(cur == mode);
        m_trayGroup->addButton(rb);
        connect(rb, &QRadioButton::toggled, this, [this, mode](bool on) {
            if (on) m_settings->setValue(QStringLiteral("interface/trayIconMode"), mode);
        });
        m_trayRadios.append({rb, mode});
        m_buttonI18n.append({rb, QString::fromLatin1(m.key)});
        if (mode == QStringLiteral("custom"))
            customRadio = rb;   // added below, next to its swatch
        else
            h->addWidget(rb);
    }

    // A colour box right of "Custom": clicking it picks the tray colour and selects Custom.
    const QColor def(0x2d, 0x7d, 0xff);
    m_trayCustomBtn = new QPushButton;
    setSwatchIcon(m_trayCustomBtn, pasteColor(QStringLiteral("interface/trayIconColor"), def));
    connect(m_trayCustomBtn, &QPushButton::clicked, this, [this, def, customRadio] {
        const QColor c = ColorDialog::getColor(
            pasteColor(QStringLiteral("interface/trayIconColor"), def), m_settings, this);
        if (c.isValid()) {
            m_settings->setValue(QStringLiteral("interface/trayIconColor"), c.name());
            setSwatchIcon(m_trayCustomBtn, c);
            if (customRadio) customRadio->setChecked(true);
        }
    });

    // Keep the Custom label and its swatch tight together (own spacing, not the row's 14px).
    auto* custRow = new QHBoxLayout;
    custRow->setContentsMargins(0, 0, 0, 0);
    custRow->setSpacing(4);
    custRow->addWidget(customRadio);
    custRow->addWidget(m_trayCustomBtn);
    h->addLayout(custRow);

    h->addStretch();
    return wrap;
}

QFont PreferencesDialog::currentIfaceFont() const
{
    const QString s = m_settings->value(QStringLiteral("interface/font")).toString();
    QFont f = QApplication::font();
    if (!s.isEmpty()) f.fromString(s);
    return f;
}

QColor PreferencesDialog::currentAccentColor() const
{
    const QString s = m_settings->value(QStringLiteral("interface/accentColor"),
                                        QStringLiteral("#2d7dff")).toString();
    const QColor c(s);
    return c.isValid() ? c : QColor(0x2d, 0x7d, 0xff);
}

void PreferencesDialog::updateFontButton()
{
    if (!m_ifaceFontBtn) return;
    const QFont f = currentIfaceFont();
    const QString label = QStringLiteral("%1, %2")
                              .arg(FontPickerDialog::displayName(f.family()))
                              .arg(f.pointSize());
    m_ifaceFontBtn->setText(label);
    m_ifaceFontBtn->setToolTip(label);   // the button elides; the tooltip does not
    // The button sizes to its text, so a shorter family name has to be allowed to
    // shrink it again — the layout keeps the old width otherwise.
    m_ifaceFontBtn->updateGeometry();
}

void PreferencesDialog::updateAccentButton()
{
    if (!m_accentColorBtn) return;
    const QColor c = currentAccentColor();
    QPixmap pm(14, 14);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QColor(0xd0, 0xd5, 0xdd));
    p.setBrush(c);
    p.drawRoundedRect(QRectF(0.5, 0.5, 13, 13), 3, 3);
    p.end();
    m_accentColorBtn->setIcon(QIcon(pm));
    m_accentColorBtn->setText(c.name(QColor::HexRgb).toUpper());
}

// Paint a small rounded color chip as a button icon (matches updateAccentButton()).
static void setSwatchIcon(QPushButton* b, const QColor& c)
{
    QPixmap pm(14, 14);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QColor(0xd0, 0xd5, 0xdd));
    p.setBrush(c);
    p.drawRoundedRect(QRectF(0.5, 0.5, 13, 13), 3, 3);
    p.end();
    b->setIcon(QIcon(pm));
}

QWidget* PreferencesDialog::buildSnipTab()
{
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(24, 20, 24, 20);
    addPageHeader(v, QStringLiteral("prefs.tab.snip"), QStringLiteral("prefs.subtitle.snip"));

    // Persist-only checkbox row that also registers itself for restore/refresh.
    auto addSnipCheck = [this](QVBoxLayout* card, const QString& key, bool def,
                               const QString& labelKey, const QString& descKey, bool divider = true) {
        QCheckBox* cb = addCheckRow(card, key, def, labelKey, descKey, divider);
        m_snipChecks.push_back({cb, key, def});
        return cb;
    };
    // A single inline persist-only checkbox added to an existing horizontal layout.
    auto addInlineCheck = [this](QHBoxLayout* h, const QString& key, bool def, const QString& labelKey) {
        auto* cb = new QCheckBox;
        cb->setChecked(m_settings->value(key, def).toBool());
        connect(cb, &QCheckBox::toggled, this, [this, key](bool on) { m_settings->setValue(key, on); });
        m_snipChecks.push_back({cb, key, def});
        m_buttonI18n.append({cb, labelKey});
        h->addWidget(cb);
        return cb;
    };
    // Persist-only spinbox, remembered for restore/refresh.
    auto makeSnipSpin = [this](const QString& key, int def, int lo, int hi, const QString& suffix = QString()) {
        auto* sp = new QSpinBox;
        sp->setRange(lo, hi);
        if (!suffix.isEmpty()) sp->setSuffix(suffix);
        sp->setValue(m_settings->value(key, def).toInt());
        connect(sp, &QSpinBox::valueChanged, this, [this, key](int val) { m_settings->setValue(key, val); });
        m_snipSpins.push_back({sp, key, def});
        return sp;
    };
    // A color-swatch button that opens ColorDialog and persists the chosen color.
    auto makeSnipColorBtn = [this](const QString& key, const QColor& def, QPushButton** store) {
        auto* btn = new QPushButton;
        setSwatchIcon(btn, pasteColor(key, def));
        connect(btn, &QPushButton::clicked, this, [this, key, def, btn] {
            const QColor c = ColorDialog::getColor(pasteColor(key, def), m_settings, this);
            if (c.isValid()) {
                m_settings->setValue(key, c.name());
                setSwatchIcon(btn, c);
            }
        });
        *store = btn;
        return btn;
    };
    // A "Double-click: [action] then [x] quit snipping" row. Persist-only.
    auto addActionRow = [this](QVBoxLayout* card, const QString& labelKey, const QString& actionKey,
                               const QString& actionDef, const QString& quitKey,
                               QComboBox** comboStore, bool divider) {
        auto* wrap = new QWidget;
        auto* h = new QHBoxLayout(wrap);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(8);
        auto* combo = new CenteredComboBox;
        static const char* const kCodes[] = {"copy", "quicksave", "save", "pin", "nothing"};
        for (const char* code : kCodes)
            combo->addItem(QString(), QString::fromLatin1(code));   // texts set in retranslate()
        const int idx = combo->findData(m_settings->value(actionKey, actionDef).toString());
        if (idx >= 0) combo->setCurrentIndex(idx);
        connect(combo, &QComboBox::currentIndexChanged, this, [this, actionKey, combo](int) {
            m_settings->setValue(actionKey, combo->currentData().toString());
        });
        h->addWidget(combo);
        auto* thenLbl = new QLabel;
        thenLbl->setObjectName(QStringLiteral("SettingDesc"));
        m_labels.append({thenLbl, QStringLiteral("prefs.snip.keys.then")});
        h->addWidget(thenLbl);
        auto* quit = new QCheckBox;
        quit->setChecked(m_settings->value(quitKey, true).toBool());
        connect(quit, &QCheckBox::toggled, this, [this, quitKey](bool on) { m_settings->setValue(quitKey, on); });
        m_snipChecks.push_back({quit, quitKey, true});
        m_buttonI18n.append({quit, QStringLiteral("prefs.snip.keys.then_quit")});
        h->addWidget(quit);
        *comboStore = combo;
        // Right-aligned like every other row's control: no trailing stretch, and the
        // wrapper sized to its contents rather than the 240 px default. With both, the
        // group used to float left inside a right-aligned box.
        addSettingRow(card, labelKey, QString(), wrap, divider, /*stretchControl=*/false);
    };

    // ------- Display -----------------------------------------------------------
    addSectionHeader(v, QStringLiteral("prefs.snip.section.display"),
                     QStringLiteral("prefs.snip.section_desc.display"));
    auto* disp = beginCard(v);

    // Spin boxes that own their row take the same narrow fixed width as the combos, so
    // every lone control in Preferences lands on one column. The three that SHARE a row
    // (the thumbnail W×H pair, and history-areas next to its Loop check) keep their
    // natural size — they already read as a group, and 133 px each would make those rows
    // wider than every other one.
    auto* borderSpin = makeSnipSpin(QStringLiteral("snip/display/borderWidth"), 3, 1, 20,
                                    QStringLiteral(" px"));
    addSettingRow(disp, QStringLiteral("prefs.snip.display.border_width"), QString(), borderSpin);
    setScaledWidth(borderSpin, kNarrowControlW, kNarrowControlW);
    addSettingRow(disp, QStringLiteral("prefs.snip.display.mask_color"), QString(),
                  makeSnipColorBtn(QStringLiteral("snip/display/maskColor"), QColor(QStringLiteral("#464646")),
                                   &m_snipMaskBtn), /*divider=*/true, /*stretchControl=*/false);
    addSnipCheck(disp, QStringLiteral("snip/display/showAnchors"), true,
                 QStringLiteral("prefs.snip.display.show_anchors"), QString());
    // Anchor border defaults to the accent color; the fill (anchor color) is white.
    const QColor themeDefault(m_settings->value(QStringLiteral("interface/accentColor"),
                                                QStringLiteral("#2d7dff")).toString());
    addSettingRow(disp, QStringLiteral("prefs.snip.display.anchor_color"), QString(),
                  makeSnipColorBtn(QStringLiteral("snip/display/anchorColor"), themeDefault, &m_snipAnchorBtn),
                  /*divider=*/true, /*stretchControl=*/false);
    addSettingRow(disp, QStringLiteral("prefs.snip.display.anchor_border_color"), QString(),
                  makeSnipColorBtn(QStringLiteral("snip/display/anchorBorderColor"), Qt::white, &m_snipAnchorBorderBtn),
                  /*divider=*/true, /*stretchControl=*/false);

    addSnipCheck(disp, QStringLiteral("snip/display/crossLines"), false,
                 QStringLiteral("prefs.snip.display.cross_lines"), QString());
    addSnipCheck(disp, QStringLiteral("snip/display/noAuxLines"), false,
                 QStringLiteral("prefs.snip.display.no_aux_lines"), QStringLiteral("prefs.desc.snip.no_aux_lines"));
    addSnipCheck(disp, QStringLiteral("snip/display/keyTips"), true,
                 QStringLiteral("prefs.snip.display.key_tips"), QString());
    addSnipCheck(disp, QStringLiteral("snip/display/roundedCorners"), false,
                 QStringLiteral("prefs.snip.display.rounded_corners"),
                 QStringLiteral("prefs.desc.snip.rounded_corners"), /*divider=*/false);

    // ------- Behavior ----------------------------------------------------------
    addSectionHeader(v, QStringLiteral("prefs.snip.section.behavior"),
                     QStringLiteral("prefs.snip.section_desc.behavior"));
    auto* beh = beginCard(v);

    const QString dm = m_settings->value(QStringLiteral("snip/detectMode"), QStringLiteral("element")).toString();

    // Auto detect windows (+ Hide 1x1 slotted on the right) — drives snip/detectMode.
    m_snipAutoWindows = addCheckRow(beh, QString(), false,
                                    QStringLiteral("prefs.snip.behavior.auto_detect_windows"), QString(),
                                    /*divider=*/false);
    m_snipAutoWindows->setChecked(dm != QStringLiteral("off"));
    if (auto* rowLayout = qobject_cast<QHBoxLayout*>(m_snipAutoWindows->parentWidget()->layout())) {
        auto* hide1 = new QCheckBox;
        hide1->setChecked(m_settings->value(QStringLiteral("snip/behavior/hide1x1"), true).toBool());
        connect(hide1, &QCheckBox::toggled, this,
                [this](bool on) { m_settings->setValue(QStringLiteral("snip/behavior/hide1x1"), on); });
        m_snipChecks.push_back({hide1, QStringLiteral("snip/behavior/hide1x1"), true});
        m_buttonI18n.append({hide1, QStringLiteral("prefs.snip.behavior.hide_1x1")});
        rowLayout->addWidget(hide1, 0, Qt::AlignVCenter);
    }
    addRowDivider(beh);

    // Auto detect UI elements — requires windows; annotation lives in the description slot.
    m_snipAutoElements = addCheckRow(beh, QString(), false,
                                     QStringLiteral("prefs.snip.behavior.auto_detect_elements"),
                                     QStringLiteral("prefs.desc.snip.detect_elements"), /*divider=*/false);
    m_snipAutoElements->setChecked(dm == QStringLiteral("element"));
    m_snipAutoElements->setEnabled(m_snipAutoWindows->isChecked());
    addRowDivider(beh);

    auto writeDetect = [this]() {
        const bool win = m_snipAutoWindows->isChecked();
        const bool el  = m_snipAutoElements->isChecked();
        m_settings->setValue(QStringLiteral("snip/detectMode"),
                             QString::fromLatin1(!win ? "off" : (el ? "element" : "window")));
    };
    connect(m_snipAutoWindows, &QCheckBox::toggled, this, [this, writeDetect](bool on) {
        if (!on && m_snipAutoElements->isChecked()) {
            const QSignalBlocker b(m_snipAutoElements);
            m_snipAutoElements->setChecked(false);
        }
        m_snipAutoElements->setEnabled(on);
        writeDetect();
    });
    connect(m_snipAutoElements, &QCheckBox::toggled, this, [this, writeDetect](bool on) {
        if (on && !m_snipAutoWindows->isChecked()) {
            const QSignalBlocker b(m_snipAutoWindows);
            m_snipAutoWindows->setChecked(true);
        }
        writeDetect();
    });

    // Capture the cursor + Disable completely (stacked rows so each keeps its own annotation).
    m_captureCursor = addCheckRow(beh, QStringLiteral("snip/captureCursor"), false,
                                  QStringLiteral("prefs.snip.behavior.capture_cursor"),
                                  QStringLiteral("prefs.desc.snip.capture_cursor"), /*divider=*/false);
    m_snipChecks.push_back({m_captureCursor, QStringLiteral("snip/captureCursor"), false});
    addRowDivider(beh);
    {
        QCheckBox* disableCursor = addCheckRow(beh, QStringLiteral("snip/behavior/disableCursorCapture"), false,
                                               QStringLiteral("prefs.snip.behavior.disable_cursor"),
                                               QStringLiteral("prefs.desc.snip.disable_cursor"), /*divider=*/false);
        m_snipChecks.push_back({disableCursor, QStringLiteral("snip/behavior/disableCursorCapture"), false});
        connect(disableCursor, &QCheckBox::toggled, this,
                [this](bool on) { if (m_captureCursor) m_captureCursor->setEnabled(!on); });
        m_captureCursor->setEnabled(!disableCursor->isChecked());
        addRowDivider(beh);
    }

    addSnipCheck(beh, QStringLiteral("snip/behavior/quitOnFocusLoss"), false,
                 QStringLiteral("prefs.snip.behavior.quit_on_focus_loss"), QString());

    // Max number of history areas (+ Loop inline).
    {
        auto* wrap = new QWidget;
        auto* h = new QHBoxLayout(wrap);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(16);
        h->addWidget(makeSnipSpin(QStringLiteral("snip/behavior/maxHistoryAreas"), 8, 1, 50));
        addInlineCheck(h, QStringLiteral("snip/behavior/historyLoop"), false, QStringLiteral("prefs.snip.behavior.loop"));
        // No trailing addStretch() and stretchControl=false: the stretch used to left-pack
        // spin+Loop inside a wrapper that still carried the 240 px minimum, so the group
        // floated well short of the row's right edge while every neighbouring row sat
        // flush against it. Sizing the wrapper to its contents right-aligns it instead.
        addSettingRow(beh, QStringLiteral("prefs.snip.behavior.max_history_areas"), QString(), wrap,
                      /*divider=*/true, /*stretchControl=*/false);
    }
    auto* recordsSpin = makeSnipSpin(QStringLiteral("snip/behavior/maxHistoryRecords"), 20, 1, 100);
    addSettingRow(beh, QStringLiteral("prefs.snip.behavior.max_history_records"), QString(),
                  recordsSpin);
    setScaledWidth(recordsSpin, kNarrowControlW, kNarrowControlW);

    // Clear all recorded snip history (the `,`/`.` ring + its on-disk files).
    {
        auto* clearBtn = new QPushButton;
        clearBtn->setProperty("i18nKey", QStringLiteral("prefs.snip.behavior.clear_history_btn"));
        m_resetButtons.append(clearBtn);   // retranslated from its "i18nKey" property
        connect(clearBtn, &QPushButton::clicked, this, [this] {
            const auto answer = QMessageBox::question(
                this, I18n::instance().t(QStringLiteral("prefs.snip.behavior.clear_history")),
                I18n::instance().t(QStringLiteral("prefs.snip.behavior.clear_history_confirm")),
                QMessageBox::Yes | QMessageBox::No);
            if (answer == QMessageBox::Yes)
                emit clearHistoryRequested();
        });
        addSettingRow(beh, QStringLiteral("prefs.snip.behavior.clear_history"),
                      QStringLiteral("prefs.snip.behavior.clear_history_desc"),
                      clearBtn, /*divider=*/false, /*stretchControl=*/false);
    }

    // ------- Keys --------------------------------------------------------------
    addSectionHeader(v, QStringLiteral("prefs.snip.section.keys"),
                     QStringLiteral("prefs.snip.section_desc.keys"));
    auto* keys = beginCard(v);

    addSnipCheck(keys, QStringLiteral("snip/keys/disableScroll"), false,
                 QStringLiteral("prefs.snip.keys.disable_scroll"), QString());
    // Default ON — must match SnipOverlay::applySnipSettings' default for this key.
    addSnipCheck(keys, QStringLiteral("snip/keys/confirmEsc"), true,
                 QStringLiteral("prefs.snip.keys.confirm_esc"), QString());
    addSnipCheck(keys, QStringLiteral("snip/keys/ignoreSimCopy"), true,
                 QStringLiteral("prefs.snip.keys.ignore_sim_copy"),
                 QStringLiteral("prefs.snip.keys.ignore_sim_copy_desc"));

    // Ignore Win key: Left / Right — annotation in the description slot.
    {
        auto* wrap = new QWidget;
        auto* h = new QHBoxLayout(wrap);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(16);
        addInlineCheck(h, QStringLiteral("snip/keys/ignoreWinLeft"), true, QStringLiteral("prefs.snip.keys.win_left"));
        addInlineCheck(h, QStringLiteral("snip/keys/ignoreWinRight"), false, QStringLiteral("prefs.snip.keys.win_right"));
        addSettingRow(keys, QStringLiteral("prefs.snip.keys.ignore_win"),
                      QStringLiteral("prefs.desc.snip.ignore_win"), wrap,
                      /*divider=*/false, /*stretchControl=*/false);   // right-aligned; see addActionRow
    }

    // ------- Actions -----------------------------------------------------------
    addSectionHeader(v, QStringLiteral("prefs.snip.section.actions"), QString());
    auto* actions = beginCard(v);
    addActionRow(actions, QStringLiteral("prefs.snip.keys.double_click"),
                 QStringLiteral("snip/keys/doubleClickAction"), QStringLiteral("copy"),
                 QStringLiteral("snip/keys/doubleClickQuit"), &m_snipDoubleCombo, true);
    addActionRow(actions, QStringLiteral("prefs.snip.keys.middle_click"),
                 QStringLiteral("snip/keys/middleClickAction"), QStringLiteral("pin"),
                 QStringLiteral("snip/keys/middleClickQuit"), &m_snipMiddleCombo, true);
    addActionRow(actions, QStringLiteral("prefs.snip.keys.enter_key"),
                 QStringLiteral("snip/keys/enterAction"), QStringLiteral("copy"),
                 QStringLiteral("snip/keys/enterQuit"), &m_snipEnterCombo, false);

    // ------- Restore Defaults (whole page) -------------------------------------
    v->addSpacing(20);
    auto* restoreBtn = new QPushButton;
    restoreBtn->setProperty("i18nKey", QStringLiteral("prefs.restore_defaults"));
    connect(restoreBtn, &QPushButton::clicked, this, &PreferencesDialog::restoreSnipDefaults);
    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnRow->addWidget(restoreBtn);
    v->addLayout(btnRow);
    v->addStretch();   // slack sits below the buttons as panel background, not in the cards
    m_resetButtons.append(restoreBtn);
    return page;
}

QWidget* PreferencesDialog::buildPasteTab()
{
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(24, 20, 24, 20);
    addPageHeader(v, QStringLiteral("prefs.tab.paste"), QStringLiteral("prefs.subtitle.paste"));

    // Generic persist-only checkbox row that also registers itself for restore/refresh.
    auto addPasteCheck = [this](QVBoxLayout* card, const QString& key, bool def,
                                const QString& labelKey, const QString& descKey, bool divider = true) {
        QCheckBox* cb = addCheckRow(card, key, def, labelKey, descKey, divider);
        m_pasteChecks.push_back({cb, key, def});
        return cb;
    };
    // Persist-only spinbox, remembered for restore/refresh; optional live-preview hook.
    auto makePasteSpin = [this](const QString& key, int def, int lo, int hi,
                                const QString& suffix = QString(), bool previewHook = false) {
        auto* sp = new QSpinBox;
        sp->setRange(lo, hi);
        if (!suffix.isEmpty()) sp->setSuffix(suffix);
        sp->setValue(m_settings->value(key, def).toInt());
        connect(sp, &QSpinBox::valueChanged, this, [this, key, previewHook](int val) {
            m_settings->setValue(key, val);
            if (previewHook) updatePastePreview();
        });
        m_pasteSpins.push_back({sp, key, def});
        return sp;
    };
    // A color-swatch button that opens ColorDialog and persists the chosen color.
    auto makePasteColorBtn = [this](const QString& key, const QColor& def, const QString& i18nKey,
                                    QPushButton** store) {
        auto* btn = new QPushButton;
        if (!i18nKey.isEmpty()) {
            btn->setProperty("i18nKey", i18nKey);   // text applied in retranslate()
            m_resetButtons.append(btn);
        }
        setSwatchIcon(btn, pasteColor(key, def));
        connect(btn, &QPushButton::clicked, this, [this, key, def, btn] {
            const QColor c = ColorDialog::getColor(pasteColor(key, def), m_settings, this);
            if (c.isValid()) {
                m_settings->setValue(key, c.name());
                setSwatchIcon(btn, c);
                updatePastePreview();
            }
        });
        *store = btn;
        return btn;
    };

    // ------- Display -----------------------------------------------------------
    addSectionHeader(v, QStringLiteral("prefs.paste.section.display"),
                     QStringLiteral("prefs.paste.section_desc.display"));
    auto* disp = beginCard(v);

    // Window shadow (checkbox) + its shadow-color swatch on the right of the same row.
    {
        QCheckBox* shadow = addCheckRow(disp, QStringLiteral("paste/display/windowShadow"), true,
                                        QStringLiteral("prefs.paste.display.window_shadow"),
                                        QStringLiteral("prefs.desc.paste.window_shadow"),
                                        /*divider=*/false);
        m_pasteChecks.push_back({shadow, QStringLiteral("paste/display/windowShadow"), true});
        auto* btn = makePasteColorBtn(QStringLiteral("paste/display/shadowColor"), Qt::black,
                                      QString(), &m_pasteShadowBtn);
        // Slot the swatch into the checkbox row's trailing area (row layout is a QHBoxLayout).
        if (auto* rowLayout = qobject_cast<QHBoxLayout*>(shadow->parentWidget()->layout()))
            rowLayout->addWidget(btn, 0, Qt::AlignVCenter);
        addRowDivider(disp);
    }

    addPasteCheck(disp, QStringLiteral("paste/display/mnemonics"), false,
                  QStringLiteral("prefs.paste.display.mnemonics"),
                  QStringLiteral("prefs.desc.paste.mnemonics"));

    auto* opacitySpin = makePasteSpin(QStringLiteral("paste/display/defaultOpacity"), 100, 10, 100,
                                      QStringLiteral(" %"));
    addSettingRow(disp, QStringLiteral("prefs.paste.display.default_opacity"),
                  QStringLiteral("prefs.desc.paste.default_opacity"), opacitySpin);
    setScaledWidth(opacitySpin, kNarrowControlW, kNarrowControlW);

    auto* maxWinSpin = makePasteSpin(QStringLiteral("paste/display/maxWindowSize"), 12000, 100, 100000);
    addSettingRow(disp, QStringLiteral("prefs.paste.display.max_window_size"),
                  QStringLiteral("prefs.desc.paste.max_window_size"), maxWinSpin);
    setScaledWidth(maxWinSpin, kNarrowControlW, kNarrowControlW);

    // Fast thumbnail size: two spinboxes joined by "×".
    {
        auto* wrap = new QWidget;
        auto* h = new QHBoxLayout(wrap);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(8);
        h->addWidget(makePasteSpin(QStringLiteral("paste/display/thumbWidth"), 75, 1, 4096));
        auto* x = new QLabel(QStringLiteral("×"));
        h->addWidget(x);
        h->addWidget(makePasteSpin(QStringLiteral("paste/display/thumbHeight"), 75, 1, 4096));
        addSettingRow(disp, QStringLiteral("prefs.paste.display.thumb_size"),
                      QStringLiteral("prefs.desc.paste.thumb_size"), wrap,
                      /*divider=*/false, /*stretchControl=*/false);   // right-aligned; see addActionRow
    }

    // ------- Behavior ----------------------------------------------------------
    addSectionHeader(v, QStringLiteral("prefs.paste.section.behavior"),
                     QStringLiteral("prefs.paste.section_desc.behavior"));
    auto* beh = beginCard(v);

    struct BehCheck { const char* key; bool def; const char* labelKey; const char* descKey; };
    static const BehCheck kBeh[] = {
        {"paste/behavior/useSnippingPosition", true,  "prefs.paste.behavior.use_snipping_position", "prefs.desc.paste.use_snipping_position"},
        {"paste/behavior/zoomAtMouse",         false, "prefs.paste.behavior.zoom_at_mouse",         "prefs.desc.paste.zoom_at_mouse"},
        {"paste/behavior/smoothScaling",       true,  "prefs.paste.behavior.smooth_scaling",        "prefs.desc.paste.smooth_scaling"},
        {"paste/behavior/resetOnThumbnail",    true,  "prefs.paste.behavior.reset_on_thumbnail",    "prefs.desc.paste.reset_on_thumbnail"},
        {"paste/behavior/translucentWhenFlashing", true, "prefs.paste.behavior.translucent_when_flashing", "prefs.desc.paste.translucent_when_flashing"},
        {"paste/behavior/flashOnGroupSwitch",  true,  "prefs.paste.behavior.flash_on_group_switch", "prefs.desc.paste.flash_on_group_switch"},
        {"paste/behavior/confirmCloseMultiple",   true, "prefs.paste.behavior.confirm_close_multiple",   "prefs.desc.paste.confirm_close_multiple"},
        {"paste/behavior/confirmDestroyMultiple", true, "prefs.paste.behavior.confirm_destroy_multiple", "prefs.desc.paste.confirm_destroy_multiple"},
    };
    for (const auto& c : kBeh)
        addPasteCheck(beh, QString::fromLatin1(c.key), c.def,
                      QString::fromLatin1(c.labelKey), QString::fromLatin1(c.descKey));

    // "When a new image is pasted:" → two inline checkboxes (Flash / Activate).
    {
        auto* wrap = new QWidget;
        auto* h = new QHBoxLayout(wrap);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(16);
        auto addInline = [this, h](const QString& key, bool def, const QString& labelKey) {
            auto* cb = new QCheckBox;
            cb->setChecked(m_settings->value(key, def).toBool());
            connect(cb, &QCheckBox::toggled, this, [this, key](bool on) { m_settings->setValue(key, on); });
            m_pasteChecks.push_back({cb, key, def});
            m_buttonI18n.append({cb, labelKey});
            h->addWidget(cb);
        };
        addInline(QStringLiteral("paste/behavior/pasteFlash"), true,
                  QStringLiteral("prefs.paste.behavior.paste_flash"));
        addInline(QStringLiteral("paste/behavior/pasteActivate"), true,
                  QStringLiteral("prefs.paste.behavior.paste_activate"));
        // Right-aligned like every other row's control: no trailing stretch, and the
        // wrapper sized to its contents instead of the 240 px default (see the Snip
        // history-areas row for the same fix).
        addSettingRow(beh, QStringLiteral("prefs.paste.behavior.on_paste"), QString(), wrap,
                      /*divider=*/true, /*stretchControl=*/false);
    }

    auto* recoverSpin = makePasteSpin(QStringLiteral("paste/behavior/maxRecoverable"), 10, 0, 99);
    addSettingRow(beh, QStringLiteral("prefs.paste.behavior.max_recoverable"),
                  QStringLiteral("prefs.desc.paste.max_recoverable"), recoverSpin,
                  /*divider=*/false);
    setScaledWidth(recoverSpin, kNarrowControlW, kNarrowControlW);

    // ------- Conversion --------------------------------------------------------
    addSectionHeader(v, QStringLiteral("prefs.paste.section.conversion"),
                     QStringLiteral("prefs.paste.section_desc.conversion"));
    auto* conv = beginCard(v);

    m_pasteTextToImage = addCheckRow(conv, QStringLiteral("paste/conversion/textToImage"), true,
                                     QStringLiteral("prefs.paste.conversion.text_to_image"),
                                     QStringLiteral("prefs.desc.paste.text_to_image"));

    // The sub-rows live in a body widget that greys out when "Text to Image" is off.
    m_pasteConvBody = new QWidget;
    auto* body = new QVBoxLayout(m_pasteConvBody);
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);
    conv->addWidget(m_pasteConvBody);

    addPasteCheck(body, QStringLiteral("paste/conversion/ignoreHtml"), false,
                  QStringLiteral("prefs.paste.conversion.ignore_html"),
                  QStringLiteral("prefs.desc.paste.ignore_html"));
    addPasteCheck(body, QStringLiteral("paste/conversion/neverPasteFilePaths"), true,
                  QStringLiteral("prefs.paste.conversion.never_paste_file_paths"),
                  QStringLiteral("prefs.desc.paste.never_paste_file_paths"));

    auto* marginSpin = makePasteSpin(QStringLiteral("paste/conversion/pageMargin"), 15, 0, 500,
                                     QStringLiteral(" px"), /*previewHook=*/true);
    addSettingRow(body, QStringLiteral("prefs.paste.conversion.page_margin"),
                  QStringLiteral("prefs.desc.paste.page_margin"), marginSpin);
    setScaledWidth(marginSpin, kNarrowControlW, kNarrowControlW);

    auto* convWidthSpin = makePasteSpin(QStringLiteral("paste/conversion/maxWidth"), 900, 50, 8000,
                                        QStringLiteral(" px"), /*previewHook=*/true);
    addSettingRow(body, QStringLiteral("prefs.paste.conversion.max_width"),
                  QStringLiteral("prefs.desc.paste.max_width"), convWidthSpin);
    setScaledWidth(convWidthSpin, kNarrowControlW, kNarrowControlW);

    // Font button (opens QFontDialog; persists via QFont::toString).
    m_pasteFontBtn = new QPushButton;
    updatePasteFontButton();
    connect(m_pasteFontBtn, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QFont f = FontPickerDialog::getFont(&ok, currentPasteFont(), this);
        if (ok) {
            m_settings->setValue(QStringLiteral("paste/conversion/font"), f.toString());
            updatePasteFontButton();
            updatePastePreview();
        }
    });
    addSettingRow(body, QStringLiteral("prefs.paste.conversion.font"),
                  QStringLiteral("prefs.desc.paste.font"), m_pasteFontBtn);

    // Colors: Background + Text swatch buttons.
    {
        auto* wrap = new QWidget;
        auto* h = new QHBoxLayout(wrap);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(8);
        h->addStretch();
        h->addWidget(makePasteColorBtn(QStringLiteral("paste/conversion/bgColor"), Qt::white,
                                       QStringLiteral("prefs.paste.conversion.bg"), &m_pasteBgBtn));
        h->addWidget(makePasteColorBtn(QStringLiteral("paste/conversion/textColor"), Qt::black,
                                       QStringLiteral("prefs.paste.conversion.text"), &m_pasteTextBtn));
        addSettingRow(body, QStringLiteral("prefs.paste.conversion.colors"),
                      QStringLiteral("prefs.desc.paste.colors"), wrap);
    }

    // Live preview of the text→image conversion.
    m_pastePreview = new QLabel;
    m_pastePreview->setFrameShape(QFrame::Box);
    m_pastePreview->setAlignment(Qt::AlignCenter);
    m_pastePreview->setMinimumSize(160, 80);
    addSettingRow(body, QStringLiteral("prefs.paste.conversion.preview"), QString(),
                  m_pastePreview, /*divider=*/false);

    // Wire the master gate and render the first preview.
    connect(m_pasteTextToImage, &QCheckBox::toggled, m_pasteConvBody, &QWidget::setEnabled);
    m_pasteConvBody->setEnabled(m_pasteTextToImage->isChecked());
    updatePastePreview();

    // ------- Restore Defaults (whole page) -------------------------------------
    v->addSpacing(20);
    m_pasteRestoreBtn = new QPushButton;
    m_pasteRestoreBtn->setProperty("i18nKey", QStringLiteral("prefs.restore_defaults"));
    connect(m_pasteRestoreBtn, &QPushButton::clicked, this, &PreferencesDialog::restorePasteDefaults);
    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnRow->addWidget(m_pasteRestoreBtn);
    v->addLayout(btnRow);
    v->addStretch();   // slack sits below the buttons as panel background, not in the cards
    m_resetButtons.append(m_pasteRestoreBtn);
    return page;
}

QFont PreferencesDialog::currentPasteFont() const
{
    QFont f(QStringLiteral("Segoe UI"), 11);
    const QString s = m_settings->value(QStringLiteral("paste/conversion/font")).toString();
    if (!s.isEmpty()) f.fromString(s);
    return f;
}

QColor PreferencesDialog::pasteColor(const QString& key, const QColor& def) const
{
    const QColor c(m_settings->value(key, def.name()).toString());
    return c.isValid() ? c : def;
}

void PreferencesDialog::updatePasteFontButton()
{
    if (!m_pasteFontBtn) return;
    const QFont f = currentPasteFont();
    m_pasteFontBtn->setText(QStringLiteral("%1, %2")
                                .arg(FontPickerDialog::displayName(f.family()))
                                .arg(f.pointSize()));
}

void PreferencesDialog::updatePastePreview()
{
    if (!m_pastePreview) return;
    clip::PasteOptions opt;
    opt.maxWidth   = m_settings->value(QStringLiteral("paste/conversion/maxWidth"), 900).toInt();
    opt.pageMargin = m_settings->value(QStringLiteral("paste/conversion/pageMargin"), 15).toInt();
    opt.textFont   = currentPasteFont();
    opt.bgColor    = pasteColor(QStringLiteral("paste/conversion/bgColor"), Qt::white);
    opt.textColor  = pasteColor(QStringLiteral("paste/conversion/textColor"), Qt::black);
    const QImage img = clip::renderText(QStringLiteral("AaOo012"), opt);
    m_pastePreview->setPixmap(QPixmap::fromImage(img));
}

void PreferencesDialog::restorePasteDefaults()
{
    const auto answer = QMessageBox::question(
        this, windowTitle(),
        I18n::instance().t(QStringLiteral("prefs.restore_page_confirm")),
        QMessageBox::Yes | QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    // Reset ONLY the paste/* keys — this page's settings, nothing else.
    for (const auto& c : m_pasteChecks)
        m_settings->setValue(c.key, c.def);
    for (const auto& s : m_pasteSpins)
        m_settings->setValue(s.key, s.def);
    m_settings->setValue(QStringLiteral("paste/conversion/textToImage"), true);
    m_settings->setValue(QStringLiteral("paste/display/shadowColor"), QColor(Qt::black).name());
    m_settings->setValue(QStringLiteral("paste/conversion/font"),
                         QFont(QStringLiteral("Segoe UI"), 11).toString());
    m_settings->setValue(QStringLiteral("paste/conversion/bgColor"), QColor(Qt::white).name());
    m_settings->setValue(QStringLiteral("paste/conversion/textColor"), QColor(Qt::black).name());

    refreshPasteControls();
}

void PreferencesDialog::refreshPasteControls()
{
    for (const auto& c : m_pasteChecks) {
        if (!c.cb) continue;
        const QSignalBlocker b(c.cb);
        c.cb->setChecked(m_settings->value(c.key, c.def).toBool());
    }
    for (const auto& s : m_pasteSpins) {
        if (!s.sp) continue;
        const QSignalBlocker b(s.sp);
        s.sp->setValue(m_settings->value(s.key, s.def).toInt());
    }
    if (m_pasteTextToImage) {
        const bool on = m_settings->value(QStringLiteral("paste/conversion/textToImage"), true).toBool();
        const QSignalBlocker b(m_pasteTextToImage);
        m_pasteTextToImage->setChecked(on);
        if (m_pasteConvBody) m_pasteConvBody->setEnabled(on);
    }
    if (m_pasteShadowBtn)
        setSwatchIcon(m_pasteShadowBtn, pasteColor(QStringLiteral("paste/display/shadowColor"), Qt::black));
    if (m_pasteBgBtn)
        setSwatchIcon(m_pasteBgBtn, pasteColor(QStringLiteral("paste/conversion/bgColor"), Qt::white));
    if (m_pasteTextBtn)
        setSwatchIcon(m_pasteTextBtn, pasteColor(QStringLiteral("paste/conversion/textColor"), Qt::black));
    updatePasteFontButton();
    updatePastePreview();
}

// --- snip page: page-scoped restore + refresh --------------------------------

void PreferencesDialog::restoreSnipDefaults()
{
    const auto answer = QMessageBox::question(
        this, windowTitle(),
        I18n::instance().t(QStringLiteral("prefs.restore_page_confirm")),
        QMessageBox::Yes | QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    // Reset ONLY the snip/* keys — this page's settings, nothing else.
    for (const auto& c : m_snipChecks)
        m_settings->setValue(c.key, c.def);
    for (const auto& s : m_snipSpins)
        m_settings->setValue(s.key, s.def);
    m_settings->setValue(QStringLiteral("snip/detectMode"), QStringLiteral("element"));
    m_settings->setValue(QStringLiteral("snip/display/maskColor"), QColor(QStringLiteral("#464646")).name());
    m_settings->setValue(QStringLiteral("snip/display/anchorBorderColor"),
                         m_settings->value(QStringLiteral("interface/accentColor"),
                                           QStringLiteral("#2d7dff")).toString());
    m_settings->setValue(QStringLiteral("snip/display/anchorColor"), QColor(Qt::white).name());
    m_settings->setValue(QStringLiteral("snip/keys/doubleClickAction"), QStringLiteral("copy"));
    m_settings->setValue(QStringLiteral("snip/keys/middleClickAction"), QStringLiteral("pin"));
    m_settings->setValue(QStringLiteral("snip/keys/enterAction"), QStringLiteral("copy"));

    refreshSnipControls();
}

void PreferencesDialog::refreshSnipControls()
{
    for (const auto& c : m_snipChecks) {
        if (!c.cb) continue;
        const QSignalBlocker b(c.cb);
        c.cb->setChecked(m_settings->value(c.key, c.def).toBool());
    }
    for (const auto& s : m_snipSpins) {
        if (!s.sp) continue;
        const QSignalBlocker b(s.sp);
        s.sp->setValue(m_settings->value(s.key, s.def).toInt());
    }

    // Quick snip: trigger box + Copy/Pin (not part of m_snipChecks / m_hotkeys).
    if (m_quickSnipEdit) {
        const QSignalBlocker b(m_quickSnipEdit);
        m_quickSnipEdit->setTriggerString(m_settings->value(QStringLiteral("snip/quickSnip/trigger")).toString());
    }
    if (m_quickCopyBox) {
        const QSignalBlocker b(m_quickCopyBox);
        m_quickCopyBox->setChecked(m_settings->value(QStringLiteral("snip/quickSnip/copy"), true).toBool());
    }
    if (m_quickPinBox) {
        const QSignalBlocker b(m_quickPinBox);
        m_quickPinBox->setChecked(m_settings->value(QStringLiteral("snip/quickSnip/pin"), false).toBool());
    }

    // Derived detect-mode checkboxes.
    const QString dm = m_settings->value(QStringLiteral("snip/detectMode"), QStringLiteral("element")).toString();
    if (m_snipAutoWindows) {
        const QSignalBlocker b(m_snipAutoWindows);
        m_snipAutoWindows->setChecked(dm != QStringLiteral("off"));
    }
    if (m_snipAutoElements) {
        const QSignalBlocker b(m_snipAutoElements);
        m_snipAutoElements->setChecked(dm == QStringLiteral("element"));
        m_snipAutoElements->setEnabled(dm != QStringLiteral("off"));
    }
    if (m_captureCursor)
        m_captureCursor->setEnabled(
            !m_settings->value(QStringLiteral("snip/behavior/disableCursorCapture"), false).toBool());

    if (m_snipMaskBtn)
        setSwatchIcon(m_snipMaskBtn, pasteColor(QStringLiteral("snip/display/maskColor"), QColor(QStringLiteral("#464646"))));
    if (m_snipAnchorBorderBtn) {
        const QColor themeDefault(m_settings->value(QStringLiteral("interface/accentColor"),
                                                    QStringLiteral("#2d7dff")).toString());
        setSwatchIcon(m_snipAnchorBorderBtn,
                      pasteColor(QStringLiteral("snip/display/anchorBorderColor"), themeDefault));
    }
    if (m_snipAnchorBtn)
        setSwatchIcon(m_snipAnchorBtn, pasteColor(QStringLiteral("snip/display/anchorColor"), Qt::white));

    const auto setCombo = [this](QComboBox* c, const QString& key, const QString& def) {
        if (!c) return;
        const QSignalBlocker b(c);
        const int i = c->findData(m_settings->value(key, def).toString());
        if (i >= 0) c->setCurrentIndex(i);
    };
    setCombo(m_snipDoubleCombo, QStringLiteral("snip/keys/doubleClickAction"), QStringLiteral("copy"));
    setCombo(m_snipMiddleCombo, QStringLiteral("snip/keys/middleClickAction"), QStringLiteral("pin"));
    setCombo(m_snipEnterCombo,  QStringLiteral("snip/keys/enterAction"),       QStringLiteral("copy"));
}

// A read-only "Preview:" value label styled like a setting description.
static QLabel* makePreviewLabel()
{
    auto* lbl = new QLabel;
    lbl->setObjectName(QStringLiteral("SettingDesc"));
    lbl->setWordWrap(true);
    lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);   // value aligns with the field above
    lbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return lbl;
}

QWidget* PreferencesDialog::buildOutputTab()
{
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(24, 20, 24, 20);
    addPageHeader(v, QStringLiteral("prefs.tab.output"), QStringLiteral("prefs.subtitle.output"));

    // "Folder" row: Open/Change-folder button pair for a template path field.
    // "Change Folder" swaps only the directory, keeping the filename template;
    // "Open Folder" reveals the (expanded) directory in the file manager.
    auto addFolderRow = [this](QVBoxLayout* card, QLineEdit* edit) {
        auto* wrap = new QWidget;
        auto* h = new QHBoxLayout(wrap);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(8);
        auto* openBtn = new QPushButton;
        openBtn->setProperty("i18nKey", QStringLiteral("prefs.output.open_folder"));
        m_resetButtons.append(openBtn);
        connect(openBtn, &QPushButton::clicked, this, [edit] {
            const QString dir = QFileInfo(FilenameTemplate::expandNow(edit->text())).path();
            if (!dir.isEmpty())
                QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
        });
        auto* changeBtn = new QPushButton;
        changeBtn->setProperty("i18nKey", QStringLiteral("prefs.output.change_folder"));
        m_resetButtons.append(changeBtn);
        connect(changeBtn, &QPushButton::clicked, this, [this, edit] {
            const QString cur = edit->text();
            const QString picked = pickFolder(QFileInfo(cur).path());
            if (picked.isEmpty())
                return;
            edit->setText(QDir(picked).filePath(QFileInfo(cur).fileName()));  // textChanged persists
        });
        h->addWidget(openBtn);
        h->addWidget(changeBtn);
        addSettingRow(card, QStringLiteral("prefs.output.folder"), QString(), wrap);
    };

    // --- Image quality (applies to every save) -----------------------------
    auto* topCard = beginCard(v);
    m_imageQuality = new QSpinBox;
    m_imageQuality->setRange(-1, 100);
    m_imageQuality->setValue(m_settings->value(QStringLiteral("output/imageQuality"), -1).toInt());
    connect(m_imageQuality, qOverload<int>(&QSpinBox::valueChanged), this, [this](int q) {
        m_settings->setValue(QStringLiteral("output/imageQuality"), q);
    });
    {
        auto* wrap = new QWidget;
        auto* h = new QHBoxLayout(wrap);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(8);
        // The narrow width goes on the WRAPPER, so spin + "(i)" together occupy the same
        // kNarrowControlW as a lone control and this row's right edge lines up with every
        // other one. The spin carries the stretch and takes whatever the badge leaves.
        // (No leading addStretch(): the wrapper is exactly the target width now, so a
        // stretch would only steal space back from the spin.)
        h->addWidget(m_imageQuality, 1);
        h->addWidget(makeInfoLabel(QStringLiteral("prefs.output.hint.image_quality")));
        addSettingRow(topCard, QStringLiteral("prefs.output.image_quality"),
                      QStringLiteral("prefs.desc.output.image_quality"), wrap,
                      /*divider=*/false, /*stretchControl=*/false);
        setScaledWidth(wrap, kNarrowControlW, kNarrowControlW);
    }

    // --- Save to PDF (consumed by ImageExport::toFile → core/codec/PdfEncoder) ---
    // Sits with Image quality rather than under one save path, because like it the
    // setting applies to every save that lands on a .pdf name.
    addSectionHeader(v, QStringLiteral("prefs.output.section.pdf"),
                     QStringLiteral("prefs.output.section_desc.pdf"));
    auto* pdfCard = beginCard(v);

    {
        m_pdfPageGroup = new QButtonGroup(this);
        auto* wrap = new QWidget;
        auto* h = new QHBoxLayout(wrap);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(12);
        const QString cur = m_settings->value(QStringLiteral("output/pdf/pageSize"),
                                              QStringLiteral("dpi96")).toString();
        const struct { const char* mode; const char* key; } sizes[] = {
            {"dpi72", "prefs.output.pdf_page.dpi72"},
            {"dpi96", "prefs.output.pdf_page.dpi96"},
            {"a4",    "prefs.output.pdf_page.a4"},
        };
        for (const auto& s : sizes) {
            auto* rb = new QRadioButton;
            rb->setChecked(cur == QLatin1String(s.mode));
            m_pdfPageGroup->addButton(rb);
            m_buttonI18n.append({rb, QString::fromLatin1(s.key)});
            m_pdfPageRadios.append({rb, QString::fromLatin1(s.mode)});
            const QString mode = QString::fromLatin1(s.mode);
            connect(rb, &QRadioButton::toggled, this, [this, mode](bool on) {
                if (on) m_settings->setValue(QStringLiteral("output/pdf/pageSize"), mode);
            });
            h->addWidget(rb);
        }
        h->addWidget(makeInfoLabel(QStringLiteral("prefs.output.hint.pdf_page_size")));
        addSettingRow(pdfCard, QStringLiteral("prefs.output.pdf_page_size"), QString(), wrap,
                      /*divider=*/false, /*stretchControl=*/false);   // right-aligned; see addActionRow
    }

    // --- File → Manual Save -------------------------------------------------
    addSectionHeader(v, QStringLiteral("prefs.output.section.manual_save"), QString());
    auto* manualCard = beginCard(v);

    m_manualFileName = new QLineEdit(
        m_settings->value(QStringLiteral("output/manualSave/fileName"), defaultFileNameTemplate()).toString());
    connect(m_manualFileName, &QLineEdit::textChanged, this, [this](const QString& t) {
        m_settings->setValue(QStringLiteral("output/manualSave/fileName"), t);
        updateOutputPreviews();
    });
    addSettingRow(manualCard, QStringLiteral("prefs.output.file_name"),
                  QStringLiteral("prefs.desc.output.file_name"), m_manualFileName);

    m_manualPreview = makePreviewLabel();
    addSettingRow(manualCard, QStringLiteral("prefs.output.preview"), QString(), m_manualPreview,
                  /*divider=*/true, /*stretchControl=*/false, /*valueTakesRow=*/true);

    m_rememberExt = addCheckRow(manualCard, QStringLiteral("output/manualSave/rememberExtension"), true,
                                QStringLiteral("prefs.output.remember_ext"), QString(), /*divider=*/false);

    // Alpha-removal toggle under Manual Save — consumed by ImageExport::toFile
    // (output/auto_alpha_channel_removal).
    m_autoAlpha = addCheckRow(manualCard, QStringLiteral("output/auto_alpha_channel_removal"), true,
                              QStringLiteral("prefs.auto_alpha_removal"),
                              QStringLiteral("prefs.desc.auto_alpha_removal"), /*divider=*/false);

    // --- Quick Save (consumed by SnipOverlay::quickSave) -------------------
    addSectionHeader(v, QStringLiteral("prefs.output.section.quick_save"),
                     QStringLiteral("prefs.output.section_desc.quick_save"));
    auto* quickCard = beginCard(v);

    m_quickSaveNotify = addCheckRow(quickCard, QStringLiteral("output/quickSave/showNotification"), true,
                                    QStringLiteral("prefs.output.show_notification"), QString());

    m_quickSavePath = new QLineEdit(
        m_settings->value(QStringLiteral("output/quickSave/pathTemplate"), defaultQuickSavePath()).toString());
    connect(m_quickSavePath, &QLineEdit::textChanged, this, [this](const QString& t) {
        m_settings->setValue(QStringLiteral("output/quickSave/pathTemplate"), t);
        updateOutputPreviews();
    });
    addFolderRow(quickCard, m_quickSavePath);

    addSettingRow(quickCard, QStringLiteral("prefs.output.path"),
                  QStringLiteral("prefs.desc.output.quick_save_path"), m_quickSavePath);

    m_quickSavePreview = makePreviewLabel();
    addSettingRow(quickCard, QStringLiteral("prefs.output.preview"), QString(), m_quickSavePreview,
                  /*divider=*/false, /*stretchControl=*/false, /*valueTakesRow=*/true);

    // --- Auto Save (consumed by SnipOverlay::autoSaveIfEnabled) ------------
    addSectionHeader(v, QStringLiteral("prefs.output.section.auto_save"),
                     QStringLiteral("prefs.output.section_desc.auto_save"));
    auto* autoCard = beginCard(v);

    m_autoSaveEnabled = addCheckRow(autoCard, QStringLiteral("output/autoSave/enabled"), false,
                                    QStringLiteral("prefs.output.auto_save_enable"), QString());

    // Dependent rows live in a container so the enable box greys the whole block.
    m_autoSaveBody = new QWidget;
    auto* autoBodyLay = new QVBoxLayout(m_autoSaveBody);
    autoBodyLay->setContentsMargins(0, 0, 0, 0);
    autoBodyLay->setSpacing(0);
    autoCard->addWidget(m_autoSaveBody);

    m_autoSavePath = new QLineEdit(
        m_settings->value(QStringLiteral("output/autoSave/pathTemplate"), defaultAutoSavePath()).toString());
    connect(m_autoSavePath, &QLineEdit::textChanged, this, [this](const QString& t) {
        m_settings->setValue(QStringLiteral("output/autoSave/pathTemplate"), t);
        updateOutputPreviews();
    });
    addFolderRow(autoBodyLay, m_autoSavePath);

    addSettingRow(autoBodyLay, QStringLiteral("prefs.output.path"),
                  QStringLiteral("prefs.desc.output.auto_save_path"), m_autoSavePath);

    m_autoSavePreview = makePreviewLabel();
    addSettingRow(autoBodyLay, QStringLiteral("prefs.output.preview"), QString(), m_autoSavePreview,
                  /*divider=*/false, /*stretchControl=*/false, /*valueTakesRow=*/true);

    m_autoSaveBody->setEnabled(m_settings->value(QStringLiteral("output/autoSave/enabled"), false).toBool());
    connect(m_autoSaveEnabled, &QCheckBox::toggled, m_autoSaveBody, &QWidget::setEnabled);

    // --- Clipboard (Windows-only; consumed by platform::IClipboard) --------
    addSectionHeader(v, QStringLiteral("prefs.output.section.clipboard"),
                     QStringLiteral("prefs.output.section_desc.clipboard"));
    auto* clipCard = beginCard(v);

    {
        auto* wrap = new QWidget;
        auto* h = new QHBoxLayout(wrap);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(12);
        m_clipDibScreenshot = new QCheckBox;
        m_clipDibScreenshot->setChecked(m_settings->value(QStringLiteral("output/clipboard/dibScreenshot"), true).toBool());
        connect(m_clipDibScreenshot, &QCheckBox::toggled, this, [this](bool on) {
            m_settings->setValue(QStringLiteral("output/clipboard/dibScreenshot"), on);
        });
        m_buttonI18n.append({m_clipDibScreenshot, QStringLiteral("prefs.output.dib_screenshot")});
        m_clipDibImageWindows = new QCheckBox;
        m_clipDibImageWindows->setChecked(m_settings->value(QStringLiteral("output/clipboard/dibImageWindows"), false).toBool());
        connect(m_clipDibImageWindows, &QCheckBox::toggled, this, [this](bool on) {
            m_settings->setValue(QStringLiteral("output/clipboard/dibImageWindows"), on);
        });
        m_buttonI18n.append({m_clipDibImageWindows, QStringLiteral("prefs.output.dib_image_windows")});
        h->addWidget(m_clipDibScreenshot);
        h->addWidget(m_clipDibImageWindows);
        h->addWidget(makeInfoLabel(QStringLiteral("prefs.output.hint.clipboard_dib")));
        addSettingRow(clipCard, QStringLiteral("prefs.output.copy_as_bitmap"), QString(), wrap,
                      /*divider=*/true, /*stretchControl=*/false);   // right-aligned; see addActionRow
    }

    {
        m_copyAsFileGroup = new QButtonGroup(this);
        auto* wrap = new QWidget;
        auto* h = new QHBoxLayout(wrap);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(12);
        const QString cur = m_settings->value(QStringLiteral("output/clipboard/copyAsFile"),
                                              QStringLiteral("disable")).toString();
        const struct { const char* mode; const char* key; } modes[] = {
            {"always",  "prefs.output.copy_file.always"},
            {"hotkey",  "prefs.output.copy_file.hotkey"},
            {"disable", "prefs.output.copy_file.disable"},
        };
        for (const auto& m : modes) {
            auto* rb = new QRadioButton;
            rb->setChecked(cur == QLatin1String(m.mode));
            m_copyAsFileGroup->addButton(rb);
            m_buttonI18n.append({rb, QString::fromLatin1(m.key)});
            m_copyAsFileRadios.append({rb, QString::fromLatin1(m.mode)});
            const QString mode = QString::fromLatin1(m.mode);
            connect(rb, &QRadioButton::toggled, this, [this, mode](bool on) {
                if (on) m_settings->setValue(QStringLiteral("output/clipboard/copyAsFile"), mode);
            });
            h->addWidget(rb);
        }
        addSettingRow(clipCard, QStringLiteral("prefs.output.copy_as_file"), QString(), wrap,
                      /*divider=*/false, /*stretchControl=*/false);   // right-aligned; see addActionRow
    }

    v->addSpacing(20);

    // Bottom bar: Naming Rules (left) · Restore Defaults (right).
    auto* namingBtn = new QPushButton;
    namingBtn->setProperty("i18nKey", QStringLiteral("prefs.output.naming_rules"));
    m_resetButtons.append(namingBtn);
    connect(namingBtn, &QPushButton::clicked, this, &PreferencesDialog::openNamingRules);
    auto* restore = new QPushButton;
    restore->setProperty("i18nKey", QStringLiteral("prefs.restore_defaults"));
    m_resetButtons.append(restore);
    connect(restore, &QPushButton::clicked, this, &PreferencesDialog::restoreOutputDefaults);
    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(namingBtn);
    btnRow->addStretch();
    btnRow->addWidget(restore);
    v->addLayout(btnRow);
    v->addStretch();   // slack sits below the buttons as panel background, not in the cards

    updateOutputPreviews();
    return page;
}

// --- OCR tab ----------------------------------------------------------------

namespace {
// The bundled manifest of downloadable languages (tessdata_fast). Names come from the
// `ocr.lang.<code>` i18n keys; a language not listed here still works as a manual drop-in.
const char* const kOcrManifest[] = {
    "eng", "chi_sim", "chi_tra", "jpn", "kor", "deu", "fra", "spa", "por", "ita", "nld", "rus",
};
} // namespace

QString PreferencesDialog::ocrTessdataDir() const
{
    // Same location the backend reads (TesseractOcr): a portable folder next to the exe.
    return QCoreApplication::applicationDirPath() + QStringLiteral("/tessdata");
}

QStringList PreferencesDialog::installedOcrLanguages() const
{
    QStringList out;
    const QDir dir(ocrTessdataDir());
    for (const QString& f : dir.entryList({QStringLiteral("*.traineddata")}, QDir::Files))
        out.append(QFileInfo(f).completeBaseName());
    out.sort();
    return out;
}

// Preferences → Recording. Sections: Video · Audio · Behavior · Output. (The GIF
// settings are rows of the Video card, shown only while the format is GIF.)
// Every control here has a live consumer — listed key by key in the
// ui/PreferencesDialog row of claude-plan/Architecture-FileMap.md.
QWidget* PreferencesDialog::buildRecordingTab()
{
    using namespace recording;
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(24, 20, 24, 20);
    addPageHeader(v, QStringLiteral("prefs.tab.recording"),
                  QStringLiteral("prefs.subtitle.recording"));

    auto store = [this](const QString& key, const QVariant& value) {
        m_settings->setValue(QStringLiteral("recording/") + key, value);
    };

    // ---- Video ----------------------------------------------------------------
    auto* videoCard = beginCard(v);

    m_recContainer = new CenteredComboBox;
    for (Container c : {Container::Mp4, Container::Mkv, Container::WebM, Container::Gif})
        m_recContainer->addItem(keyFor(c).toUpper(), keyFor(c));
    connect(m_recContainer, &QComboBox::currentIndexChanged, this, [this, store](int) {
        store(QStringLiteral("video/container"), m_recContainer->currentData().toString());
        applyFormatFrameRate();
        updateRecordingEnablement();
        updateOutputPreviews();
    });
    // Rows in this card carry a LEADING divider rather than a trailing one, so a whole
    // group can be hidden per format without stranding the line that belonged to it or
    // stacking two dividers where the group used to be.
    addSettingRow(videoCard, QStringLiteral("prefs.recording.format"),
                  QStringLiteral("prefs.desc.recording.format"), m_recContainer,
                  /*divider*/ false, /*stretchControl*/ false);
    // Every combo on this page takes the same narrow fixed width as the General/Interface
    // ones, so the card reads as one column instead of five differently-sized boxes.
    setScaledWidth(m_recContainer, kNarrowControlW, kNarrowControlW);

    // Codec: meaningless for GIF, which has no codec at all — hidden, not greyed.
    m_recCodecRows = new QWidget;
    auto* codecLay = new QVBoxLayout(m_recCodecRows);
    codecLay->setContentsMargins(0, 0, 0, 0);
    codecLay->setSpacing(0);
    addRowDivider(codecLay);

    m_recCodec = new CenteredComboBox;
    // Codec names are proper nouns — not translated, hence literals rather than keys.
    m_recCodec->addItem(QStringLiteral("H.264"),        keyFor(Codec::H264));
    m_recCodec->addItem(QStringLiteral("H.265 (HEVC)"), keyFor(Codec::H265));
    m_recCodec->addItem(QStringLiteral("AV1"),          keyFor(Codec::AV1));
    m_recCodec->addItem(QStringLiteral("VP9"),          keyFor(Codec::VP9));
    connect(m_recCodec, &QComboBox::currentIndexChanged, this, [this, store](int) {
        store(QStringLiteral("video/codec"), m_recCodec->currentData().toString());
        updateRecordingEnablement();
    });
    addSettingRow(codecLay, QStringLiteral("prefs.recording.codec"),
                  QStringLiteral("prefs.desc.recording.codec"), m_recCodec, false, false);
    setScaledWidth(m_recCodec, kNarrowControlW, kNarrowControlW);
    videoCard->addWidget(m_recCodecRows);

    // Not a setting — what the current choice actually resolves to on THIS machine,
    // so "hardware or software?" is answerable without reading splog.txt. Stays visible
    // for GIF, where it explains that GIF brings its own encoder.
    addRowDivider(videoCard);
    m_recEncoderInfo = new QLabel;
    m_recEncoderInfo->setWordWrap(true);
    m_recEncoderInfo->setObjectName(QStringLiteral("prefsHint"));
    videoCard->addWidget(m_recEncoderInfo);
    addRowDivider(videoCard);

    // ONE frame-rate row for every format: it retargets to the key the current format
    // actually encodes from (video/fps, or gif/fps for GIF — which never reads video/fps).
    // Two rows both labelled "Frame rate" is what made the GIF case look broken.
    m_recFps = new QSpinBox;
    m_recFps->setRange(5, 60);
    m_recFps->setSuffix(QStringLiteral(" fps"));
    connect(m_recFps, &QSpinBox::valueChanged, this, [this, store](int f) {
        store(recContainerIsGif() ? QStringLiteral("gif/fps") : QStringLiteral("video/fps"), f);
    });
    addSettingRow(videoCard, QStringLiteral("prefs.recording.fps"),
                  QStringLiteral("prefs.desc.recording.fps"), m_recFps, false, false);
    setScaledWidth(m_recFps, kNarrowControlW, kNarrowControlW);

    // Rate mode / quality / bitrate / encoder: all video-encoder knobs, so all hidden
    // for GIF, which is written by its own palette chain instead.
    m_recEncodeRows = new QWidget;
    auto* encLay = new QVBoxLayout(m_recEncodeRows);
    encLay->setContentsMargins(0, 0, 0, 0);
    encLay->setSpacing(0);

    m_recRateMode = new CenteredComboBox;
    m_recRateMode->addItem(QString(), keyFor(RateMode::Quality));
    m_recRateMode->addItem(QString(), keyFor(RateMode::Bitrate));
    connect(m_recRateMode, &QComboBox::currentIndexChanged, this, [this, store](int) {
        store(QStringLiteral("video/rateMode"), m_recRateMode->currentData().toString());
        updateRecordingEnablement();
    });
    addRowDivider(encLay);
    addSettingRow(encLay, QStringLiteral("prefs.recording.rate_mode"),
                  QStringLiteral("prefs.desc.recording.rate_mode"), m_recRateMode, false, false);
    setScaledWidth(m_recRateMode, kNarrowControlW, kNarrowControlW);

    auto* qualityBox = new QWidget;
    auto* qLay = new QHBoxLayout(qualityBox);
    qLay->setContentsMargins(0, 0, 0, 0);
    m_recQuality = new QSlider(Qt::Horizontal);
    m_recQuality->setRange(0, 100);
    m_recQuality->setMinimumWidth(160);
    m_recQualityValue = new QLabel;
    m_recQualityValue->setMinimumWidth(32);
    qLay->addWidget(m_recQuality);
    qLay->addWidget(m_recQualityValue);
    connect(m_recQuality, &QSlider::valueChanged, this, [this, store](int q) {
        store(QStringLiteral("video/quality"), q);
        m_recQualityValue->setText(QString::number(q));
    });
    // Quality and Target bitrate are alternatives: the rate mode above picks one, and only
    // that one is shown. Each is its own group carrying its own leading divider, so the
    // swap never strands a line — exactly one of the pair is visible at any time.
    m_recQualityRow = new QWidget;
    auto* qRowLay = new QVBoxLayout(m_recQualityRow);
    qRowLay->setContentsMargins(0, 0, 0, 0);
    qRowLay->setSpacing(0);
    addRowDivider(qRowLay);
    addSettingRow(qRowLay, QStringLiteral("prefs.recording.quality"),
                  QStringLiteral("prefs.desc.recording.quality"), qualityBox, false, false);
    encLay->addWidget(m_recQualityRow);

    m_recBitrate = new QSpinBox;
    m_recBitrate->setRange(500, 100000);
    m_recBitrate->setSingleStep(500);
    m_recBitrate->setSuffix(QStringLiteral(" kbps"));
    connect(m_recBitrate, &QSpinBox::valueChanged, this, [store](int b) {
        store(QStringLiteral("video/bitrateKbps"), b);
    });
    m_recBitrateRow = new QWidget;
    auto* bRowLay = new QVBoxLayout(m_recBitrateRow);
    bRowLay->setContentsMargins(0, 0, 0, 0);
    bRowLay->setSpacing(0);
    addRowDivider(bRowLay);
    addSettingRow(bRowLay, QStringLiteral("prefs.recording.bitrate"),
                  QStringLiteral("prefs.desc.recording.bitrate"), m_recBitrate, false, false);
    setScaledWidth(m_recBitrate, kNarrowControlW, kNarrowControlW);
    encLay->addWidget(m_recBitrateRow);

    m_recHw = new CenteredComboBox;
    for (HwPreference h : {HwPreference::Auto, HwPreference::PreferHardware,
                           HwPreference::ForceSoftware})
        m_recHw->addItem(QString(), keyFor(h));
    connect(m_recHw, &QComboBox::currentIndexChanged, this, [this, store](int) {
        store(QStringLiteral("video/hwPreference"), m_recHw->currentData().toString());
        updateRecordingEnablement();
    });
    addRowDivider(encLay);
    addSettingRow(encLay, QStringLiteral("prefs.recording.hw"),
                  QStringLiteral("prefs.desc.recording.hw"), m_recHw, false, false);
    setScaledWidth(m_recHw, kNarrowControlW, kNarrowControlW);
    videoCard->addWidget(m_recEncodeRows);

    // GIF-only rows, in this same card rather than a section of their own — they are
    // settings for the format chosen at the top. Wrapped in ONE container so a single
    // setVisible() carries the leading divider with them; hiding bare rows would leave
    // the card ending on a dangling divider whenever the format is not GIF.
    m_recGifRows = new QWidget;
    auto* gifLay = new QVBoxLayout(m_recGifRows);
    gifLay->setContentsMargins(0, 0, 0, 0);
    gifLay->setSpacing(0);
    addRowDivider(gifLay);

    m_recGifWidth = new QSpinBox;
    m_recGifWidth->setRange(240, 1920);
    m_recGifWidth->setSingleStep(20);
    m_recGifWidth->setSuffix(QStringLiteral(" px"));
    connect(m_recGifWidth, &QSpinBox::valueChanged, this, [store](int w) {
        store(QStringLiteral("gif/maxWidth"), w);
    });
    addSettingRow(gifLay, QStringLiteral("prefs.recording.gif_width"),
                  QStringLiteral("prefs.desc.recording.gif_width"), m_recGifWidth, false, false);
    setScaledWidth(m_recGifWidth, kNarrowControlW, kNarrowControlW);

    addRowDivider(gifLay);
    m_recGifLoop = addCheckRow(gifLay, QStringLiteral("recording/gif/loop"), true,
                               QStringLiteral("prefs.recording.gif_loop"),
                               QStringLiteral("prefs.desc.recording.gif_loop"), false);
    videoCard->addWidget(m_recGifRows);

    // ---- Audio ----------------------------------------------------------------
    // GIF has no audio stream at all (WinRecorder skips openAudio() for it), so the
    // whole section goes away for GIF rather than greying — same call the codec and
    // encoder rows get. Header AND card live in one container because they are two
    // separate additions to the page layout: hiding only the card would leave an
    // "Audio" heading standing over nothing.
    m_recAudioSection = new QWidget;
    auto* audioSecLay = new QVBoxLayout(m_recAudioSection);
    audioSecLay->setContentsMargins(0, 0, 0, 0);
    audioSecLay->setSpacing(v->spacing());
    v->addWidget(m_recAudioSection);

    addSectionHeader(audioSecLay, QStringLiteral("prefs.recording.section.audio"),
                     QStringLiteral("prefs.desc.recording.section.audio"));
    auto* audioCard = beginCard(audioSecLay);
    m_recSystemAudio = addCheckRow(audioCard, QStringLiteral("recording/audio/system"), true,
                                   QStringLiteral("prefs.recording.system_audio"),
                                   QStringLiteral("prefs.desc.recording.system_audio"));
    m_recMic = addCheckRow(audioCard, QStringLiteral("recording/audio/mic"), false,
                           QStringLiteral("prefs.recording.mic"),
                           QStringLiteral("prefs.desc.recording.mic"));
    m_recAudioBitrate = new CenteredComboBox;
    for (int kbps : {96, 128, 192, 256})
        m_recAudioBitrate->addItem(QStringLiteral("%1 kbps").arg(kbps), kbps);
    connect(m_recAudioBitrate, &QComboBox::currentIndexChanged, this, [this, store](int) {
        store(QStringLiteral("audio/bitrateKbps"), m_recAudioBitrate->currentData().toInt());
    });
    addSettingRow(audioCard, QStringLiteral("prefs.recording.audio_bitrate"),
                  QStringLiteral("prefs.desc.recording.audio_bitrate"), m_recAudioBitrate,
                  false, false);
    setScaledWidth(m_recAudioBitrate, kNarrowControlW, kNarrowControlW);

    // ---- Behavior -------------------------------------------------------------
    addSectionHeader(v, QStringLiteral("prefs.recording.section.behavior"), QString());
    auto* behCard = beginCard(v);
    m_recCountdown = new QSpinBox;
    m_recCountdown->setRange(0, 10);
    m_recCountdown->setSuffix(QStringLiteral(" s"));
    connect(m_recCountdown, &QSpinBox::valueChanged, this, [store](int s) {
        store(QStringLiteral("behavior/countdown"), s);
    });
    addSettingRow(behCard, QStringLiteral("prefs.recording.countdown"),
                  QStringLiteral("prefs.desc.recording.countdown"), m_recCountdown, true, false);
    setScaledWidth(m_recCountdown, kNarrowControlW, kNarrowControlW);

    m_recCursor = addCheckRow(behCard, QStringLiteral("recording/behavior/captureCursor"), true,
                              QStringLiteral("prefs.recording.cursor"),
                              QStringLiteral("prefs.desc.recording.cursor"));

    m_recMaxMinutes = new QSpinBox;
    m_recMaxMinutes->setRange(0, 600);
    m_recMaxMinutes->setSpecialValueText(QStringLiteral("∞"));     // 0 = no limit
    m_recMaxMinutes->setSuffix(QStringLiteral(" min"));
    connect(m_recMaxMinutes, &QSpinBox::valueChanged, this, [store](int m) {
        store(QStringLiteral("behavior/maxDurationMin"), m);
    });
    addSettingRow(behCard, QStringLiteral("prefs.recording.max_duration"),
                  QStringLiteral("prefs.desc.recording.max_duration"), m_recMaxMinutes,
                  false, false);
    setScaledWidth(m_recMaxMinutes, kNarrowControlW, kNarrowControlW);

    // ---- Output ---------------------------------------------------------------
    addSectionHeader(v, QStringLiteral("prefs.recording.section.output"), QString());
    auto* outCard = beginCard(v);
    m_recPath = new QLineEdit;
    connect(m_recPath, &QLineEdit::textChanged, this, [this, store](const QString& t) {
        store(QStringLiteral("output/pathTemplate"), t);
        updateOutputPreviews();
    });
    auto* browseRow = new QWidget;
    auto* bLay = new QHBoxLayout(browseRow);
    bLay->setContentsMargins(0, 0, 0, 0);
    bLay->addWidget(m_recPath, 1);
    // A labelled Browse button like the General page's, not the old fixed-width "…":
    // a text button sizes to its own label, so it follows the interface font (and the
    // locale) without a width of its own. The i18nKey property puts it on the same
    // retranslate loop as every other Browse button.
    auto* browse = new QPushButton;
    browse->setProperty("i18nKey", QStringLiteral("prefs.browse"));
    browse->setCursor(Qt::PointingHandCursor);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QFileInfo fi(m_recPath->text());
        const QString dir = pickFolder(fi.path());
        if (!dir.isEmpty())
            m_recPath->setText(QDir(dir).filePath(fi.fileName()));
    });
    bLay->addWidget(browse);
    m_resetButtons.append(browse);   // reuse the i18nKey retranslate loop
    addSettingRow(outCard, QStringLiteral("prefs.recording.path"),
                  QStringLiteral("prefs.desc.recording.path"), browseRow,
                  /*divider=*/true, /*stretchControl=*/false);   // width on the edit — see Log path
    setScaledWidth(m_recPath, kControlW);
    // Same shape as the Output page's Manual/Quick/Auto save previews: a labelled row
    // whose value takes the row's width, so a long path wraps at the row edge.
    m_recPathPreview = makePreviewLabel();
    addSettingRow(outCard, QStringLiteral("prefs.output.preview"), QString(), m_recPathPreview,
                  /*divider=*/true, /*stretchControl=*/false, /*valueTakesRow=*/true);

    m_recOpenPreview = addCheckRow(outCard, QStringLiteral("recording/output/openPreview"), true,
                                   QStringLiteral("prefs.recording.open_preview"),
                                   QStringLiteral("prefs.desc.recording.open_preview"), false);

    // Page-scoped restore, matching the other tabs.
    auto* barRow = new QWidget;
    auto* barLay = new QHBoxLayout(barRow);
    barLay->setContentsMargins(0, 0, 0, 0);
    barLay->addStretch(1);
    m_recRestoreBtn = new QPushButton;
    m_recRestoreBtn->setCursor(Qt::PointingHandCursor);
    m_buttonI18n.append({m_recRestoreBtn, QStringLiteral("prefs.restore_defaults")});
    connect(m_recRestoreBtn, &QPushButton::clicked, this, [this] { restoreRecordingDefaults(); });
    barLay->addWidget(m_recRestoreBtn);
    v->addWidget(barRow);

    v->addStretch(1);
    refreshRecordingControls();
    return page;
}

void PreferencesDialog::refreshRecordingControls()
{
    using namespace recording;
    if (!m_recContainer)
        return;
    auto get = [this](const QString& key, const QVariant& def) {
        return m_settings->value(QStringLiteral("recording/") + key, def);
    };
    const Config d;      // struct defaults are the factory defaults

    auto selectData = [](QComboBox* box, const QString& value) {
        const int i = box->findData(value);
        box->blockSignals(true);
        box->setCurrentIndex(i >= 0 ? i : 0);
        box->blockSignals(false);
    };
    selectData(m_recContainer, get(QStringLiteral("video/container"), keyFor(d.container)).toString());
    selectData(m_recCodec,     get(QStringLiteral("video/codec"),     keyFor(d.codec)).toString());
    selectData(m_recRateMode,  get(QStringLiteral("video/rateMode"),  keyFor(d.rateMode)).toString());
    selectData(m_recHw,        get(QStringLiteral("video/hwPreference"), keyFor(d.hw)).toString());

    auto setSpin = [](QSpinBox* s, int v) {
        s->blockSignals(true); s->setValue(v); s->blockSignals(false);
    };
    // The shared frame-rate row follows the format selected just above.
    applyFrameRateRange(get(recContainerIsGif() ? QStringLiteral("gif/fps")
                                                : QStringLiteral("video/fps"),
                            recContainerIsGif() ? d.gifFps : d.fps).toInt());
    setSpin(m_recBitrate,    get(QStringLiteral("video/bitrateKbps"), d.bitrateKbps).toInt());
    setSpin(m_recCountdown,  get(QStringLiteral("behavior/countdown"), 0).toInt());
    setSpin(m_recMaxMinutes, get(QStringLiteral("behavior/maxDurationMin"), 0).toInt());
    setSpin(m_recGifWidth,   get(QStringLiteral("gif/maxWidth"), d.gifMaxWidth).toInt());

    const int q = get(QStringLiteral("video/quality"), d.quality).toInt();
    m_recQuality->blockSignals(true);
    m_recQuality->setValue(q);
    m_recQuality->blockSignals(false);
    m_recQualityValue->setText(QString::number(q));

    const int ab = get(QStringLiteral("audio/bitrateKbps"), d.audioBitrateKbps).toInt();
    // Fall back to the default's item rather than a hard-coded index, so the two can't
    // drift apart when the default changes.
    int abIdx = m_recAudioBitrate->findData(ab);
    if (abIdx < 0)
        abIdx = m_recAudioBitrate->findData(d.audioBitrateKbps);
    m_recAudioBitrate->blockSignals(true);
    m_recAudioBitrate->setCurrentIndex(qMax(0, abIdx));
    m_recAudioBitrate->blockSignals(false);

    auto setCheck = [](QCheckBox* c, bool on) {
        c->blockSignals(true); c->setChecked(on); c->blockSignals(false);
    };
    setCheck(m_recSystemAudio, get(QStringLiteral("audio/system"), d.systemAudio).toBool());
    setCheck(m_recMic,         get(QStringLiteral("audio/mic"), d.microphone).toBool());
    setCheck(m_recCursor,      get(QStringLiteral("behavior/captureCursor"), d.captureCursor).toBool());
    setCheck(m_recGifLoop,     get(QStringLiteral("gif/loop"), d.gifLoop).toBool());
    setCheck(m_recOpenPreview, get(QStringLiteral("output/openPreview"), true).toBool());

    m_recPath->blockSignals(true);
    m_recPath->setText(get(QStringLiteral("output/pathTemplate"), defaultPathTemplate()).toString());
    m_recPath->blockSignals(false);

    updateRecordingEnablement();
    updateOutputPreviews();
}

bool PreferencesDialog::recContainerIsGif() const
{
    return m_recContainer
        && recording::containerFromKey(m_recContainer->currentData().toString())
               == recording::Container::Gif;
}

// Point the shared row at the current format: GIF tops out at 30 fps, video at 60.
// Signals stay blocked because narrowing the range CLAMPS the value and would emit
// valueChanged on the way through — writing the clamped number to the new format's key
// before the caller has decided what that key should hold.
void PreferencesDialog::applyFrameRateRange(int value)
{
    if (!m_recFps)
        return;
    m_recFps->blockSignals(true);
    m_recFps->setRange(5, recContainerIsGif() ? 30 : 60);
    m_recFps->setValue(value);
    m_recFps->blockSignals(false);
}

void PreferencesDialog::applyFormatFrameRate()
{
    using namespace recording;
    if (!m_recContainer || !m_recFps)
        return;
    const Config d;
    const bool gif = recContainerIsGif();
    const int fps = gif ? d.gifFps : d.fps;
    applyFrameRateRange(fps);
    // Written explicitly rather than through the spin box's valueChanged, which the line
    // above deliberately suppresses.
    m_settings->setValue(gif ? QStringLiteral("recording/gif/fps")
                             : QStringLiteral("recording/video/fps"), fps);
}

void PreferencesDialog::updateRecordingEnablement()
{
    using namespace recording;
    if (!m_recContainer)
        return;
    const Container container = containerFromKey(m_recContainer->currentData().toString());
    const Codec codec = codecFromKey(m_recCodec->currentData().toString());
    const bool gif = (container == Container::Gif);

    // GIF is its own encoder: no codec, no quality/bitrate/encoder knobs, no audio track.
    // Those rows are hidden rather than greyed — a GIF has no such setting to reason about.
    if (m_recCodecRows)  m_recCodecRows->setVisible(!gif);
    if (m_recEncodeRows) m_recEncodeRows->setVisible(!gif);
    if (m_recGifRows)    m_recGifRows->setVisible(gif);
    // Ditto the Audio section (header + card): recording/audio/system defaults to ON,
    // so leaving it up for GIF showed a ticked "System audio" over a silent file.
    if (m_recAudioSection) m_recAudioSection->setVisible(!gif);

    // Rate mode picks one of the pair; show that one and hide the other. No `gif` term —
    // both live inside m_recEncodeRows, which is already hidden wholesale for GIF.
    const bool quality =
        rateModeFromKey(m_recRateMode->currentData().toString()) == RateMode::Quality;
    if (m_recQualityRow) m_recQualityRow->setVisible(quality);
    if (m_recBitrateRow) m_recBitrateRow->setVisible(!quality);

    // WebM carries only VP9/AV1 — grey the rest rather than let the recording fail
    // at mux time with something the user cannot interpret.
    for (int i = 0; i < m_recCodec->count(); ++i) {
        const Codec c = codecFromKey(m_recCodec->itemData(i).toString());
        const bool ok = supports(container, c) || gif;
        auto* model = qobject_cast<QStandardItemModel*>(m_recCodec->model());
        if (model && model->item(i))
            model->item(i)->setEnabled(ok);
    }
    // If the current codec just became invalid for this container, move to one that works.
    if (!gif && !supports(container, codec)) {
        for (int i = 0; i < m_recCodec->count(); ++i) {
            if (supports(container, codecFromKey(m_recCodec->itemData(i).toString()))) {
                m_recCodec->setCurrentIndex(i);      // emits → stores + re-enters here
                return;
            }
        }
    }

    // What the current choice actually resolves to on this machine.
    if (m_recEncoderInfo) {
        const I18n& tr = I18n::instance();
        if (gif) {
            m_recEncoderInfo->setText(tr.t(QStringLiteral("prefs.recording.encoder_gif")));
        } else {
            const QString enc = platform::activeVideoEncoder(
                codec, hwFromKey(m_recHw->currentData().toString()));
            if (enc.isEmpty()) {
                m_recEncoderInfo->setText(tr.t(QStringLiteral("prefs.recording.encoder_none")));
            } else {
                m_recEncoderInfo->setText(
                    tr.t(QStringLiteral("prefs.recording.encoder_info"))
                        .replace(QStringLiteral("{encoder}"), enc)
                        .replace(QStringLiteral("{kind}"),
                                 tr.t(isSoftwareEncoder(enc)
                                          ? QStringLiteral("prefs.recording.encoder_software")
                                          : QStringLiteral("prefs.recording.encoder_hardware"))));
            }
        }
    }

    // Showing/hiding whole groups changes how tall this page needs to be, and the content
    // stack is FIXED-height (see adjustStackHeightForCurrentPage) — without re-running it
    // the card keeps its old height and the rows overlap until something resizes the
    // window. Guarded on isVisible() because this also runs while the page is still being
    // built, when the current stack page is some other tab.
    if (isVisible())
        adjustStackHeightForCurrentPage();
}

void PreferencesDialog::restoreRecordingDefaults()
{
    using namespace recording;
    const Config d;
    m_settings->setValue(QStringLiteral("recording/video/container"), keyFor(d.container));
    m_settings->setValue(QStringLiteral("recording/video/codec"), keyFor(d.codec));
    m_settings->setValue(QStringLiteral("recording/video/fps"), d.fps);
    m_settings->setValue(QStringLiteral("recording/video/rateMode"), keyFor(d.rateMode));
    m_settings->setValue(QStringLiteral("recording/video/quality"), d.quality);
    m_settings->setValue(QStringLiteral("recording/video/bitrateKbps"), d.bitrateKbps);
    m_settings->setValue(QStringLiteral("recording/video/hwPreference"), keyFor(d.hw));
    m_settings->setValue(QStringLiteral("recording/audio/system"), d.systemAudio);
    m_settings->setValue(QStringLiteral("recording/audio/mic"), d.microphone);
    m_settings->setValue(QStringLiteral("recording/audio/bitrateKbps"), d.audioBitrateKbps);
    m_settings->setValue(QStringLiteral("recording/behavior/countdown"), 0);
    m_settings->setValue(QStringLiteral("recording/behavior/captureCursor"), d.captureCursor);
    m_settings->setValue(QStringLiteral("recording/behavior/maxDurationMin"), 0);
    m_settings->setValue(QStringLiteral("recording/gif/fps"), d.gifFps);
    m_settings->setValue(QStringLiteral("recording/gif/maxWidth"), d.gifMaxWidth);
    m_settings->setValue(QStringLiteral("recording/gif/loop"), d.gifLoop);
    m_settings->setValue(QStringLiteral("recording/output/pathTemplate"), defaultPathTemplate());
    m_settings->setValue(QStringLiteral("recording/output/openPreview"), true);
    refreshRecordingControls();
}

QWidget* PreferencesDialog::buildOcrTab()
{
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(24, 20, 24, 20);
    addPageHeader(v, QStringLiteral("prefs.tab.ocr"), QStringLiteral("prefs.subtitle.ocr"));

    // Default language (empty data = follow the UI locale).
    auto* topCard = beginCard(v);
    m_ocrDefaultLang = new CenteredComboBox;
    connect(m_ocrDefaultLang, &QComboBox::currentIndexChanged, this, [this](int) {
        if (m_ocrDefaultLang)
            m_settings->setValue(QStringLiteral("ocr/language"),
                                 m_ocrDefaultLang->currentData().toString());
    });
    addSettingRow(topCard, QStringLiteral("ocr.prefs.default_lang"),
                  QStringLiteral("ocr.prefs.default_lang_desc"), m_ocrDefaultLang,
                  /*divider*/ false, /*stretchControl*/ false);

    // Folder — above the language list.
    addSectionHeader(v, QStringLiteral("ocr.prefs.folder"),
                     QStringLiteral("ocr.prefs.open_folder_desc"));
    auto* openCard = beginCard(v);
    auto* openBtn = new QPushButton;
    openBtn->setCursor(Qt::PointingHandCursor);
    m_buttonI18n.append({openBtn, QStringLiteral("ocr.prefs.open")});
    connect(openBtn, &QPushButton::clicked, this, [this] {
        const QString dir = ocrTessdataDir();
        QDir().mkpath(dir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    addSettingRow(openCard, QStringLiteral("ocr.prefs.open_folder"), QString(), openBtn,
                  /*divider*/ false, /*stretchControl*/ false);

    // Language list: the manifest plus any drop-ins already on disk.
    addSectionHeader(v, QStringLiteral("ocr.prefs.languages"),
                     QStringLiteral("ocr.prefs.languages_desc"));
    auto* listCard = beginCard(v);

    QStringList manifest;
    for (const char* c : kOcrManifest)
        manifest.append(QString::fromLatin1(c));
    QStringList codes = manifest;
    for (const QString& code : installedOcrLanguages())
        if (!codes.contains(code))
            codes.append(code);

    for (int i = 0; i < codes.size(); ++i) {
        const QString code = codes.at(i);
        const bool known = manifest.contains(code);

        auto* ctrl = new QWidget;
        auto* ch = new QHBoxLayout(ctrl);
        ch->setContentsMargins(0, 0, 0, 0);
        ch->setSpacing(8);
        auto* status = new QLabel;
        status->setObjectName(QStringLiteral("SettingDesc"));
        auto* btn = new QPushButton;
        btn->setCursor(Qt::PointingHandCursor);
        ch->addWidget(status);
        ch->addWidget(btn);
        connect(btn, &QPushButton::clicked, this, [this, code] {
            if (installedOcrLanguages().contains(code)) removeOcrLanguage(code);
            else                                        startOcrDownload(code);
        });
        m_ocrRows.append({code, btn, status});

        // Known languages get a retranslatable name key; drop-ins show their raw code.
        const QString nameKey = known ? QStringLiteral("ocr.lang.") + code : code;
        addSettingRow(listCard, nameKey, QString(), ctrl, /*divider*/ i < codes.size() - 1,
                      /*stretchControl*/ false);
    }

    refreshOcrControls();
    v->addStretch();
    return page;
}

void PreferencesDialog::refreshOcrControls()
{
    const QStringList installed = installedOcrLanguages();

    for (const OcrLangRow& r : m_ocrRows) {
        const bool have = installed.contains(r.code);
        if (r.status)
            r.status->setText(have ? I18n::instance().t(QStringLiteral("ocr.prefs.installed"))
                                   : QString());
        if (r.action) {
            r.action->setEnabled(true);
            r.action->setText(I18n::instance().t(have ? QStringLiteral("ocr.prefs.remove")
                                                      : QStringLiteral("ocr.prefs.download")));
        }
    }

    if (m_ocrDefaultLang) {
        const QString cur = m_settings->value(QStringLiteral("ocr/language")).toString();
        const QSignalBlocker block(m_ocrDefaultLang);
        m_ocrDefaultLang->clear();
        m_ocrDefaultLang->addItem(I18n::instance().t(QStringLiteral("ocr.prefs.default_auto")),
                                  QString());
        for (const QString& code : installed)
            m_ocrDefaultLang->addItem(OcrBar::displayName(code), code);
        const int idx = m_ocrDefaultLang->findData(cur);
        m_ocrDefaultLang->setCurrentIndex(idx >= 0 ? idx : 0);
    }
}

void PreferencesDialog::startOcrDownload(const QString& code)
{
    if (!m_ocrNet)
        m_ocrNet = new QNetworkAccessManager(this);

    const QString dir = ocrTessdataDir();
    QDir().mkpath(dir);

    // tessdata_fast raw file — a direct 200 (no redirect); Schannel handles TLS on Windows.
    const QUrl url(QStringLiteral(
        "https://raw.githubusercontent.com/tesseract-ocr/tessdata_fast/main/%1.traineddata")
                       .arg(code));
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    for (const OcrLangRow& r : m_ocrRows)
        if (r.code == code && r.action) {
            r.action->setEnabled(false);
            r.action->setText(I18n::instance().t(QStringLiteral("ocr.prefs.downloading")));
        }

    QNetworkReply* reply = m_ocrNet->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, code, dir] {
        reply->deleteLater();
        const QByteArray data = reply->readAll();
        bool ok = reply->error() == QNetworkReply::NoError && !data.isEmpty();
        if (ok) {
            QFile f(dir + QLatin1Char('/') + code + QStringLiteral(".traineddata"));
            ok = f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
        }
        if (!ok)
            QMessageBox::warning(
                this, I18n::instance().t(QStringLiteral("ocr.title")),
                I18n::instance().t(QStringLiteral("ocr.prefs.download_failed"),
                                   {{QStringLiteral("name"), OcrBar::displayName(code)}}));
        refreshOcrControls();
    });
}

void PreferencesDialog::removeOcrLanguage(const QString& code)
{
    const auto btn = QMessageBox::question(
        this, I18n::instance().t(QStringLiteral("ocr.title")),
        I18n::instance().t(QStringLiteral("ocr.prefs.remove_confirm"),
                           {{QStringLiteral("name"), OcrBar::displayName(code)}}));
    if (btn != QMessageBox::Yes)
        return;
    QFile::remove(ocrTessdataDir() + QLatin1Char('/') + code + QStringLiteral(".traineddata"));
    refreshOcrControls();
}

QWidget* PreferencesDialog::buildControlTab()
{
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(24, 20, 24, 20);
    addPageHeader(v, QStringLiteral("prefs.tab.control"), QStringLiteral("prefs.subtitle.control"));

    // Search box: filters the hotkey rows below (see filterControlRows).
    m_controlSearch = new QLineEdit;
    m_controlSearch->setObjectName(QStringLiteral("PrefsSearch"));
    m_controlSearch->setClearButtonEnabled(true);
    m_controlSearch->setPlaceholderText(
        I18n::instance().t(QStringLiteral("prefs.control.search")));
    connect(m_controlSearch, &QLineEdit::textChanged, this, &PreferencesDialog::filterControlRows);
    v->addWidget(m_controlSearch);
    v->addSpacing(10);

    // Four inline sections (Core/View/Tools/Misc). Each header+card lives in a
    // container so the whole section can be hidden as one unit when it has no
    // visible rows (an active filter, or the empty Tools section).
    const int nSections = int(std::size(kSectionTitles));
    for (int s = 0; s < nSections; ++s) {
        auto* container = new QWidget;
        auto* cv = new QVBoxLayout(container);
        cv->setContentsMargins(0, 0, 0, 0);
        cv->setSpacing(0);
        addSectionHeader(cv, QString::fromLatin1(kSectionTitles[s]));
        auto* card = beginCard(cv);
        m_controlSections.append(container);
        v->addWidget(container);

        // Rows belonging to this section, in declared order.
        QVector<int> idxs;
        for (int i = 0; i < int(std::size(kGlobalDefaults)); ++i)
            if (kGlobalDefaults[i].section == s)
                idxs.append(i);

        for (int k = 0; k < idxs.size(); ++k) {
            const auto& g = kGlobalDefaults[idxs[k]];
            HotkeyRow proto;
            proto.name = QString::fromLatin1(g.name);
            proto.defaultSeq = QString::fromLatin1(g.seq);
            const bool divider = k < idxs.size() - 1;
            const QString labelKey = QString::fromLatin1(g.labelKey);

            // Quick snip has a bespoke vertical layout (description on top, Copy/Pin +
            // the hold-and-drag hotkey below), so it skips the shared row builder.
            if (proto.name == QStringLiteral("quickSnip")) {
                buildQuickSnipRow(card, proto, labelKey, divider, s);
                continue;
            }

            QWidget* wrap = buildHotkeyRow(card, proto, labelKey, divider, s);
            // The custom-snip row gets a Configure button for the area/delay it applies,
            // sitting left of the capture box.
            if (proto.name == QStringLiteral("customSnip")) {
                auto* cfg = new QPushButton;
                cfg->setProperty("i18nKey", QStringLiteral("prefs.hotkey.custom_snip_configure"));
                connect(cfg, &QPushButton::clicked, this, &PreferencesDialog::openCustomSnip);
                qobject_cast<QHBoxLayout*>(wrap->layout())->insertWidget(0, cfg);
                m_resetButtons.append(cfg);   // retranslated with the other i18nKey buttons
            }
        }

        // Tools has no rows yet: a muted placeholder keeps the empty card from
        // looking broken. It's inside the container, so a filter hides both together.
        if (idxs.isEmpty()) {
            auto* pad = new QWidget;
            auto* ph = new QHBoxLayout(pad);
            ph->setContentsMargins(0, 12, 0, 12);
            auto* empty = new QLabel;
            empty->setObjectName(QStringLiteral("SettingDesc"));
            m_labels.append({empty, QStringLiteral("prefs.control.tools_empty")});
            ph->addWidget(empty);
            card->addWidget(pad);
        }
    }

    // Hidden internal-conflict text (per-row red crosses are the primary signal).
    // Lives inline in the button row rather than the vertical flow, so an empty
    // label doesn't inflate the gap above the buttons — keeping this tab's spacing
    // identical to the others (card → addSpacing(20) → btnRow).
    m_hotkeyConflict = new QLabel;
    m_hotkeyConflict->setObjectName(QStringLiteral("hotkeyConflict"));

    // Bottom bar: Key Bindings (left) · conflict text · Restore Defaults (right).
    // Wrapped in a container (with the former addSpacing(20) as its top margin) so a
    // search that matches no hotkey rows hides the buttons too — they belong to the
    // hotkey list, not the page (see filterControlRows).
    auto* kbBtn = new QPushButton;
    kbBtn->setProperty("i18nKey", QStringLiteral("prefs.hotkey.key_bindings"));
    connect(kbBtn, &QPushButton::clicked, this, &PreferencesDialog::openKeyBindings);
    auto* restore = new QPushButton;
    restore->setProperty("i18nKey", QStringLiteral("prefs.restore_defaults"));
    connect(restore, &QPushButton::clicked, this, &PreferencesDialog::restoreControlDefaults);
    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(kbBtn);
    btnRow->addSpacing(12);
    btnRow->addWidget(m_hotkeyConflict);
    btnRow->addStretch();
    btnRow->addWidget(restore);
    m_hotkeyButtonBar = new QWidget;
    auto* barWrap = new QVBoxLayout(m_hotkeyButtonBar);
    barWrap->setContentsMargins(0, 20, 0, 0);
    barWrap->addLayout(btnRow);
    v->addWidget(m_hotkeyButtonBar);
    m_resetButtons.append(kbBtn);
    m_resetButtons.append(restore);

    // App exclusions: apps whose foreground state mutes our global hotkeys so the
    // key reaches them (see WinGlobalHotkey::setIgnoredApps). Wrapped in its own
    // container so the search box can show/hide the whole block by matching its
    // title/hint (but not the listed app names) — see filterControlRows.
    m_exclusionsSection = new QWidget;
    auto* ev = new QVBoxLayout(m_exclusionsSection);
    ev->setContentsMargins(0, 0, 0, 0);
    ev->setSpacing(0);
    ev->addSpacing(20);
    addSectionHeader(ev, QStringLiteral("prefs.control.exclusions.title"),
                     QStringLiteral("prefs.control.exclusions.hint"));
    auto* exCard = beginCard(ev);
    v->addWidget(m_exclusionsSection);
    m_ignoreAppsList = new QListWidget;
    m_ignoreAppsList->setObjectName(QStringLiteral("IgnoreAppsList"));
    m_ignoreAppsList->setMaximumHeight(140);
    for (const QString& exe : m_settings->value(QStringLiteral("control/ignoreApps")).toStringList())
        m_ignoreAppsList->addItem(exe);
    exCard->addWidget(m_ignoreAppsList);

    auto* addBtn = new QPushButton;
    addBtn->setProperty("i18nKey", QStringLiteral("prefs.control.exclusions.add"));
    connect(addBtn, &QPushButton::clicked, this, &PreferencesDialog::addIgnoreApps);
    auto* rmBtn = new QPushButton;
    rmBtn->setProperty("i18nKey", QStringLiteral("prefs.control.exclusions.remove"));
    connect(rmBtn, &QPushButton::clicked, this, &PreferencesDialog::removeSelectedIgnoreApp);
    auto* exBtns = new QHBoxLayout;
    exBtns->addWidget(addBtn);
    exBtns->addWidget(rmBtn);
    exBtns->addStretch();
    exCard->addLayout(exBtns);
    m_resetButtons.append(addBtn);
    m_resetButtons.append(rmBtn);

    v->addStretch();   // slack sits below the cards as panel background, not in the cards

    updateHotkeyStatus();
    return page;
}

void PreferencesDialog::addIgnoreApps()
{
    // A little menu: pick a currently-running app, or browse for any .exe.
    QMenu menu(this);
    QAction* fromRunning = menu.addAction(
        I18n::instance().t(QStringLiteral("prefs.control.exclusions.running")));
    QAction* browse = menu.addAction(
        I18n::instance().t(QStringLiteral("prefs.control.exclusions.browse")));
    QAction* chosen = menu.exec(QCursor::pos());
    if (chosen == fromRunning) {
        const QList<platform::RunningApp> apps = platform::runningApplications();
        QStringList labels;
        for (const platform::RunningApp& a : apps)
            labels << (a.title.isEmpty() ? a.exe : a.exe + QStringLiteral("  —  ") + a.title);
        if (labels.isEmpty())
            return;
        bool ok = false;
        const QString pick = QInputDialog::getItem(
            this, I18n::instance().t(QStringLiteral("prefs.control.exclusions.title")),
            I18n::instance().t(QStringLiteral("prefs.control.exclusions.running")),
            labels, 0, false, &ok);
        if (ok && !pick.isEmpty())
            appendIgnoreApp(apps[labels.indexOf(pick)].exe);
    } else if (chosen == browse) {
        const QString path = QFileDialog::getOpenFileName(
            this, I18n::instance().t(QStringLiteral("prefs.control.exclusions.browse")),
            QString(), QStringLiteral("Applications (*.exe)"));
        if (!path.isEmpty())
            appendIgnoreApp(QFileInfo(path).fileName().toLower());
    }
}

void PreferencesDialog::appendIgnoreApp(const QString& exe)
{
    const QString name = exe.trimmed().toLower();
    if (name.isEmpty())
        return;
    for (int i = 0; i < m_ignoreAppsList->count(); ++i)   // dedupe (case-insensitive)
        if (m_ignoreAppsList->item(i)->text().compare(name, Qt::CaseInsensitive) == 0)
            return;
    m_ignoreAppsList->addItem(name);
    persistIgnoreApps();
}

void PreferencesDialog::removeSelectedIgnoreApp()
{
    delete m_ignoreAppsList->takeItem(m_ignoreAppsList->currentRow());
    persistIgnoreApps();
}

void PreferencesDialog::persistIgnoreApps()
{
    QStringList apps;
    for (int i = 0; i < m_ignoreAppsList->count(); ++i)
        apps << m_ignoreAppsList->item(i)->text();
    m_settings->setValue(QStringLiteral("control/ignoreApps"), apps);
}

// Builds one "label + description | [hotkey box][status] " row and records it.
QWidget* PreferencesDialog::buildHotkeyRow(QVBoxLayout* card, const HotkeyRow& proto,
                                           const QString& labelKey, bool divider, int section)
{
    HotkeyRow row = proto;
    row.edit = new HotkeyEdit;
    // Fixed, not minimum: a flexible box would soak up the extra width on a row that
    // carries another control (Custom snip's Configure), leaving that row's box wider
    // and further right than the rest.
    row.edit->setFixedWidth(200);
    const QString cur = m_settings->value(QStringLiteral("hotkey/") + row.name).toString();
    row.edit->setKeySequence(QKeySequence(cur.isEmpty() ? row.defaultSeq : cur));
    connect(row.edit, &HotkeyEdit::keySequenceChanged, this, &PreferencesDialog::applyHotkeys);

    row.status = new QLabel;
    row.status->setFixedWidth(20);
    row.status->setAlignment(Qt::AlignCenter);

    auto* wrap = new QWidget;
    auto* h = new QHBoxLayout(wrap);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(8);
    // Leading stretch pins the box+status to the row's right edge and absorbs any
    // slack (addSettingRow floors the wrap at 240px), so every box lands on the same
    // column. Extra controls are inserted at index 0, i.e. left of this stretch.
    h->addStretch(1);
    h->addWidget(row.edit);
    h->addWidget(row.status);

    QString descKey = labelKey;
    descKey.replace(QStringLiteral("prefs.hotkey."), QStringLiteral("prefs.desc.hotkey."));

    // Capture the row container (and its trailing divider) addSettingRow appends, so
    // the search filter can show/hide them individually.
    const int preCount = card->count();
    addSettingRow(card, labelKey, descKey, wrap, divider);
    QWidget* rowWidget = card->itemAt(preCount)->widget();
    QFrame*  dividerW  = divider ? qobject_cast<QFrame*>(card->itemAt(preCount + 1)->widget())
                                 : nullptr;

    m_hotkeys.append(row);
    m_controlRows.append({rowWidget, dividerW, labelKey, descKey, row.edit, section});
    return wrap;
}

// The Quick-snip row: label + description in the left column (inline, like every other
// row), with a right-aligned block holding the Copy/Pin checkboxes stacked and the
// "[hotkey] + mouse drag ✓" group centered against them. Registers with the same tables
// as buildHotkeyRow so validation, filtering, hotkey-status and retranslation all work.
void PreferencesDialog::buildQuickSnipRow(QVBoxLayout* card, const HotkeyRow& proto,
                                          const QString& labelKey, bool divider, int section)
{
    HotkeyRow row = proto;
    row.edit = new HotkeyEdit;
    row.edit->setFixedWidth(150);              // narrower — the row groups extra controls
    // Quick snip captures a modifier-only combo ("Ctrl+Alt") that arms the hold-and-drag
    // gesture. It drives its own code path via snip/quickSnip/trigger, NOT the keymap/hotkey
    // override system, so this row is kept out of m_hotkeys (no applyHotkeys / probe / conflict).
    row.edit->setModifierOnly(true);
    m_quickSnipEdit = row.edit;
    row.edit->setTriggerString(m_settings->value(QStringLiteral("snip/quickSnip/trigger")).toString());
    connect(row.edit, &HotkeyEdit::keySequenceChanged, this, [this] {
        m_settings->setValue(QStringLiteral("snip/quickSnip/trigger"),
                             m_quickSnipEdit->triggerString());
    });

    row.status = new QLabel;
    row.status->setFixedWidth(20);
    row.status->setAlignment(Qt::AlignCenter);

    // Extra-action checkboxes (Copy above Pin).
    auto* copyBox = new QCheckBox;
    copyBox->setChecked(m_settings->value(QStringLiteral("snip/quickSnip/copy"), true).toBool());
    m_buttonI18n.append({copyBox, QStringLiteral("prefs.hotkey.quick_snip_copy")});
    auto* pinBox = new QCheckBox;
    pinBox->setChecked(m_settings->value(QStringLiteral("snip/quickSnip/pin"), false).toBool());
    m_buttonI18n.append({pinBox, QStringLiteral("prefs.hotkey.quick_snip_pin")});
    m_quickCopyBox = copyBox;
    m_quickPinBox = pinBox;
    connect(copyBox, &QCheckBox::toggled, this, [this](bool on) {
        m_settings->setValue(QStringLiteral("snip/quickSnip/copy"), on);
        if (!on) enforceQuickSnipAction(m_quickCopyBox);
    });
    connect(pinBox, &QCheckBox::toggled, this, [this](bool on) {
        m_settings->setValue(QStringLiteral("snip/quickSnip/pin"), on);
        if (!on) enforceQuickSnipAction(m_quickPinBox);
    });
    // Binding the hotkey while neither box is checked is just as invalid as unchecking
    // the last box — guard both by re-enabling Copy.
    connect(row.edit, &HotkeyEdit::keySequenceChanged, this,
            [this] { enforceQuickSnipAction(m_quickCopyBox); });

    // "[hotkey] + mouse drag  ✓" group. The "+" and hint are primary-text (not muted).
    auto* plus = new QLabel(QStringLiteral("+"));
    plus->setObjectName(QStringLiteral("SettingDescStrong"));
    auto* dragHint = new QLabel;
    dragHint->setObjectName(QStringLiteral("SettingDescStrong"));
    m_labels.append({dragHint, QStringLiteral("prefs.hotkey.quick_snip_drag")});
    auto* hk = new QWidget;
    auto* hkl = new QHBoxLayout(hk);
    hkl->setContentsMargins(0, 0, 0, 0);
    hkl->setSpacing(8);
    hkl->addWidget(row.edit);
    hkl->addWidget(plus);
    hkl->addWidget(dragHint);
    hkl->addWidget(row.status);

    // Checkbox column (Copy above Pin). A real widget so the page's height-for-width
    // counts its full sizeHint — a bare nested layout reports a too-small minimum and
    // the column gets crushed below its text height.
    auto* boxCol = new QWidget;
    auto* bcl = new QVBoxLayout(boxCol);
    bcl->setContentsMargins(0, 0, 0, 0);
    bcl->setSpacing(6);
    bcl->addWidget(copyBox);
    bcl->addWidget(pinBox);

    // Right-aligned controls block: checkbox column, then the hotkey group centered
    // vertically against it (the taller of the two sets the row height).
    auto* controlsW = new QWidget;
    auto* controls = new QHBoxLayout(controlsW);
    controls->setContentsMargins(0, 0, 0, 0);
    controls->setSpacing(24);
    controls->addStretch(1);
    controls->addWidget(boxCol);
    controls->addWidget(hk, 0, Qt::AlignVCenter);

    QString descKey = labelKey;
    descKey.replace(QStringLiteral("prefs.hotkey."), QStringLiteral("prefs.desc.hotkey."));

    // Same inline shape as addSettingRow: label + description in a left column, with
    // the controls block on the right of the same row.
    auto* rowWidget = new QWidget;
    auto* h = new QHBoxLayout(rowWidget);
    h->setContentsMargins(0, 12, 0, 12);
    h->setSpacing(16);
    auto* left = new QVBoxLayout;
    left->setSpacing(2);
    auto* lbl = new QLabel;
    lbl->setObjectName(QStringLiteral("SettingLabel"));
    m_labels.append({lbl, labelKey});
    left->addWidget(lbl);
    auto* desc = new QLabel;
    desc->setObjectName(QStringLiteral("SettingDesc"));
    desc->setWordWrap(true);
    m_labels.append({desc, descKey});
    left->addWidget(desc);
    h->addLayout(left, 1);
    h->addWidget(controlsW, 0, Qt::AlignVCenter);

    card->addWidget(rowWidget);
    QFrame* dividerW = nullptr;
    if (divider) {
        addRowDivider(card);
        dividerW = qobject_cast<QFrame*>(card->itemAt(card->count() - 1)->widget());
    }

    // NOT appended to m_hotkeys (see above) — only tracked as a Control row for
    // search-filtering and live retranslation of its label/description.
    m_controlRows.append({rowWidget, dividerW, labelKey, descKey, row.edit, section});
}

void PreferencesDialog::enforceQuickSnipAction(QCheckBox* preferBox)
{
    if (!m_quickCopyBox || !m_quickPinBox || !m_quickSnipEdit || !preferBox)
        return;
    // Only enforced while the feature is armed: an empty trigger means Quick snip is off,
    // so both boxes off is fine. Bound + both off is the invalid state we block.
    if (m_quickSnipEdit->triggerString().isEmpty())
        return;
    if (m_quickCopyBox->isChecked() || m_quickPinBox->isChecked())
        return;
    QMessageBox::warning(this, windowTitle(),
        I18n::instance().t(QStringLiteral("prefs.hotkey.quick_snip_need_action")));
    const QSignalBlocker block(preferBox);   // re-check without re-entering the toggled slot
    preferBox->setChecked(true);
    m_settings->setValue(preferBox == m_quickCopyBox ? QStringLiteral("snip/quickSnip/copy")
                                                     : QStringLiteral("snip/quickSnip/pin"),
                         true);
}

// Show only the Control rows whose label, description, or current shortcut contains
// the query. Empty query = show everything. Dividers are re-derived from what stays
// visible, and a section with no visible rows is hidden whole (the empty Tools
// section shows only when there's no active query).
void PreferencesDialog::filterControlRows(const QString& text)
{
    auto& tr = I18n::instance();
    const QString q = text.trimmed();
    const int nSections = m_controlSections.size();

    QVector<int>  visible(nSections, 0);
    QVector<bool> hasRows(nSections, false);

    // Pass 1: per-row visibility.
    for (const ControlRowRef& r : m_controlRows) {
        hasRows[r.section] = true;
        bool match = q.isEmpty();
        if (!match) {
            const QString hay = tr.t(r.labelKey) + QLatin1Char(' ') + tr.t(r.descKey)
                              + QLatin1Char(' ')
                              + r.edit->keySequence().toString(QKeySequence::NativeText);
            match = hay.contains(q, Qt::CaseInsensitive);
        }
        r.row->setVisible(match);
        if (match)
            ++visible[r.section];
    }

    // Pass 2: a row's trailing divider shows only if a later row in the same section
    // is still visible (rows are stored section-contiguous, in display order).
    for (int i = 0; i < m_controlRows.size(); ++i) {
        const ControlRowRef& r = m_controlRows[i];
        if (!r.divider)
            continue;
        bool laterVisible = false;
        for (int j = i + 1; j < m_controlRows.size() && m_controlRows[j].section == r.section; ++j) {
            if (m_controlRows[j].row->isVisible()) { laterVisible = true; break; }
        }
        r.divider->setVisible(r.row->isVisible() && laterVisible);
    }

    // Pass 3: section containers.
    int totalVisible = 0;
    for (int s = 0; s < nSections; ++s) {
        const bool show = hasRows[s] ? (visible[s] > 0) : q.isEmpty();
        m_controlSections[s]->setVisible(show);
        totalVisible += visible[s];
    }

    // The Key Bindings / Restore Defaults bar belongs to the hotkey list: keep it
    // when nothing's filtered, but hide it once a search leaves no hotkey rows (it
    // reads as stray floating buttons otherwise).
    if (m_hotkeyButtonBar)
        m_hotkeyButtonBar->setVisible(q.isEmpty() || totalVisible > 0);

    // App exclusions: matched on its title + hint only, never on the listed app
    // names (an entry like "chrome.exe" must not keep the block visible).
    if (m_exclusionsSection) {
        bool show = q.isEmpty();
        if (!show) {
            const QString hay = tr.t(QStringLiteral("prefs.control.exclusions.title"))
                              + QLatin1Char(' ')
                              + tr.t(QStringLiteral("prefs.control.exclusions.hint"));
            show = hay.contains(q, Qt::CaseInsensitive);
        }
        m_exclusionsSection->setVisible(show);
    }

    adjustStackHeightForCurrentPage();
}

void PreferencesDialog::updateHotkeyStatus()
{
    auto& tr = I18n::instance();
    for (const HotkeyRow& r : m_hotkeys) {
        if (!r.status) continue;
        const QString seq = r.edit->keySequence().toString(QKeySequence::PortableText);

        // Empty binding = intentionally unbound → no validator badge.
        if (seq.isEmpty()) {
            r.status->clear();
            r.status->setToolTip(QString());
            continue;
        }

        bool ok = true;
        QString tip = tr.t(QStringLiteral("prefs.hotkey.status_ok"));
        if (!conflictFor(seq, r.name).isEmpty()) {        // clashes with another row
            ok = false;
            tip = tr.t(QStringLiteral("prefs.hotkey.status_conflict"));
        } else if (m_probe && !m_probe->probe(seq)) {     // owned by another app
            ok = false;
            tip = tr.t(QStringLiteral("prefs.hotkey.status_taken"));
        }
        r.status->setPixmap(makeStatusPixmap(ok));
        r.status->setToolTip(tip);
    }
}

void PreferencesDialog::showEvent(QShowEvent* e)
{
    QDialog::showEvent(e);
    // Re-sync the Snip controls from settings: this dialog is a reused singleton, so a
    // value changed elsewhere while it was hidden (e.g. the ` / ! cursor-capture toggle
    // during a snip persists snip/captureCursor) must show up when it's reopened.
    refreshSnipControls();
    // By now the app has released its own global hotkeys (see
    // Application::showPreferences), so the OS probe reflects *other* apps only.
    updateHotkeyStatus();
    // Now that the dialog has its real width, redo the content height calculation —
    // the first call (during construction) had to guess the width for word-wrapped
    // labels, which threw off the height (and thus the gap before the bottom buttons).
    adjustStackHeightForCurrentPage();
}

void PreferencesDialog::resizeEvent(QResizeEvent* e)
{
    QDialog::resizeEvent(e);
    // The viewport height changed, so re-clamp the page height to keep it filling the
    // panel (see adjustStackHeightForCurrentPage). m_stack exists after construction.
    if (m_stack)
        adjustStackHeightForCurrentPage();
}

void PreferencesDialog::openKeyBindings()
{
    KeyBindingsDialog dlg(this);
    dlg.exec();
}

void PreferencesDialog::openCustomSnip()
{
    CustomSnipDialog dlg(m_settings, this);
    dlg.exec();
}

void PreferencesDialog::restoreControlDefaults()
{
    const auto answer = QMessageBox::question(
        this, windowTitle(),
        I18n::instance().t(QStringLiteral("prefs.restore_page_confirm")),
        QMessageBox::Yes | QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    for (HotkeyRow& r : m_hotkeys) {
        const QSignalBlocker block(r.edit);
        r.edit->setKeySequence(QKeySequence(r.defaultSeq));
    }
    applyHotkeys();
    if (m_controlSearch)
        filterControlRows(m_controlSearch->text());   // shortcut matches may have changed
}

QWidget* PreferencesDialog::buildUpdateTab()
{
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(24, 20, 24, 20);
    addPageHeader(v, QStringLiteral("prefs.tab.update"), QStringLiteral("prefs.subtitle.update"));
    auto* card = beginCard(v);

    auto* lbl = new QLabel;
    lbl->setObjectName(QStringLiteral("SettingDesc"));
    lbl->setWordWrap(true);
    lbl->setContentsMargins(0, 12, 0, 12);
    m_labels.append({lbl, QStringLiteral("prefs.update_hint")});
    card->addWidget(lbl);

    v->addStretch();
    return page;
}

// Where the license files live: beside the exe in the portable tree (CMake copies
// them next to SnapCap.exe post-build, and the packaged ZIP ships them at top level).
// They carry a .txt extension precisely so this opens them in the default text
// viewer -- extensionless LICENSE files make Windows raise its "how do you want to
// open this?" picker instead.
static void openLicenseFile(const QString& fileName)
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(
        QDir(QCoreApplication::applicationDirPath()).filePath(fileName)));
}

QWidget* PreferencesDialog::buildAboutTab()
{
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(24, 20, 24, 20);
    addPageHeader(v, QStringLiteral("prefs.tab.about"), QStringLiteral("prefs.subtitle.about"));

    // --- identity ----------------------------------------------------------
    auto* idCard = beginCard(v);

    auto* tagline = new QLabel;
    tagline->setObjectName(QStringLiteral("SettingDesc"));
    tagline->setWordWrap(true);
    tagline->setContentsMargins(0, 12, 0, 0);
    m_labels.append({tagline, QStringLiteral("prefs.about_text")});
    idCard->addWidget(tagline);

    // Version + Qt version carry {placeholders}, so retranslate() can't drive them
    // from m_labels (which is a plain t(key) loop) -- updateAboutLabels() does.
    m_aboutVersion = new QLabel;
    m_aboutVersion->setObjectName(QStringLiteral("SettingDesc"));
    m_aboutVersion->setContentsMargins(0, 0, 0, 12);
    idCard->addWidget(m_aboutVersion);

    // --- Qt / LGPLv3 -------------------------------------------------------
    auto* qtCard = beginCard(v);

    m_aboutQt = new QLabel;
    m_aboutQt->setObjectName(QStringLiteral("SettingDesc"));
    m_aboutQt->setWordWrap(true);
    m_aboutQt->setContentsMargins(0, 12, 0, 0);
    qtCard->addWidget(m_aboutQt);

    auto* relink = new QLabel;
    relink->setObjectName(QStringLiteral("SettingDesc"));
    relink->setWordWrap(true);
    relink->setContentsMargins(0, 0, 0, 12);
    m_labels.append({relink, QStringLiteral("about.qt_relink")});
    qtCard->addWidget(relink);

    addRowDivider(qtCard);

    // LGPLv3 s4(d)(0): the Corresponding Source is conveyed alongside the binary,
    // so this points at the release page that carries both the ZIP and the archive.
    auto* srcBtn = new QPushButton;
    srcBtn->setProperty("i18nKey", QStringLiteral("about.qt_source_button"));
    m_resetButtons.append(srcBtn);
    connect(srcBtn, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral(SNAPCAP_QT_SOURCE_URL)));
    });
    addSettingRow(qtCard, QStringLiteral("about.qt_source"),
                  QStringLiteral("about.qt_source_desc"), srcBtn, true);

    // FFmpeg carries its own LGPL corresponding-source obligation, separate from Qt's:
    // the recorder and the preview player link it, and SnapCap builds and ships it
    // rather than relying on a system copy. Only shown when the backend is actually
    // compiled in — a build without recording conveys no FFmpeg to account for.
#ifdef SNAPCAP_HAVE_FFMPEG
    auto* ffBtn = new QPushButton;
    ffBtn->setCursor(Qt::PointingHandCursor);
    ffBtn->setProperty("i18nKey", QStringLiteral("about.ffmpeg_source_button"));
    m_resetButtons.append(ffBtn);
    connect(ffBtn, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral(SNAPCAP_FFMPEG_SOURCE_URL)));
    });
    addSettingRow(qtCard, QStringLiteral("about.ffmpeg_source"),
                  QStringLiteral("about.ffmpeg_source_desc"), ffBtn, false);
#endif

    // --- licenses & notices ------------------------------------------------
    // One button per shipped document, opening it directly in the text viewer.
    auto* licCard = beginCard(v);

    auto* licRow = new QWidget;
    auto* licBtns = new QHBoxLayout(licRow);
    licBtns->setContentsMargins(0, 0, 0, 0);
    licBtns->setSpacing(8);
    const std::pair<const char*, const char*> kDocs[] = {
        {"about.license_lgpl",    "LICENSE.LGPLv3.txt"},
        {"about.license_gpl",     "LICENSE.GPLv3.txt"},
        {"about.license_notices", "THIRD_PARTY_NOTICES.txt"},
    };
    for (const auto& [key, file] : kDocs) {
        auto* btn = new QPushButton;
        btn->setProperty("i18nKey", QString::fromLatin1(key));
        m_resetButtons.append(btn);
        const QString fileName = QString::fromLatin1(file);
        connect(btn, &QPushButton::clicked, this, [fileName] { openLicenseFile(fileName); });
        licBtns->addWidget(btn);
    }
    addSettingRow(licCard, QStringLiteral("about.licenses"),
                  QStringLiteral("about.licenses_desc"), licRow, false);

    v->addStretch();
    return page;
}

// Version/Qt lines: {placeholder} interpolation, so they are refreshed here rather
// than by retranslate()'s plain m_labels loop. qVersion() (runtime, not QT_VERSION_STR)
// is the honest number -- the Qt DLLs beside the exe are user-replaceable.
void PreferencesDialog::updateAboutLabels()
{
    auto& tr = I18n::instance();
    if (m_aboutVersion)
        m_aboutVersion->setText(tr.t(QStringLiteral("about.version"),
                                     {{QStringLiteral("version"), QStringLiteral(SNAPCAP_VERSION)}}));
    if (m_aboutQt)
        m_aboutQt->setText(tr.t(QStringLiteral("about.uses_qt"),
                                {{QStringLiteral("qt"), QString::fromLatin1(qVersion())}}));
}

// --- hotkeys ----------------------------------------------------------------

QString PreferencesDialog::conflictFor(const QString& seq, const QString& exceptName) const
{
    if (seq.isEmpty())
        return QString();
    for (const HotkeyRow& r : m_hotkeys) {
        if (r.name == exceptName)
            continue;
        if (r.edit->keySequence().toString(QKeySequence::PortableText) == seq)
            return r.name;
    }
    return QString();
}

void PreferencesDialog::applyHotkeys()
{
    QString conflictMsg;
    for (const HotkeyRow& r : m_hotkeys) {
        const QString seq = r.edit->keySequence().toString(QKeySequence::PortableText);
        if (!seq.isEmpty() && !conflictFor(seq, r.name).isEmpty()) {
            conflictMsg = I18n::instance().t(QStringLiteral("prefs.hotkey.conflict"),
                                             {{QStringLiteral("seq"), seq}});
            continue;   // don't persist a conflicting binding
        }
        m_settings->setValue(QStringLiteral("hotkey/") + r.name, seq);
        if (m_actions)
            if (auto id = ActionRegistry::actionIdFromName(r.name))
                m_actions->bindKey(*id, seq, KeyContext::Global);
    }
    if (m_hotkeyConflict)
        m_hotkeyConflict->setText(conflictMsg);
    updateHotkeyStatus();
    emit hotkeysChanged();
}

// --- restore defaults -------------------------------------------------------

// General page: reflect the actual OS autostart state on the two checkboxes
// (real state, not just config — the entry may have been changed externally).
void PreferencesDialog::refreshAutostartControls()
{
    if (!m_runStartup || !m_asAdmin) return;
    const QSignalBlocker b1(m_runStartup);
    const QSignalBlocker b2(m_asAdmin);
    if (!m_autostart) {                 // no backend (non-Windows) → leave disabled
        m_runStartup->setEnabled(false);
        m_asAdmin->setEnabled(false);
        return;
    }
    const bool enabled  = m_autostart->isEnabled();
    const bool elevated = m_autostart->isElevated();
    m_runStartup->setEnabled(true);
    m_runStartup->setChecked(enabled);
    m_asAdmin->setChecked(elevated);
    m_asAdmin->setEnabled(enabled);     // admin only meaningful while startup is on
}

// General page: persist both checkboxes and reconcile the OS entry. When the
// elevated form needs administrator rights we don't have, offer a UAC relaunch
// (which also satisfies "relaunch now"); the elevated instance recreates the
// Scheduled Task on startup. The two backing forms (HKCU Run value vs. a
// /rl-highest logon Scheduled Task) are described in platform/win/WinAutostart
// and its row in claude-plan/Architecture-FileMap.md.
void PreferencesDialog::applyAutostart()
{
    if (!m_autostart) return;
    const bool enabled  = m_runStartup->isChecked();
    const bool elevated = enabled && m_asAdmin->isChecked();

    m_settings->setValue(QStringLiteral("general/runOnStartup"), enabled);
    m_settings->setValue(QStringLiteral("general/startupElevated"), elevated);

    if (m_autostart->apply(enabled, elevated))
        return;

    // apply() failed, which always means the Scheduled Task form needs rights we
    // do not have — to *create* it, or equally to *remove* it when autostart is
    // switched off or dropped back to the Run-key form. Offer the relaunch for
    // every one of those; the elevated instance finishes the job from its startup
    // reconcile in Application. Offering it only for the create case is what used
    // to strand a task with no way to remove it from inside the app (G2).
    if (!m_autostart->currentProcessElevated()) {
        const bool removing = !elevated;        // switching admin (or autostart) off
        const auto btn = QMessageBox::question(
            this, windowTitle(),
            I18n::instance().t(removing ? QStringLiteral("prefs.admin_relaunch_remove_confirm")
                                        : QStringLiteral("prefs.admin_relaunch_confirm")),
            QMessageBox::Yes | QMessageBox::No);
        if (btn == QMessageBox::Yes && m_autostart->relaunchElevated()) {
            qApp->quit();                       // hand off to the elevated instance
            return;
        }
        if (elevated) {
            // Declined / launch failed → fall back to the non-elevated form.
            const QSignalBlocker b(m_asAdmin);
            m_asAdmin->setChecked(false);
            m_settings->setValue(QStringLiteral("general/startupElevated"), false);
            m_autostart->apply(enabled, false);
            return;
        }
        // Declined a removal → the OS is unchanged, so put the settings back in
        // step with it. Leaving config claiming autostart is off while a task
        // still launches SnapCap at logon is the desync that hid G2 for weeks.
        m_settings->setValue(QStringLiteral("general/runOnStartup"), m_autostart->isEnabled());
        m_settings->setValue(QStringLiteral("general/startupElevated"), m_autostart->isElevated());
        refreshAutostartControls();
        return;
    }

    // Already elevated and still failing → not something a relaunch can fix.
    QMessageBox::information(this, windowTitle(),
                             I18n::instance().t(QStringLiteral("prefs.admin_needs_elevation")));
    refreshAutostartControls();
}

void PreferencesDialog::restoreDefaults()
{
    const auto answer = QMessageBox::question(
        this, windowTitle(),
        I18n::instance().t(QStringLiteral("prefs.restore_confirm")),
        QMessageBox::Yes | QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    m_settings->resetToDefaults();   // clears all keys + re-seeds (keeps file path)
    refreshControls();

    // Re-apply hotkeys from their factory defaults and re-register globally.
    for (HotkeyRow& r : m_hotkeys) {
        const QString cur = m_settings->value(QStringLiteral("hotkey/") + r.name).toString();
        const QSignalBlocker block(r.edit);
        r.edit->setKeySequence(QKeySequence(cur.isEmpty() ? r.defaultSeq : cur));
    }
    applyHotkeys();

    // Locale may have reset to default — re-apply so the whole UI follows.
    I18n::instance().setLocale(m_settings->value(QStringLiteral("general/locale"),
                                                 QStringLiteral("en")).toString());
}

void PreferencesDialog::refreshControls()
{
    const auto setCombo = [](QComboBox* c, const QString& data) {
        if (!c) return; const QSignalBlocker b(c);
        const int i = c->findData(data); if (i >= 0) c->setCurrentIndex(i);
    };

    setCombo(m_langCombo, m_settings->value(QStringLiteral("general/locale"), QStringLiteral("en")).toString());
    setCombo(m_logLevel,  m_settings->value(QStringLiteral("log/level"), QStringLiteral("normal")).toString());
    setCombo(m_gpuAccel,  m_settings->value(QStringLiteral("general/gpu_acceleration"),
                                            QStringLiteral("auto")).toString());
    if (m_configPathEdit) m_configPathEdit->setText(m_settings->filePath());
    if (m_logPathEdit)    m_logPathEdit->setText(m_settings->logFilePath());
    Logging::setLevel(Logging::levelFromString(   // keep the live logger in sync (e.g. after Restore Defaults)
        m_settings->value(QStringLiteral("log/level"), QStringLiteral("normal")).toString()));
    refreshAutostartControls();
    refreshSnipControls();
    refreshPasteControls();
    refreshOutputControls();
    refreshInterfaceControls();
}

// --- output page: page-scoped restore + refresh -----------------------------

void PreferencesDialog::updateOutputPreviews()
{
    const QDateTime now = QDateTime::currentDateTime();
    // Show the OS-native path separator (backslash on Windows) in the previews.
    auto preview = [&now](const QString& t) {
        return QDir::toNativeSeparators(FilenameTemplate::expand(t, now));
    };
    if (m_manualPreview && m_manualFileName)
        m_manualPreview->setText(preview(m_manualFileName->text()));
    if (m_quickSavePreview && m_quickSavePath)
        m_quickSavePreview->setText(preview(m_quickSavePath->text()));
    if (m_autoSavePreview && m_autoSavePath)
        m_autoSavePreview->setText(preview(m_autoSavePath->text()));
    // The recording preview goes through resolveOutputPath so it shows the extension
    // the chosen format will actually force, not whatever the template happens to say.
    if (m_recPathPreview && m_recPath && m_recContainer) {
        const recording::Container c =
            recording::containerFromKey(m_recContainer->currentData().toString());
        m_recPathPreview->setText(QDir::toNativeSeparators(
            recording::resolveOutputPath(m_recPath->text(), c, now)));
    }
}

void PreferencesDialog::openNamingRules()
{
    NamingRulesDialog(this).exec();
}

void PreferencesDialog::restoreOutputDefaults()
{
    const auto answer = QMessageBox::question(
        this, windowTitle(),
        I18n::instance().t(QStringLiteral("prefs.restore_page_confirm")),
        QMessageBox::Yes | QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    // Reset ONLY the output/* keys — this page's settings, nothing else.
    m_settings->setValue(QStringLiteral("output/imageQuality"), -1);
    m_settings->setValue(QStringLiteral("output/manualSave/fileName"), defaultFileNameTemplate());
    m_settings->setValue(QStringLiteral("output/manualSave/rememberExtension"), true);
    m_settings->setValue(QStringLiteral("output/auto_alpha_channel_removal"), true);
    m_settings->setValue(QStringLiteral("output/pdf/pageSize"), QStringLiteral("dpi96"));
    m_settings->setValue(QStringLiteral("output/quickSave/showNotification"), true);
    m_settings->setValue(QStringLiteral("output/quickSave/pathTemplate"), defaultQuickSavePath());
    m_settings->setValue(QStringLiteral("output/autoSave/enabled"), false);
    m_settings->setValue(QStringLiteral("output/autoSave/pathTemplate"), defaultAutoSavePath());
    m_settings->setValue(QStringLiteral("output/clipboard/dibScreenshot"), true);
    m_settings->setValue(QStringLiteral("output/clipboard/dibImageWindows"), false);
    m_settings->setValue(QStringLiteral("output/clipboard/copyAsFile"), QStringLiteral("disable"));

    refreshOutputControls();
}

void PreferencesDialog::refreshOutputControls()
{
    if (m_imageQuality) {
        const QSignalBlocker b(m_imageQuality);
        m_imageQuality->setValue(m_settings->value(QStringLiteral("output/imageQuality"), -1).toInt());
    }
    if (m_manualFileName) {
        const QSignalBlocker b(m_manualFileName);
        m_manualFileName->setText(m_settings->value(QStringLiteral("output/manualSave/fileName"),
                                                    defaultFileNameTemplate()).toString());
    }
    if (m_rememberExt) {
        const QSignalBlocker b(m_rememberExt);
        m_rememberExt->setChecked(m_settings->value(QStringLiteral("output/manualSave/rememberExtension"), true).toBool());
    }
    if (m_autoAlpha) {
        const QSignalBlocker b(m_autoAlpha);
        m_autoAlpha->setChecked(m_settings->value(QStringLiteral("output/auto_alpha_channel_removal"), true).toBool());
    }
    const QString pdfPage = m_settings->value(QStringLiteral("output/pdf/pageSize"),
                                              QStringLiteral("dpi96")).toString();
    for (const auto& [rb, mode] : m_pdfPageRadios) {
        const QSignalBlocker b(rb);
        rb->setChecked(mode == pdfPage);
    }
    if (m_quickSaveNotify) {
        const QSignalBlocker b(m_quickSaveNotify);
        m_quickSaveNotify->setChecked(m_settings->value(QStringLiteral("output/quickSave/showNotification"), true).toBool());
    }
    if (m_quickSavePath) {
        const QSignalBlocker b(m_quickSavePath);
        m_quickSavePath->setText(m_settings->value(QStringLiteral("output/quickSave/pathTemplate"),
                                                   defaultQuickSavePath()).toString());
    }
    const bool autoOn = m_settings->value(QStringLiteral("output/autoSave/enabled"), false).toBool();
    if (m_autoSaveEnabled) {
        const QSignalBlocker b(m_autoSaveEnabled);
        m_autoSaveEnabled->setChecked(autoOn);
    }
    if (m_autoSaveBody)
        m_autoSaveBody->setEnabled(autoOn);
    if (m_autoSavePath) {
        const QSignalBlocker b(m_autoSavePath);
        m_autoSavePath->setText(m_settings->value(QStringLiteral("output/autoSave/pathTemplate"),
                                                  defaultAutoSavePath()).toString());
    }
    if (m_clipDibScreenshot) {
        const QSignalBlocker b(m_clipDibScreenshot);
        m_clipDibScreenshot->setChecked(m_settings->value(QStringLiteral("output/clipboard/dibScreenshot"), true).toBool());
    }
    if (m_clipDibImageWindows) {
        const QSignalBlocker b(m_clipDibImageWindows);
        m_clipDibImageWindows->setChecked(m_settings->value(QStringLiteral("output/clipboard/dibImageWindows"), false).toBool());
    }
    const QString caf = m_settings->value(QStringLiteral("output/clipboard/copyAsFile"),
                                          QStringLiteral("disable")).toString();
    for (const auto& [rb, mode] : m_copyAsFileRadios) {
        const QSignalBlocker b(rb);
        rb->setChecked(mode == caf);
    }
    updateOutputPreviews();
}

// --- interface page: page-scoped restore + refresh --------------------------

void PreferencesDialog::restoreInterfaceDefaults()
{
    const auto answer = QMessageBox::question(
        this, windowTitle(),
        I18n::instance().t(QStringLiteral("prefs.restore_page_confirm")),
        QMessageBox::Yes | QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    // Reset ONLY the interface/* keys — this page's settings, nothing else.
    m_settings->setValue(QStringLiteral("interface/font"), QString());
    m_settings->setValue(QStringLiteral("interface/theme"), QStringLiteral("auto"));
    m_settings->setValue(QStringLiteral("interface/accentColor"), QStringLiteral("#2d7dff"));
    m_settings->setValue(QStringLiteral("interface/trayIconMode"), QStringLiteral("color"));
    m_settings->setValue(QStringLiteral("interface/trayIconColor"), QStringLiteral("#2d7dff"));
    m_settings->setValue(QStringLiteral("interface/magnifier/formats"), QStringLiteral("rgb,hex"));
    m_settings->setValue(QStringLiteral("interface/magnifier/selectedFormat"), QString());
    m_settings->setValue(QStringLiteral("interface/magnifier/noHash"), false);
    m_settings->setValue(QStringLiteral("interface/magnifier/hexUppercase"), false);
    m_settings->setValue(QStringLiteral("interface/magnifier/usageTips"), true);
    m_settings->setValue(QStringLiteral("interface/magnifier/gridLines"), true);
    m_settings->setValue(QStringLiteral("interface/magnifier/coordinates"), true);
    m_settings->setValue(QStringLiteral("interface/magnifier/sizePercent"), 100);

    refreshInterfaceControls();
}

void PreferencesDialog::refreshInterfaceControls()
{
    updateFontButton();
    updateAccentButton();

    if (m_themeMode) {
        const QSignalBlocker b(m_themeMode);
        const int idx = m_themeMode->findData(
            m_settings->value(QStringLiteral("interface/theme"), QStringLiteral("auto")).toString());
        m_themeMode->setCurrentIndex(idx >= 0 ? idx : m_themeMode->findData(QStringLiteral("auto")));
    }

    QString trayMode = m_settings->value(QStringLiteral("interface/trayIconMode"),
                                         QStringLiteral("color")).toString();
    if (trayMode != QStringLiteral("custom"))
        trayMode = QStringLiteral("color");
    for (const auto& [rb, mode] : m_trayRadios) {
        const QSignalBlocker b(rb);
        rb->setChecked(mode == trayMode);
    }
    if (m_trayCustomBtn)
        setSwatchIcon(m_trayCustomBtn,
                      pasteColor(QStringLiteral("interface/trayIconColor"), QColor(0x2d, 0x7d, 0xff)));

    const QStringList on = m_settings->value(QStringLiteral("interface/magnifier/formats"),
                                             QStringLiteral("rgb,hex"))
                               .toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const auto& [cb, code] : m_colorFormats) {
        const QSignalBlocker b(cb);
        cb->setChecked(on.contains(code));
    }

    const auto setCheck = [this](QCheckBox* c, const QString& key, bool def) {
        if (!c) return; const QSignalBlocker b(c);
        c->setChecked(m_settings->value(key, def).toBool());
    };
    setCheck(m_magNoHash,    QStringLiteral("interface/magnifier/noHash"), false);
    setCheck(m_magHexUpper,  QStringLiteral("interface/magnifier/hexUppercase"), false);
    setCheck(m_magUsageTips, QStringLiteral("interface/magnifier/usageTips"), true);
    setCheck(m_magGrid,      QStringLiteral("interface/magnifier/gridLines"), true);
    setCheck(m_magCoords,    QStringLiteral("interface/magnifier/coordinates"), true);
    if (m_magSize) {
        const QSignalBlocker b(m_magSize);
        m_magSize->setValue(m_settings->value(QStringLiteral("interface/magnifier/sizePercent"),
                                              100).toInt());
    }
}

// --- retranslate ------------------------------------------------------------

void PreferencesDialog::retranslate()
{
    // Recording combos: their items are translated text with a stable data key, so
    // they need explicit re-labelling (the generic label/button loops can't see them).
    if (m_recRateMode && m_recRateMode->count() >= 2) {
        m_recRateMode->setItemText(0, I18n::instance().t(QStringLiteral("prefs.recording.rate_quality")));
        m_recRateMode->setItemText(1, I18n::instance().t(QStringLiteral("prefs.recording.rate_bitrate")));
    }
    if (m_recHw && m_recHw->count() >= 3) {
        m_recHw->setItemText(0, I18n::instance().t(QStringLiteral("prefs.recording.hw_auto")));
        m_recHw->setItemText(1, I18n::instance().t(QStringLiteral("prefs.recording.hw_hardware")));
        m_recHw->setItemText(2, I18n::instance().t(QStringLiteral("prefs.recording.hw_software")));
    }
    updateRecordingEnablement();     // the "will use ..." line is interpolated text
    auto& tr = I18n::instance();
    setWindowTitle(tr.t(QStringLiteral("preferences.title")));

    for (const auto& [item, key] : m_sidebarItems)
        if (item) item->setText(tr.t(key));
    for (const auto& [lbl, key] : m_labels)
        if (lbl) lbl->setText(tr.t(key));
    for (const auto& [btn, key] : m_buttonI18n)
        if (btn) btn->setText(tr.t(key));
    for (QPushButton* b : m_resetButtons)
        b->setText(tr.t(b->property("i18nKey").toString()));

    // Font button text is dynamic (family/size); theme button carries a t()-label.
    updateAccentButton();
    updatePasteFontButton();
    updateAboutLabels();
    if (m_ocrDefaultLang) refreshOcrControls();   // OCR button/combo texts are state-driven

    for (const HotkeyRow& r : m_hotkeys)
        if (r.edit)
            r.edit->setPlaceholderText(tr.t(QStringLiteral("prefs.hotkey.press_shortcut")));
    updateHotkeyStatus();
    if (m_controlSearch) {
        m_controlSearch->setPlaceholderText(tr.t(QStringLiteral("prefs.control.search")));
        filterControlRows(m_controlSearch->text());   // labels changed → re-evaluate matches
    }

    if (m_logLevel) {                 // retranslate the level item texts (build order)
        const QSignalBlocker block(m_logLevel);
        m_logLevel->setItemText(0, tr.t(QStringLiteral("prefs.log.off")));
        m_logLevel->setItemText(1, tr.t(QStringLiteral("prefs.log.errors")));
        m_logLevel->setItemText(2, tr.t(QStringLiteral("prefs.log.normal")));
        m_logLevel->setItemText(3, tr.t(QStringLiteral("prefs.log.verbose")));
    }

    if (m_gpuAccel) {                 // same: item texts are built, not bound
        const QSignalBlocker block(m_gpuAccel);
        m_gpuAccel->setItemText(0, tr.t(QStringLiteral("prefs.gpu.auto")));
        m_gpuAccel->setItemText(1, tr.t(QStringLiteral("prefs.gpu.cpu")));
    }

    if (m_themeMode) {                // same
        const QSignalBlocker block(m_themeMode);
        m_themeMode->setItemText(0, tr.t(QStringLiteral("prefs.iface.theme_opt.auto")));
        m_themeMode->setItemText(1, tr.t(QStringLiteral("prefs.iface.theme_opt.light")));
        m_themeMode->setItemText(2, tr.t(QStringLiteral("prefs.iface.theme_opt.dark")));
    }

    if (m_langCombo) {
        const QSignalBlocker block(m_langCombo);
        const int i = m_langCombo->findData(tr.locale());
        if (i >= 0) m_langCombo->setCurrentIndex(i);
    }
    const auto setActionCombo = [&tr](QComboBox* c) {
        if (!c) return;
        c->setItemText(0, tr.t(QStringLiteral("prefs.snip.action.copy")));
        c->setItemText(1, tr.t(QStringLiteral("prefs.snip.action.quicksave")));
        c->setItemText(2, tr.t(QStringLiteral("prefs.snip.action.save")));
        c->setItemText(3, tr.t(QStringLiteral("prefs.snip.action.pin")));
        c->setItemText(4, tr.t(QStringLiteral("prefs.snip.action.nothing")));
    };
    setActionCombo(m_snipDoubleCombo);
    setActionCombo(m_snipMiddleCombo);
    setActionCombo(m_snipEnterCombo);

    updateOutputPreviews();   // filename previews are data-driven, not in m_labels
}
