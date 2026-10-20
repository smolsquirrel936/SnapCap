#include <QtTest>
#include <QImage>

#include "core/ColorPicker.h"

// Stage 3: color formatting + sampling. Pure logic; the clipboard copy needs a
// GUI app and is exercised manually.
class TstColor : public QObject
{
    Q_OBJECT
private slots:
    void hexLowercase();
    void hexUppercase();
    void hexNoHash();
    void rgbTriplet();
    void hsvFormat();
    void hslFormat();
    void hsiFormat();
    void cmykFormat();
    void decFormat();
    void formatToggleCycles();
    void codeRoundTrip();
    void parseFormatsList();
    void sampleReadsPixel();
    void sampleClampsOutOfBounds();
    void sampleNullImage();
    void basicPaletteShape();
    void parseHtmlForms();
    void parseHtmlRejectsBadInput();
    void htmlRoundTrip();
};

void TstColor::hexLowercase()
{
    QCOMPARE(ColorPicker::format(QColor(0xAB, 0xCD, 0xEF), ColorPicker::Format::Hex, false),
             QStringLiteral("#abcdef"));
}

void TstColor::hexUppercase()
{
    QCOMPARE(ColorPicker::format(QColor(0xAB, 0xCD, 0xEF), ColorPicker::Format::Hex, true),
             QStringLiteral("#ABCDEF"));
}

void TstColor::hexNoHash()
{
    // noHash drops the leading '#'; uppercase still applies.
    QCOMPARE(ColorPicker::format(QColor(0xAB, 0xCD, 0xEF), ColorPicker::Format::Hex, false, true),
             QStringLiteral("abcdef"));
    QCOMPARE(ColorPicker::format(QColor(0xAB, 0xCD, 0xEF), ColorPicker::Format::Hex, true, true),
             QStringLiteral("ABCDEF"));
}

void TstColor::rgbTriplet()
{
    // Uppercase flag must not affect RGB output.
    QCOMPARE(ColorPicker::format(QColor(255, 128, 0), ColorPicker::Format::Rgb, true),
             QStringLiteral("255, 128, 0"));
    QCOMPARE(ColorPicker::format(QColor(0, 0, 0), ColorPicker::Format::Rgb, false),
             QStringLiteral("0, 0, 0"));
}

void TstColor::hsvFormat()
{
    // Pure red: H=0, S=100%, V=100%.
    QCOMPARE(ColorPicker::format(QColor(255, 0, 0), ColorPicker::Format::Hsv, false),
             QStringLiteral("0, 100%, 100%"));
    // Black: achromatic hue reported as 0, S=0, V=0.
    QCOMPARE(ColorPicker::format(QColor(0, 0, 0), ColorPicker::Format::Hsv, false),
             QStringLiteral("0, 0%, 0%"));
}

void TstColor::hslFormat()
{
    // Pure red: H=0, S=100%, L=50%.
    QCOMPARE(ColorPicker::format(QColor(255, 0, 0), ColorPicker::Format::Hsl, false),
             QStringLiteral("0, 100%, 50%"));
    // White: L=100%, S=0.
    QCOMPARE(ColorPicker::format(QColor(255, 255, 255), ColorPicker::Format::Hsl, false),
             QStringLiteral("0, 0%, 100%"));
}

void TstColor::hsiFormat()
{
    // Pure red: H=0, S=100% (sits on a primary, so fully off the gray axis),
    // I = (255+0+0)/3 / 255 = 33%.
    QCOMPARE(ColorPicker::format(QColor(255, 0, 0), ColorPicker::Format::Hsi, false),
             QStringLiteral("0, 100%, 33%"));
    // Gray is achromatic: hue undefined -> 0, S=0, I tracks the level.
    QCOMPARE(ColorPicker::format(QColor(128, 128, 128), ColorPicker::Format::Hsi, false),
             QStringLiteral("0, 0%, 50%"));
    // Black: sum is 0, so the S divisor guard must not produce NaN.
    QCOMPARE(ColorPicker::format(QColor(0, 0, 0), ColorPicker::Format::Hsi, false),
             QStringLiteral("0, 0%, 0%"));
    // Blue sits past green on the hue circle -> the 360-theta branch.
    QCOMPARE(ColorPicker::format(QColor(0, 0, 255), ColorPicker::Format::Hsi, false),
             QStringLiteral("240, 100%, 33%"));
}

void TstColor::cmykFormat()
{
    // Pure red -> C0 M100 Y100 K0.
    QCOMPARE(ColorPicker::format(QColor(255, 0, 0), ColorPicker::Format::Cmyk, false),
             QStringLiteral("0%, 100%, 100%, 0%"));
    // Black -> K100.
    QCOMPARE(ColorPicker::format(QColor(0, 0, 0), ColorPicker::Format::Cmyk, false),
             QStringLiteral("0%, 0%, 0%, 100%"));
}

void TstColor::decFormat()
{
    // 24-bit packed integer r*65536 + g*256 + b.
    QCOMPARE(ColorPicker::format(QColor(0xAB, 0xCD, 0xEF), ColorPicker::Format::Dec, false),
             QStringLiteral("11259375"));
    QCOMPARE(ColorPicker::format(QColor(255, 255, 255), ColorPicker::Format::Dec, false),
             QStringLiteral("16777215"));
    QCOMPARE(ColorPicker::format(QColor(0, 0, 0), ColorPicker::Format::Dec, false),
             QStringLiteral("0"));
}

void TstColor::formatToggleCycles()
{
    // Full-enum cycle: Hex -> Rgb -> Hsv -> Hsl -> Hsi -> Cmyk -> Dec -> Hex.
    QCOMPARE(ColorPicker::nextFormat(ColorPicker::Format::Hex),  ColorPicker::Format::Rgb);
    QCOMPARE(ColorPicker::nextFormat(ColorPicker::Format::Rgb),  ColorPicker::Format::Hsv);
    QCOMPARE(ColorPicker::nextFormat(ColorPicker::Format::Hsv),  ColorPicker::Format::Hsl);
    QCOMPARE(ColorPicker::nextFormat(ColorPicker::Format::Hsl),  ColorPicker::Format::Hsi);
    QCOMPARE(ColorPicker::nextFormat(ColorPicker::Format::Hsi),  ColorPicker::Format::Cmyk);
    QCOMPARE(ColorPicker::nextFormat(ColorPicker::Format::Cmyk), ColorPicker::Format::Dec);
    QCOMPARE(ColorPicker::nextFormat(ColorPicker::Format::Dec),  ColorPicker::Format::Hex);
}

void TstColor::codeRoundTrip()
{
    QSet<QString> seen;
    for (ColorPicker::Format f : {ColorPicker::Format::Hex, ColorPicker::Format::Rgb,
                                  ColorPicker::Format::Hsv, ColorPicker::Format::Hsl,
                                  ColorPicker::Format::Hsi, ColorPicker::Format::Cmyk,
                                  ColorPicker::Format::Dec}) {
        const auto parsed = ColorPicker::parseFormats(ColorPicker::codeFor(f));
        QCOMPARE(parsed.size(), 1);
        QCOMPARE(parsed.first(), f);
        // The magnifier prints one row per format, so every format needs a
        // distinct label — a new format added without one shows up here.
        const QString label = ColorPicker::labelFor(f);
        QVERIFY(!label.isEmpty());
        QVERIFY(!seen.contains(label));
        seen.insert(label);
    }
}

void TstColor::parseFormatsList()
{
    // Order preserved, unknown codes skipped, duplicates removed, case/space tolerant.
    const auto v = ColorPicker::parseFormats(QStringLiteral(" HEX , dec ,bogus,hex,cmyk"));
    QCOMPARE(v.size(), 3);
    QCOMPARE(v.at(0), ColorPicker::Format::Hex);
    QCOMPARE(v.at(1), ColorPicker::Format::Dec);
    QCOMPARE(v.at(2), ColorPicker::Format::Cmyk);
    // Empty input yields an empty list.
    QVERIFY(ColorPicker::parseFormats(QString()).isEmpty());
}

void TstColor::sampleReadsPixel()
{
    QImage img(4, 3, QImage::Format_ARGB32);
    img.fill(Qt::red);
    img.setPixelColor(2, 1, QColor(10, 20, 30));
    QCOMPARE(ColorPicker::sampleAt(img, QPoint(2, 1)), QColor(10, 20, 30));
    QCOMPARE(ColorPicker::sampleAt(img, QPoint(0, 0)), QColor(Qt::red));
}

void TstColor::sampleClampsOutOfBounds()
{
    QImage img(4, 3, QImage::Format_ARGB32);
    img.fill(Qt::blue);
    img.setPixelColor(3, 2, QColor(7, 7, 7));   // bottom-right
    // Out-of-range points clamp to the nearest edge pixel, never crash.
    QCOMPARE(ColorPicker::sampleAt(img, QPoint(100, 100)), QColor(7, 7, 7));
    QCOMPARE(ColorPicker::sampleAt(img, QPoint(-5, -5)), QColor(Qt::blue));
}

void TstColor::sampleNullImage()
{
    QVERIFY(!ColorPicker::sampleAt(QImage(), QPoint(0, 0)).isValid());
}

void TstColor::basicPaletteShape()
{
    const auto pal = ColorPicker::basicPalette();
    QCOMPARE(pal.size(), 48);            // 8 columns x 6 rows
    for (const QColor& c : pal)
        QVERIFY(c.isValid());
    // Matches Qt's QColorDialog basic palette exactly.
    QCOMPARE(pal.first(), QColor(0x00, 0x00, 0x00));   // top-left black
    QCOMPARE(pal.at(1),   QColor(0xaa, 0x00, 0x00));
    QCOMPARE(pal.last(),  QColor(0xff, 0xff, 0xff));    // bottom-right white
}

void TstColor::parseHtmlForms()
{
    QCOMPARE(ColorPicker::parseHtml(QStringLiteral("#22b14c")), QColor(0x22, 0xb1, 0x4c));
    QCOMPARE(ColorPicker::parseHtml(QStringLiteral("22b14c")),  QColor(0x22, 0xb1, 0x4c));   // no '#'
    QCOMPARE(ColorPicker::parseHtml(QStringLiteral("  #FFF ")), QColor(0xff, 0xff, 0xff));   // #rgb + spaces
    QVERIFY(ColorPicker::parseHtml(QStringLiteral("#22b14c")).alpha() == 255);
}

void TstColor::parseHtmlRejectsBadInput()
{
    QVERIFY(!ColorPicker::parseHtml(QString()).isValid());
    QVERIFY(!ColorPicker::parseHtml(QStringLiteral("#12")).isValid());      // wrong length
    QVERIFY(!ColorPicker::parseHtml(QStringLiteral("#xyzxyz")).isValid());  // non-hex
    QVERIFY(!ColorPicker::parseHtml(QStringLiteral("#1234567")).isValid()); // too long
}

void TstColor::htmlRoundTrip()
{
    const QColor c(0xAB, 0xCD, 0xEF);
    QCOMPARE(ColorPicker::toHtml(c), QStringLiteral("#abcdef"));
    QCOMPARE(ColorPicker::parseHtml(ColorPicker::toHtml(c)), c);
}

QTEST_GUILESS_MAIN(TstColor)
#include "tst_color.moc"
