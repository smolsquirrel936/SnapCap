#pragma once

#include <QDialog>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QColor>
#include <QFont>

class Settings;
class ActionRegistry;
class HotkeyEdit;
namespace platform { class IGlobalHotkey; }
namespace platform { class IAutostart; }
class QComboBox;
class QCheckBox;
class QRadioButton;
class QButtonGroup;
class QAbstractButton;
class QSpinBox;
class QSlider;
class QDoubleSpinBox;
class QLineEdit;
class QKeySequenceEdit;
class QLabel;
class QWidget;
class QFrame;
class QPushButton;
class QVBoxLayout;
class QListWidget;
class QListWidgetItem;
class QStackedWidget;
class QNetworkAccessManager;

// The real Preferences dialog (replaces the Stage 1 placeholder). Categories:
// General, Interface, Snip, Paste, Output, Control (hotkeys), Update, About.
//
// Visual shell (Stage 6 redesign): a branding header + a left sidebar of
// categories + an optional sub-category column + a stacked content area of
// rich "label + description | control" rows inside cards, themed by
// res/preferences.qss. The sub-category column is wired generically but stays
// hidden until a category declares more than one sub-category.
//
// Reads/writes Settings live; the Language dropdown drives I18n::setLocale so the
// whole UI (including this dialog) retranslates live. Hotkey capture widgets edit
// the global bindings with reset-to-default + conflict detection; changes emit
// hotkeysChanged() so the app re-registers the global hotkeys.
class PreferencesDialog : public QDialog {
    Q_OBJECT
public:
    PreferencesDialog(Settings* settings, ActionRegistry* actions,
                      platform::IGlobalHotkey* probe = nullptr,
                      platform::IAutostart* autostart = nullptr, QWidget* parent = nullptr);

signals:
    void hotkeysChanged();   // app re-registers global hotkeys after a binding edits
    void clearHistoryRequested();   // Snip page "Clear history" → app wipes the History ring

protected:
    void showEvent(QShowEvent* e) override;   // re-probe hotkey status once shown
    void resizeEvent(QResizeEvent* e) override;   // keep the page filling the viewport

private:
    // One captured global hotkey row.
    struct HotkeyRow {
        QString name;            // settings key suffix ("snip", "snipCopy", …)
        QString defaultSeq;      // factory default (mirrors res/keymap.default.yaml)
        HotkeyEdit* edit = nullptr;
        QLabel* status = nullptr;   // green check / red cross validator
    };

    // One sidebar category. Future-proofs the sub-category column: a category with
    // more than one sub-category surfaces m_subList; otherwise the column is hidden
    // and selecting the category jumps straight to its single page.
    struct Category {
        QStringList subKeys;       // sub-category labels; size<=1 ⇒ column hidden
        QList<int>  stackIndices;  // page index in m_stack per sub-category
    };

    QWidget* buildGeneralTab();
    QWidget* buildInterfaceTab();
    QWidget* buildSnipTab();
    QWidget* buildPasteTab();
    QWidget* buildOutputTab();
    QWidget* buildOcrTab();
    QWidget* buildRecordingTab();
    QWidget* buildControlTab();
    QWidget* buildUpdateTab();
    QWidget* buildAboutTab();
    void     updateAboutLabels();

    void retranslate();              // re-apply every label/sidebar title from i18n keys
    void applyHotkeys();             // write overrides + emit hotkeysChanged()
    QString conflictFor(const QString& seq, const QString& exceptName) const;
    void updateHotkeyStatus();       // recompute each row's validator icon
    void restoreControlDefaults();   // reset ONLY hotkey/* keys to factory defaults
    // Quick-snip needs at least one extra action (Copy/Pin) while its hotkey is bound;
    // if that's violated, warn and re-check preferBox so one action always stays on.
    void enforceQuickSnipAction(QCheckBox* preferBox);
    void openKeyBindings();          // pop up the Key Bindings reference dialog
    void openCustomSnip();           // pop up the Custom snip area/delay settings dialog
    QWidget* buildHotkeyRow(QVBoxLayout* card, const HotkeyRow& proto,
                            const QString& labelKey, bool divider, int section);
    // Quick snip's bespoke row: full-width description on top, Copy/Pin + the
    // hold-and-drag hotkey group below (see buildControlTab).
    void buildQuickSnipRow(QVBoxLayout* card, const HotkeyRow& proto,
                           const QString& labelKey, bool divider, int section);
    void filterControlRows(const QString& text);   // Control search: show only rows whose
                                                   // label/description/shortcut matches

    // Control page "App exclusions": apps whose foreground state mutes our hotkeys.
    void addIgnoreApps();            // Add button: pick from running apps or browse for an .exe
    void appendIgnoreApp(const QString& exe);   // add one exe basename (deduped), persist, relist
    void removeSelectedIgnoreApp();  // drop the selected row, persist
    void persistIgnoreApps();        // write the list widget back to control/ignoreApps

    void restoreDefaults();          // reset all settings + refresh visible controls
    void refreshControls();          // re-pull every control's value from Settings

    // General-tab autostart: persist both checkboxes + reconcile the OS entry,
    // prompting a UAC relaunch when the elevated form needs administrator rights.
    void applyAutostart();
    void refreshAutostartControls(); // re-pull the two checkboxes from the OS state

    void onCategorySelected(int row);     // sidebar → show sub-cats / switch page
    void onSubCategorySelected(int row);  // sub-category column → switch page
    void adjustStackHeightForCurrentPage();   // size the scrollable content to the visible page
    void applyFontScaledSize();               // window size scaled by the interface font

    // Control widths are authored against the 10 pt design font and re-scaled by
    // fontScale() so a bigger interface font widens the controls with the text.
    // setScaledWidth() records the widget and applies at once; applyScaledWidths()
    // re-applies the whole set after a font change (widgets are never rebuilt).
    double fontScale() const;                 // current interface pt / 10 (the design pt)
    void setScaledWidth(QWidget* w, int minPx, int maxPx = 0);   // maxPx <= 0 ⇒ uncapped
    void applyScaledWidths();

    // Shell builders.
    void loadStyleSheet();                              // apply res/preferences.qss
    QWidget* buildHeader();                             // branding band
    void addCategory(QWidget* page, const QString& titleKey, const QStringList& subKeys = {});

    // Page/row builders for the rich "label+description | control" layout.
    void addPageHeader(QVBoxLayout* page, const QString& titleKey, const QString& subtitleKey);
    // Subcategory heading rendered inline on a page (not a nav item) — see Interface.
    void addSectionHeader(QVBoxLayout* page, const QString& titleKey, const QString& descKey = QString());
    QVBoxLayout* beginCard(QVBoxLayout* page);          // returns the card's inner layout
    void addRowDivider(QVBoxLayout* card);
    QWidget* addSettingRow(QVBoxLayout* card, const QString& labelKey,
                           const QString& descKey, QWidget* control, bool divider = true,
                           bool stretchControl = true, bool valueTakesRow = false);
    QCheckBox* addCheckRow(QVBoxLayout* card, const QString& key, bool def,
                           const QString& labelKey, const QString& descKey, bool divider = true);

    QString pickFolder(const QString& current);

    // Interface tab (General + Magnifier subcategories).
    QWidget* buildColorFormatsControl();   // magnifier "Color values" checkbox grid
    QWidget* buildTrayIconControl();       // Auto / Colored / Custom radios
    void saveColorFormats();               // persist checked magnifier formats
    QFont  currentIfaceFont() const;       // stored interface font, else app default
    QColor currentAccentColor() const;      // stored UI accent, else brand blue
    void updateFontButton();               // reflect current font on the button
    void updateAccentButton();              // reflect current accent swatch on the button
    void restoreInterfaceDefaults();       // reset ONLY interface/* keys
    void refreshInterfaceControls();       // re-pull interface widgets from Settings

    // Output tab helpers.
    void restoreOutputDefaults();          // reset ONLY output/* keys
    void refreshOutputControls();          // re-pull output widgets from Settings
    void updateOutputPreviews();           // recompute filename previews from templates
    void openNamingRules();                // pop up the "$...$" syntax help dialog

    // Paste tab helpers.
    void restorePasteDefaults();           // reset ONLY paste/* keys
    void refreshPasteControls();           // re-pull paste widgets from Settings
    void updatePastePreview();             // re-render the Conversion "AaOo012" preview
    void updatePasteFontButton();          // reflect the stored conversion font on the button
    QFont  currentPasteFont() const;       // stored conversion font, else Segoe UI 11
    QColor pasteColor(const QString& key, const QColor& def) const;   // read a stored paste color

    // Snip tab helpers (Display / Behavior / Keys inline subcategories).
    void restoreSnipDefaults();            // reset ONLY snip/* keys
    void refreshSnipControls();            // re-pull snip widgets from Settings

    // Recording tab helpers.
    void restoreRecordingDefaults();       // reset ONLY recording/* keys
    void refreshRecordingControls();       // re-pull recording widgets from Settings
    // Container/codec are not independent: WebM takes only VP9/AV1, and GIF has no
    // codec and no quality/bitrate/encoder knobs at all. The two cases are handled
    // DIFFERENTLY, and the difference is deliberate: GIF **hides** the rows that
    // stop applying (a GIF has no such setting to reason about), while WebM **greys
    // the individual items** inside the codec dropdown (the setting still exists,
    // that value just won't mux). Rate mode likewise shows one of Quality/Target
    // bitrate and hides the other. Also refreshes the "will use" encoder line.
    void updateRecordingEnablement();
    // There is ONE Frame rate row for all formats; it retargets to the key the selected
    // format actually encodes from — recording/video/fps, or recording/gif/fps for GIF,
    // which never reads video/fps. Picking a format resets that key to its default (60 fps
    // for video, 30 for GIF), deliberately overwriting a custom value; the user can set it
    // again afterwards.
    bool recContainerIsGif() const;
    void applyFrameRateRange(int value);   // range for the current format, then the value
    void applyFormatFrameRate();

    // OCR tab helpers.
    QString ocrTessdataDir() const;        // <exe>/tessdata (matches the backend)
    QStringList installedOcrLanguages() const;  // *.traineddata codes on disk
    void refreshOcrControls();             // re-pull the default combo + per-language buttons
    void startOcrDownload(const QString& code);  // fetch <code>.traineddata into tessdata/
    void removeOcrLanguage(const QString& code); // delete the pack (with confirm)

    Settings* m_settings = nullptr;
    ActionRegistry* m_actions = nullptr;
    platform::IGlobalHotkey* m_probe = nullptr;   // OS-level "is this combo free?" test
    platform::IAutostart* m_autostart = nullptr;  // OS autostart/elevation (null ⇒ boxes disabled)

    // Shell.
    QListWidget*    m_sidebar = nullptr;
    QListWidget*    m_subList = nullptr;
    QStackedWidget* m_stack = nullptr;
    QVector<Category> m_categories;
    int m_currentCategory = 0;

    // Design-px widths re-applied on every interface-font change (see setScaledWidth).
    struct ScaledWidth { QWidget* w = nullptr; int minPx = 0; int maxPx = 0; };
    QVector<ScaledWidth> m_scaledWidths;

    QComboBox*  m_langCombo = nullptr;
    QLineEdit*  m_configPathEdit = nullptr;
    QLineEdit*  m_logPathEdit = nullptr;

    // General-tab deferred placeholders (text retranslated directly).
    QCheckBox*  m_runStartup = nullptr;
    QCheckBox*  m_asAdmin = nullptr;
    QComboBox*  m_logLevel = nullptr;
    QComboBox*  m_gpuAccel = nullptr;      // general/gpu_acceleration (auto|cpu) — blur only
    QPushButton* m_restoreBtn = nullptr;
    QVector<HotkeyRow> m_hotkeys;
    QVector<QPushButton*> m_resetButtons;
    QCheckBox* m_quickCopyBox = nullptr;   // Quick-snip row: "Copy" extra action
    QCheckBox* m_quickPinBox = nullptr;    // Quick-snip row: "Pin" extra action
    HotkeyEdit* m_quickSnipEdit = nullptr; // Quick-snip row's capture box (for validation)

    // Control page: hotkey rows grouped into Core/View/Tools/Misc sections, with a
    // live search box. Each ref carries the handles the filter toggles.
    struct ControlRowRef {
        QWidget* row = nullptr;      // the settingRow container (hidden when filtered out)
        QFrame*  divider = nullptr;  // its trailing divider, if any
        QString  labelKey;
        QString  descKey;
        HotkeyEdit* edit = nullptr;  // for the current shortcut text
        int section = 0;             // index into m_controlSections
    };
    QLineEdit* m_controlSearch = nullptr;
    QVector<ControlRowRef> m_controlRows;
    QVector<QWidget*> m_controlSections;   // Core/View/Tools/Misc container widgets
    QWidget* m_hotkeyButtonBar = nullptr;      // Key Bindings / Restore Defaults bar (hidden on no-match)
    QWidget* m_exclusionsSection = nullptr;    // App-exclusions block (search hides it by title/hint)
    QListWidget* m_ignoreAppsList = nullptr;   // App-exclusions list (exe basename per row)

    // Interface-tab controls kept for refresh + page-scoped Restore Defaults.
    QPushButton* m_ifaceFontBtn = nullptr;
    QComboBox*  m_themeMode = nullptr;      // interface/theme (auto|light|dark)
    QPushButton* m_accentColorBtn = nullptr;
    QButtonGroup* m_trayGroup = nullptr;
    QVector<QPair<QRadioButton*, QString>> m_trayRadios;    // radio → mode ("color"/"custom")
    QPushButton* m_trayCustomBtn = nullptr;                 // custom tray-icon colour swatch
    QVector<QPair<QCheckBox*, QString>> m_colorFormats;     // checkbox → format code
    QCheckBox*  m_magNoHash = nullptr;
    QCheckBox*  m_magHexUpper = nullptr;
    QCheckBox*  m_magUsageTips = nullptr;
    QCheckBox*  m_magGrid = nullptr;
    QCheckBox*  m_magCoords = nullptr;
    QSpinBox*   m_magSize = nullptr;            // loupe size, percent (50..200)
    QPushButton* m_ifaceRestoreBtn = nullptr;

    // Snip controls kept for refresh + page-scoped Restore Defaults. Every control is
    // persist-only (deferred) except the detect checkboxes, which derive snip/detectMode.
    QCheckBox*   m_captureCursor = nullptr;
    QCheckBox*   m_snipAutoWindows = nullptr;    // with m_snipAutoElements derives snip/detectMode
    QCheckBox*   m_snipAutoElements = nullptr;
    QPushButton* m_snipMaskBtn = nullptr;        // Display mask-color swatch
    QPushButton* m_snipAnchorBorderBtn = nullptr; // Display anchor-border-color swatch
    QPushButton* m_snipAnchorBtn = nullptr;      // Display anchor-color swatch
    QComboBox*   m_snipDoubleCombo = nullptr;    // Keys → Actions click-action dropdowns
    QComboBox*   m_snipMiddleCombo = nullptr;
    QComboBox*   m_snipEnterCombo = nullptr;

    // Paste tab (Display / Behavior / Conversion inline subcategories). Every control
    // is persist-only (deferred) except the Conversion live preview. Generic checkbox
    // and spinbox rows carry their key+default so restore/refresh can loop them.
    struct PasteCheck { QCheckBox* cb; QString key; bool def; };
    struct PasteSpin  { QSpinBox*  sp; QString key; int  def; };
    QVector<PasteCheck> m_pasteChecks;
    QVector<PasteSpin>  m_pasteSpins;
    QVector<PasteCheck> m_snipChecks;   // reuse the {cb,key,def} shape for Snip persist rows
    QVector<PasteSpin>  m_snipSpins;
    QCheckBox*   m_pasteTextToImage = nullptr;   // Conversion master → gates m_pasteConvBody
    QWidget*     m_pasteConvBody = nullptr;
    QPushButton* m_pasteShadowBtn = nullptr;     // Display shadow-color swatch
    QPushButton* m_pasteFontBtn = nullptr;       // Conversion font button
    QPushButton* m_pasteBgBtn = nullptr;         // Conversion background-color swatch
    QPushButton* m_pasteTextBtn = nullptr;       // Conversion text-color swatch
    QLabel*      m_pastePreview = nullptr;        // live text→image preview
    QPushButton* m_pasteRestoreBtn = nullptr;
    QCheckBox*  m_autoAlpha = nullptr;
    // Output tab (File + Clipboard subcategories).
    QSpinBox*   m_imageQuality = nullptr;
    QLineEdit*  m_manualFileName = nullptr;
    QLabel*     m_manualPreview = nullptr;
    QCheckBox*  m_rememberExt = nullptr;
    QCheckBox*  m_quickSaveNotify = nullptr;
    QLineEdit*  m_quickSavePath = nullptr;
    QLabel*     m_quickSavePreview = nullptr;
    QCheckBox*  m_autoSaveEnabled = nullptr;
    QLineEdit*  m_autoSavePath = nullptr;
    QLabel*     m_autoSavePreview = nullptr;
    QWidget*    m_autoSaveBody = nullptr;                   // path/preview/buttons gated by the enable box
    QButtonGroup* m_pdfPageGroup = nullptr;
    QVector<QPair<QRadioButton*, QString>> m_pdfPageRadios;  // radio → output/pdf/pageSize value
    QCheckBox*  m_clipDibScreenshot = nullptr;
    QCheckBox*  m_clipDibImageWindows = nullptr;

    // Recording tab (Video / Audio / Behavior / GIF / Output).
    QComboBox*   m_recContainer = nullptr;
    QComboBox*   m_recCodec = nullptr;
    QLabel*      m_recEncoderInfo = nullptr;    // "will use: h264_nvenc (hardware)"
    QSpinBox*    m_recFps = nullptr;
    QComboBox*   m_recRateMode = nullptr;
    QSlider*     m_recQuality = nullptr;
    QLabel*      m_recQualityValue = nullptr;
    QSpinBox*    m_recBitrate = nullptr;
    QComboBox*   m_recHw = nullptr;
    QCheckBox*   m_recSystemAudio = nullptr;
    QCheckBox*   m_recMic = nullptr;
    QComboBox*   m_recAudioBitrate = nullptr;
    QSpinBox*    m_recCountdown = nullptr;
    QCheckBox*   m_recCursor = nullptr;
    QSpinBox*    m_recMaxMinutes = nullptr;
    QSpinBox*    m_recGifWidth = nullptr;
    QCheckBox*   m_recGifLoop = nullptr;
    QCheckBox*   m_recOpenPreview = nullptr;
    QLineEdit*   m_recPath = nullptr;
    QLabel*      m_recPathPreview = nullptr;
    QPushButton* m_recRestoreBtn = nullptr;
    // Alternatives selected by the rate mode: exactly one is SHOWN, the other hidden
    // (see updateRecordingEnablement — nothing on this page greys a whole row). Each is
    // a group that owns its leading divider (same idiom as the format groups below).
    QWidget*     m_recQualityRow = nullptr;
    QWidget*     m_recBitrateRow = nullptr;
    // Groups in the Video card that are shown per format (see updateRecordingEnablement):
    // the video-encoder rows for MP4/MKV/WebM, the GIF rows for GIF. Each owns its own
    // LEADING divider, so hiding one never strands a line or stacks two.
    QWidget*     m_recCodecRows = nullptr;
    QWidget*     m_recEncodeRows = nullptr;
    QWidget*     m_recGifRows = nullptr;   // GIF-only rows in the Video card; shown iff GIF
    // The whole Audio section — its section HEADER as well as its card, which are two
    // separate additions to the page layout and so need a container to hide as one.
    // Hidden for GIF, which carries no audio stream (WinRecorder skips openAudio()).
    QWidget*     m_recAudioSection = nullptr;

    // OCR tab (Languages). Each manifest/installed language gets an action button whose
    // label flips Download → Downloading… → Installed/Remove as the folder state changes.
    QComboBox* m_ocrDefaultLang = nullptr;
    QNetworkAccessManager* m_ocrNet = nullptr;
    struct OcrLangRow { QString code; QPushButton* action = nullptr; QLabel* status = nullptr; };
    QVector<OcrLangRow> m_ocrRows;
    QButtonGroup* m_copyAsFileGroup = nullptr;
    QVector<QPair<QRadioButton*, QString>> m_copyAsFileRadios;  // radio → mode ("always"…)

    // Retranslatable registries: widget → i18n key. m_labels now also holds page
    // titles/subtitles and per-setting descriptions (all are QLabel ← t(key)).
    QVector<QPair<QLabel*, QString>> m_labels;
    QVector<QPair<QListWidgetItem*, QString>> m_sidebarItems;  // sidebar item → title key
    QVector<QPair<QAbstractButton*, QString>> m_buttonI18n;    // radios/checkboxes with own text
    QLabel* m_hotkeyConflict = nullptr;

    // About tab: the two lines that interpolate {placeholders} (app + Qt version),
    // so they need updateAboutLabels() rather than retranslate()'s plain t(key) loop.
    QLabel* m_aboutVersion = nullptr;
    QLabel* m_aboutQt = nullptr;
};
