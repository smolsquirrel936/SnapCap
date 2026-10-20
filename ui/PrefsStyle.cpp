#include "ui/PrefsStyle.h"

#include "core/Settings.h"

#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QFile>
#include <QFontInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSettings>
#include <QStyleHints>

namespace PrefsStyle {
namespace {

// The two palettes, in the same order, so a role can be compared across schemes by
// reading down one column. Dark mirrors the light ramp's structure rather than
// inverting it channel-wise: cards stay a step lighter than the page, borders a step
// lighter again, and the text ramp keeps its four levels of emphasis.
struct Palette {
    const char* page;           // page + scroll viewport background
    const char* card;           // cards, header, inputs, buttons, popup backgrounds
    const char* border;         // card and header borders
    const char* borderInput;    // input / button borders
    const char* hover;          // sidebar row hover
    const char* divider;        // setting-row divider, button pressed
    const char* subtle;         // read-only inputs, button hover
    const char* disabledBg;     // disabled inputs
    const char* text;           // primary text (titles, input text)
    const char* textLabel;      // setting labels, button text
    const char* textMuted;      // sidebar items, read-only text
    const char* textSecondary;  // descriptions, subtitles
    const char* textDisabled;   // disabled text
    const char* danger;         // hotkey conflict message
    const char* textStrong;     // key-bindings table text
};

constexpr Palette kLight{
    "#f7f8fa", "#ffffff", "#e5e7eb", "#d0d5dd", "#f0f2f5", "#f0f1f3", "#f9fafb", "#f2f4f7",
    "#101828", "#344054", "#475467", "#667085", "#98a2b3", "#c0392b", "#000000",
};

constexpr Palette kDark{
    "#16181d", "#1e2128", "#2c313a", "#3a4150", "#262b33", "#2a2f37", "#1a1d23", "#22262d",
    "#e8eaed", "#cdd2d9", "#a8b0bb", "#8b93a0", "#5c6470", "#ef6b5e", "#f2f4f7",
};

// Read straight from the portable config.ini, without constructing a Settings (whose
// ctor seeds defaults = a disk write). Read-only here — same approach as accentColor().
QString configValue(const QString& key, const QString& fallback)
{
    QSettings qs(Settings::resolveConfigPath(), QSettings::IniFormat);
    return qs.value(key, fallback).toString();
}

// `over` laid on `base` at opacity `a`. The selected-row tint used to be computed
// against a hard-coded white; on a dark card that produced a glowing white row, so
// the surface is now passed in.
QColor mix(const QColor& over, const QColor& base, double a)
{
    return QColor(qRound(base.red()   * (1 - a) + over.red()   * a),
                  qRound(base.green() * (1 - a) + over.green() * a),
                  qRound(base.blue()  * (1 - a) + over.blue()  * a));
}

QHash<QString, QString> buildTokens(Scheme scheme, const QColor& accent)
{
    const Palette& p = (scheme == Scheme::Dark) ? kDark : kLight;

    // The accent-derived shades. `accent-text` has to move in opposite directions per
    // scheme: darkening reads as emphasis on a light row but disappears into a dark
    // one, so dark lightens instead.
    const QColor card(QString::fromLatin1(p.card));
    const QColor accentText = (scheme == Scheme::Dark) ? accent.lighter(130) : accent.darker(130);

    return {
        {QStringLiteral("@page"),           QString::fromLatin1(p.page)},
        {QStringLiteral("@card"),           QString::fromLatin1(p.card)},
        {QStringLiteral("@border"),         QString::fromLatin1(p.border)},
        {QStringLiteral("@border-input"),   QString::fromLatin1(p.borderInput)},
        {QStringLiteral("@hover"),          QString::fromLatin1(p.hover)},
        {QStringLiteral("@divider"),        QString::fromLatin1(p.divider)},
        {QStringLiteral("@subtle"),         QString::fromLatin1(p.subtle)},
        {QStringLiteral("@disabled-bg"),    QString::fromLatin1(p.disabledBg)},
        {QStringLiteral("@text"),           QString::fromLatin1(p.text)},
        {QStringLiteral("@text-label"),     QString::fromLatin1(p.textLabel)},
        {QStringLiteral("@text-muted"),     QString::fromLatin1(p.textMuted)},
        {QStringLiteral("@text-secondary"), QString::fromLatin1(p.textSecondary)},
        {QStringLiteral("@text-disabled"),  QString::fromLatin1(p.textDisabled)},
        {QStringLiteral("@danger"),         QString::fromLatin1(p.danger)},
        {QStringLiteral("@text-strong"),    QString::fromLatin1(p.textStrong)},
        {QStringLiteral("@accent"),         accent.name()},
        {QStringLiteral("@accent-hover"),   accent.darker(112).name()},
        {QStringLiteral("@accent-tint"),    mix(accent, card, 0.14).name()},
        {QStringLiteral("@accent-text"),    accentText.name()},
        // Text drawn ON the accent (primary button, selected text). White reads on a
        // saturated accent in both schemes, so this is one value, not two.
        {QStringLiteral("@on-accent"),      QStringLiteral("#ffffff")},
    };
}

}  // namespace

QColor accentColor()
{
    const QColor c(configValue(QStringLiteral("interface/accentColor"), QStringLiteral("#2d7dff")));
    return c.isValid() ? c : QColor(0x2d, 0x7d, 0xff);
}

Scheme effectiveScheme()
{
    const QString mode = configValue(QStringLiteral("interface/theme"), QStringLiteral("auto"));
    if (mode.compare(QLatin1String("dark"), Qt::CaseInsensitive) == 0)
        return Scheme::Dark;
    if (mode.compare(QLatin1String("light"), Qt::CaseInsensitive) == 0)
        return Scheme::Light;

    // "auto" (and anything unrecognised): follow the OS. Qt reports Unknown when it
    // cannot tell, which we treat as light — the scheme this app shipped with.
    return (QApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark) ? Scheme::Dark
                                                                               : Scheme::Light;
}

QString load()
{
    const QString path = QCoreApplication::applicationDirPath()
                         + QStringLiteral("/res/preferences.qss");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};                                   // missing file = no stylesheet
    QString qss = QString::fromUtf8(f.readAll());

    // The QSS px sizes were authored for the 9pt default app font; scale them by
    // the current app font so all styled text tracks the interface font. At the
    // default font this is 1.0 → the stylesheet is unchanged.
    double scale = QFontInfo(QApplication::font()).pointSizeF() / 9.0;
    if (scale <= 0.0)
        scale = 1.0;

    // Rewrite only `font-size: Npx` declarations, leaving other px values alone.
    static const QRegularExpression fontRe(QStringLiteral("font-size\\s*:\\s*(\\d+)px"));
    QString out;
    out.reserve(qss.size());
    qsizetype last = 0;
    auto it = fontRe.globalMatch(qss);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += qss.mid(last, m.capturedStart() - last);
        const int px = qMax(1, qRound(m.captured(1).toInt() * scale));
        out += QStringLiteral("font-size: %1px").arg(px);
        last = m.capturedEnd();
    }
    out += qss.mid(last);

    // Resolve the colour tokens. Matched by regex in one pass rather than by repeated
    // replace(), so that `@text` cannot eat the prefix of `@text-label`. An unknown
    // token is left in place: Qt then drops that one declaration as invalid, which is
    // far easier to spot than a silently wrong colour.
    const QHash<QString, QString> tokens = buildTokens(effectiveScheme(), accentColor());
    static const QRegularExpression tokenRe(QStringLiteral("@[a-z][a-z-]*"));
    QString themed;
    themed.reserve(out.size());
    last = 0;
    auto t = tokenRe.globalMatch(out);
    while (t.hasNext()) {
        const QRegularExpressionMatch m = t.next();
        const auto hit = tokens.constFind(m.captured());
        if (hit == tokens.cend())
            continue;                                // unknown token: leave it verbatim
        themed += out.mid(last, m.capturedStart() - last);
        themed += *hit;
        last = m.capturedEnd();
    }
    themed += out.mid(last);
    return themed;
}

}  // namespace PrefsStyle
