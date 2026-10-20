#pragma once

#include <QDialog>

class QTableWidget;

// Read-only reference of the filename-template naming rules — the popup opened by
// the "Naming Rules" button on the Output page. Same visual grammar as
// KeyBindingsDialog (two-column HoverTable, bold spanned section headings,
// full-row hover outline, Copy button, Preferences QSS theme).
//
// Every token listed here is really expanded by core/FilenameTemplate: the
// "$...$" date/time tokens, the "%...%" environment variables (%os%,
// %computername%, %username%, and any other system env var), and the "special
// variables" %title% / %title:50% — the latter via the specials map that
// SnipOverlay::filenameSpecials() fills from the foreground window title, which
// Application captures *before* the overlay steals focus.
class NamingRulesDialog : public QDialog {
    Q_OBJECT
public:
    explicit NamingRulesDialog(QWidget* parent = nullptr);

private:
    void populate();
    void copyToClipboard() const;

    QTableWidget* m_table = nullptr;
};
