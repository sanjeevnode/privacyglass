#include "window/TaskbarBadge.h"

#include <shobjidl.h>
#include <string>

namespace {

// Red notification badge: a circle for one digit, widening into a pill for
// longer numbers -- the familiar phone/desktop convention.
//
// Drawn at runtime rather than shipped as resources so any count can render.
// The bitmap is oversampled and downscaled because GDI has no antialiasing:
// drawing the circle directly at 32px leaves visibly jagged edges.
HICON MakeBadgeIcon(int count) {
    const int size  = 32;    // final icon; the shell scales it as needed
    const int scale = 4;     // supersample factor
    const int big   = size * scale;

    const std::wstring text = count > 999 ? L"999+" : std::to_wstring(count);

    HDC screen = GetDC(nullptr);

    // --- 1. render oversized -------------------------------------------------
    BITMAPINFO bi{};
    bi.bmiHeader.biSize        = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth       = big;
    bi.bmiHeader.biHeight      = -big;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bigBits = nullptr;
    HBITMAP bigBmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bigBits, nullptr, 0);
    if (!bigBmp) { ReleaseDC(nullptr, screen); return nullptr; }

    HDC bigDc = CreateCompatibleDC(screen);
    HGDIOBJ oldBigBmp = SelectObject(bigDc, bigBmp);
    memset(bigBits, 0, static_cast<size_t>(big) * big * 4);

    // Font first: the pill has to be wide enough for the text it will hold.
    const int fontH = static_cast<int>(big * 0.62);
    HFONT font = CreateFontW(fontH, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                             L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(bigDc, font);

    SIZE ext{};
    GetTextExtentPoint32W(bigDc, text.c_str(), static_cast<int>(text.size()), &ext);

    // Height is fixed; width grows with the text, never narrower than a circle.
    const int h = big;
    int w = ext.cx + static_cast<int>(big * 0.45);   // padding either side
    if (w < h) w = h;                                // one digit stays circular
    if (w > big) w = big;                            // never exceed the canvas

    // Centre horizontally when the pill is narrower than the canvas.
    const int left = (big - w) / 2;
    const int right = left + w;

    HBRUSH fill = CreateSolidBrush(RGB(0xE8, 0x11, 0x23));   // notification red
    HGDIOBJ oldBrush = SelectObject(bigDc, fill);
    HGDIOBJ oldPen   = SelectObject(bigDc, GetStockObject(NULL_PEN));

    // RoundRect with a full-height radius gives a circle at w == h and a pill
    // when wider, so one call covers both cases.
    RoundRect(bigDc, left, 0, right + 1, h + 1, h, h);

    SelectObject(bigDc, oldBrush);
    SelectObject(bigDc, oldPen);
    DeleteObject(fill);

    SetBkMode(bigDc, TRANSPARENT);
    SetTextColor(bigDc, RGB(255, 255, 255));
    RECT tr{ left, 0, right, h };
    DrawTextW(bigDc, text.c_str(), -1, &tr,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);

    SelectObject(bigDc, oldFont);
    DeleteObject(font);

    // GDI never writes alpha, so recover it: any painted pixel becomes opaque.
    auto* bp = static_cast<DWORD*>(bigBits);
    for (int i = 0; i < big * big; ++i)
        if (bp[i] & 0x00FFFFFF) bp[i] |= 0xFF000000;

    // --- 2. downscale with alpha-weighted averaging --------------------------
    bi.bmiHeader.biWidth  = size;
    bi.bmiHeader.biHeight = -size;
    void* bits = nullptr;
    HBITMAP colour = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);

    if (colour) {
        auto* dp = static_cast<DWORD*>(bits);
        const int n = scale * scale;
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                // Accumulate alpha, and colour premultiplied by alpha, so that
                // transparent samples contribute no colour and cannot drag the
                // antialiased edge toward black.
                unsigned sumA = 0, r = 0, g = 0, b = 0;
                for (int sy = 0; sy < scale; ++sy) {
                    for (int sx = 0; sx < scale; ++sx) {
                        const DWORD p = bp[(y * scale + sy) * big + (x * scale + sx)];
                        const unsigned pa = (p >> 24) & 0xFF;
                        sumA += pa;
                        r += ((p >> 16) & 0xFF) * pa;
                        g += ((p >>  8) & 0xFF) * pa;
                        b += ( p        & 0xFF) * pa;
                    }
                }

                // A fully transparent block has no colour to recover, and
                // dividing by sumA would be a divide by zero.
                if (sumA == 0) { dp[y * size + x] = 0; continue; }

                const unsigned a = sumA / n;          // mean coverage
                dp[y * size + x] = (a << 24) | (((r / sumA) & 0xFF) << 16) |
                                   (((g / sumA) & 0xFF) << 8) | ((b / sumA) & 0xFF);
            }
        }
    }

    SelectObject(bigDc, oldBigBmp);
    DeleteDC(bigDc);
    DeleteObject(bigBmp);
    if (!colour) return nullptr;

    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO ii{};
    ii.fIcon    = TRUE;
    ii.hbmMask  = mask;
    ii.hbmColor = colour;
    HICON icon = CreateIconIndirect(&ii);

    DeleteObject(mask);
    DeleteObject(colour);
    return icon;
}

}  // namespace

int ParseUnreadCount(const wchar_t* title) {
    if (!title) return 0;

    // WhatsApp formats the title as "(3) WhatsApp". Anything else means no
    // unread messages -- including the plain "WhatsApp" and the loading states.
    const wchar_t* p = title;
    while (*p == L' ') ++p;
    if (*p != L'(') return 0;

    ++p;
    int n = 0;
    bool digits = false;
    while (*p >= L'0' && *p <= L'9') {
        // Clamp rather than overflow on an absurd title.
        if (n < 100000) n = n * 10 + (*p - L'0');
        digits = true;
        ++p;
    }
    // WhatsApp uses "(99+)" past a threshold; accept the plus.
    if (*p == L'+') ++p;
    if (!digits || *p != L')') return 0;
    return n;
}

TaskbarBadge::~TaskbarBadge() {
    if (icon_) DestroyIcon(icon_);
    if (taskbar_) taskbar_->Release();
}

bool TaskbarBadge::EnsureTaskbarList() {
    if (taskbar_) return true;
    // Fails before the taskbar button exists; the next call retries.
    if (FAILED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&taskbar_))))
        return false;
    if (FAILED(taskbar_->HrInit())) {
        taskbar_->Release();
        taskbar_ = nullptr;
        return false;
    }
    return true;
}

void TaskbarBadge::Show(HWND hwnd, int count) {
    if (count <= 0) { Clear(hwnd); return; }
    if (count == shown_) return;              // nothing changed
    if (!EnsureTaskbarList()) return;

    HICON fresh = MakeBadgeIcon(count);
    if (!fresh) return;

    // Set the new overlay before destroying the old icon: the shell reads the
    // handle during this call.
    taskbar_->SetOverlayIcon(hwnd, fresh, L"Unread messages");
    if (icon_) DestroyIcon(icon_);
    icon_  = fresh;
    shown_ = count;
}

void TaskbarBadge::Clear(HWND hwnd) {
    if (shown_ == 0) return;
    if (EnsureTaskbarList()) taskbar_->SetOverlayIcon(hwnd, nullptr, nullptr);
    if (icon_) { DestroyIcon(icon_); icon_ = nullptr; }
    shown_ = 0;
}
