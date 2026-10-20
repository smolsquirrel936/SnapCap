#pragma once

#include <QMap>
#include <QString>

class Settings;

// The Settings-driven pieces of a manual "Save as..." dialog (Output -> Manual Save),
// shared by the snip overlay and the pin window: what to call the file, which formats
// to offer, and remembering the extension the user picked. The dialog itself stays in
// ui/ — this is pure Qt-Core logic, so it is headless unit-testable. A null Settings
// falls back to the built-in defaults.
namespace ManualSave {

// Template used when output/manualSave/fileName is unset.
extern const char* const kDefaultNameTemplate;

// Suggested file NAME (not a full path): the output/manualSave/fileName template
// expanded against the current date/time, with its extension swapped for the
// remembered output/manualSave/lastExtension when rememberExtension is on.
// `specials` feeds FilenameTemplate's %title% etc. (empty = they resolve to empty).
QString suggestedName(Settings* s, const QMap<QString, QString>& specials = {});

// Save-dialog filter listing `fileName`'s own format first, then the rest of the
// offered set (Features.md §5). Only formats ImageExport can actually write appear,
// so the dialog can never offer one that would fail — a format whose Qt plugin is
// missing just drops out of the list.
QString filterFor(const QString& fileName);

// Record the extension the user actually chose, so the next Save dialog offers it.
// No-op when output/manualSave/rememberExtension is off — otherwise the app would
// keep "remembering" with the box unchecked.
void rememberExtension(Settings* s, const QString& chosenPath);

} // namespace ManualSave
