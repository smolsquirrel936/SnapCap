#pragma once

#include <QDialog>
#include <QFont>

class QListWidget;
class QMouseEvent;
class QSpinBox;
class QLabel;

// Modal font picker that lists each family under its *native* name — 微软雅黑,
// 游ゴシック, 맑은 고딕 — i.e. the name in the script the font is designed for,
// instead of QFontDialog's English-only labels (which is all Windows reports on a
// non-CJK OS). Family / Style / Size columns + a live preview; returns a QFont.
// Drop-in replacement for QFontDialog::getFont at the Preferences font buttons.
class FontPickerDialog : public QDialog {
    Q_OBJECT
public:
    // `maxSize` caps the size column. The default is the full range; callers whose font
    // drives a layout (the interface font, which scales the whole UI) pass a lower cap.
    explicit FontPickerDialog(const QFont& initial, QWidget* parent = nullptr,
                              int maxSize = 96);

    QFont selectedFont() const;

    // Mirrors QFontDialog::getFont(&ok, initial, parent): runs modally, sets *ok
    // to whether the user accepted, returns the chosen font (or `initial`).
    static QFont getFont(bool* ok, const QFont& initial, QWidget* parent = nullptr,
                         int maxSize = 96);

    // Native display name for a family, or the family itself when it has no CJK
    // name (Latin fonts). Cached; also used for the font-button labels.
    static QString displayName(const QString& family);

protected:
    void mousePressEvent(QMouseEvent* e) override;   // click blank space = clear focus

private:
    void populateFamilies(const QString& selectFamily);
    void reloadStyles(const QString& selectStyle);
    void updatePreview();
    QString currentFamily() const;
    QString currentStyle() const;

    QListWidget* m_families = nullptr;
    QListWidget* m_styles = nullptr;
    QSpinBox* m_size = nullptr;
    QLabel* m_preview = nullptr;
};
