// Throwaway visual harness for ui/PreferencesDialog (not part of the app).
// Build with -DSNAPCAP_BUILD_DEMOS=ON, then run:
//   build\prefs_demo.exe [out-dir] [locale]
// Grabs each category page to <out-dir>/pref_<n>_<name>.png and exits.
#include <iterator>

#include <QApplication>
#include <QDir>
#include <QTimer>
#include <QListWidget>
#include <QDebug>

#include "core/i18n/I18n.h"
#include "core/Settings.h"
#include "platform/platform.h"
#include "ui/PreferencesDialog.h"
#include "ui/KeyBindingsDialog.h"

// This demo doesn't link the Win platform backend, so stub the one free function
// PreferencesDialog's App-exclusions section references (real impl: WinPlatform.cpp).
// This harness compiles PreferencesDialog.cpp but links no platform backend, so every
// platform:: free function the dialog calls needs a stub here.
namespace platform {
QList<RunningApp> runningApplications() { return {}; }
QString activeVideoEncoder(recording::Codec, recording::HwPreference) { return {}; }
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("SnapCap"));
    QApplication::setOrganizationName(QStringLiteral("SnapCap"));

    const QString langDir = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("lang"));
    if (I18n::instance().load(langDir) == 0)
        qWarning() << "[i18n] no locale files found in" << langDir;
    if (argc > 2)
        I18n::instance().setLocale(QString::fromLocal8Bit(argv[2]));

    const QString outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral(".");

    Settings settings;
    PreferencesDialog dlg(&settings, nullptr);   // ctor never derefs the registry
    dlg.show();

    QTimer::singleShot(300, &app, [&] {
        auto* sb = dlg.findChild<QListWidget*>(QStringLiteral("PrefsSidebar"));
        // Must match PreferencesDialog's addCategory() order.
        const char* names[] = {"general", "interface", "snip", "paste",
                               "output", "control", "ocr", "update", "about"};
        for (int i = 0; i < int(std::size(names)) && sb; ++i) {
            sb->setCurrentRow(i);
            QApplication::processEvents();
            const QString p = QDir(outDir).filePath(
                QStringLiteral("pref_%1_%2.png").arg(i).arg(QString::fromLatin1(names[i])));
            if (dlg.grab().save(p))
                qInfo() << "saved" << p;
        }
        KeyBindingsDialog kb;
        kb.show();
        kb.resize(820, 1770);   // tall enough to grab the whole reference table
        QApplication::processEvents();
        const QString kbp = QDir(outDir).filePath(QStringLiteral("pref_keybindings.png"));
        if (kb.grab().save(kbp))
            qInfo() << "saved" << kbp;
        app.quit();
    });
    return app.exec();
}
