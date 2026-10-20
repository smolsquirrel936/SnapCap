#pragma once

#include <QLineEdit>
#include <QKeySequence>

class QAction;

// A single-shortcut capture field styled for the Preferences "Control" page.
//
// Looks like a read-only, centered text box showing the current shortcut (or a
// "Press shortcut" placeholder when empty). Clicking it gives focus — the QSS
// theme paints a theme-color underline to hint "capturing now"; the next full
// combo (modifier(s) + a non-modifier key) is captured and shown. A trailing
// "X" action clears the binding (useful to leave a function unbound). Esc while
// capturing cancels and restores the previous value.
//
// Exposes keySequence()/setKeySequence() mirroring QKeySequenceEdit so the
// dialog's existing apply/conflict logic reuses it unchanged; setKeySequence()
// is silent (no signal), user capture/clear emits keySequenceChanged().
class HotkeyEdit : public QLineEdit {
    Q_OBJECT
public:
    explicit HotkeyEdit(QWidget* parent = nullptr);

    QKeySequence keySequence() const { return m_seq; }
    void setKeySequence(const QKeySequence& seq);   // silent (programmatic)

    // Modifier-only mode (Quick snip): capture ONLY a modifier combo (e.g. "Ctrl+Alt",
    // committed when all modifiers are released). A non-modifier key is ignored — a full
    // hotkey can't be entered, so what you see is exactly what arms the hold-drag gesture.
    // Off by default so every other row keeps full-combo capture.
    void setModifierOnly(bool on) { m_modifierOnly = on; }

    // Portable modifier-only trigger string ("Ctrl+Alt"); empty when unbound. Used by the
    // Quick snip Preferences row. setTriggerString() is silent (no signal).
    QString triggerString() const;
    void setTriggerString(const QString& s);

signals:
    void keySequenceChanged();   // user captured a new combo or cleared it

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;
    void focusInEvent(QFocusEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;

private:
    void refreshText();          // repaint the box from the current value (native text)
    void commit(const QKeySequence& seq);           // full combo: set + emit if changed
    void commitMods(Qt::KeyboardModifiers mods);    // modifier-only: set + emit if changed

    QKeySequence m_seq;
    Qt::KeyboardModifiers m_committedMods = Qt::NoModifier;  // set iff value is modifier-only
    Qt::KeyboardModifiers m_pendingMods = Qt::NoModifier;    // peak modifier set this capture
    Qt::KeyboardModifiers m_heldMods = Qt::NoModifier;       // modifiers currently still down
    bool m_modifierOnly = false;
    bool m_committedThisFocus = false;              // a combo already fired this focus
    QAction*     m_clearAction = nullptr;
};
