#include <QtTest>
#include <QGuiApplication>
#include <QMimeData>
#include <QUrl>
#include <QImage>
#include <QTemporaryDir>

#include "core/ClipboardContent.h"

// Stage 5: clipboard classification + color parsing + text/card rendering. Uses a
// QGuiApplication (custom main) because rendering text needs the font database.
class TstClipboardContent : public QObject
{
    Q_OBJECT
private slots:
    void parseHex();
    void parseIntTriplet();
    void parseFloatTriplet();
    void parseRejectsGarbage();
    void parseRejectsOutOfRange();
    void classifyImage();
    void classifyColorText();
    void classifyPlainText();
    void classifyFilePaths();
    void filePathsDisabled();
    void imageFileSurvivesFilePathsDisabled();
    void textToImageOffIgnoresText();
    void textToImageOffKeepsColorCard();
    void textToImageOffIgnoresFilePaths();
    void classifyHtml();
    void ignoreHtmlForcesPlainText();
    void classifyEmpty();
};

void TstClipboardContent::parseHex()
{
    QCOMPARE(clip::parseColor(QStringLiteral("#ff0000")).value(), QColor(255, 0, 0));
    QCOMPARE(clip::parseColor(QStringLiteral("  #00FF00 ")).value(), QColor(0, 255, 0));
}

void TstClipboardContent::parseIntTriplet()
{
    QCOMPARE(clip::parseColor(QStringLiteral("255, 0, 0")).value(), QColor(255, 0, 0));
    QCOMPARE(clip::parseColor(QStringLiteral("10 20 30")).value(), QColor(10, 20, 30));
    // Integers without a dot stay integers (NOT 0-1 floats).
    QCOMPARE(clip::parseColor(QStringLiteral("1, 0, 0")).value(), QColor(1, 0, 0));
}

void TstClipboardContent::parseFloatTriplet()
{
    QCOMPARE(clip::parseColor(QStringLiteral("1.0, 0.0, 0.0")).value(), QColor(255, 0, 0));
    const QColor g = clip::parseColor(QStringLiteral("0.5, 0.5, 0.5")).value();
    QVERIFY(qAbs(g.red() - 128) <= 2);
}

void TstClipboardContent::parseRejectsGarbage()
{
    QVERIFY(!clip::parseColor(QStringLiteral("hello")).has_value());
    QVERIFY(!clip::parseColor(QStringLiteral("1, 2")).has_value());
    QVERIFY(!clip::parseColor(QString()).has_value());
}

void TstClipboardContent::parseRejectsOutOfRange()
{
    QVERIFY(!clip::parseColor(QStringLiteral("300, 0, 0")).has_value());
    QVERIFY(!clip::parseColor(QStringLiteral("1.5, 0.0, 0.0")).has_value());
}

void TstClipboardContent::classifyImage()
{
    QImage src(8, 6, QImage::Format_ARGB32);
    src.fill(Qt::red);
    QMimeData md;
    md.setImageData(src);
    const clip::Content c = clip::classify(md, clip::PasteOptions{});
    QCOMPARE(c.kind, clip::Kind::Image);
    QCOMPARE(c.image.size(), QSize(8, 6));
}

void TstClipboardContent::classifyColorText()
{
    QMimeData md;
    md.setText(QStringLiteral("#0000ff"));
    const clip::Content c = clip::classify(md, clip::PasteOptions{});
    QCOMPARE(c.kind, clip::Kind::Color);
    QCOMPARE(c.color, QColor(0, 0, 255));
    QVERIFY(!c.image.isNull());
}

void TstClipboardContent::classifyPlainText()
{
    QMimeData md;
    md.setText(QStringLiteral("hello world"));
    const clip::Content c = clip::classify(md, clip::PasteOptions{});
    QCOMPARE(c.kind, clip::Kind::PlainText);
    QCOMPARE(c.text, QStringLiteral("hello world"));
    QVERIFY(!c.image.isNull());
    QVERIFY(c.image.width() > 0 && c.image.height() > 0);
}

void TstClipboardContent::classifyFilePaths()
{
    QMimeData md;
    md.setUrls({QUrl::fromLocalFile(QStringLiteral("C:/tmp/notes.txt"))});
    const clip::Content c = clip::classify(md, clip::PasteOptions{});
    QCOMPARE(c.kind, clip::Kind::FilePaths);
    QCOMPARE(c.files.size(), 1);
    QVERIFY(!c.image.isNull());
}

void TstClipboardContent::filePathsDisabled()
{
    QMimeData md;
    md.setUrls({QUrl::fromLocalFile(QStringLiteral("C:/tmp/notes.txt"))});
    clip::PasteOptions opt;
    opt.pasteFilePaths = false;
    const clip::Content c = clip::classify(md, opt);
    QCOMPARE(c.kind, clip::Kind::None);
}

// "Never paste file paths" suppresses path *rendering* only — a copied image file still
// pins the picture itself (lang/en.yaml: "Never turn copied file paths into image windows").
void TstClipboardContent::imageFileSurvivesFilePathsDisabled()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString png = dir.filePath(QStringLiteral("cat.png"));
    QImage src(8, 6, QImage::Format_ARGB32);
    src.fill(Qt::red);
    QVERIFY(src.save(png));

    QMimeData md;
    md.setUrls({QUrl::fromLocalFile(png)});
    clip::PasteOptions opt;
    opt.pasteFilePaths = false;
    opt.textToImage = false;        // neither gate may block a real image file
    const clip::Content c = clip::classify(md, opt);
    QCOMPARE(c.kind, clip::Kind::Image);
    QCOMPARE(c.image.size(), QSize(8, 6));
}

void TstClipboardContent::textToImageOffIgnoresText()
{
    QMimeData md;
    md.setText(QStringLiteral("hello world"));
    clip::PasteOptions opt;
    opt.textToImage = false;
    const clip::Content c = clip::classify(md, opt);
    QCOMPARE(c.kind, clip::Kind::None);
    QVERIFY(!c.isValid());
}

// A color card is a swatch, not rendered glyphs — it outlives the text→image toggle.
void TstClipboardContent::textToImageOffKeepsColorCard()
{
    QMimeData md;
    md.setText(QStringLiteral("#0000ff"));
    clip::PasteOptions opt;
    opt.textToImage = false;
    const clip::Content c = clip::classify(md, opt);
    QCOMPARE(c.kind, clip::Kind::Color);
    QCOMPARE(c.color, QColor(0, 0, 255));
    QVERIFY(!c.image.isNull());
}

// Drawing a path list *is* a text→image conversion, so the toggle kills it too.
void TstClipboardContent::textToImageOffIgnoresFilePaths()
{
    QMimeData md;
    md.setUrls({QUrl::fromLocalFile(QStringLiteral("C:/tmp/notes.txt"))});
    clip::PasteOptions opt;
    opt.textToImage = false;
    const clip::Content c = clip::classify(md, opt);
    QCOMPARE(c.kind, clip::Kind::None);
}

void TstClipboardContent::classifyHtml()
{
    QMimeData md;
    md.setText(QStringLiteral("hello"));
    md.setHtml(QStringLiteral("<b style='color:#ff0000'>hello</b>"));
    const clip::Content c = clip::classify(md, clip::PasteOptions{});
    QCOMPARE(c.kind, clip::Kind::Html);
    QCOMPARE(c.text, QStringLiteral("hello"));
    QVERIFY(!c.image.isNull());
    // Bold + red markup must not render identically to the plain-text path.
    QVERIFY(c.image != clip::renderText(QStringLiteral("hello"), clip::PasteOptions{}));
}

void TstClipboardContent::ignoreHtmlForcesPlainText()
{
    QMimeData md;
    md.setText(QStringLiteral("hello"));
    md.setHtml(QStringLiteral("<b style='color:#ff0000'>hello</b>"));
    clip::PasteOptions opt;
    opt.ignoreHtml = true;
    const clip::Content c = clip::classify(md, opt);
    QCOMPARE(c.kind, clip::Kind::PlainText);
    QCOMPARE(c.image, clip::renderText(QStringLiteral("hello"), opt));
}

void TstClipboardContent::classifyEmpty()
{
    QMimeData md;
    const clip::Content c = clip::classify(md, clip::PasteOptions{});
    QCOMPARE(c.kind, clip::Kind::None);
    QVERIFY(!c.isValid());
}

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    TstClipboardContent tc;
    return QTest::qExec(&tc, argc, argv);
}
#include "tst_clipboard_content.moc"
