#pragma once

#include <QColor>
#include <QFont>
#include <QImage>
#include <QString>
#include <QStringList>
#include <optional>

class QMimeData;

// Classify the system clipboard into something pin-able and render it to an image.
// Follows the paste rules in Features.md §3: image; color
// (RGB ints/floats or HEX); text (plain/HTML); file paths. Pure logic (Qt Gui
// only, no widgets/Win32) so classification + color parsing are unit-tested
// headlessly. Stage 5.
namespace clip {

enum class Kind { None, Image, Color, PlainText, Html, FilePaths };

struct PasteOptions {
    bool textToImage = true;        // when false, text clipboards produce no content
    bool ignoreHtml = false;        // when true, HTML clipboards render as plain text
    bool pasteFilePaths = true;     // when false, path-list clipboards are ignored
    bool forceHtmlWrap = true;      // wrap rendered text to maxWidth
    int  maxWidth = 600;            // wrap width / max card width (logical px)
    bool uppercaseHex = false;      // color-card HEX casing

    // Text→image render styling. The defaults reproduce renderText()'s original
    // hardcoded look, so a default-constructed PasteOptions renders identically.
    int    pageMargin = 10;                 // inner padding around the text (logical px)
    QFont  textFont = QFont(QString(), 12); // default family @ 12pt
    QColor bgColor = Qt::white;             // page background
    QColor textColor = Qt::black;           // ink color
};

// The classified result. `image` is the visual to pin (always set for a non-None
// kind). `color`/`text`/`files` carry the source data (e.g. for color-card edit
// or Ctrl+Shift+C copy-text).
struct Content {
    Kind kind = Kind::None;
    QImage image;
    QColor color;
    QString text;
    QStringList files;

    bool isValid() const { return kind != Kind::None && !image.isNull(); }
};

// Inspect the clipboard mime data and produce a pin-able Content.
Content classify(const QMimeData& mime, const PasteOptions& opt);

// Parse a color string: "#rgb"/"#rrggbb"/"#aarrggbb", "r, g, b" ints (0-255), or
// three 0-1 floats (a token containing '.' makes the triple float-valued). Shares
// the 0-255/0-1 conventions with core/ColorPicker formatting. nullopt if not a color.
std::optional<QColor> parseColor(const QString& s);

// Renderers (also usable directly).
QImage renderText(const QString& text, const PasteOptions& opt);
// Rich-text render of an HTML fragment. bgColor/textFont/textColor act as *defaults*
// that markup in the fragment overrides (that's what PasteOptions::ignoreHtml defeats).
QImage renderHtml(const QString& html, const PasteOptions& opt);
QImage renderColorCard(const QColor& c, bool uppercaseHex);
QImage renderFilePaths(const QStringList& files, const PasteOptions& opt);

} // namespace clip
