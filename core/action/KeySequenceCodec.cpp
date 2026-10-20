#include "core/action/KeySequenceCodec.h"

#include <QHash>
#include <QStringList>

namespace {

// Token (e.g. "F3", "A", "Esc") -> Qt::Key; 0 if unrecognized.
int keyFromName(const QString& token)
{
    const QString u = token.trimmed().toUpper();
    if (u.isEmpty())
        return 0;

    if (u.size() == 1) {
        const QChar c = u.at(0);
        if (c >= 'A' && c <= 'Z') return Qt::Key_A + (c.unicode() - 'A');
        if (c >= '0' && c <= '9') return Qt::Key_0 + (c.unicode() - '0');
        switch (c.unicode()) {
            case ',': return Qt::Key_Comma;
            case '.': return Qt::Key_Period;
            case '/': return Qt::Key_Slash;
            case ';': return Qt::Key_Semicolon;
            case '`': return Qt::Key_QuoteLeft;
        }
        return 0;
    }

    if (u.at(0) == 'F') {                       // function keys F1..F35
        bool ok = false;
        const int n = u.mid(1).toInt(&ok);
        if (ok && n >= 1 && n <= 35)
            return Qt::Key_F1 + (n - 1);
    }

    static const QHash<QString, int> named = {
        {"ESC", Qt::Key_Escape},   {"ESCAPE", Qt::Key_Escape},
        {"TAB", Qt::Key_Tab},      {"SPACE", Qt::Key_Space},
        {"ENTER", Qt::Key_Return}, {"RETURN", Qt::Key_Return},
        {"BACKSPACE", Qt::Key_Backspace},
        {"DELETE", Qt::Key_Delete}, {"DEL", Qt::Key_Delete},
        {"INSERT", Qt::Key_Insert}, {"HOME", Qt::Key_Home}, {"END", Qt::Key_End},
        {"PAGEUP", Qt::Key_PageUp}, {"PAGEDOWN", Qt::Key_PageDown},
        {"UP", Qt::Key_Up}, {"DOWN", Qt::Key_Down},
        {"LEFT", Qt::Key_Left}, {"RIGHT", Qt::Key_Right},
        {"PRINT", Qt::Key_Print}, {"PAUSE", Qt::Key_Pause},
    };
    return named.value(u, 0);
}

// Qt::Key -> display name (inverse of keyFromName).
QString keyName(int key)
{
    if (key >= Qt::Key_A && key <= Qt::Key_Z) return QString(QChar('A' + (key - Qt::Key_A)));
    if (key >= Qt::Key_0 && key <= Qt::Key_9) return QString(QChar('0' + (key - Qt::Key_0)));
    if (key >= Qt::Key_F1 && key <= Qt::Key_F35) return QStringLiteral("F%1").arg(key - Qt::Key_F1 + 1);
    switch (key) {
        case Qt::Key_Escape:   return QStringLiteral("Esc");
        case Qt::Key_Tab:      return QStringLiteral("Tab");
        case Qt::Key_Space:    return QStringLiteral("Space");
        case Qt::Key_Return:
        case Qt::Key_Enter:    return QStringLiteral("Enter");
        case Qt::Key_Backspace:return QStringLiteral("Backspace");
        case Qt::Key_Delete:   return QStringLiteral("Delete");
        case Qt::Key_Insert:   return QStringLiteral("Insert");
        case Qt::Key_Home:     return QStringLiteral("Home");
        case Qt::Key_End:      return QStringLiteral("End");
        case Qt::Key_PageUp:   return QStringLiteral("PageUp");
        case Qt::Key_PageDown: return QStringLiteral("PageDown");
        case Qt::Key_Up:       return QStringLiteral("Up");
        case Qt::Key_Down:     return QStringLiteral("Down");
        case Qt::Key_Left:     return QStringLiteral("Left");
        case Qt::Key_Right:    return QStringLiteral("Right");
        case Qt::Key_Comma:    return QStringLiteral(",");
        case Qt::Key_Period:   return QStringLiteral(".");
        case Qt::Key_Slash:    return QStringLiteral("/");
        case Qt::Key_Semicolon:return QStringLiteral(";");
        case Qt::Key_QuoteLeft:return QStringLiteral("`");
        case Qt::Key_Print:    return QStringLiteral("Print");
        case Qt::Key_Pause:    return QStringLiteral("Pause");
    }
    return QString();
}

} // namespace

namespace KeySequenceCodec {

KeySequence parse(const QString& text)
{
    KeySequence ks;
    const QStringList parts = text.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return ks;

    Qt::KeyboardModifiers mods = Qt::NoModifier;
    int key = 0;
    bool keySet = false;
    bool bad = false;

    for (const QString& raw : parts) {
        const QString u = raw.trimmed().toUpper();
        if (u == "CTRL" || u == "CONTROL")      mods |= Qt::ControlModifier;
        else if (u == "SHIFT")                  mods |= Qt::ShiftModifier;
        else if (u == "ALT")                    mods |= Qt::AltModifier;
        else if (u == "WIN" || u == "META" || u == "SUPER" || u == "CMD" || u == "COMMAND")
                                                mods |= Qt::MetaModifier;
        else {
            const int k = keyFromName(raw);
            if (k == 0 || keySet) bad = true;   // unknown key, or two keys
            else { key = k; keySet = true; }
        }
    }

    if (bad || !keySet)
        return ks;                              // invalid -> ks.valid stays false

    ks.mods = mods;
    ks.key = key;
    ks.valid = true;
    return ks;
}

QString toString(const KeySequence& seq)
{
    if (!seq.valid)
        return QString();
    QString out;
    if (seq.mods.testFlag(Qt::ControlModifier)) out += QStringLiteral("Ctrl+");
    if (seq.mods.testFlag(Qt::ShiftModifier))   out += QStringLiteral("Shift+");
    if (seq.mods.testFlag(Qt::AltModifier))     out += QStringLiteral("Alt+");
    if (seq.mods.testFlag(Qt::MetaModifier))    out += QStringLiteral("Win+");
    out += keyName(seq.key);
    return out;
}

QString normalize(const QString& text)
{
    return toString(parse(text));
}

Qt::KeyboardModifiers modsFromString(const QString& text)
{
    Qt::KeyboardModifiers mods = Qt::NoModifier;
    const QStringList parts = text.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    for (const QString& raw : parts) {
        const QString u = raw.trimmed().toUpper();
        if (u == "CTRL" || u == "CONTROL")      mods |= Qt::ControlModifier;
        else if (u == "SHIFT")                  mods |= Qt::ShiftModifier;
        else if (u == "ALT")                    mods |= Qt::AltModifier;
        else if (u == "WIN" || u == "META" || u == "SUPER" || u == "CMD" || u == "COMMAND")
                                                mods |= Qt::MetaModifier;
        // non-modifier tokens are ignored (a full hotkey is classified via triggerHasKey)
    }
    return mods;
}

QString modsToString(Qt::KeyboardModifiers mods)
{
    QString out;
    if (mods.testFlag(Qt::ControlModifier)) out += QStringLiteral("Ctrl+");
    if (mods.testFlag(Qt::ShiftModifier))   out += QStringLiteral("Shift+");
    if (mods.testFlag(Qt::AltModifier))     out += QStringLiteral("Alt+");
    if (mods.testFlag(Qt::MetaModifier))    out += QStringLiteral("Win+");
    if (out.endsWith(QLatin1Char('+')))
        out.chop(1);
    return out;
}

} // namespace KeySequenceCodec
