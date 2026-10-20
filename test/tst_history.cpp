#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QImage>
#include <QFile>

#include "core/History.h"
#include "core/ScreenComposite.h"   // ScreenShot

// Stage 6: the bounded snip-history ring. record() persists a full image +
// thumbnail and prepends it (newest first); the ring caps at max (oldest evicted);
// entries survive a restart (re-discovered from disk); prev()/next() back the
// in-snip `,`/`.` replay cursor.
class TstHistory : public QObject
{
    Q_OBJECT
private:
    static QImage img(int gray)
    {
        QImage i(8, 8, QImage::Format_ARGB32);
        i.fill(QColor(gray, gray, gray));
        return i;
    }

    // A one-monitor freeze for the full-screen-replay tests.
    static ScreenShot screen(const QRect& geo, qreal dpr, int gray)
    {
        ScreenShot s;
        s.geo = geo;
        s.dpr = dpr;
        s.shot = img(gray);
        return s;
    }

private slots:
    void recordPrependsNewestFirst();
    void capsAtMaxEvictingOldest();
    void persistsAcrossReopen();
    void maxZeroDisablesHistory();
    void replayCursorWalksOlderThenNewer();
    void clearEmptiesRing();
    void storesAndRestoresFullScreen();
    void evictionRemovesFreezeFiles();
};

void TstHistory::recordPrependsNewestFirst()
{
    QTemporaryDir dir;
    History h(dir.path(), 10);
    QSignalSpy spy(&h, &History::changed);
    QVERIFY(!h.record(img(10)).isEmpty());
    QVERIFY(!h.record(img(20)).isEmpty());
    QCOMPARE(h.count(), 2);
    QVERIFY(spy.count() >= 2);
    // Newest (gray 20) is at index 0.
    QCOMPARE(h.fullAt(0).pixelColor(0, 0), QColor(20, 20, 20));
    QCOMPARE(h.fullAt(1).pixelColor(0, 0), QColor(10, 10, 10));
}

void TstHistory::capsAtMaxEvictingOldest()
{
    QTemporaryDir dir;
    History h(dir.path(), 2);
    h.record(img(10));
    h.record(img(20));
    h.record(img(30));
    QCOMPARE(h.count(), 2);
    QCOMPARE(h.fullAt(0).pixelColor(0, 0), QColor(30, 30, 30));   // newest
    QCOMPARE(h.fullAt(1).pixelColor(0, 0), QColor(20, 20, 20));   // 10 evicted
}

void TstHistory::persistsAcrossReopen()
{
    QTemporaryDir dir;
    {
        History h(dir.path(), 5);
        h.record(img(10));
        h.record(img(20));
    }
    History reopened(dir.path(), 5);
    QCOMPARE(reopened.count(), 2);
    QCOMPARE(reopened.fullAt(0).pixelColor(0, 0), QColor(20, 20, 20));  // newest first
}

void TstHistory::maxZeroDisablesHistory()
{
    QTemporaryDir dir;
    History h(dir.path(), 0);
    QVERIFY(h.record(img(10)).isEmpty());
    QCOMPARE(h.count(), 0);
}

void TstHistory::replayCursorWalksOlderThenNewer()
{
    QTemporaryDir dir;
    History h(dir.path(), 5);
    h.record(img(10));   // oldest
    h.record(img(20));
    h.record(img(30));   // newest

    QCOMPARE(h.prev().pixelColor(0, 0), QColor(30, 30, 30));   // first prev → newest
    QCOMPARE(h.prev().pixelColor(0, 0), QColor(20, 20, 20));
    QCOMPARE(h.prev().pixelColor(0, 0), QColor(10, 10, 10));   // oldest
    QVERIFY(h.prev().isNull());                                // clamped at the oldest end
    QCOMPARE(h.next().pixelColor(0, 0), QColor(20, 20, 20));   // walk back toward newer
}

void TstHistory::clearEmptiesRing()
{
    QTemporaryDir dir;
    History h(dir.path(), 5);
    h.record(img(10));
    h.record(img(20));
    h.clear();
    QCOMPARE(h.count(), 0);
    QVERIFY(h.fullAt(0).isNull());
}

void TstHistory::storesAndRestoresFullScreen()
{
    QTemporaryDir dir;
    const QRect sel(10, 20, 30, 40);
    {
        History h(dir.path(), 5);
        const QList<ScreenShot> shots = {screen(QRect(0, 0, 100, 80), 1.0, 40),
                                         screen(QRect(100, 0, 60, 80), 2.0, 50)};
        QVERIFY(!h.record(img(60), shots, sel).isEmpty());
        h.waitForWrites();             // the full-screen PNGs + sidecar write in the background

        QCOMPARE(h.selectionAt(0), sel);
        const QList<ScreenShot> got = h.screensAt(0);
        QCOMPARE(got.size(), 2);
        QCOMPARE(got[0].geo, QRect(0, 0, 100, 80));
        QCOMPARE(got[0].dpr, 1.0);
        QCOMPARE(got[0].shot.pixelColor(0, 0), QColor(40, 40, 40));
        QCOMPARE(got[1].geo, QRect(100, 0, 60, 80));
        QCOMPARE(got[1].dpr, 2.0);
        QCOMPARE(got[1].shot.pixelColor(0, 0), QColor(50, 50, 50));
    }
    // The freeze + area survive a restart (re-discovered from the sidecar on disk).
    History reopened(dir.path(), 5);
    QCOMPARE(reopened.selectionAt(0), sel);
    const QList<ScreenShot> got = reopened.screensAt(0);
    QCOMPARE(got.size(), 2);
    QCOMPARE(got[1].shot.pixelColor(0, 0), QColor(50, 50, 50));
}

void TstHistory::evictionRemovesFreezeFiles()
{
    QTemporaryDir dir;
    History h(dir.path(), 1);
    const QList<ScreenShot> shots = {screen(QRect(0, 0, 10, 10), 1.0, 10)};
    h.record(img(10), shots, QRect(1, 1, 2, 2));
    const QString freeze = h.entries().first().screens.first().imagePath;
    h.waitForWrites();
    QVERIFY(QFile::exists(freeze));

    h.record(img(20), shots, QRect(1, 1, 2, 2));   // evicts the first (max = 1)
    h.waitForWrites();                             // let the background delete run
    QCOMPARE(h.count(), 1);
    QVERIFY(!QFile::exists(freeze));               // evicted entry's freeze image deleted
}

QTEST_GUILESS_MAIN(TstHistory)
#include "tst_history.moc"
