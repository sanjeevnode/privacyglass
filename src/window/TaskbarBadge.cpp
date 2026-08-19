#include "window/TaskbarBadge.h"

#include <shobjidl.h>
#include <string>

namespace {

// Draws a filled circle with the count centred, as a 16x16 32-bit icon.
// Built at runtime rather than shipped as resources so any count can render.
HICON MakeBadgeIcon(int count) {
    const int size = 16;

    BITMAPINFO bi{};
    bi.bmiHeader.biSize        = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth       = size;
    bi.bmiHeader.biHeight      = -size;      // top-down
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP colour = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!colour) return nullptr;

    HDC dc = CreateCompatibleDC(nullptr);
    HGDIOBJ oldBmp = SelectObject(dc, colour);

    // Transparent ground; the circle supplies its own alpha.
    memset(bits, 0, static_cast<size_t>(size) * size * 4);

    // WhatsApp-style unread green reads as "messages" at a glance.
    HBRUSH fill = CreateSolidBrush(RGB(0x25, 0xD3, 0x66));
    HPEN   pen  = CreatePen(PS_SOLID, 1, RGB(0x1E, 0xA9, 0x54));
    HGDIOBJ oldBrush = SelectObject(dc, fill);
    HGDIOBJ oldPen   = SelectObject(dc, pen);
    Ellipse(dc, 0, 0, size, size);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(fill);
    DeleteObject(pen);

    // GDI text drawing does not set alpha, so force every pixel inside the
    // circle opaque afterwards; otherwise the glyph renders invisible.
    const std::wstring text = count > 9 ? L"9+" : std::to_wstring(count);

    HFONT font = CreateFontW(count > 9 ? 9 : 11, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                             L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    RECT r{ 0, 0, size, size };
    DrawTextW(dc, text.c_str(), -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    SelectObject(dc, oldFont);
    DeleteObject(font);

    // Restore alpha: anything that was painted (circle or glyph) is opaque.
    auto* px = static_cast<DWORD*>(bits);
    for (int i = 0; i < size * size; ++i)
        if (px[i] & 0x00FFFFFF) px[i] |= 0xFF000000;

    SelectObject(dc, oldBmp);
    DeleteDC(dc);

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
