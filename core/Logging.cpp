#include "core/Logging.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QMutex>
#include <QTextStream>
#include <QDateTime>

namespace {

QFile g_file;
QMutex g_mutex;
double g_maxBytes = 2.0 * 1024 * 1024;
QtMessageHandler g_prev = nullptr;
Logging::Level g_level = Logging::Level::Normal;

// True if a message of `type` should be written to the file at the active level.
bool passesLevel(QtMsgType type)
{
    switch (g_level) {
        case Logging::Level::Off:
            return false;
        case Logging::Level::Errors:
            return type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg;
        case Logging::Level::Normal:
            return type != QtDebugMsg;               // Info and above
        case Logging::Level::Verbose:
            return true;                             // everything, incl. Debug
    }
    return true;
}

const char* levelTag(QtMsgType t)
{
    switch (t) {
        case QtDebugMsg:    return "D";
        case QtInfoMsg:     return "I";
        case QtWarningMsg:  return "W";
        case QtCriticalMsg: return "C";
        case QtFatalMsg:    return "F";
    }
    return "?";
}

void handler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
    {
        QMutexLocker lock(&g_mutex);
        if (g_file.isOpen() && passesLevel(type)) {
            if (g_file.size() > g_maxBytes) {     // rotate
                const QString p = g_file.fileName();
                g_file.close();
                QFile::remove(p + QStringLiteral(".1"));
                QFile::rename(p, p + QStringLiteral(".1"));
                (void) g_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
            }
            QTextStream ts(&g_file);
            ts << QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
               << " [" << levelTag(type) << "] " << msg << '\n';
            ts.flush();
        }
    }
    if (g_prev)
        g_prev(type, ctx, msg);                   // keep console/default output
}

} // namespace

namespace Logging {

void init(const QString& path, double maxMb)
{
    QMutexLocker lock(&g_mutex);
    QDir().mkpath(QFileInfo(path).absolutePath());
    g_file.setFileName(path);
    (void) g_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    g_maxBytes = maxMb * 1024 * 1024;

    static bool installed = false;
    if (!installed) {
        g_prev = qInstallMessageHandler(handler);
        installed = true;
    }
}

bool relocate(const QString& newPath)
{
    QMutexLocker lock(&g_mutex);
    const QString oldPath = g_file.fileName();
    if (!oldPath.isEmpty()
        && QFileInfo(newPath).absoluteFilePath() == QFileInfo(oldPath).absoluteFilePath())
        return true;                                  // already there

    const bool wasOpen = g_file.isOpen();
    if (wasOpen)
        g_file.close();

    QDir().mkpath(QFileInfo(newPath).absolutePath());

    // Best-effort move of the existing log to the new location (rename can fail
    // across volumes → fall back to copy; if both fail we just start fresh).
    if (!oldPath.isEmpty() && QFile::exists(oldPath)) {
        QFile::remove(newPath);
        if (!QFile::rename(oldPath, newPath))
            QFile::copy(oldPath, newPath);
    }

    g_file.setFileName(newPath);
    return g_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
}

void setLevel(Level level)
{
    QMutexLocker lock(&g_mutex);
    g_level = level;
}

Level levelFromString(const QString& s)
{
    const QString v = s.trimmed().toLower();
    if (v == QLatin1String("off"))     return Level::Off;
    if (v == QLatin1String("errors"))  return Level::Errors;
    if (v == QLatin1String("verbose")) return Level::Verbose;
    return Level::Normal;
}

QString levelToString(Level level)
{
    switch (level) {
        case Level::Off:     return QStringLiteral("off");
        case Level::Errors:  return QStringLiteral("errors");
        case Level::Normal:  return QStringLiteral("normal");
        case Level::Verbose: return QStringLiteral("verbose");
    }
    return QStringLiteral("normal");
}

} // namespace Logging
