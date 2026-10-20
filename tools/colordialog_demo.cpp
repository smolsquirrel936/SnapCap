// Throwaway visual harness for ui/ColorDialog (not part of the app).
// Build with -DSNAPCAP_BUILD_DEMOS=ON, then run:
//   build\colordialog_demo.exe [locale]
// e.g. `colordialog_demo.exe zh_tw` to see the Traditional-Chinese strings.
#include <QApplication>
#include <QDir>
#include <QDebug>

#include "core/i18n/I18n.h"
#include "core/Settings.h"
#include "ui/ColorDialog.h"

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("SnapCap"));
    QApplication::setOrganizationName(QStringLiteral("SnapCap"));

    const QString langDir = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("lang"));
    if (I18n::instance().load(langDir) == 0)
        qWarning() << "[i18n] no locale files found in" << langDir;
    if (argc > 1)
        I18n::instance().setLocale(QString::fromLocal8Bit(argv[1]));

    Settings settings;   // shares the app's config.ini so custom colors persist
    const QColor picked = ColorDialog::getColor(QColor(0x22, 0xb1, 0x4c), &settings);
    qInfo() << "selected:" << (picked.isValid() ? picked.name(QColor::HexArgb) : QStringLiteral("<cancelled>"));
    return 0;
}
