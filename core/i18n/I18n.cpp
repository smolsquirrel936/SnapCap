#include "core/i18n/I18n.h"

#include <QDir>
#include <QFileInfo>
#include <QDebug>

#include <yaml-cpp/yaml.h>

namespace {

// Recursively flatten a YAML map into dotted keys:
//   snip: { toolbar: { copy: "Copy" } }  ->  "snip.toolbar.copy" = "Copy"
void flatten(const YAML::Node& node, const QString& prefix,
             QHash<QString, QString>& out)
{
    if (node.IsMap()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
            const QString k = QString::fromStdString(it->first.as<std::string>());
            const QString next = prefix.isEmpty() ? k : prefix + QLatin1Char('.') + k;
            flatten(it->second, next, out);
        }
    } else if (node.IsScalar()) {
        out.insert(prefix, QString::fromStdString(node.Scalar()));
    }
    // Sequences are intentionally ignored: UI strings are scalar key->value.
}

} // namespace

I18n& I18n::instance()
{
    static I18n inst;
    return inst;
}

I18n::I18n(QObject* parent) : QObject(parent) {}

int I18n::load(const QString& langDir)
{
    m_langDir = langDir;
    m_strings.clear();
    m_localeNames.clear();

    QDir dir(langDir);
    const QStringList files = dir.entryList({QStringLiteral("*.yaml"), QStringLiteral("*.yml")},
                                            QDir::Files, QDir::Name);
    for (const QString& file : files) {
        const QString path = dir.filePath(file);
        QHash<QString, QString> flat;
        try {
            const YAML::Node root = YAML::LoadFile(path.toStdString());
            flatten(root, QString(), flat);
        } catch (const std::exception& e) {
            qWarning() << "[i18n] failed to parse" << path << ":" << e.what();
            continue;
        }

        // Locale code: meta.locale if present, else the file stem.
        QString code = flat.value(QStringLiteral("meta.locale"));
        if (code.isEmpty())
            code = QFileInfo(file).completeBaseName();

        QString name = flat.value(QStringLiteral("meta.name"));
        if (name.isEmpty())
            name = code;

        m_strings.insert(code, flat);
        m_localeNames.insert(code, name);
    }

    if (!m_strings.contains(m_defaultLocale) && !m_strings.isEmpty())
        m_defaultLocale = m_strings.constBegin().key();

    return m_strings.size();
}

bool I18n::setLocale(const QString& locale)
{
    if (!m_strings.contains(locale)) {
        qWarning() << "[i18n] unknown locale:" << locale;
        return false;
    }
    if (locale == m_locale)
        return true;
    m_locale = locale;
    emit localeChanged();
    return true;
}

QList<QPair<QString, QString>> I18n::availableLocales() const
{
    QList<QPair<QString, QString>> out;
    out.reserve(m_localeNames.size());
    for (auto it = m_localeNames.constBegin(); it != m_localeNames.constEnd(); ++it)
        out.append({it.key(), it.value()});
    std::sort(out.begin(), out.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });
    return out;
}

QString I18n::lookup(const QString& locale, const QString& key) const
{
    const auto it = m_strings.constFind(locale);
    if (it == m_strings.constEnd())
        return QString();
    return it->value(key);  // empty if absent
}

QString I18n::t(const QString& key) const
{
    QString v = lookup(m_locale, key);
    if (v.isNull() && m_locale != m_defaultLocale)
        v = lookup(m_defaultLocale, key);
    return v.isNull() ? key : v;  // last-resort: surface the key itself
}

QString I18n::t(const QString& key, const QVariantMap& params) const
{
    QString s = t(key);
    for (auto it = params.constBegin(); it != params.constEnd(); ++it)
        s.replace(QLatin1Char('{') + it.key() + QLatin1Char('}'), it.value().toString());
    return s;
}
