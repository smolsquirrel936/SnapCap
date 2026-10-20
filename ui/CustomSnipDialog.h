#pragma once

#include <QDialog>

class QSpinBox;
class QDoubleSpinBox;
class Settings;

// Settings window for the "Custom snip" global hotkey — opened by the Configure
// button on that Preferences → Control row. It holds exactly the position, the size,
// and the delay.
//
// This is a *persisted* config rather than a per-capture prompt:
// the values live under snip/customSnip/* and the hotkey applies them straight away
// (see Application's CustomSnip action). Position/size are **capture px** — the real
// pixels of the exported image — so "800x600" yields an 800x600 file on a 150%-scaled
// monitor just as it does on a 100% one. They are handed to SnipOverlay::setPresetArea
// unconverted.
class CustomSnipDialog : public QDialog {
    Q_OBJECT
public:
    explicit CustomSnipDialog(Settings* settings, QWidget* parent = nullptr);

private:
    void save();

    Settings* m_settings = nullptr;
    QSpinBox* m_x = nullptr;
    QSpinBox* m_y = nullptr;
    QSpinBox* m_width = nullptr;
    QSpinBox* m_height = nullptr;
    QDoubleSpinBox* m_delay = nullptr;
};
