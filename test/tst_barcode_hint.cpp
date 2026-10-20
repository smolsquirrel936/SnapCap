#include <QtTest>

#include "core/barcode/BarcodeHint.h"

// Display logic for the snip code hint: which payloads Ctrl+click may open,
// and the host + "…" + tail elision that fits a link into the selection's width.
class TstBarcodeHint : public QObject
{
    Q_OBJECT
private slots:
    void onlyHttpAndHttpsAreOpenable();
    void singleLineCollapsesWhitespace();
    void shortPayloadIsUntouched();
    void schemeDroppedBeforeAnyEllipsis();
    void longUrlKeepsHostAndTail();
    void nonUrlPayloadClipsFromTheTail();
    void tinyBudgetDegradesGracefully();
};

// A measurer with a fixed 10px advance per character, so expected widths in the
// tests are just `10 * length` — the real view wraps QFontMetrics instead.
static const int kCharPx = 10;
static BarcodeHint::Measure fixedWidth()
{
    return [](const QString& s) { return kCharPx * int(s.size()); };
}

void TstBarcodeHint::onlyHttpAndHttpsAreOpenable()
{
    QVERIFY(BarcodeHint::isOpenableUrl(QStringLiteral("https://example.com/a")));
    QVERIFY(BarcodeHint::isOpenableUrl(QStringLiteral("http://example.com")));
    QVERIFY(BarcodeHint::isOpenableUrl(QStringLiteral("HTTPS://Example.COM")));   // case-insensitive

    // Copyable, but not openable — the user chose "copy only" for non-web payloads.
    QVERIFY(!BarcodeHint::isOpenableUrl(QStringLiteral("mailto:someone@example.com")));
    QVERIFY(!BarcodeHint::isOpenableUrl(QStringLiteral("WIFI:S:MyNet;T:WPA;P:hunter2;;")));
    QVERIFY(!BarcodeHint::isOpenableUrl(QStringLiteral("just some plain text")));
    QVERIFY(!BarcodeHint::isOpenableUrl(QString()));
    QVERIFY(!BarcodeHint::isOpenableUrl(QStringLiteral("https://")));             // no host
}

void TstBarcodeHint::singleLineCollapsesWhitespace()
{
    QCOMPARE(BarcodeHint::singleLine(QStringLiteral("BEGIN:VCARD\nFN:Ada\r\nEND:VCARD")),
             QStringLiteral("BEGIN:VCARD FN:Ada END:VCARD"));
    QCOMPARE(BarcodeHint::singleLine(QStringLiteral("  padded\ttext  ")),
             QStringLiteral("padded text"));
}

void TstBarcodeHint::shortPayloadIsUntouched()
{
    // 21 chars * 10px = 210 <= 400, so it survives with its scheme intact.
    const QString url = QStringLiteral("https://example.com/a");
    QCOMPARE(BarcodeHint::elide(url, 400, fixedWidth()), url);
}

void TstBarcodeHint::schemeDroppedBeforeAnyEllipsis()
{
    // Doesn't fit whole (210 > 150) but does without the scheme (13*10=130 <= 150).
    const QString url = QStringLiteral("https://example.com/a");
    QCOMPARE(BarcodeHint::elide(url, 150, fixedWidth()), QStringLiteral("example.com/a"));
}

void TstBarcodeHint::longUrlKeepsHostAndTail()
{
    const QString url =
        QStringLiteral("https://github.com/anthropics/claude-code/issues/12345");
    // Budget 240px = 24 chars. "github.com/…" is 12, leaving 12 for the tail.
    const QString out = BarcodeHint::elide(url, 240, fixedWidth());
    QVERIFY2(out.size() * kCharPx <= 240, qPrintable(out));
    QVERIFY2(out.startsWith(QStringLiteral("github.com/…")), qPrintable(out));
    QVERIFY2(out.endsWith(QStringLiteral("12345")), qPrintable(out));      // tail preserved
    QCOMPARE(out, QStringLiteral("github.com/…issues/12345"));
}

void TstBarcodeHint::nonUrlPayloadClipsFromTheTail()
{
    // No host to keep, so it degrades to a plain trailing-ellipsis clip.
    const QString text = QStringLiteral("the quick brown fox jumps over the lazy dog");
    const QString out = BarcodeHint::elide(text, 100, fixedWidth());
    QCOMPARE(out, QStringLiteral("the quick…"));                            // 10 chars
    QVERIFY(out.size() * kCharPx <= 100);
}

void TstBarcodeHint::tinyBudgetDegradesGracefully()
{
    const QString url = QStringLiteral("https://github.com/anthropics/claude-code");
    // 30px = 3 chars: too tight for "github.com/…", so it falls back to a clip.
    const QString out = BarcodeHint::elide(url, 30, fixedWidth());
    QVERIFY2(out.size() * kCharPx <= 30, qPrintable(out));
    QVERIFY2(out.endsWith(QStringLiteral("…")), qPrintable(out));

    // And a budget with room for nothing at all yields an empty string rather
    // than a bare ellipsis the caller would have to special-case.
    QVERIFY(BarcodeHint::elide(url, 5, fixedWidth()).isEmpty());
    QVERIFY(BarcodeHint::elide(url, 0, fixedWidth()).isEmpty());
    QVERIFY(BarcodeHint::elide(QString(), 500, fixedWidth()).isEmpty());
}

QTEST_MAIN(TstBarcodeHint)
#include "tst_barcode_hint.moc"
