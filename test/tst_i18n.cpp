#include <QtTest>
#include <QTemporaryDir>
#include <QSignalSpy>

#include "core/i18n/I18n.h"

// Stage 0 unit tests for the YAML-backed i18n provider.
// Validates the behaviors promised in Tech-Stack-Plan.md ("i18n architecture"):
// fallback chain, {placeholder} interpolation, locale discovery, unknown-locale
// handling, and that localeChanged() fires exactly once per real change.
class TstI18n : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();   // write locale fixtures once
    void init();           // reset to a known state before each test

    void loadsAllLocales();
    void defaultLocaleResolves();
    void switchLocaleResolves();
    void fallsBackToDefaultLocale();
    void missingKeyReturnsKey();
    void interpolatesPlaceholders();
    void unknownLocaleRejected();
    void localeChangedEmittedOncePerChange();

private:
    QTemporaryDir m_dir;   // holds the fixture lang/ files for the whole run
};

void TstI18n::initTestCase()
{
    QVERIFY(m_dir.isValid());

    const QString en = QStringLiteral(
        "meta:\n"
        "  name: \"English\"\n"
        "  locale: \"en\"\n"
        "greeting: \"Hello\"\n"
        "only_en: \"EN only\"\n"
        "size: \"{w} x {h}\"\n");

    // zh_cn intentionally omits 'only_en' to exercise the fallback chain.
    // Use fromUtf8 so the \x byte sequences decode as UTF-8 (你好 / 简体中文),
    // not as per-byte Latin-1 code points.
    const QString zh = QString::fromUtf8(
        "meta:\n"
        "  name: \"\xe7\xae\x80\xe4\xbd\x93\xe4\xb8\xad\xe6\x96\x87\"\n"
        "  locale: \"zh_cn\"\n"
        "greeting: \"\xe4\xbd\xa0\xe5\xa5\xbd\"\n"
        "size: \"{w} x {h}\"\n");

    auto write = [&](const QString& file, const QString& content) {
        QFile f(m_dir.filePath(file));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write(content.toUtf8());
        f.close();
    };
    write(QStringLiteral("en.yaml"), en);
    write(QStringLiteral("zh_cn.yaml"), zh);
}

void TstI18n::init()
{
    // Singleton is shared across slots; reload + reset locale for isolation.
    const int n = I18n::instance().load(m_dir.path());
    QCOMPARE(n, 2);
    I18n::instance().setLocale(QStringLiteral("en"));
}

void TstI18n::loadsAllLocales()
{
    const auto locales = I18n::instance().availableLocales();
    QCOMPARE(locales.size(), 2);

    QMap<QString, QString> byCode;
    for (const auto& [code, name] : locales)
        byCode.insert(code, name);
    QVERIFY(byCode.contains(QStringLiteral("en")));
    QVERIFY(byCode.contains(QStringLiteral("zh_cn")));
    QCOMPARE(byCode.value(QStringLiteral("en")), QStringLiteral("English"));
}

void TstI18n::defaultLocaleResolves()
{
    QCOMPARE(I18n::instance().locale(), QStringLiteral("en"));
    QCOMPARE(I18n::instance().t(QStringLiteral("greeting")), QStringLiteral("Hello"));
}

void TstI18n::switchLocaleResolves()
{
    QVERIFY(I18n::instance().setLocale(QStringLiteral("zh_cn")));
    QCOMPARE(I18n::instance().t(QStringLiteral("greeting")),
             QString::fromUtf8("\xe4\xbd\xa0\xe5\xa5\xbd"));  // 你好
}

void TstI18n::fallsBackToDefaultLocale()
{
    I18n::instance().setLocale(QStringLiteral("zh_cn"));
    // 'only_en' is absent in zh_cn -> must fall back to the en value.
    QCOMPARE(I18n::instance().t(QStringLiteral("only_en")), QStringLiteral("EN only"));
}

void TstI18n::missingKeyReturnsKey()
{
    // Last-resort: surface the key itself so missing strings are visible, not crashy.
    QCOMPARE(I18n::instance().t(QStringLiteral("does.not.exist")),
             QStringLiteral("does.not.exist"));
}

void TstI18n::interpolatesPlaceholders()
{
    const QString out = I18n::instance().t(QStringLiteral("size"),
                                           {{QStringLiteral("w"), 1280},
                                            {QStringLiteral("h"), 720}});
    QCOMPARE(out, QStringLiteral("1280 x 720"));
}

void TstI18n::unknownLocaleRejected()
{
    QVERIFY(!I18n::instance().setLocale(QStringLiteral("xx")));
    // Active locale unchanged after a rejected switch.
    QCOMPARE(I18n::instance().locale(), QStringLiteral("en"));
}

void TstI18n::localeChangedEmittedOncePerChange()
{
    QSignalSpy spy(&I18n::instance(), &I18n::localeChanged);

    QVERIFY(I18n::instance().setLocale(QStringLiteral("zh_cn")));  // real change -> 1
    QCOMPARE(spy.count(), 1);

    QVERIFY(I18n::instance().setLocale(QStringLiteral("zh_cn")));  // no-op -> no signal
    QCOMPARE(spy.count(), 1);

    QVERIFY(!I18n::instance().setLocale(QStringLiteral("xx")));    // rejected -> no signal
    QCOMPARE(spy.count(), 1);
}

QTEST_MAIN(TstI18n)
#include "tst_i18n.moc"
