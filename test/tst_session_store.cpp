#include <QtTest>
#include <QTemporaryDir>
#include <QImage>
#include <QJsonObject>

#include "core/SessionStore.h"
#include "core/PinModel.h"

// Stage 6: SessionStore backs up the open pins (pixels + transform state +
// geometry + annotations) to a folder and restores them losslessly across a
// restart, resolving the Stage 5 "recovered pin loses position" deferral.
class TstSessionStore : public QObject
{
    Q_OBJECT
private:
    static QImage img(int w, int h, QColor c)
    {
        QImage i(w, h, QImage::Format_ARGB32);
        i.fill(c);
        return i;
    }

private slots:
    void emptyByDefault();
    void roundTripsTransformsAndGeometry();
    void emptySaveClearsSession();
    void staleSidecarsRemovedOnResave();
};

void TstSessionStore::emptyByDefault()
{
    QTemporaryDir dir;
    SessionStore store(dir.filePath(QStringLiteral("session")));
    QVERIFY(!store.hasSession());
    QVERIFY(store.load().isEmpty());
}

void TstSessionStore::roundTripsTransformsAndGeometry()
{
    QTemporaryDir dir;
    SessionStore store(dir.filePath(QStringLiteral("session")));

    PinModel a(img(4, 6, Qt::red), QStringLiteral("hello"));
    a.rotateCW();              // 90
    a.flipHorizontal();
    a.setScale(2.0);
    a.setOpacity(0.5);
    a.setGroup(3);
    a.setSourceDpr(1.5);       // capture dpr — must survive so the pin keeps its logical size

    PinModel b(img(8, 2, Qt::blue));

    QVector<PinSnapshot> in;
    in.append({a.source(), a.toJson(), QRect(10, 20, 100, 80), QJsonObject{}});
    in.append({b.source(), b.toJson(), QRect(200, 50, 60, 60), QJsonObject{}});
    QVERIFY(store.save(in));
    QVERIFY(store.hasSession());

    const QVector<PinSnapshot> out = store.load();
    QCOMPARE(out.size(), 2);

    // First pin: transforms + geometry + pixels survive.
    PinModel ra(out[0].image);
    ra.fromJson(out[0].transform);
    QCOMPARE(out[0].image.size(), QSize(4, 6));
    QCOMPARE(out[0].image.pixelColor(0, 0), QColor(Qt::red));
    QCOMPARE(ra.rotation(), 90);
    QVERIFY(ra.flippedH());
    QVERIFY(!ra.flippedV());
    QCOMPARE(ra.scale(), 2.0);
    QCOMPARE(ra.opacity(), 0.5);
    QCOMPARE(ra.group(), 3);
    QCOMPARE(ra.sourceDpr(), 1.5);
    QCOMPARE(ra.sourceText(), QStringLiteral("hello"));
    QCOMPARE(out[0].geometry, QRect(10, 20, 100, 80));

    // Second pin: defaults + its own geometry.
    PinModel rb(out[1].image);
    rb.fromJson(out[1].transform);
    QCOMPARE(out[1].image.size(), QSize(8, 2));
    QCOMPARE(rb.rotation(), 0);
    QCOMPARE(rb.scale(), 1.0);
    QCOMPARE(rb.sourceDpr(), 0.0);        // unset stays unset (clipboard/no-dpr images)
    QVERIFY(rb.sourceText().isEmpty());
    QCOMPARE(out[1].geometry, QRect(200, 50, 60, 60));
}

void TstSessionStore::emptySaveClearsSession()
{
    QTemporaryDir dir;
    SessionStore store(dir.filePath(QStringLiteral("session")));
    PinModel a(img(4, 4, Qt::green));
    store.save({{a.source(), a.toJson(), QRect(0, 0, 4, 4), QJsonObject{}}});
    QVERIFY(store.hasSession());

    store.clear();
    QVERIFY(!store.hasSession());
    QVERIFY(store.load().isEmpty());
}

void TstSessionStore::staleSidecarsRemovedOnResave()
{
    QTemporaryDir dir;
    SessionStore store(dir.filePath(QStringLiteral("session")));
    PinModel a(img(4, 4, Qt::green));
    // Save three, then re-save one: the load must reflect exactly one pin.
    store.save({{a.source(), a.toJson(), QRect(), QJsonObject{}},
                {a.source(), a.toJson(), QRect(), QJsonObject{}},
                {a.source(), a.toJson(), QRect(), QJsonObject{}}});
    QCOMPARE(store.load().size(), 3);
    store.save({{a.source(), a.toJson(), QRect(), QJsonObject{}}});
    QCOMPARE(store.load().size(), 1);
}

QTEST_GUILESS_MAIN(TstSessionStore)
#include "tst_session_store.moc"
