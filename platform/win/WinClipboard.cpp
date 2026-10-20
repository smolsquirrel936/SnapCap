#include "platform/win/WinClipboard.h"

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>     // DROPFILES (CF_HDROP payload)

#include <QGuiApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <cstring>
#include <string>
#include <vector>

#include "core/ImageExport.h"

namespace platform {
namespace {

// Allocate a moveable global block and copy `bytes` into it (GlobalLock/Unlock).
HGLOBAL globalFromBytes(const void* bytes, size_t size)
{
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!h)
        return nullptr;
    void* dst = GlobalLock(h);
    if (!dst) {
        GlobalFree(h);
        return nullptr;
    }
    std::memcpy(dst, bytes, size);
    GlobalUnlock(h);
    return h;
}

// CF_DIBV5: a 32-bit straight-alpha DIB (bottom-up). QImage Format_ARGB32 scanlines
// are already B,G,R,A byte order on little-endian Windows, so rows copy verbatim.
HGLOBAL buildDibV5(const QImage& src)
{
    const QImage img = src.convertToFormat(QImage::Format_ARGB32);
    const int w = img.width(), h = img.height();
    const int stride = w * 4;
    const size_t total = sizeof(BITMAPV5HEADER) + size_t(stride) * h;

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, total);
    if (!hMem)
        return nullptr;
    auto* base = static_cast<uint8_t*>(GlobalLock(hMem));
    if (!base) { GlobalFree(hMem); return nullptr; }

    auto* hdr = reinterpret_cast<BITMAPV5HEADER*>(base);
    std::memset(hdr, 0, sizeof(BITMAPV5HEADER));
    hdr->bV5Size        = sizeof(BITMAPV5HEADER);
    hdr->bV5Width       = w;
    hdr->bV5Height      = h;                       // positive = bottom-up
    hdr->bV5Planes      = 1;
    hdr->bV5BitCount    = 32;
    hdr->bV5Compression = BI_BITFIELDS;
    hdr->bV5RedMask     = 0x00FF0000;
    hdr->bV5GreenMask   = 0x0000FF00;
    hdr->bV5BlueMask    = 0x000000FF;
    hdr->bV5AlphaMask   = 0xFF000000;
    hdr->bV5CSType      = LCS_WINDOWS_COLOR_SPACE;
    hdr->bV5Intent      = LCS_GM_GRAPHICS;

    uint8_t* pix = base + sizeof(BITMAPV5HEADER);
    for (int y = 0; y < h; ++y)                    // flip to bottom-up
        std::memcpy(pix + size_t(stride) * y, img.constScanLine(h - 1 - y), stride);

    GlobalUnlock(hMem);
    return hMem;
}

// CF_DIB: a 24-bit opaque DIB (bottom-up), alpha composited over white. This is
// the "Windows Bitmap" legacy apps read without an alpha channel.
HGLOBAL buildDib24(const QImage& src)
{
    QImage flat(src.size(), QImage::Format_RGB32);
    flat.fill(Qt::white);
    QPainter p(&flat);
    p.drawImage(0, 0, src);
    p.end();

    const int w = flat.width(), h = flat.height();
    const int stride = ((w * 3 + 3) / 4) * 4;      // rows padded to a DWORD boundary
    const size_t total = sizeof(BITMAPINFOHEADER) + size_t(stride) * h;

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, total);
    if (!hMem)
        return nullptr;
    auto* base = static_cast<uint8_t*>(GlobalLock(hMem));
    if (!base) { GlobalFree(hMem); return nullptr; }

    auto* hdr = reinterpret_cast<BITMAPINFOHEADER*>(base);
    std::memset(hdr, 0, sizeof(BITMAPINFOHEADER));
    hdr->biSize     = sizeof(BITMAPINFOHEADER);
    hdr->biWidth    = w;
    hdr->biHeight   = h;                            // positive = bottom-up
    hdr->biPlanes   = 1;
    hdr->biBitCount = 24;
    hdr->biCompression = BI_RGB;

    uint8_t* pix = base + sizeof(BITMAPINFOHEADER);
    for (int y = 0; y < h; ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(flat.constScanLine(h - 1 - y));
        uint8_t* out = pix + size_t(stride) * y;
        for (int x = 0; x < w; ++x) {
            const QRgb c = row[x];
            *out++ = uint8_t(qBlue(c));
            *out++ = uint8_t(qGreen(c));
            *out++ = uint8_t(qRed(c));
        }
    }

    GlobalUnlock(hMem);
    return hMem;
}

// CF_HDROP payload: DROPFILES header + a double-null-terminated wide path list.
HGLOBAL buildHDrop(const QString& path)
{
    const QString native = QDir::toNativeSeparators(path);
    const std::wstring w = native.toStdWString();
    const size_t listBytes = (w.size() + 2) * sizeof(wchar_t);   // path + 2 nulls
    const size_t total = sizeof(DROPFILES) + listBytes;

    std::vector<uint8_t> buf(total, 0);
    auto* df = reinterpret_cast<DROPFILES*>(buf.data());
    df->pFiles = sizeof(DROPFILES);
    df->fWide  = TRUE;
    auto* dst = reinterpret_cast<wchar_t*>(buf.data() + sizeof(DROPFILES));
    std::memcpy(dst, w.c_str(), w.size() * sizeof(wchar_t));      // trailing nulls already zeroed

    return globalFromBytes(buf.data(), total);
}

// Write the image to a temp file for the CF_HDROP copy-as-file mode. Returns the
// path, or empty on failure.
QString writeTempFile(const QImage& img, const QString& baseName, int quality)
{
    QString name = baseName.trimmed();
    if (name.isEmpty())
        name = QStringLiteral("SnapCap_%1.png")
                   .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss")));
    if (QFileInfo(name).suffix().isEmpty())
        name += QStringLiteral(".png");

    const QString dir = QDir(QDir::tempPath()).filePath(QStringLiteral("SnapCap"));
    QDir().mkpath(dir);
    const QString path = QDir(dir).filePath(name);
    if (!ImageExport::toFile(img, path, quality))
        return QString();
    return path;
}

} // namespace

bool WinClipboard::setImage(const QImage& img, const ImageClipboardOptions& opt)
{
    if (img.isNull())
        return false;

    // Common case: no extra formats → let Qt handle it (CF_DIBV5/CF_DIB/CF_BITMAP).
    if (!opt.addFlatDib && !opt.addFile) {
        QClipboard* cb = QGuiApplication::clipboard();
        if (!cb)
            return false;
        cb->setImage(img);
        return true;
    }

    // A temp file is needed before we own the clipboard (write can fail independently).
    QString filePath;
    if (opt.addFile) {
        filePath = writeTempFile(img, opt.fileName, opt.quality);
        if (filePath.isEmpty())
            return false;
    }

    HWND owner = GetForegroundWindow();
    if (!OpenClipboard(owner))
        return false;
    EmptyClipboard();

    bool any = false;
    if (HGLOBAL dibv5 = buildDibV5(img)) {          // base image (alpha-capable)
        if (SetClipboardData(CF_DIBV5, dibv5)) any = true; else GlobalFree(dibv5);
    }
    if (opt.addFlatDib) {
        if (HGLOBAL dib = buildDib24(img)) {        // flattened, opaque, legacy-friendly
            if (SetClipboardData(CF_DIB, dib)) any = true; else GlobalFree(dib);
        }
    }
    if (opt.addFile) {
        if (HGLOBAL drop = buildHDrop(filePath)) {
            if (SetClipboardData(CF_HDROP, drop)) any = true; else GlobalFree(drop);
        }
    }

    CloseClipboard();
    return any;
}

} // namespace platform
