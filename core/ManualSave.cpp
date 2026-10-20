#include "core/ManualSave.h"

#include <QFileInfo>

#include "core/FilenameTemplate.h"
#include "core/ImageExport.h"
#include "core/Settings.h"

namespace {

bool rememberOn(Settings* s)
{
    return s
        && s->value(QStringLiteral("output/manualSave/rememberExtension"), true).toBool();
}

// The save formats SnapCap offers (Features.md §5), in dialog order. This is the
// CURATED half: QImageWriter also writes ppm/xbm/xpm/cur, which would be noise in a
// screenshot tool's Save dialog. The other half is runtime capability — each row is
// kept only when ImageExport can actually write it, so a format whose plugin is
// missing (TIFF without qtimageformats) silently drops out instead of being offered
// and then failing.
struct Fmt
{
    const char* label;
    const char* ext;         // primary extension, matched against supportedSaveFormats()
    const char* patterns;    // every extension this row accepts
};

constexpr Fmt kFormats[] = {
    { "PNG",  "png",  "*.png" },
    { "JPEG", "jpg",  "*.jpg *.jpeg" },
    { "BMP",  "bmp",  "*.bmp" },
    { "ICO",  "ico",  "*.ico" },
    { "TIFF", "tiff", "*.tif *.tiff" },
    { "TGA",  "tga",  "*.tga" },
    { "GIF",  "gif",  "*.gif" },
    { "PDF",  "pdf",  "*.pdf" },
};

QString entryFor(const Fmt& f)
{
    return QStringLiteral("%1 (%2)").arg(QLatin1String(f.label), QLatin1String(f.patterns));
}

// True when `ext` is one of this row's accepted extensions ("jpeg" matches the JPEG row).
bool rowAccepts(const Fmt& f, const QString& ext)
{
    return QString::fromLatin1(f.patterns)
        .split(QLatin1Char(' '))
        .contains(QStringLiteral("*.") + ext);
}

} // namespace

namespace ManualSave {

const char* const kDefaultNameTemplate = "SnapCap_$yyyy-MM-dd_HH-mm-ss$.png";

QString suggestedName(Settings* s, const QMap<QString, QString>& specials)
{
    QString templ = QString::fromLatin1(kDefaultNameTemplate);
    if (s)
        templ = s->value(QStringLiteral("output/manualSave/fileName"), templ).toString();

    QString name = FilenameTemplate::expandNow(templ, specials);

    // Remember-last-extension: swap the template's extension for the one chosen in the
    // previous Save dialog, so the format sticks.
    if (rememberOn(s)) {
        const QString last =
            s->value(QStringLiteral("output/manualSave/lastExtension"), QString()).toString();
        if (!last.isEmpty() && !name.isEmpty()) {
            const QFileInfo fi(name);
            name = (fi.completeBaseName().isEmpty() ? name : fi.completeBaseName())
                   + QLatin1Char('.') + last;
        }
    }
    return name;
}

QString filterFor(const QString& fileName)
{
    const QStringList writable = ImageExport::supportedSaveFormats();
    const QString ext = QFileInfo(fileName).suffix().toLower();

    QStringList entries;
    QString own;                                  // the suggested name's own format, if offered
    for (const Fmt& f : kFormats) {
        if (!writable.contains(QLatin1String(f.ext)))
            continue;                             // no writer for it -> never offer it
        if (!ext.isEmpty() && own.isEmpty() && rowAccepts(f, ext))
            own = entryFor(f);                    // hoisted to the front below
        else
            entries << entryFor(f);
    }
    if (!own.isEmpty())
        entries.prepend(own);
    return entries.join(QStringLiteral(";;"));
}

void rememberExtension(Settings* s, const QString& chosenPath)
{
    if (rememberOn(s))
        s->setValue(QStringLiteral("output/manualSave/lastExtension"),
                    QFileInfo(chosenPath).suffix().toLower());
}

} // namespace ManualSave
