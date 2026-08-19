#include "window/MainWindow.h"
#include "webview/WebViewManager.h"

#include <windowsx.h>    // GET_X_LPARAM / GET_Y_LPARAM
#include <commctrl.h>    // TaskDialogIndirect
#include <shellapi.h>    // ShellExecuteW
#include <vector>

namespace {
constexpr wchar_t kClassName[] = L"WhatsAppPrivacyWindow";
constexpr wchar_t kTitle[]     = L"WhatsApp Privacy";
constexpr int     kHotkeyId    = 1;   // Ctrl+Shift+P
constexpr int     kProbeHotkeyId = 3; // Ctrl+Shift+D -- dump selector diagnostics
constexpr UINT_PTR kSelfCheckTimeoutId = 2;


// Control ids.
// System-menu command ids. Windows reserves >= 0xF000 for SC_* and masks the low
// 4 bits of wParam in WM_SYSCOMMAND, so these must be below 0xF000 and 16-aligned.
constexpr int kIdMaster        = 0x0010;
constexpr int kIdFirstCategory = 0x0020;   // 0x20,0x30,0x40,0x50
constexpr int kIdHover         = 0x0060;
constexpr int kIdAbout         = 0x0070;

constexpr wchar_t kRepoUrl[] = L"https://github.com/sanjeevnode/win-whatsapp-privacy";

struct Category { const char* key; const wchar_t* label; };
constexpr Category kCategories[4] = {
    { "names",    L"Names"    },
    { "messages", L"Messages" },
    { "pictures", L"Photos"   },
    { "previews", L"Previews" },
};
}

MainWindow::MainWindow() = default;

MainWindow::~MainWindow() = default;

// No toolbar and no checkboxes: the WebView owns the whole client area, and the
// privacy options live in the window menu (right-click the title bar, or press
// Alt+Space). Custom-painted caption buttons are not viable here -- Windows 11
// composites the frame through DWM and paints over anything drawn via
// GetWindowDC.
void MainWindow::CreateToolbar() {
    HMENU sys = GetSystemMenu(hwnd_, FALSE);
    if (!sys) return;

    AppendMenuW(sys, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(sys, MF_STRING, kIdMaster, L"Privacy Mode\tCtrl+Shift+P");
    for (int i = 0; i < 4; ++i)
        AppendMenuW(sys, MF_STRING, kIdFirstCategory + i * 16, kCategories[i].label);
    AppendMenuW(sys, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(sys, MF_STRING, kIdHover, L"Hover to reveal");
    AppendMenuW(sys, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(sys, MF_STRING, kIdAbout, L"About...");

    SyncSystemMenu();
}

// Check marks are set on demand (WM_INITMENUPOPUP) so they always match state.
void MainWindow::SyncSystemMenu() {
    HMENU sys = GetSystemMenu(hwnd_, FALSE);
    if (!sys) return;

    const auto& s = privacy_.Get();
    CheckMenuItem(sys, kIdMaster, MF_BYCOMMAND | (s.on ? MF_CHECKED : MF_UNCHECKED));

    const bool flags[4] = { s.names, s.messages, s.pictures, s.previews };
    for (int i = 0; i < 4; ++i) {
        CheckMenuItem(sys, kIdFirstCategory + i * 16,
                      MF_BYCOMMAND | (flags[i] ? MF_CHECKED : MF_UNCHECKED));
        // Categories are gated by the master toggle anyway.
        EnableMenuItem(sys, kIdFirstCategory + i * 16,
                       MF_BYCOMMAND | (s.on ? MF_ENABLED : MF_GRAYED));
    }
    CheckMenuItem(sys, kIdHover,
                  MF_BYCOMMAND | (s.hoverReveal ? MF_CHECKED : MF_UNCHECKED));
    EnableMenuItem(sys, kIdHover, MF_BYCOMMAND | (s.on ? MF_ENABLED : MF_GRAYED));
}

void MainWindow::LayoutChildren() {
    if (webview_) webview_->Resize();
}

// Reads FileVersion out of our own version resource, so the dialog always shows
// the version CI stamped rather than a hardcoded string.
static std::wstring AppVersion() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);

    DWORD dummy = 0;
    const DWORD size = GetFileVersionInfoSizeW(path, &dummy);
    if (!size) return L"unknown";

    std::vector<BYTE> buf(size);
    if (!GetFileVersionInfoW(path, 0, size, buf.data())) return L"unknown";

    VS_FIXEDFILEINFO* fi = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(buf.data(), L"\\", reinterpret_cast<LPVOID*>(&fi), &len) || !fi)
        return L"unknown";

    wchar_t out[64];
    swprintf_s(out, L"%u.%u.%u",
               HIWORD(fi->dwFileVersionMS), LOWORD(fi->dwFileVersionMS),
               HIWORD(fi->dwFileVersionLS));
    return out;
}

void MainWindow::ShowAbout() {
    const std::wstring version = L"Version " + AppVersion();
    const std::wstring body =
        L"Blurs names, messages, photos and previews in WhatsApp Web.\n\n"
        L"Ctrl+Shift+P toggles privacy instantly, from anywhere in the window.\n"
        L"Right-click the title bar (or press Alt+Space) for per-category options.\n"
        L"Hover over anything blurred to peek at it.\n\n"
        L"Everything runs locally: no server, no accounts, and chat content is "
        L"never written to disk or logged.\n\n"
        // ASCII only: this file is UTF-8 but MSVC reads sources as ANSI unless
        // told otherwise, so non-ASCII literals arrive as mojibake.
        L"<A HREF=\"https://sanjeevnode.in\">sanjeevnode.in</A>"
        L"          "
        L"<A HREF=\"" + std::wstring(kRepoUrl) + L"\">Source code</A>";

    TASKDIALOGCONFIG cfg{ sizeof(cfg) };
    cfg.hwndParent           = hwnd_;
    cfg.dwFlags              = TDF_ENABLE_HYPERLINKS | TDF_USE_HICON_MAIN;
    cfg.hMainIcon            = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),
                                   MAKEINTRESOURCEW(1), IMAGE_ICON, 48, 48, LR_SHARED));
    cfg.dwCommonButtons      = TDCBF_CLOSE_BUTTON;
    cfg.pszWindowTitle       = L"About WhatsApp Privacy";
    cfg.pszMainInstruction   = L"WhatsApp Privacy";
    cfg.pszContent           = body.c_str();
    cfg.pszFooter            = version.c_str();
    // Hyperlinks are inert unless the app opens them itself.
    cfg.pfCallback = [](HWND, UINT msg, WPARAM, LPARAM lp, LONG_PTR) -> HRESULT {
        if (msg == TDN_HYPERLINK_CLICKED)
            ShellExecuteW(nullptr, L"open", reinterpret_cast<LPCWSTR>(lp),
                          nullptr, nullptr, SW_SHOWNORMAL);
        return S_OK;
    };

    if (FAILED(TaskDialogIndirect(&cfg, nullptr, nullptr, nullptr))) {
        // TaskDialog needs a comctl32 v6 manifest; fall back if it is missing.
        MessageBoxW(hwnd_,
            (L"WhatsApp Privacy " + AppVersion() +
             L"\n\nhttps://sanjeevnode.in\n" + kRepoUrl).c_str(),
            L"About WhatsApp Privacy", MB_OK | MB_ICONINFORMATION);
    }
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
        // No toolbar any more: the WebView owns the entire client area.
        webview_->SetTopInset(0);
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
        RegisterHotKey(hwnd_, kProbeHotkeyId, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'D');
        return 0;

    // Right-click on the title bar (and Alt+Space) opens the window menu; the
    // privacy items are appended to it in CreateToolbar(). Custom-painted
    // caption buttons do not survive DWM composition on Windows 11, so the
    // system menu is the reliable place for this.
    case WM_SYSCOMMAND:
        if (wp == kIdMaster) { privacy_.Toggle(); return 0; }
        for (int i = 0; i < 4; ++i) {
            if (wp == static_cast<WPARAM>(kIdFirstCategory + i * 16)) {
                privacy_.ToggleCategory(kCategories[i].key);
                return 0;
            }
        }
        if (wp == kIdHover) { privacy_.ToggleCategory("hoverReveal"); return 0; }
        if (wp == kIdAbout) { ShowAbout(); return 0; }
        break;

    case WM_INITMENUPOPUP:
        SyncSystemMenu();   // refresh check marks before the menu is shown
        break;              // must still reach DefWindowProc

    case WM_HOTKEY:
        if (wp == kHotkeyId) {
            privacy_.Toggle();   // sink pushes it to the page
            SyncSystemMenu();    // hotkey and menu share one source of truth
        } else if (wp == kProbeHotkeyId && webview_) {
            webview_->DumpDiagnostics();
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
