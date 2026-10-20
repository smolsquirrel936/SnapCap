#include "ui/HotkeyEdit.h"

#include <QAction>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QMouseEvent>
#include <QKeyCombination>
#include <QPainter>
#include <QPixmap>
#include <QIcon>

#include "core/i18n/I18n.h"
#include "core/action/KeySequenceCodec.h"

namespace {
// Small grey circle with a white "x" — the inline clear button, drawn so no
// image asset is needed (matches the app's other programmatic icons).
QIcon makeClearIcon()
{
    QPixmap pm(16, 16);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x98, 0xa2, 0xb3));
    p.drawEllipse(QRectF(0, 0, 16, 16));
    QPen pen(Qt::white);
    pen.setWidthF(1.6);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.drawLine(QPointF(5, 5), QPointF(11, 11));
    p.drawLine(QPointF(11, 5), QPointF(5, 11));
    p.end();
    return QIcon(pm);
}

bool isModifierKey(int key)
{
    return key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt
        || key == Qt::Key_Meta    || key == Qt::Key_AltGr || key == Qt::Key_unknown;
}

// The single modifier flag a modifier key represents (NoModifier for non-modifiers).
Qt::KeyboardModifiers modifierFlagFor(int key)
{
    switch (key) {
        case Qt::Key_Control: return Qt::ControlModifier;
        case Qt::Key_Shift:   return Qt::ShiftModifier;
        case Qt::Key_Alt:
        case Qt::Key_AltGr:   return Qt::AltModifier;
        case Qt::Key_Meta:    return Qt::MetaModifier;
    }
    return Qt::NoModifier;
}
} // namespace

HotkeyEdit::HotkeyEdit(QWidget* parent)
    : QLineEdit(parent)
{
    setObjectName(QStringLiteral("HotkeyEdit"));
    setReadOnly(true);                 // typed text is meaningless; we capture keys
    setAlignment(Qt::AlignCenter);
    setContextMenuPolicy(Qt::NoContextMenu);
    setPlaceholderText(I18n::instance().t(QStringLiteral("prefs.hotkey.press_shortcut")));

    m_clearAction = addAction(makeClearIcon(), QLineEdit::TrailingPosition);
    m_clearAction->setToolTip(I18n::instance().t(QStringLiteral("prefs.hotkey.clear")));
    connect(m_clearAction, &QAction::triggered, this, [this] {
        commit(QKeySequence());
        clearFocus();
    });
}

void HotkeyEdit::setKeySequence(const QKeySequence& seq)
{
    m_seq = seq;
    m_committedMods = Qt::NoModifier;
    refreshText();
}

QString HotkeyEdit::triggerString() const
{
    return KeySequenceCodec::modsToString(m_committedMods);   // empty when unbound
}

void HotkeyEdit::setTriggerString(const QString& s)
{
    // Modifier-only box: keep only the modifiers (a legacy full-hotkey value migrates to
    // its modifiers, shown as-is — never silently stripped behind the scenes).
    m_seq = QKeySequence();
    m_committedMods = KeySequenceCodec::modsFromString(s);
    refreshText();
}

void HotkeyEdit::commit(const QKeySequence& seq)
{
    const bool changed = (seq != m_seq) || (m_committedMods != Qt::NoModifier);
    m_seq = seq;
    m_committedMods = Qt::NoModifier;
    m_committedThisFocus = true;
    refreshText();
    if (changed)
        emit keySequenceChanged();
}

void HotkeyEdit::commitMods(Qt::KeyboardModifiers mods)
{
    const bool changed = (mods != m_committedMods) || !m_seq.isEmpty();
    m_seq = QKeySequence();
    m_committedMods = mods;
    m_committedThisFocus = true;
    refreshText();
    if (changed)
        emit keySequenceChanged();
}

void HotkeyEdit::refreshText()
{
    if (m_committedMods != Qt::NoModifier)
        setText(KeySequenceCodec::modsToString(m_committedMods));
    else
        setText(m_seq.isEmpty() ? QString() : m_seq.toString(QKeySequence::NativeText));
}

void HotkeyEdit::mousePressEvent(QMouseEvent* e)
{
    setFocus(Qt::MouseFocusReason);    // click anywhere in the box → start capturing
    e->accept();
}

void HotkeyEdit::focusInEvent(QFocusEvent* e)
{
    QLineEdit::focusInEvent(e);        // QSS :focus paints the theme-color underline
    m_pendingMods = Qt::NoModifier;    // fresh capture
    m_heldMods = Qt::NoModifier;
    m_committedThisFocus = false;
}

void HotkeyEdit::focusOutEvent(QFocusEvent* e)
{
    QLineEdit::focusOutEvent(e);
    refreshText();                     // drop any live "Ctrl+Alt…" preview if nothing committed
}

void HotkeyEdit::keyPressEvent(QKeyEvent* e)
{
    const int key = e->key();

    if (key == Qt::Key_Escape) {       // cancel capture, keep current value
        clearFocus();
        e->accept();
        return;
    }
    if (key == Qt::Key_Backspace || key == Qt::Key_Delete) {
        commit(QKeySequence());
        e->accept();
        return;
    }
    if (isModifierKey(key)) {
        // Modifier-only mode: accumulate the held modifiers and preview them live; the
        // combo commits on release (keyReleaseEvent). Otherwise just wait for a real key.
        if (m_modifierOnly && !m_committedThisFocus) {
            const Qt::KeyboardModifiers flag = modifierFlagFor(key);
            m_heldMods |= flag;
            m_pendingMods |= flag;
            if (m_pendingMods != Qt::NoModifier)
                setText(KeySequenceCodec::modsToString(m_pendingMods));
        }
        e->accept();
        return;
    }

    // Modifier-only mode: a real key is not a valid trigger — ignore it (the value will
    // still commit from the modifiers held, on release), so a full hotkey can't be set.
    if (m_modifierOnly) {
        e->accept();
        return;
    }

    const QKeySequence seq(QKeyCombination(e->modifiers(), Qt::Key(key)));
    commit(seq);
    clearFocus();                       // one combo finalizes the capture
    e->accept();
}

void HotkeyEdit::keyReleaseEvent(QKeyEvent* e)
{
    if (m_modifierOnly && !m_committedThisFocus) {
        m_heldMods &= ~modifierFlagFor(e->key());
        // All modifiers released with no real key pressed → commit the modifier-only combo.
        if (m_heldMods == Qt::NoModifier && m_pendingMods != Qt::NoModifier) {
            commitMods(m_pendingMods);
            clearFocus();
            e->accept();
            return;
        }
    }
    QLineEdit::keyReleaseEvent(e);
}
