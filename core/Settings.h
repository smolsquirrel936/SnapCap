#pragma once

#include <QObject>
#include <QSettings>
#include <QVariant>

#include <memory>

// Typed-ish wrapper over QSettings (INI format -> config.ini).
// Emits changed(key) so components can react live.
// Group names follow the Preferences tabs (general/snip/paste/control/...).
//
// Portable model: by default the config file lives next to SnapCap.exe; a small
// pointer file ("config.path") next to the exe records a user-relocated path so
// it survives restarts. resolveConfigPath() reads that pointer at startup.
class Settings : public QObject {
    Q_OBJECT
public:
    // iniPath empty -> the resolved portable config path (see resolveConfigPath).
    // Pass an explicit path in tests.
    explicit Settings(const QString& iniPath = QString(), QObject* parent = nullptr);

    QVariant value(const QString& key, const QVariant& def = QVariant()) const;
    void setValue(const QString& key, const QVariant& value);   // emits changed()
    QString filePath() const;

    // Absolute path of the log file: the `log/file` value used as-is when it is
    // absolute, otherwise resolved relative to the config-file directory (so the
    // default bare "splog.txt" lands next to config.ini, keeping it portable).
    QString logFilePath() const;

    // Move the config file to newPath, re-point the backing store, and persist
    // the choice in the pointer file next to the exe. Returns false on failure.
    bool relocate(const QString& newPath);

    // Clear every key and re-seed defaults (keeps the current file location).
    void resetToDefaults();

    // The config path to use when none is given: pointer file next to the exe if
    // present and usable, else config.ini next to the exe (else app-config dir).
    static QString resolveConfigPath();

signals:
    void changed(const QString& key);

private:
    void seedDefaults();
    std::unique_ptr<QSettings> m_settings;
};
