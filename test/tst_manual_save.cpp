#include <QtTest>
#include <QTemporaryDir>

#include "core/ImageExport.h"
#include "core/ManualSave.h"
#include "core/Settings.h"

// The Settings-driven half of the manual "Save as..." dialog (Output -> Manual Save),
// shared by the snip overlay and the pin window.
class TstManualSave : public QObject
{
    Q_OBJECT
private slots:
    void nullSettingsUsesDefaultTemplate();
    void expandsFileNameTemplate();
    void expandsSpecials();
    void rememberedExtensionReplacesTemplateOne();
    void rememberOffIgnoresLastExtension();
    void filterListsOwnExtensionFirst();
    void filterOffersOnlyWritableFormats();
    void rememberExtensionHonoursTheSetting();

private:
    QTemporaryDir m_dir;
    QString ini() { return m_dir.filePath(QStringLiteral("config.ini")); }
};

void TstManualSave::nullSettingsUsesDefaultTemplate()
{
    // No Settings (tests / headless) must still yield a usable, dated name.
    const QString name = ManualSave::suggestedName(nullptr);
    QVERIFY(name.startsWith(QStringLiteral("SnapCap_")));
    QVERIFY(name.endsWith(QStringLiteral(".png")));
    QVERIFY(!name.contains(QLatin1Char('$')));        // the date span was expanded
}

void TstManualSave::expandsFileNameTemplate()
{
    Settings s(ini());
    s.setValue(QStringLiteral("output/manualSave/fileName"),
               QStringLiteral("shot_$yyyy$.jpg"));
    const QString name = ManualSave::suggestedName(&s);
    QCOMPARE(name, QStringLiteral("shot_%1.jpg").arg(QDate::currentDate().year()));
}

void TstManualSave::expandsSpecials()
{
    Settings s(ini());
    s.setValue(QStringLiteral("output/manualSave/fileName"), QStringLiteral("%title%.png"));
    const QString name =
        ManualSave::suggestedName(&s, {{QStringLiteral("title"), QStringLiteral("Untitled")}});
    QCOMPARE(name, QStringLiteral("Untitled.png"));
}

void TstManualSave::rememberedExtensionReplacesTemplateOne()
{
    Settings s(ini());
    s.setValue(QStringLiteral("output/manualSave/fileName"), QStringLiteral("shot.png"));
    s.setValue(QStringLiteral("output/manualSave/rememberExtension"), true);
    s.setValue(QStringLiteral("output/manualSave/lastExtension"), QStringLiteral("bmp"));
    QCOMPARE(ManualSave::suggestedName(&s), QStringLiteral("shot.bmp"));
}

void TstManualSave::rememberOffIgnoresLastExtension()
{
    // With the box unchecked the template's own extension must win, even though a
    // lastExtension is still sitting in the config from an earlier run.
    Settings s(ini());
    s.setValue(QStringLiteral("output/manualSave/fileName"), QStringLiteral("shot.png"));
    s.setValue(QStringLiteral("output/manualSave/rememberExtension"), false);
    s.setValue(QStringLiteral("output/manualSave/lastExtension"), QStringLiteral("bmp"));
    QCOMPARE(ManualSave::suggestedName(&s), QStringLiteral("shot.png"));
}

void TstManualSave::filterListsOwnExtensionFirst()
{
    // The suggested name's own format is hoisted to the front so the dialog defaults to it.
    QVERIFY(ManualSave::filterFor(QStringLiteral("a.bmp"))
                .startsWith(QStringLiteral("BMP (*.bmp);;")));
    // An alias resolves to its row: "jpeg" selects the JPEG entry, not a bogus "JPEG (*.jpeg)".
    QVERIFY(ManualSave::filterFor(QStringLiteral("a.jpeg"))
                .startsWith(QStringLiteral("JPEG (*.jpg *.jpeg);;")));
    // PDF hoists like any other row, so a remembered .pdf extension stays selected.
    QVERIFY(ManualSave::filterFor(QStringLiteral("a.pdf"))
                .startsWith(QStringLiteral("PDF (*.pdf);;")));
    // No extension → the offered set in canonical order, with no empty leading entry.
    const QString plain = ManualSave::filterFor(QStringLiteral("a"));
    QVERIFY(!plain.startsWith(QStringLiteral(";;")));
    QVERIFY(plain.startsWith(QStringLiteral("PNG (*.png);;")));
}

void TstManualSave::filterOffersOnlyWritableFormats()
{
    const QString f = ManualSave::filterFor(QStringLiteral("a"));
    const QStringList writable = ImageExport::supportedSaveFormats();

    // PNG/JPEG/BMP are built into Qt and must always be there.
    QVERIFY(f.contains(QStringLiteral("PNG (*.png)")));
    QVERIFY(f.contains(QStringLiteral("JPEG (*.jpg *.jpeg)")));
    QVERIFY(f.contains(QStringLiteral("BMP (*.bmp)")));

    // Every other row appears if and only if there is a writer for it. This is the
    // whole point of the runtime gate, so assert the biconditional rather than a
    // hard-coded expectation: TIFF flips to present once qtimageformats is installed,
    // and TGA/GIF once their encoders land, with no test edit needed.
    const auto offered = [&f](const char* label) { return f.contains(QLatin1String(label)); };
    QCOMPARE(offered("ICO (*.ico)"),        writable.contains(QStringLiteral("ico")));
    QCOMPARE(offered("TIFF (*.tif *.tiff)"), writable.contains(QStringLiteral("tiff")));
    QCOMPARE(offered("TGA (*.tga)"),        writable.contains(QStringLiteral("tga")));
    QCOMPARE(offered("GIF (*.gif)"),        writable.contains(QStringLiteral("gif")));
    QCOMPARE(offered("PDF (*.pdf)"),        writable.contains(QStringLiteral("pdf")));

    // WebP has no writer in this build, so it must never be offered — and an
    // unsupported suggested extension must not sneak in through the hoist path.
    QVERIFY(!f.contains(QStringLiteral("WebP")));
    QVERIFY(!ManualSave::filterFor(QStringLiteral("a.webp")).contains(QStringLiteral("webp")));
}

void TstManualSave::rememberExtensionHonoursTheSetting()
{
    Settings s(ini());
    s.setValue(QStringLiteral("output/manualSave/rememberExtension"), true);
    ManualSave::rememberExtension(&s, QStringLiteral("C:/pics/shot.JPG"));
    QCOMPARE(s.value(QStringLiteral("output/manualSave/lastExtension")).toString(),
             QStringLiteral("jpg"));            // normalized to lower case

    // Off → the app must not keep "remembering" behind the user's back.
    s.setValue(QStringLiteral("output/manualSave/rememberExtension"), false);
    ManualSave::rememberExtension(&s, QStringLiteral("C:/pics/shot.bmp"));
    QCOMPARE(s.value(QStringLiteral("output/manualSave/lastExtension")).toString(),
             QStringLiteral("jpg"));            // unchanged

    ManualSave::rememberExtension(nullptr, QStringLiteral("C:/pics/shot.bmp"));   // no crash
}

QTEST_GUILESS_MAIN(TstManualSave)
#include "tst_manual_save.moc"
