#include <QtTest>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>

#include "core/ImageExport.h"

// Stage 2: file export round-trips (clipboard needs a GUI app, tested manually).
class TstImageExport : public QObject
{
    Q_OBJECT
private slots:
    void savesPng();
    void savesBmp();
    void rejectsNullImage();
    void savesJpegWithQuality();
    void removesAlphaWhenOpaque();
    void keepsAlphaWhenTranslucent();
    void supportedFormatsCoverTheQtBuiltins();
    void everyAdvertisedFormatActuallyWrites();
    void reportsAReasonWhenTheWriteFails();
    void detectsIcoDownscale();
    void transparencyGoesWhiteInAlphaLessFormats();
    void transparencySurvivesInPng();

private:
    QImage sample() const
    {
        QImage img(8, 6, QImage::Format_ARGB32);
        img.fill(Qt::red);
        img.setPixelColor(0, 0, Qt::green);
        return img;
    }
    QTemporaryDir m_dir;
};

void TstImageExport::savesPng()
{
    const QString path = m_dir.filePath(QStringLiteral("out.png"));
    QVERIFY(ImageExport::toFile(sample(), path));
    QImage loaded(path);
    QVERIFY(!loaded.isNull());
    QCOMPARE(loaded.size(), QSize(8, 6));
    QCOMPARE(loaded.pixelColor(0, 0), QColor(Qt::green));
}

void TstImageExport::savesBmp()
{
    const QString path = m_dir.filePath(QStringLiteral("out.bmp"));
    QVERIFY(ImageExport::toFile(sample(), path));
    QImage loaded(path);
    QVERIFY(!loaded.isNull());
    QCOMPARE(loaded.size(), QSize(8, 6));
}

void TstImageExport::rejectsNullImage()
{
    QVERIFY(!ImageExport::toFile(QImage(), m_dir.filePath(QStringLiteral("x.png"))));
}

void TstImageExport::savesJpegWithQuality()
{
    // A larger, noisy image makes the lossy-quality effect measurable.
    QImage img(64, 64, QImage::Format_RGB32);
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            img.setPixelColor(x, y, QColor((x * 37) % 256, (y * 53) % 256, (x * y) % 256));

    const QString low = m_dir.filePath(QStringLiteral("low.jpg"));
    const QString high = m_dir.filePath(QStringLiteral("high.jpg"));
    QVERIFY(ImageExport::toFile(img, low, 5));
    QVERIFY(ImageExport::toFile(img, high, 95));

    // Both decode back to the right size, and low quality yields a smaller file.
    QVERIFY(!QImage(low).isNull());
    QVERIFY(!QImage(high).isNull());
    QVERIFY(QFileInfo(low).size() < QFileInfo(high).size());
}

void TstImageExport::removesAlphaWhenOpaque()
{
    // A fully-opaque ARGB image saved with removeAlphaIfOpaque loses its alpha channel.
    QImage img(4, 4, QImage::Format_ARGB32);
    img.fill(Qt::red);                                  // alpha == 255 everywhere
    QVERIFY(ImageExport::isFullyOpaque(img));
    const QString path = m_dir.filePath(QStringLiteral("opaque.png"));
    QVERIFY(ImageExport::toFile(img, path, -1, /*removeAlphaIfOpaque=*/true));
    QImage loaded(path);
    QVERIFY(!loaded.isNull());
    QVERIFY(!loaded.hasAlphaChannel());
    QCOMPARE(loaded.pixelColor(0, 0), QColor(Qt::red));
}

void TstImageExport::keepsAlphaWhenTranslucent()
{
    // A single translucent pixel keeps the alpha channel even with the flag on.
    QImage img(4, 4, QImage::Format_ARGB32);
    img.fill(Qt::red);
    img.setPixelColor(1, 1, QColor(0, 255, 0, 128));    // partially transparent
    QVERIFY(!ImageExport::isFullyOpaque(img));
    const QString path = m_dir.filePath(QStringLiteral("translucent.png"));
    QVERIFY(ImageExport::toFile(img, path, -1, /*removeAlphaIfOpaque=*/true));
    QImage loaded(path);
    QVERIFY(!loaded.isNull());
    QVERIFY(loaded.hasAlphaChannel());
    QCOMPARE(qAlpha(loaded.pixel(1, 1)), 128);
}

void TstImageExport::supportedFormatsCoverTheQtBuiltins()
{
    const QStringList f = ImageExport::supportedSaveFormats();
    // These three are compiled into QtGui itself, so they hold on any build.
    QVERIFY(f.contains(QStringLiteral("png")));
    QVERIFY(f.contains(QStringLiteral("bmp")));
    QVERIFY(f.contains(QStringLiteral("jpg")));
    // Normalised: lowercase and deduplicated.
    for (const QString& e : f)
        QCOMPARE(e, e.toLower());
    QStringList uniq = f;
    uniq.removeDuplicates();
    QCOMPARE(uniq.size(), f.size());
}

void TstImageExport::everyAdvertisedFormatActuallyWrites()
{
    // The invariant this whole save-format change exists to protect: if the Save
    // dialog offers a format, writing it must really work. Advertising something
    // unroutable is precisely how a save fails silently.
    //
    // Deliberately driven by the RUNTIME list, not a hard-coded one, so it adapts:
    // TIFF is covered only on machines where qtimageformats is installed, and
    // starts being covered automatically once it is.
    const QStringList writable = ImageExport::supportedSaveFormats();
    const QStringList offered = { QStringLiteral("png"),  QStringLiteral("jpg"),
                                  QStringLiteral("bmp"),  QStringLiteral("ico"),
                                  QStringLiteral("tiff"), QStringLiteral("tga"),
                                  QStringLiteral("gif"),  QStringLiteral("pdf") };

    QImage img(64, 48, QImage::Format_ARGB32);
    for (int y = 0; y < 48; ++y)
        for (int x = 0; x < 64; ++x)
            img.setPixelColor(x, y, QColor(x * 4, y * 5, (x + y) * 2));

    QStringList covered;
    for (const QString& ext : offered) {
        if (!writable.contains(ext))
            continue;                      // no writer here; the dialog won't offer it either
        covered << ext;
        const QString path = m_dir.filePath(QStringLiteral("adv.") + ext);
        QString err;
        QVERIFY2(ImageExport::toFile(img, path, -1, false, &err),
                 qPrintable(QStringLiteral("%1 advertised but failed: %2").arg(ext, err)));
        QVERIFY2(QFileInfo(path).size() > 0, qPrintable(ext + QStringLiteral(" wrote 0 bytes")));

        // Where a reader exists, confirm it decodes to the right size. ICO is exempt:
        // it legitimately downscales past 256px (not the case here, but keep it honest).
        if (QImageReader::supportedImageFormats().contains(ext.toLatin1())) {
            const QImage back(path);
            QVERIFY2(!back.isNull(), qPrintable(ext + QStringLiteral(" decoded to null")));
            QCOMPARE(back.size(), img.size());
        }
    }
    // Logged because the set is machine-dependent: seeing which formats a given
    // build actually exercised is the difference between "6 passed" and knowing
    // whether TIFF was among them.
    qInfo().noquote() << "formats verified writable:" << covered.join(QStringLiteral(", "))
                      << QStringLiteral("(%1 of %2)").arg(covered.size()).arg(offered.size());
    QVERIFY(covered.size() >= 7);          // png/jpg/bmp/ico/tga/gif/pdf at minimum
}

void TstImageExport::reportsAReasonWhenTheWriteFails()
{
    QString err;
    // An extension with no writer must fail loudly rather than silently doing nothing.
    QVERIFY(!ImageExport::toFile(sample(), m_dir.filePath(QStringLiteral("x.nosuchfmt")),
                                 -1, false, &err));
    QVERIFY(!err.isEmpty());

    err.clear();
    QVERIFY(!ImageExport::toFile(QImage(), m_dir.filePath(QStringLiteral("x.png")),
                                 -1, false, &err));
    QVERIFY(!err.isEmpty());

    // A success clears the slot, so a stale message can't be shown after a good save.
    err = QStringLiteral("stale");
    QVERIFY(ImageExport::toFile(sample(), m_dir.filePath(QStringLiteral("ok.png")),
                                -1, false, &err));
    QVERIFY(err.isEmpty());
}

void TstImageExport::detectsIcoDownscale()
{
    // Qt's ICO writer silently shrinks anything over 256px and still returns true,
    // so callers must be able to detect it up front. Only .ico paths are affected.
    const QImage big(300, 200, QImage::Format_ARGB32);
    const QImage small(64, 64, QImage::Format_ARGB32);
    QVERIFY(ImageExport::icoWouldDownscale(big, QStringLiteral("a.ico")));
    QVERIFY(ImageExport::icoWouldDownscale(big, QStringLiteral("a.ICO")));   // case-insensitive
    QVERIFY(!ImageExport::icoWouldDownscale(small, QStringLiteral("a.ico")));
    QVERIFY(!ImageExport::icoWouldDownscale(big, QStringLiteral("a.png")));
    QVERIFY(!ImageExport::icoWouldDownscale(QImage(), QStringLiteral("a.ico")));

    // Exactly at the cap is fine; one pixel over is not.
    QVERIFY(!ImageExport::icoWouldDownscale(QImage(256, 256, QImage::Format_ARGB32),
                                            QStringLiteral("a.ico")));
    QVERIFY(ImageExport::icoWouldDownscale(QImage(257, 256, QImage::Format_ARGB32),
                                           QStringLiteral("a.ico")));
}

void TstImageExport::transparencyGoesWhiteInAlphaLessFormats()
{
    // A rounded-corner snip saved as JPG/BMP must come out with WHITE corners —
    // Qt's writers would otherwise land the transparent pixels on black.
    QImage img(16, 16, QImage::Format_ARGB32);
    img.fill(Qt::red);
    img.setPixelColor(0, 0, QColor(0, 0, 0, 0));            // the "cut corner"

    const QString bmp = m_dir.filePath(QStringLiteral("cut.bmp"));
    QVERIFY(ImageExport::toFile(img, bmp));
    const QImage backBmp(bmp);
    QVERIFY(!backBmp.isNull());
    QCOMPARE(backBmp.pixelColor(0, 0), QColor(Qt::white));
    QCOMPARE(backBmp.pixelColor(8, 8), QColor(Qt::red));

    // JPEG is lossy, so the corner is checked as "near white" rather than exact.
    const QString jpg = m_dir.filePath(QStringLiteral("cut.jpg"));
    QVERIFY(ImageExport::toFile(img, jpg, 95));
    const QImage backJpg(jpg);
    QVERIFY(!backJpg.isNull());
    const QColor c = backJpg.pixelColor(0, 0);
    QVERIFY2(c.red() > 200 && c.green() > 200 && c.blue() > 200,
             qPrintable(QStringLiteral("jpeg corner was %1").arg(c.name())));
}

void TstImageExport::transparencySurvivesInPng()
{
    // The flatten must be scoped to the alpha-less formats only.
    QImage img(16, 16, QImage::Format_ARGB32);
    img.fill(Qt::red);
    img.setPixelColor(0, 0, QColor(0, 0, 0, 0));
    const QString path = m_dir.filePath(QStringLiteral("cut.png"));
    QVERIFY(ImageExport::toFile(img, path));
    const QImage back(path);
    QVERIFY(back.hasAlphaChannel());
    QCOMPARE(qAlpha(back.pixel(0, 0)), 0);
}

QTEST_GUILESS_MAIN(TstImageExport)
#include "tst_image_export.moc"
