#include <QApplication>
#include <QDir>

#include "core/i18n/I18n.h"
#include "app/Application.h"

// SnapCap entry point: a tray-resident app (no main window). Stage 1 wires the
// Action layer, global hotkeys, tray, and settings. See
// claude-plan/Implementation-Plan.md (Stage 1).
int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("SnapCap"));
    QApplication::setOrganizationName(QStringLiteral("SnapCap"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    app.setQuitOnLastWindowClosed(false);          // tray app outlives its windows

    // Editable language files ship next to the executable.
    const QString langDir = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("lang"));
    if (I18n::instance().load(langDir) == 0)
        qWarning() << "[i18n] no locale files found in" << langDir;

    // A UAC "As administrator" relaunch passes this flag; skip the single-instance
    // guard so the elevated instance doesn't hand off to the (medium-integrity)
    // instance it's replacing, which is quitting. See platform/win/WinAutostart.
    bool elevatedRelaunch = false;
    for (int i = 1; i < argc; ++i)
        if (qstrcmp(argv[i], "--relaunched-elevated") == 0)
            elevatedRelaunch = true;

    if (!elevatedRelaunch
        && Application::isAnotherInstanceRunning(QStringLiteral("SnapCapInstance")))
        return 0;                                  // hand off to the running instance

    Application shell;
    return app.exec();
}
