#include <QtTest>
#include <QSignalSpy>
#include <QImage>

#include "core/PinManager.h"
#include "core/PinModel.h"

// Stage 5: the recover ring + active list bookkeeping. close() makes a pin
// recoverable (bounded by max, oldest evicted), destroy() is permanent, and
// recoverOldest() brings back the oldest still-recoverable pin (FIFO). added()/removed()
// drive the UI glue, so their emission is asserted too.
class TstRecoverRing : public QObject
{
    Q_OBJECT
private:
    static QImage img() { QImage i(2, 2, QImage::Format_ARGB32); i.fill(Qt::gray); return i; }

private slots:
    void addEmitsAndCounts();
    void closeIsRecoverableWithinMax();
    void closeEvictsOldestPastMax();
    void recoverBringsBackOne();
    void recoverIsOldestFirst();
    void destroyIsPermanent();
    void maxZeroNeverRecovers();
};

void TstRecoverRing::addEmitsAndCounts()
{
    PinManager mgr;
    QSignalSpy added(&mgr, &PinManager::added);
    mgr.addImage(img());
    mgr.addImage(img());
    QCOMPARE(mgr.activeCount(), 2);
    QCOMPARE(mgr.recoverableCount(), 0);
    QCOMPARE(added.count(), 2);
    QVERIFY(mgr.addImage(QImage()) == nullptr);     // null image ignored
    QCOMPARE(mgr.activeCount(), 2);
}

void TstRecoverRing::closeIsRecoverableWithinMax()
{
    PinManager mgr;
    mgr.setMaxRecoverable(1);
    PinModel* a = mgr.addImage(img());
    QSignalSpy removed(&mgr, &PinManager::removed);
    mgr.close(a);
    QCOMPARE(removed.count(), 1);
    QCOMPARE(mgr.activeCount(), 0);
    QCOMPARE(mgr.recoverableCount(), 1);
}

void TstRecoverRing::closeEvictsOldestPastMax()
{
    PinManager mgr;
    mgr.setMaxRecoverable(1);
    PinModel* a = mgr.addImage(img());
    PinModel* b = mgr.addImage(img());
    mgr.close(a);
    mgr.close(b);
    QCOMPARE(mgr.activeCount(), 0);
    QCOMPARE(mgr.recoverableCount(), 1);            // ring capped at 1
}

void TstRecoverRing::recoverBringsBackOne()
{
    PinManager mgr;
    mgr.setMaxRecoverable(2);
    PinModel* a = mgr.addImage(img());
    mgr.close(a);
    QSignalSpy added(&mgr, &PinManager::added);
    PinModel* r = mgr.recoverOldest();
    QVERIFY(r != nullptr);
    QCOMPARE(added.count(), 1);
    QCOMPARE(mgr.activeCount(), 1);
    QCOMPARE(mgr.recoverableCount(), 0);
}

void TstRecoverRing::recoverIsOldestFirst()
{
    PinManager mgr;
    mgr.setMaxRecoverable(3);
    PinModel* a = mgr.addImage(img());
    PinModel* b = mgr.addImage(img());
    PinModel* c = mgr.addImage(img());
    mgr.close(a);                                   // close order a → b → c (a oldest)
    mgr.close(b);
    mgr.close(c);
    QCOMPARE(mgr.recoverOldest(), a);               // oldest-closed comes back first
    QCOMPARE(mgr.recoverOldest(), b);
    QCOMPARE(mgr.recoverOldest(), c);
    QCOMPARE(mgr.recoverableCount(), 0);
    QVERIFY(mgr.recoverOldest() == nullptr);        // ring drained
}

void TstRecoverRing::destroyIsPermanent()
{
    PinManager mgr;
    mgr.setMaxRecoverable(2);
    PinModel* a = mgr.addImage(img());
    mgr.destroy(a);
    QCOMPARE(mgr.activeCount(), 0);
    QCOMPARE(mgr.recoverableCount(), 0);
    QVERIFY(mgr.recoverOldest() == nullptr);
}

void TstRecoverRing::maxZeroNeverRecovers()
{
    PinManager mgr;
    mgr.setMaxRecoverable(0);
    PinModel* a = mgr.addImage(img());
    mgr.close(a);
    QCOMPARE(mgr.activeCount(), 0);
    QCOMPARE(mgr.recoverableCount(), 0);
}

QTEST_GUILESS_MAIN(TstRecoverRing)
#include "tst_recover_ring.moc"
