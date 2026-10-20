#include <QtTest>
#include <QSignalSpy>

#include "core/SelectionModel.h"

// Stage 2: the pure selection state machine (no screen, no GUI).
class TstSelection : public QObject
{
    Q_OBJECT
private slots:
    void dragBuildsNormalizedRect();
    void dragClampsToBounds();
    void growAndShrinkEdges();
    void growStopsAtBounds();
    void shrinkKeepsMinSize();
    void moveByClampsInside();
    void fullscreenAndRepeatLast();
    void emitsChangedOnlyOnRealChange();

private:
    static QRect desktop() { return QRect(0, 0, 1920, 1080); }
};

void TstSelection::dragBuildsNormalizedRect()
{
    SelectionModel m;
    m.setBounds(desktop());
    m.beginAt({100, 100});
    m.dragTo({300, 250});
    QCOMPARE(m.rect(), QRect(100, 100, 201, 151));   // QRect width = right-left+1

    // Dragging up-left yields the same normalized rect.
    m.beginAt({300, 250});
    m.dragTo({100, 100});
    QCOMPARE(m.rect(), QRect(100, 100, 201, 151));
    QVERIFY(m.isValid());
}

void TstSelection::dragClampsToBounds()
{
    SelectionModel m;
    m.setBounds(desktop());
    m.beginAt({100, 100});
    m.dragTo({5000, 5000});
    QCOMPARE(m.rect(), QRect(100, 100, 1820, 980));  // clamped to 1919/1079 edges
}

void TstSelection::growAndShrinkEdges()
{
    SelectionModel m;
    m.setBounds(desktop());
    m.setRect(QRect(100, 100, 200, 200));            // right=299, bottom=299
    m.grow(SelectionModel::Edge::Right);
    QCOMPARE(m.rect().width(), 201);
    m.shrink(SelectionModel::Edge::Right);
    QCOMPARE(m.rect().width(), 200);
    m.grow(SelectionModel::Edge::Left);
    QCOMPARE(m.rect().left(), 99);
    m.grow(SelectionModel::Edge::Top);
    QCOMPARE(m.rect().top(), 99);
}

void TstSelection::growStopsAtBounds()
{
    SelectionModel m;
    m.setBounds(desktop());
    m.setRect(QRect(0, 0, 10, 10));                  // touches left/top edges
    m.grow(SelectionModel::Edge::Left);
    QCOMPARE(m.rect().left(), 0);                    // cannot grow past bounds
    m.grow(SelectionModel::Edge::Top);
    QCOMPARE(m.rect().top(), 0);
}

void TstSelection::shrinkKeepsMinSize()
{
    SelectionModel m;
    m.setBounds(desktop());
    m.setRect(QRect(50, 50, 1, 1));
    m.shrink(SelectionModel::Edge::Right);
    QCOMPARE(m.rect().width(), 1);                   // refuses to go below 1px
    m.shrink(SelectionModel::Edge::Bottom);
    QCOMPARE(m.rect().height(), 1);
}

void TstSelection::moveByClampsInside()
{
    SelectionModel m;
    m.setBounds(desktop());
    m.setRect(QRect(1000, 1000, 50, 50));
    m.moveBy(100000, 0);                             // shove far right
    QCOMPARE(m.rect(), QRect(1870, 1000, 50, 50));   // size kept, snapped inside
}

void TstSelection::fullscreenAndRepeatLast()
{
    SelectionModel m;
    m.setBounds(desktop());

    m.beginAt({10, 10});
    m.dragTo({60, 40});
    m.commit();
    const QRect committed = m.rect();

    m.selectFullscreen();
    QCOMPARE(m.rect(), desktop());

    m.repeatLast();
    QCOMPARE(m.rect(), committed);
}

void TstSelection::emitsChangedOnlyOnRealChange()
{
    SelectionModel m;
    m.setBounds(desktop());
    m.setRect(QRect(0, 0, 100, 100));
    QSignalSpy spy(&m, &SelectionModel::changed);
    m.setRect(QRect(0, 0, 100, 100));                // identical -> no emit
    QCOMPARE(spy.count(), 0);
    m.setRect(QRect(0, 0, 120, 100));                // different -> 1 emit
    QCOMPARE(spy.count(), 1);
}

QTEST_GUILESS_MAIN(TstSelection)
#include "tst_selection.moc"
