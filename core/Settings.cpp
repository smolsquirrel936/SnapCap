#include "core/Settings.h"

#include <QCoreApplication>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

namespace {
// Pointer file (next to the exe) that records a user-relocated config path.
QString pointerFilePath()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("config.path"));
}

// Can we actually create a file here? QFileInfo::isWritable() cannot answer this
// on Windows: it consults the read-only attribute (meaningless for directories)
// unless Qt's NTFS permission lookup is enabled, which it is not by default. It
// therefore reports an ACL-locked directory -- the C:\Program Files case -- as
// writable, and every setting, log, history entry and session is then written to
// a path that silently rejects them. Probing with a real file is the only check
// that agrees with what the writes will do.
bool dirAcceptsWrites(const QDir& dir)
{
    QFile probe(dir.filePath(QStringLiteral(".snapcap-write-test")));
    if (!probe.open(QIODevice::WriteOnly))
        return false;
    probe.close();
    probe.remove();
    return true;
}

// config.ini next to the exe; falls back to the app-config location if the exe
// directory is not writable.
QString portableIniPath()
{
    const QDir exeDir(QCoreApplication::applicationDirPath());
    if (dirAcceptsWrites(exeDir))
        return exeDir.filePath(QStringLiteral("config.ini"));

    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (dir.isEmpty())
        dir = QDir::homePath() + QStringLiteral("/.snapcap");
    QDir().mkpath(dir);
    return QDir(dir).filePath(QStringLiteral("config.ini"));
}
} // namespace

QString Settings::resolveConfigPath()
{
    const QString pointer = pointerFilePath();
    QFile f(pointer);
    if (f.exists() && f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString chosen = QString::fromUtf8(f.readAll()).trimmed();
        f.close();
        if (!chosen.isEmpty()) {
            // Ensure the target directory exists / is creatable before trusting it.
            const QDir parent = QFileInfo(chosen).absoluteDir();
            if (parent.exists() || QDir().mkpath(parent.absolutePath()))
                return chosen;
        }
    }
    return portableIniPath();
}

Settings::Settings(const QString& iniPath, QObject* parent)
    : QObject(parent),
      m_settings(std::make_unique<QSettings>(
          iniPath.isEmpty() ? resolveConfigPath() : iniPath, QSettings::IniFormat))
{
    seedDefaults();
}

QVariant Settings::value(const QString& key, const QVariant& def) const
{
    return m_settings->value(key, def);
}

void Settings::setValue(const QString& key, const QVariant& value)
{
    if (m_settings->value(key) == value)
        return;
    m_settings->setValue(key, value);
    m_settings->sync();
    emit changed(key);
}

QString Settings::filePath() const
{
    return m_settings->fileName();
}

QString Settings::logFilePath() const
{
    const QString v = m_settings->value(QStringLiteral("log/file"),
                                        QStringLiteral("splog.txt")).toString();
    if (QDir::isAbsolutePath(v))
        return QDir::cleanPath(v);
    return QDir(QFileInfo(m_settings->fileName()).absolutePath()).filePath(v);
}

bool Settings::relocate(const QString& newPath)
{
    const QString cleanNew = QDir::cleanPath(newPath);
    if (cleanNew.isEmpty())
        return false;
    if (QFileInfo(cleanNew).absoluteFilePath() == QFileInfo(m_settings->fileName()).absoluteFilePath())
        return true;   // already there

    m_settings->sync();
    const QString oldPath = m_settings->fileName();

    // Make sure the destination directory exists.
    const QDir destDir = QFileInfo(cleanNew).absoluteDir();
    if (!destDir.exists() && !QDir().mkpath(destDir.absolutePath()))
        return false;

    // Copy the current ini to the new location (overwrite if present).
    if (QFile::exists(oldPath)) {
        if (QFile::exists(cleanNew))
            QFile::remove(cleanNew);
        if (!QFile::copy(oldPath, cleanNew))
            return false;
    }

    // Re-point the backing store at the new file.
    m_settings = std::make_unique<QSettings>(cleanNew, QSettings::IniFormat);
    seedDefaults();

    // Persist the choice in the pointer file next to the exe.
    QFile ptr(pointerFilePath());
    if (ptr.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream(&ptr) << cleanNew;
        ptr.close();
    }

    emit changed(QStringLiteral("general/configPath"));
    return true;
}

void Settings::resetToDefaults()
{
    m_settings->clear();
    seedDefaults();
    m_settings->sync();
    emit changed(QString());   // empty key => "everything changed"
}

void Settings::seedDefaults()
{
    const auto def = [&](const QString& key, const QVariant& v) {
        if (!m_settings->contains(key))
            m_settings->setValue(key, v);
    };
    // Hotkeys are NOT seeded here — defaults live in res/keymap.default.yaml;
    // config.ini only stores user overrides (hotkey/<action>).
    def(QStringLiteral("general/hideTray"), false);
    def(QStringLiteral("general/locale"), QStringLiteral("en"));
    def(QStringLiteral("general/runOnStartup"), false);
    def(QStringLiteral("general/startupElevated"), false);
    def(QStringLiteral("log/file"), QStringLiteral("splog.txt"));
    def(QStringLiteral("log/rotateSizeMb"), 2.0);
    def(QStringLiteral("log/level"), QStringLiteral("normal"));
    m_settings->sync();
}
