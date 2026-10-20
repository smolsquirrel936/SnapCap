#include <QtTest>
#include <QTemporaryDir>
#include <QSignalSpy>

#include "core/Settings.h"

// Stage 1: settings persistence (isolated to a temp config.ini).
class TstSettings : public QObject
{
    Q_OBJECT
private slots:
    void seedsDefaults();
    void roundTripsAndSignals();
    void noSignalWhenUnchanged();

private:
    QTemporaryDir m_dir;
    QString ini() { return m_dir.filePath(QStringLiteral("config.ini")); }
};

void TstSettings::seedsDefaults()
{
    Settings s(ini());
    QCOMPARE(s.value(QStringLiteral("log/file")).toString(), QStringLiteral("splog.txt"));
    QCOMPARE(s.value(QStringLiteral("general/hideTray")).toBool(), false);
    QCOMPARE(s.value(QStringLiteral("general/locale")).toString(), QStringLiteral("en"));
}

void TstSettings::roundTripsAndSignals()
{
    {
        Settings s(ini());
        QSignalSpy spy(&s, &Settings::changed);
        s.setValue(QStringLiteral("hotkey/snip"), QStringLiteral("Ctrl+F1"));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toString(), QStringLiteral("hotkey/snip"));
    }
    // Reopen: value persisted to disk.
    Settings s2(ini());
    QCOMPARE(s2.value(QStringLiteral("hotkey/snip")).toString(), QStringLiteral("Ctrl+F1"));
}

void TstSettings::noSignalWhenUnchanged()
{
    Settings s(ini());
    s.setValue(QStringLiteral("general/locale"), QStringLiteral("zh_tw"));
    QSignalSpy spy(&s, &Settings::changed);
    s.setValue(QStringLiteral("general/locale"), QStringLiteral("zh_tw"));  // same value
    QCOMPARE(spy.count(), 0);
}

QTEST_GUILESS_MAIN(TstSettings)
#include "tst_settings.moc"
