#pragma once

#include <QDialog>
#include <QColor>
#include <QVector>
#include <QPair>
#include <QString>

class Settings;
class QMouseEvent;
class QSpinBox;
class QLineEdit;
class QLabel;
class QPushButton;

// File-local custom widgets (defined in the .cpp).
class ColorField;        // hue (x) x saturation (y) square
class ColorValueBar;     // vertical value/brightness bar
class ColorPreview;      // current-color swatch (alpha-aware)
class ScreenPickOverlay; // frozen full-desktop eyedropper surface

// SnapCap's own color selector — a custom replacement for QColorDialog so every
// string flows through our YAML i18n system (the native dialog uses Qt's own
// translation catalog, which we can't drive). Reproduces the classic layout:
// basic-colors grid, custom-colors grid (persisted in Settings), an HSV square +
// value bar, HSV/RGB/alpha spin boxes, an HTML hex field, and a live cross-cursor
// screen eyedropper. Built standalone for future use (annotation color boards,
// pin tint, …); see claude-plan/Architecture-FileMap.md.
class ColorDialog : public QDialog {
    Q_OBJECT
public:
    ColorDialog(const QColor& initial, Settings* settings, QWidget* parent = nullptr);

    QColor selectedColor() const;

    // Convenience mirroring QColorDialog::getColor — returns an invalid QColor
    // when the user cancels.
    static QColor getColor(const QColor& initial, Settings* settings, QWidget* parent = nullptr);

protected:
    void mousePressEvent(QMouseEvent* e) override;   // click blank space = clear focus

private:
    // Which control originated a change (so we don't fight the HTML caret).
    enum class Src { External, Field, ValueBar, Hsv, Rgb, Alpha, Html, Swatch, Pick };

    void buildUi();
    QWidget* buildBasicGrid();
    QWidget* buildCustomGrid();
    QWidget* buildEditors();        // preview + spin boxes + html + buttons

    void setFromColor(const QColor& c, Src src);     // adopt an external/RGB color
    void syncControls(Src src);                      // push state into every widget
    QColor color() const;                            // current HSVA -> QColor
    void retranslate();

    // Custom-colors persistence.
    void loadCustomColors();
    void saveCustomColors();
    void addCurrentToCustom();
    void refreshSwatchSelection();

    // Eyedropper: a frozen full-virtual-desktop overlay captures input over every
    // window (a plain grabMouse can't track moves over other apps on Windows).
    void startPick();
    void finishPick(const QColor& c, bool commit);

    Settings* m_settings = nullptr;
    QColor m_accent;                // UI accent (interface/accentColor); selected-swatch ring

    // Authoritative state: HSV + alpha (kept as ints so dragging value to 0 never
    // loses the hue, unlike a round-trip through RGB).
    int m_h = 0, m_s = 0, m_v = 0, m_a = 255;

    bool   m_picking = false;
    QColor m_prePick;               // restored on Esc
    ScreenPickOverlay* m_overlay = nullptr;

    ColorField*    m_field   = nullptr;
    ColorValueBar* m_valueBar= nullptr;
    ColorPreview*  m_preview = nullptr;

    QSpinBox *m_hue = nullptr, *m_sat = nullptr, *m_valv = nullptr;
    QSpinBox *m_red = nullptr, *m_green = nullptr, *m_blue = nullptr, *m_alpha = nullptr;
    QLineEdit* m_html = nullptr;

    QPushButton* m_pickBtn = nullptr;
    QPushButton* m_addBtn  = nullptr;
    QPushButton* m_okBtn   = nullptr;
    QPushButton* m_cancelBtn = nullptr;
    QLabel* m_basicLabel  = nullptr;
    QLabel* m_customLabel = nullptr;
    QLabel* m_cursorLabel = nullptr;   // "Cursor: x, y" under the pick button (pick mode)
    QLabel* m_hintLabel   = nullptr;   // "Press Esc to cancel" (pick mode)

    // All clickable swatches (basic + custom) with their color, for click + the
    // selection ring.
    QVector<QPair<QPushButton*, QColor>> m_swatches;

    // Custom-color slots (16), invalid color = empty.
    QVector<QPushButton*> m_customSlots;
    QVector<QColor>       m_customColors;
    int m_customWrap = 0;              // next slot to overwrite when full

    // Retranslatable labels: widget -> i18n key.
    QVector<QPair<QLabel*, QString>> m_labels;
};
