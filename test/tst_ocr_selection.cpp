#include <QtTest>

#include "core/ocr/OcrSelection.h"

using ocr::Span;

// Pure reading-order OCR text-selection math: hit-testing words, drag spans,
// select-all, highlight boxes, and text joining (line-break vs. space).
class TstOcrSelection : public QObject
{
    Q_OBJECT
private slots:
    void wordCountAndHitTest();
    void nearestExtendsPastBoxes();
    void spanBetweenIsNormalized();
    void selectAllSpansEverything();
    void boxesForSpanMatch();
    void textJoinsLinesAndSpaces();
    void cjkJoinsWithoutSpaces();
    void emptyResultIsSafe();

private:
    static platform::OcrResult sample();
};

// Two lines:
//   line 0: "Hello" [10,10 40x20]   "World" [60,10 40x20]
//   line 1: "Foo"   [10,40 30x20]   "Bar"   [50,40 30x20]   "Baz" [90,40 30x20]
// Flat reading order: Hello(0) World(1) Foo(2) Bar(3) Baz(4)
platform::OcrResult TstOcrSelection::sample()
{
    platform::OcrResult r;
    r.imageSize = QSize(200, 80);
    platform::OcrLine l0;
    l0.words = { { QRect(10, 10, 40, 20), "Hello" }, { QRect(60, 10, 40, 20), "World" } };
    platform::OcrLine l1;
    l1.words = { { QRect(10, 40, 30, 20), "Foo" },
                 { QRect(50, 40, 30, 20), "Bar" },
                 { QRect(90, 40, 30, 20), "Baz" } };
    r.lines = { l0, l1 };
    return r;
}

void TstOcrSelection::wordCountAndHitTest()
{
    const auto r = sample();
    QCOMPARE(ocr::wordCount(r), 5);
    QCOMPARE(ocr::wordAt(r, QPoint(65, 15)), 1);   // inside "World"
    QCOMPARE(ocr::wordAt(r, QPoint(95, 45)), 4);   // inside "Baz"
    QCOMPARE(ocr::wordAt(r, QPoint(55, 15)), -1);  // gap between "Hello"/"World"
}

void TstOcrSelection::nearestExtendsPastBoxes()
{
    const auto r = sample();
    QCOMPARE(ocr::nearestWord(r, QPoint(500, 45)), 4);   // far right of "Baz"
    QCOMPARE(ocr::nearestWord(r, QPoint(-50, 12)), 0);   // far left of "Hello"
}

void TstOcrSelection::spanBetweenIsNormalized()
{
    const auto r = sample();
    const Span fwd = ocr::spanBetween(r, QPoint(15, 15), QPoint(55, 45)); // Hello -> Bar
    QCOMPARE(fwd.first, 0);
    QCOMPARE(fwd.last, 3);
    // Reversed drag yields the same normalized span.
    const Span rev = ocr::spanBetween(r, QPoint(55, 45), QPoint(15, 15));
    QCOMPARE(rev.first, 0);
    QCOMPARE(rev.last, 3);
}

void TstOcrSelection::selectAllSpansEverything()
{
    const auto r = sample();
    const Span a = ocr::all(r);
    QCOMPARE(a.first, 0);
    QCOMPARE(a.last, 4);
    QCOMPARE(a.count(), 5);
}

void TstOcrSelection::boxesForSpanMatch()
{
    const auto r = sample();
    const QList<QRect> boxes = ocr::boxesForSpan(r, Span{ 0, 1 });
    QCOMPARE(boxes.size(), 2);
    QCOMPARE(boxes[0], QRect(10, 10, 40, 20));
    QCOMPARE(boxes[1], QRect(60, 10, 40, 20));
}

void TstOcrSelection::textJoinsLinesAndSpaces()
{
    const auto r = sample();
    const Span s{ 0, 3 };  // Hello World / Foo Bar
    QCOMPARE(ocr::textForSpan(r, s, /*keepLineBreaks*/ true), QString("Hello World\nFoo Bar"));
    QCOMPARE(ocr::textForSpan(r, s, /*keepLineBreaks*/ false), QString("Hello World Foo Bar"));
    QCOMPARE(ocr::textForSpan(r, ocr::all(r), true), QString("Hello World\nFoo Bar Baz"));
}

void TstOcrSelection::cjkJoinsWithoutSpaces()
{
    // CJK words carry no inter-word space; a Latin/CJK boundary also drops the space,
    // but two Latin tokens keep it. Line 0 is pure CJK, line 1 mixes a digit + CJK.
    platform::OcrResult r;
    r.imageSize = QSize(400, 80);
    platform::OcrLine l0;
    l0.words = { { QRect(10, 10, 30, 20), QString::fromUtf8("四种") },
                 { QRect(45, 10, 30, 20), QString::fromUtf8("背景") },
                 { QRect(80, 10, 30, 20), QString::fromUtf8("模式") } };
    platform::OcrLine l1;
    l1.words = { { QRect(10, 40, 15, 20), QString::fromUtf8("5") },
                 { QRect(30, 40, 30, 20), QString::fromUtf8("透明") } };
    r.lines = { l0, l1 };

    QCOMPARE(ocr::textForSpan(r, ocr::all(r), /*keepLineBreaks*/ true),
             QString::fromUtf8("四种背景模式\n5透明"));
    // Joined (no line breaks): the CJK boundary between lines also gets no space.
    QCOMPARE(ocr::textForSpan(r, ocr::all(r), /*keepLineBreaks*/ false),
             QString::fromUtf8("四种背景模式5透明"));
}

void TstOcrSelection::emptyResultIsSafe()
{
    const platform::OcrResult empty;
    QVERIFY(ocr::all(empty).isEmpty());
    QVERIFY(ocr::spanBetween(empty, QPoint(0, 0), QPoint(9, 9)).isEmpty());
    QCOMPARE(ocr::wordAt(empty, QPoint(0, 0)), -1);
    QCOMPARE(ocr::textForSpan(empty, ocr::all(empty), true), QString());
}

QTEST_APPLESS_MAIN(TstOcrSelection)
#include "tst_ocr_selection.moc"
