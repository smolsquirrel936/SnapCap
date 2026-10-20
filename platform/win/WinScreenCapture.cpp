#include "platform/win/WinScreenCapture.h"

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#include <shellscalingapi.h>   // GetDpiForMonitor (cursor sized to the OS pointer size)

namespace {

// BitBlt the given screen rect (physical pixels) into a top-down RGB32 QImage.
QImage grabPhysical(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0)
        return {};

    HDC screenDC = GetDC(nullptr);
    HDC memDC = CreateCompatibleDC(screenDC);
    HBITMAP bmp = CreateCompatibleBitmap(screenDC, w, h);
    HGDIOBJ old = SelectObject(memDC, bmp);

    BitBlt(memDC, 0, 0, w, h, screenDC, x, y, SRCCOPY | CAPTUREBLT);

    QImage img(w, h, QImage::Format_RGB32);   // 0xffRRGGBB == BGRA bytes, matches GetDIBits
    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;               // negative -> top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    GetDIBits(memDC, bmp, 0, h, img.bits(), &bi, DIB_RGB_COLORS);

    SelectObject(memDC, old);
    DeleteObject(bmp);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);
    return img;
}

} // namespace

QImage WinScreenCapture::grabVirtualDesktop()
{
    const int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return grabPhysical(x, y, w, h);
}

QImage WinScreenCapture::grabRect(const QRect& rect)
{
    return grabPhysical(rect.x(), rect.y(), rect.width(), rect.height());
}

platform::CursorImage WinScreenCapture::grabCursor()
{
    CURSORINFO ci;
    ZeroMemory(&ci, sizeof(ci));
    ci.cbSize = sizeof(ci);
    if (!GetCursorInfo(&ci) || !(ci.flags & CURSOR_SHOWING) || !ci.hCursor)
        return {};

    ICONINFO ii;
    ZeroMemory(&ii, sizeof(ii));
    if (!GetIconInfo(ci.hCursor, &ii))
        return {};

    // Native bitmap size (for hotspot scaling + aspect): prefer the colour bitmap;
    // monochrome cursors have no colour bitmap and pack AND+XOR masks vertically,
    // so the real height is half.
    BITMAP bm;
    ZeroMemory(&bm, sizeof(bm));
    int nw = 0, nh = 0;
    if (ii.hbmColor && GetObject(ii.hbmColor, sizeof(bm), &bm)) {
        nw = bm.bmWidth;
        nh = bm.bmHeight;
    } else if (ii.hbmMask && GetObject(ii.hbmMask, sizeof(bm), &bm)) {
        nw = bm.bmWidth;
        nh = bm.bmHeight / 2;
    }

    // Render the cursor at the size Windows actually displays it on the cursor's
    // monitor: the accessibility base pointer size (HKCU\Control Panel\Cursors\
    // CursorBaseSize, logical px, default 32) scaled to that monitor's DPI. This
    // matches the on-screen cursor regardless of GetIconInfo's bitmap resolution.
    // SnipOverlay divides by the monitor's Qt DPR, recovering the logical size.
    DWORD baseSize = 32, cb = sizeof(baseSize);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Control Panel\\Cursors", L"CursorBaseSize",
                     RRF_RT_REG_DWORD, nullptr, &baseSize, &cb) != ERROR_SUCCESS
        || baseSize == 0)
        baseSize = 32;
    UINT dpiX = 96, dpiY = 96;
    if (HMONITOR mon = MonitorFromPoint(ci.ptScreenPos, MONITOR_DEFAULTTONEAREST))
        GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    const int target = MulDiv(int(baseSize), int(dpiX), 96);   // physical px, square

    platform::CursorImage out;
    if (target > 0 && nw > 0 && nh > 0) {
        HDC screenDC = GetDC(nullptr);

        // GDI's DrawIconEx onto a 32-bit DIB does not fill a usable alpha channel
        // (it comes out all-zero -> fully transparent). Recover alpha by drawing the
        // cursor twice, over black and over white, then deriving per-pixel coverage
        // from the difference (transparent px differ by 255; opaque px are identical).
        // The black-background render already holds the premultiplied colour.
        auto renderOver = [&](COLORREF bg) -> QImage {
            HDC memDC = CreateCompatibleDC(screenDC);
            BITMAPINFO bi;
            ZeroMemory(&bi, sizeof(bi));
            bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bi.bmiHeader.biWidth = target;
            bi.bmiHeader.biHeight = -target;      // negative -> top-down
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;
            void* bits = nullptr;
            HBITMAP dib = CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
            QImage img;
            if (dib && bits) {
                HGDIOBJ old = SelectObject(memDC, dib);
                RECT r{0, 0, target, target};
                HBRUSH br = CreateSolidBrush(bg);
                FillRect(memDC, &r, br);
                DeleteObject(br);
                DrawIconEx(memDC, 0, 0, ci.hCursor, target, target, 0, nullptr, DI_NORMAL);
                img = QImage(reinterpret_cast<uchar*>(bits), target, target,
                             QImage::Format_RGB32).copy();
                SelectObject(memDC, old);
            }
            if (dib)
                DeleteObject(dib);
            DeleteDC(memDC);
            return img;
        };

        const QImage onBlack = renderOver(RGB(0, 0, 0));
        const QImage onWhite = renderOver(RGB(255, 255, 255));
        if (!onBlack.isNull() && !onWhite.isNull()) {
            QImage result(target, target, QImage::Format_ARGB32_Premultiplied);
            for (int y = 0; y < target; ++y) {
                const QRgb* pb = reinterpret_cast<const QRgb*>(onBlack.constScanLine(y));
                const QRgb* pw = reinterpret_cast<const QRgb*>(onWhite.constScanLine(y));
                QRgb* pd = reinterpret_cast<QRgb*>(result.scanLine(y));
                for (int x = 0; x < target; ++x) {
                    int a = 255 - (qRed(pw[x]) - qRed(pb[x]));   // gray bg -> use any channel
                    a = qBound(0, a, 255);
                    pd[x] = qRgba(qRed(pb[x]), qGreen(pb[x]), qBlue(pb[x]), a);  // premultiplied
                }
            }
            out.image = result;
            out.visible = true;
            // Hotspot scales from native bitmap coords to the rendered target size.
            out.hotspot = QPoint(MulDiv(int(ii.xHotspot), target, nw),
                                 MulDiv(int(ii.yHotspot), target, nh));
        }

        ReleaseDC(nullptr, screenDC);
    }

    if (ii.hbmColor)
        DeleteObject(ii.hbmColor);
    if (ii.hbmMask)
        DeleteObject(ii.hbmMask);
    return out;
}
