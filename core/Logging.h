#pragma once

#include <QString>

// Minimal rotating-file logger installed as the Qt message handler, configured by
// file name + rotate size. Console output (if any) is
// preserved by chaining to the previous handler.
namespace Logging {

// Verbosity of the file log (the General-tab "Log level" control). Messages
// below the active level are dropped before being written to the file (console
// output via the chained handler is unaffected).
//   Off     — write nothing to the file.
//   Errors  — Warning / Critical / Fatal only.
//   Normal  — Info and above (excludes Debug). Default.
//   Verbose — everything, including Debug.
enum class Level { Off, Errors, Normal, Verbose };

// Begin writing qDebug/qInfo/qWarning/... to `path`, rotating to `path.1` once
// it exceeds maxMb. Safe to call once at startup.
void init(const QString& path, double maxMb = 2.0);

// Switch the active log file to `newPath` (the General-tab "Log path" control):
// closes the current file, best-effort moves its existing content to the new
// location, and resumes logging there. Returns false if the new file could not
// be opened (the caller should keep the old path). Safe to call any time.
bool relocate(const QString& newPath);

// Change the active file-log verbosity (live; safe to call any time).
void setLevel(Level level);

// Portable config-string mapping ("off"/"errors"/"normal"/"verbose").
// Unknown strings fall back to Normal.
Level   levelFromString(const QString& s);
QString levelToString(Level level);

} // namespace Logging
