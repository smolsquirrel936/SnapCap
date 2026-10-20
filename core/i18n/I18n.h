#pragma once

#include <QObject>
#include <QString>
#include <QHash>
#include <QList>
#include <QPair>
#include <QVariantMap>

// SnapCap i18n: every UI string lives in editable YAML files under lang/, never
// in code. Widgets reference dotted keys (e.g. "snip.toolbar.copy"); this class
// resolves them at runtime with a fallback chain and supports live switching.
//
// Design: see claude-plan/Tech-Stack-Plan.md (i18n architecture).
class I18n : public QObject
{
    Q_OBJECT
public:
    static I18n& instance();

    // Scan a directory for "<locale>.yaml" files and load them all. Safe to call
    // again to reload from disk. Returns the number of locales loaded.
    int load(const QString& langDir);

    // Activate a locale (e.g. "zh_cn"). No-op + returns false if unknown.
    // Emits localeChanged() on success.
    bool setLocale(const QString& locale);
    QString locale() const { return m_locale; }
    QString defaultLocale() const { return m_defaultLocale; }

    // Discovered locales as {code, displayName} (displayName from meta.name).
    QList<QPair<QString, QString>> availableLocales() const;

    // Resolve a key. Fallback chain: active locale -> default locale -> the key
    // itself (so missing strings are visible but never crash).
    QString t(const QString& key) const;

    // Same, with {placeholder} interpolation, e.g.
    //   t("snip.hint_size", {{"w", 1280}, {"h", 720}})  ->  "1280 x 720"
    QString t(const QString& key, const QVariantMap& params) const;

signals:
    void localeChanged();

private:
    explicit I18n(QObject* parent = nullptr);
    Q_DISABLE_COPY(I18n)

    QString lookup(const QString& locale, const QString& key) const;

    QString m_langDir;
    QString m_locale = QStringLiteral("en");
    QString m_defaultLocale = QStringLiteral("en");

    QHash<QString, QHash<QString, QString>> m_strings;  // locale -> (dottedKey -> value)
    QHash<QString, QString> m_localeNames;              // locale -> display name
};

// Convenience for call sites: TR("menu.snip")
#define TR(key) (I18n::instance().t(QStringLiteral(key)))
