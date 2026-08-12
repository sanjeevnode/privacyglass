#include "window/MainWindow.h"
#include "webview/WebViewManager.h"

namespace {
constexpr wchar_t kClassName[] = L"WhatsAppPrivacyWindow";
constexpr wchar_t kTitle[]     = L"WhatsApp Privacy";
constexpr int     kHotkeyId    = 1;   // Ctrl+Shift+P
constexpr UINT_PTR kSelfCheckTimeoutId = 2;

constexpr int kToolbarHeight = 40;

// Control ids.
constexpr int kIdMaster = 100;
constexpr int kIdFirstCategory = 101;   // 101..104 in kCategories order
constexpr int kIdHover = 110;

struct Category { const char* key; const wchar_t* label; };
constexpr Category kCategories[4] = {
    { "names",    L"Names"    },
    { "messages", L"Messages" },
    { "pictures", L"Photos"   },
    { "previews", L"Previews" },
};
}

MainWindow::MainWindow() = default;

MainWindow::~MainWindow() {
    if (font_) DeleteObject(font_);
}

void MainWindow::CreateToolbar() {
    const HINSTANCE inst = GetModuleHandleW(nullptr);

    toolbar_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT,
                               0, 0, 0, kToolbarHeight, hwnd_, nullptr, inst, nullptr);

    // Match the shell UI font instead of the 1990s system default.
    NONCLIENTMETRICSW ncm{ sizeof(ncm) };
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
        font_ = CreateFontIndirectW(&ncm.lfMessageFont);

    auto styleChild = [&](HWND h) {
        if (font_) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    };

    master_ = CreateWindowExW(0, L"BUTTON", L"Privacy Mode",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        0, 0, 0, 0, toolbar_, reinterpret_cast<HMENU>(kIdMaster), inst, nullptr);
    styleChild(master_);

    for (int i = 0; i < 4; ++i) {
        checks_[i] = CreateWindowExW(0, L"BUTTON", kCategories[i].label,
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            0, 0, 0, 0, toolbar_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdFirstCategory + i)),
            inst, nullptr);
        styleChild(checks_[i]);
    }

    hover_ = CreateWindowExW(0, L"BUTTON", L"Hover to reveal",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        0, 0, 0, 0, toolbar_, reinterpret_cast<HMENU>(kIdHover), inst, nullptr);
    styleChild(hover_);

    SyncToolbar();
}

void MainWindow::LayoutChildren() {
    RECT rc{};
    GetClientRect(hwnd_, &rc);

    if (toolbar_)
        SetWindowPos(toolbar_, nullptr, 0, 0, rc.right, kToolbarHeight,
                     SWP_NOZORDER);

    // Lay the checkboxes out left-to-right, sized to their text.
    int x = 10;
    const int y = (kToolbarHeight - 22) / 2;
    HDC dc = GetDC(toolbar_);
    HGDIOBJ old = font_ ? SelectObject(dc, font_) : nullptr;

    auto place = [&](HWND h, const wchar_t* text, int pad) {
        SIZE sz{};
        GetTextExtentPoint32W(dc, text, lstrlenW(text), &sz);
        const int w = sz.cx + pad;
        SetWindowPos(h, nullptr, x, y, w, 22, SWP_NOZORDER);
        x += w + 12;
    };

    if (master_) place(master_, L"Privacy Mode", 28);
    x += 8;   // visual gap between master and the per-category group
    for (int i = 0; i < 4; ++i)
        if (checks_[i]) place(checks_[i], kCategories[i].label, 26);
    x += 8;
    if (hover_) place(hover_, L"Hover to reveal", 26);

    if (old) SelectObject(dc, old);
    ReleaseDC(toolbar_, dc);

    if (webview_) webview_->Resize();
}

// Pushes PrivacyManager state into the checkboxes. Categories are disabled while
// the master toggle is off, since it gates them anyway.
void MainWindow::SyncToolbar() {
    if (!master_) return;
    syncing_ = true;

    const auto& s = privacy_.Get();
    SendMessageW(master_, BM_SETCHECK, s.on ? BST_CHECKED : BST_UNCHECKED, 0);

    const bool flags[4] = { s.names, s.messages, s.pictures, s.previews };
    for (int i = 0; i < 4; ++i) {
        if (!checks_[i]) continue;
        SendMessageW(checks_[i], BM_SETCHECK, flags[i] ? BST_CHECKED : BST_UNCHECKED, 0);
        EnableWindow(checks_[i], s.on);
    }

    if (hover_) {
        SendMessageW(hover_, BM_SETCHECK, s.hoverReveal ? BST_CHECKED : BST_UNCHECKED, 0);
        EnableWindow(hover_, s.on);   // nothing to reveal when privacy is off
    }

    syncing_ = false;
}

// Writes the JS self-check output to selfcheck.txt beside the exe and quits with
// 0 (all passed) or 1 (any failure), so CI and the build script can gate on it.
void MainWindow::ReportSelfCheck(const std::wstring& json) {
    KillTimer(hwnd_, kSelfCheckTimeoutId);

    auto number = [&](const wchar_t* key) -> int {
        size_t p = json.find(key);
        if (p == std::wstring::npos) return -1;
        p = json.find(L':', p);
        return (p == std::wstring::npos) ? -1 : _wtoi(json.c_str() + p + 1);
    };
    const int passed = number(L"\"passed\"");
    const int total  = number(L"\"total\"");

    // "lines" holds \n-escaped [PASS]/[FAIL] rows; unescape for readability.
    std::wstring body;
    if (size_t p = json.find(L"\"lines\""); p != std::wstring::npos) {
        p = json.find(L'"', json.find(L':', p));
        for (size_t i = p + 1; i + 1 < json.size() && json[i] != L'"'; ++i) {
            if (json[i] == L'\\' && json[i + 1] == L'n') { body += L"\r\n"; ++i; }
            else body += json[i];
        }
    }

    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring out(exePath);
    out = out.substr(0, out.find_last_of(L'\\') + 1) + L"selfcheck.txt";

    std::wstring text = body + L"\r\n" + std::to_wstring(passed) + L"/" +
                        std::to_wstring(total) + L" passed\r\n" +
                        L"--- bridge messages ---\r\n" + selfCheckLog_;
    if (HANDLE f = CreateFileW(out.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        f != INVALID_HANDLE_VALUE) {
        // UTF-8 so CI logs render it correctly.
        int n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string utf8(n > 0 ? n - 1 : 0, '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, utf8.data(), n, nullptr, nullptr);
        DWORD written = 0;
        WriteFile(f, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
        CloseHandle(f);
    }

    OutputDebugStringW(text.c_str());
    PostQuitMessage(total > 0 && passed == total ? 0 : 1);
}

bool MainWindow::Create() {
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = &MainWindow::WndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kClassName;
    // Resource id 1 is the app icon (see version.rc.in). LoadIconW picks the
    // large size; hIconSm gets the 16x16 frame for the title bar.
    wc.hIcon   = static_cast<HICON>(LoadImageW(wc.hInstance, MAKEINTRESOURCEW(1),
                     IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED));
    wc.hIconSm = static_cast<HICON>(LoadImageW(wc.hInstance, MAKEINTRESOURCEW(1),
                     IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                     GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(
        0, kClassName, kTitle, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1100, 800,
        nullptr, nullptr, wc.hInstance, this);

    return hwnd_ != nullptr;
}

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    MainWindow* self = nullptr;

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<MainWindow*>(cs->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) return self->HandleMessage(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT MainWindow::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        if (!selfCheck_) CreateToolbar();
        webview_ = std::make_unique<WebViewManager>(hwnd_, &privacy_);
        webview_->SetSelfCheck(selfCheck_);
        webview_->SetTopInset(selfCheck_ ? 0 : kToolbarHeight);
        if (selfCheck_) {
            webview_->SetMessageHandler([this](const std::wstring& json) {
                selfCheckLog_ += json + L"\r\n";
                if (json.find(L"\"selfcheck\"") == std::wstring::npos) return;
                ReportSelfCheck(json);
            });
            // Don't hang forever if the page never reports.
            SetTimer(hwnd_, kSelfCheckTimeoutId, 30000, nullptr);
        }
        webview_->Initialize();
        // Global toggle; works regardless of which control has focus, including
        // when the WebView2 child window owns it.
        RegisterHotKey(hwnd_, kHotkeyId, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'P');
        return 0;

    case WM_COMMAND: {
        if (syncing_ || HIWORD(wp) != BN_CLICKED) return 0;
        const int id = LOWORD(wp);
        const HWND ctrl = reinterpret_cast<HWND>(lp);
        const bool checked = SendMessageW(ctrl, BM_GETCHECK, 0, 0) == BST_CHECKED;

        if (id == kIdMaster) {
            auto s = privacy_.Get();
            s.on = checked;
            privacy_.Set(s);
        } else if (id >= kIdFirstCategory && id < kIdFirstCategory + 4) {
            privacy_.SetCategory(kCategories[id - kIdFirstCategory].key, checked);
        } else if (id == kIdHover) {
            privacy_.SetCategory("hoverReveal", checked);
        } else {
            return 0;
        }
        SyncToolbar();   // keeps the category enable/disable state consistent
        return 0;
    }

    case WM_HOTKEY:
        if (wp == kHotkeyId) {
            privacy_.Toggle();   // sink pushes it to the page
            SyncToolbar();       // hotkey and UI share one source of truth
        }
        return 0;

    case WM_TIMER:
        if (wp == kSelfCheckTimeoutId) {
            ReportSelfCheck(L"{\"type\":\"selfcheck\",\"passed\":0,\"total\":0,"
                            L"\"lines\":\"[FAIL] TIMEOUT - messages seen:\"}");
        }
        return 0;

    case WM_SIZE:
        LayoutChildren();
        return 0;

    case WM_DESTROY:
        UnregisterHotKey(hwnd_, kHotkeyId);
        webview_.reset();   // release WebView2 before the loop exits
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}
