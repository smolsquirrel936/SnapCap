#include <QtTest>

#include "core/action/KeySequenceCodec.h"

// Stage 1: the pure key-sequence codec (no Win32 / no GUI).
class TstKeySeqCodec : public QObject
{
    Q_OBJECT
private slots:
    void parsesFunctionKey();
    void parsesModifiersAnyOrder();
    void normalizeIsCanonical();
    void rejectsInvalid();
    void modifierOnlyRoundTrips();
};

void TstKeySeqCodec::parsesFunctionKey()
{
    const KeySequence ks = KeySequenceCodec::parse(QStringLiteral("F1"));
    QVERIFY(ks.valid);
    QCOMPARE(ks.mods, Qt::NoModifier);
    QCOMPARE(ks.key, int(Qt::Key_F1));
    QCOMPARE(KeySequenceCodec::toString(ks), QStringLiteral("F1"));
}

void TstKeySeqCodec::parsesModifiersAnyOrder()
{
    const KeySequence ks = KeySequenceCodec::parse(QStringLiteral("shift+ctrl+s"));
    QVERIFY(ks.valid);
    QVERIFY(ks.mods.testFlag(Qt::ControlModifier));
    QVERIFY(ks.mods.testFlag(Qt::ShiftModifier));
    QCOMPARE(ks.key, int(Qt::Key_S));
    // Canonical order is Ctrl, Shift, Alt, Win regardless of input order.
    QCOMPARE(KeySequenceCodec::toString(ks), QStringLiteral("Ctrl+Shift+S"));
}

void TstKeySeqCodec::normalizeIsCanonical()
{
    QCOMPARE(KeySequenceCodec::normalize(QStringLiteral("shift+f3")), QStringLiteral("Shift+F3"));
    QCOMPARE(KeySequenceCodec::normalize(QStringLiteral("WIN+a")),    QStringLiteral("Win+A"));
    QCOMPARE(KeySequenceCodec::normalize(QStringLiteral("  ctrl + shift + s ")),
             QStringLiteral("Ctrl+Shift+S"));
}

void TstKeySeqCodec::rejectsInvalid()
{
    QVERIFY(!KeySequenceCodec::parse(QStringLiteral("")).valid);          // empty
    QVERIFY(!KeySequenceCodec::parse(QStringLiteral("Ctrl+Alt")).valid);  // no key
    QVERIFY(!KeySequenceCodec::parse(QStringLiteral("Ctrl+A+B")).valid);  // two keys
    QVERIFY(KeySequenceCodec::normalize(QStringLiteral("nope")).isEmpty());
}

void TstKeySeqCodec::modifierOnlyRoundTrips()
{
    // Quick-snip hold-and-drag triggers are modifier-only (no key).
    QCOMPARE(KeySequenceCodec::modsFromString(QStringLiteral("Ctrl+Alt")),
             Qt::KeyboardModifiers(Qt::ControlModifier | Qt::AltModifier));
    QCOMPARE(KeySequenceCodec::modsFromString(QStringLiteral("win")), Qt::KeyboardModifiers(Qt::MetaModifier));
    // Canonical order Ctrl, Shift, Alt, Win, independent of input order.
    QCOMPARE(KeySequenceCodec::modsToString(Qt::AltModifier | Qt::ControlModifier),
             QStringLiteral("Ctrl+Alt"));
    QCOMPARE(KeySequenceCodec::modsToString(Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier),
             QStringLiteral("Ctrl+Alt+Win"));
    QCOMPARE(KeySequenceCodec::modsToString(Qt::NoModifier), QString());
}

QTEST_APPLESS_MAIN(TstKeySeqCodec)
#include "tst_keyseq_codec.moc"
