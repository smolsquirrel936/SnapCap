#pragma once

#include <QColor>
#include <QStringList>
#include <QWidget>

class QPushButton;
class QComboBox;

// The 2nd-level OCR toolbar, shown while OCR mode is active (mirrors PaintBar's
// sub-bar look). Controls: Select all · Copy · Language ▾ · Line-break toggle.
// Pure presentation — the host controller (SnipOverlay / PinEditController) applies
// the signals to the shared OcrTextLayer and the clipboard.
class OcrBar : public QWidget {
    Q_OBJECT
public:
    explicit OcrBar(QWidget* parent = nullptr);

    void retranslate();
    void setAccent(const QColor& c);       // UI accent for hover/checked chrome

    // Best default OCR language for the current UI locale intersected with what is
    // installed: the locale's Tesseract code if present, else "eng", else the first
    // installed code (or "eng" when nothing is installed yet). Shared by both hosts.
    static QString defaultLanguage(const QStringList& installed);

    // Friendly, translated name for a Tesseract code (`ocr.lang.<code>`), falling back
    // to the raw code for languages with no translation (e.g. manual drop-ins).
    static QString displayName(const QString& code);

    // Populate the language dropdown from installed traineddata codes; `current` is
    // pre-selected. Emits nothing.
    void setLanguages(const QStringList& codes, const QString& current);
    QString currentLanguage() const;

    bool keepLineBreaks() const;

signals:
    void selectAllRequested();
    void copyRequested();
    void keepLineBreaksChanged(bool keep);
    void languageChanged(const QString& code);

private:
    QString barQss() const;

    QPushButton* m_selectAll = nullptr;
    QPushButton* m_copy = nullptr;
    QPushButton* m_lineBreak = nullptr;    // checkable: keep line breaks vs. join with spaces
    QComboBox*   m_lang = nullptr;
    QColor       m_accent;
};
