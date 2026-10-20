#include <QtTest>
#include <QSignalSpy>
#include <QJsonArray>
#include <QJsonObject>

#include "core/GroupRegistry.h"

// The named-group bookkeeping: create/rename/remove, id allocation (never reused),
// the shown set (any number of groups at once), and JSON round-trip. The heavy
// window/disk work lives in the app layer and is verified manually — here we only
// assert the data + signals.
class TstGroupRegistry : public QObject
{
    Q_OBJECT
private slots:
    void createAllocatesAndSignals();
    void renameChangesName();
    void setShownEmitsAndToggles();
    void multipleGroupsShownAtOnce();
    void removeEmitsAndUnshows();
    void removeOtherKeepsShown();
    void jsonRoundTrip();
    void fromJsonReadsLegacyActiveId();
    void fromJsonReusesNoIds();
};

void TstGroupRegistry::createAllocatesAndSignals()
{
    GroupRegistry r;
    QSignalSpy changed(&r, &GroupRegistry::changed);
    const int a = r.create(QStringLiteral("Work"));
    const int b = r.create(QStringLiteral("Home"));
    QCOMPARE(a, 1);
    QCOMPARE(b, 2);
    QCOMPARE(r.count(), 2);
    QCOMPARE(r.name(a), QStringLiteral("Work"));
    QVERIFY(r.shownIds().isEmpty());    // creating does not show
    QCOMPARE(changed.count(), 2);
}

void TstGroupRegistry::renameChangesName()
{
    GroupRegistry r;
    const int a = r.create(QStringLiteral("Work"));
    QVERIFY(r.rename(a, QStringLiteral("Office")));
    QCOMPARE(r.name(a), QStringLiteral("Office"));
    QVERIFY(!r.rename(999, QStringLiteral("Nope")));
}

void TstGroupRegistry::setShownEmitsAndToggles()
{
    GroupRegistry r;
    const int a = r.create(QStringLiteral("Work"));
    QSignalSpy shown(&r, &GroupRegistry::shown);
    QSignalSpy hidden(&r, &GroupRegistry::hidden);

    r.setShown(a, true);
    QVERIFY(r.isShown(a));
    QCOMPARE(shown.count(), 1);
    QCOMPARE(shown.takeFirst().at(0).toInt(), a);

    r.setShown(a, true);                // no-op when already shown
    QCOMPARE(shown.count(), 0);

    r.setShown(a, false);
    QVERIFY(!r.isShown(a));
    QCOMPARE(hidden.count(), 1);
    QCOMPARE(hidden.takeFirst().at(0).toInt(), a);

    r.setShown(a, false);               // no-op when already hidden
    QCOMPARE(hidden.count(), 0);

    r.setShown(999, true);              // unknown id ignored
    r.setShown(0, true);                // 0 is the ungrouped set, not a group
    QVERIFY(r.shownIds().isEmpty());
    QCOMPARE(shown.count(), 0);
}

// The point of the feature: showing one group must not hide any other.
void TstGroupRegistry::multipleGroupsShownAtOnce()
{
    GroupRegistry r;
    const int a = r.create(QStringLiteral("Work"));
    const int b = r.create(QStringLiteral("Home"));
    const int c = r.create(QStringLiteral("Trip"));
    QSignalSpy hidden(&r, &GroupRegistry::hidden);

    r.setShown(a, true);
    r.setShown(b, true);
    r.setShown(c, true);
    QCOMPARE(r.shownIds(), QVector<int>({a, b, c}));   // insertion order
    QCOMPARE(hidden.count(), 0);                       // nothing was evicted

    r.setShown(b, false);                              // hiding one leaves the rest alone
    QCOMPARE(r.shownIds(), QVector<int>({a, c}));
    QVERIFY(r.isShown(a));
    QVERIFY(r.isShown(c));
}

void TstGroupRegistry::removeEmitsAndUnshows()
{
    GroupRegistry r;
    const int a = r.create(QStringLiteral("Work"));
    r.setShown(a, true);
    QSignalSpy removed(&r, &GroupRegistry::removed);
    QSignalSpy hidden(&r, &GroupRegistry::hidden);
    QVERIFY(r.remove(a));
    QCOMPARE(removed.count(), 1);
    QCOMPARE(removed.takeFirst().at(0).toInt(), a);
    QCOMPARE(r.count(), 0);
    QVERIFY(!r.isShown(a));             // removing a shown group drops it from the shown set
    // removed() means "discard the live pins", so no hidden() (which would serialize them).
    QCOMPARE(hidden.count(), 0);
    QVERIFY(!r.remove(a));              // already gone
}

void TstGroupRegistry::removeOtherKeepsShown()
{
    GroupRegistry r;
    const int a = r.create(QStringLiteral("Work"));
    const int b = r.create(QStringLiteral("Home"));
    r.setShown(a, true);
    QVERIFY(r.remove(b));
    QVERIFY(r.isShown(a));              // unrelated removal leaves the shown set intact
}

void TstGroupRegistry::jsonRoundTrip()
{
    GroupRegistry r;
    const int a = r.create(QStringLiteral("Work"));
    const int b = r.create(QStringLiteral("Home"));
    r.create(QStringLiteral("Trip"));
    r.setShown(a, true);
    r.setShown(b, true);                // the multi-show set must survive a round-trip
    const QJsonObject o = r.toJson();

    GroupRegistry r2;
    r2.fromJson(o);
    QCOMPARE(r2.count(), 3);
    QCOMPARE(r2.name(a), QStringLiteral("Work"));
    QCOMPARE(r2.shownIds(), QVector<int>({a, b}));
    // Next create must not collide with a restored id.
    QCOMPARE(r2.create(QStringLiteral("New")), 4);
}

// A groups.json written before multi-show carries a scalar "activeId" — it must still
// restore as a one-group shown set rather than silently showing nothing.
void TstGroupRegistry::fromJsonReadsLegacyActiveId()
{
    QJsonObject legacy;
    QJsonArray groups;
    QJsonObject g;
    g[QStringLiteral("id")]   = 2;
    g[QStringLiteral("name")] = QStringLiteral("Home");
    groups.append(g);
    legacy[QStringLiteral("groups")]   = groups;
    legacy[QStringLiteral("nextId")]   = 3;
    legacy[QStringLiteral("activeId")] = 2;

    GroupRegistry r;
    r.fromJson(legacy);
    QCOMPARE(r.shownIds(), QVector<int>({2}));

    // A legacy file with nothing active restores an empty shown set (not a bogus id 0).
    legacy[QStringLiteral("activeId")] = 0;
    GroupRegistry r2;
    r2.fromJson(legacy);
    QVERIFY(r2.shownIds().isEmpty());
}

void TstGroupRegistry::fromJsonReusesNoIds()
{
    GroupRegistry r;
    r.create(QStringLiteral("A"));      // id 1
    const int b = r.create(QStringLiteral("B"));   // id 2
    r.remove(b);                        // drop id 2
    GroupRegistry r2;
    r2.fromJson(r.toJson());
    // The gap left by the removed id must not be handed out again.
    QCOMPARE(r2.create(QStringLiteral("C")), 3);
}

QTEST_GUILESS_MAIN(TstGroupRegistry)
#include "tst_group_registry.moc"
