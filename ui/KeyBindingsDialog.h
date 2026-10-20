#pragma once

#include <QDialog>

class QTableWidget;

// Read-only reference of every SnapCap shortcut, grouped Snip only / Paste only /
// Common — the popup opened by the "Key Bindings" button on the Control page.
// Mirrors Features.md §4 (the authoritative binding table). The shortcut column
// is input notation kept literal; the command column is i18n prose. A "Copy"
// button dumps the whole table as tab-separated text to the clipboard.
class KeyBindingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit KeyBindingsDialog(QWidget* parent = nullptr);

private:
    void populate();
    void copyToClipboard() const;

    QTableWidget* m_table = nullptr;
};
