#include "core/FilenameTemplate.h"

#include <QProcessEnvironment>
#include <optional>

namespace FilenameTemplate {
namespace {

// Look up an environment variable by name. Windows environment variable names
// are case-insensitive, so "%os%"/"%OS%" resolve identically there; we make the
// lookup case-insensitive on every platform so a template behaves the same
// regardless of where the assertions run. Returns an empty string when unset —
// matching the reference app, where an unknown variable simply vanishes.
QString lookupEnv(const QString& name, const QProcessEnvironment& env)
{
    if (env.contains(name))
        return env.value(name);
    const QString lname = name.toLower();
    const QStringList keys = env.keys();
    for (const QString& key : keys)
        if (key.toLower() == lname)
            return env.value(key);
    return QString();
}

// Replace characters that are illegal in a Windows filename component (also unsafe on
// other OSes) with '_' so a special variable like %title% ("1:30 - YouTube") can't
// break the save. Substitutes the reserved set < > : " / \ | ? * and control chars,
// then trims surrounding whitespace and trailing dots. Applied only to substituted
// special values, never to the literal template, so real path separators survive.
QString sanitizeComponent(const QString& s)
{
    static const QString reserved = QStringLiteral("<>:\"/\\|?*");
    QString out;
    out.reserve(s.size());
    for (const QChar c : s)
        out.append(c.unicode() < 0x20 || reserved.contains(c) ? QLatin1Char('_') : c);
    out = out.trimmed();
    while (!out.isEmpty() && out.endsWith(QLatin1Char('.')))
        out.chop(1);
    return out;
}

// Resolve a "%name%" body against the special-variable map. Recognizes an optional
// ":N" length suffix (%title:50%). Returns nullopt when the base name isn't a
// special key (so the caller falls back to the environment lookup).
std::optional<QString> lookupSpecial(const QString& body, const QMap<QString, QString>& specials)
{
    const int colon = body.indexOf(QLatin1Char(':'));
    const QString base = (colon < 0 ? body : body.left(colon)).toLower();
    const auto it = specials.constFind(base);
    if (it == specials.constEnd())
        return std::nullopt;
    QString value = sanitizeComponent(it.value());
    if (colon >= 0) {
        bool ok = false;
        const int limit = body.mid(colon + 1).toInt(&ok);
        if (ok && limit >= 0 && value.size() > limit)
            value.truncate(limit);
    }
    return value;
}

} // namespace

QString expand(const QString& templ, const QDateTime& when)
{
    return expand(templ, when, QMap<QString, QString>());
}

QString expand(const QString& templ, const QDateTime& when,
               const QMap<QString, QString>& specials)
{
    QString out;
    out.reserve(templ.size());

    // Snapshot the system environment once per call (keys resolved lazily below).
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

    const int n = templ.size();
    int i = 0;
    while (i < n) {
        const QChar c = templ.at(i);

        // "$...$" — a Qt date/time format span.
        if (c == QLatin1Char('$')) {
            // "$$" is an escaped literal dollar sign.
            if (i + 1 < n && templ.at(i + 1) == QLatin1Char('$')) {
                out.append(QLatin1Char('$'));
                i += 2;
                continue;
            }
            const int close = templ.indexOf(QLatin1Char('$'), i + 1);
            if (close < 0) {
                // Dangling '$' with no partner — emit the remainder verbatim.
                out.append(templ.mid(i));
                break;
            }
            const QString fmt = templ.mid(i + 1, close - (i + 1));
            out.append(when.toString(fmt));
            i = close + 1;
            continue;
        }

        // "%name%" — a special variable (e.g. %title% / %title:N%, from `specials`)
        // or, failing that, a system environment variable (%os%, %username%, ...).
        if (c == QLatin1Char('%')) {
            // "%%" is an escaped literal percent sign.
            if (i + 1 < n && templ.at(i + 1) == QLatin1Char('%')) {
                out.append(QLatin1Char('%'));
                i += 2;
                continue;
            }
            const int close = templ.indexOf(QLatin1Char('%'), i + 1);
            if (close < 0) {
                // Dangling '%' with no partner — emit the remainder verbatim.
                out.append(templ.mid(i));
                break;
            }
            const QString name = templ.mid(i + 1, close - (i + 1));
            if (const std::optional<QString> special = lookupSpecial(name, specials))
                out.append(*special);
            else
                out.append(lookupEnv(name, env));
            i = close + 1;
            continue;
        }

        out.append(c);
        ++i;
    }
    return out;
}

} // namespace FilenameTemplate
