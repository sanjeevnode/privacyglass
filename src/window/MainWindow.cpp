#include "window/MainWindow.h"
#include "webview/WebViewManager.h"

namespace {
constexpr wchar_t kClassName[] = L"WhatsAppPrivacyWindow";
constexpr wchar_t kTitle[]     = L"WhatsApp Privacy";
constexpr int     kHotkeyId    = 1;   // Ctrl+Shift+P
constexpr UINT_PTR kSelfCheckTimeoutId = 2;
}

MainWindow::MainWindow() = default;
MainWindow::~MainWindow() = default;

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
    wc.hIcon         = LoadIconW(nullptr, IDI_APPLICATION);
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
        webview_ = std::make_unique<WebViewManager>(hwnd_, &privacy_);
        webview_->SetSelfCheck(selfCheck_);
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

    case WM_HOTKEY:
        if (wp == kHotkeyId) privacy_.Toggle();   // sink pushes it to the page
        return 0;

    case WM_TIMER:
        if (wp == kSelfCheckTimeoutId) {
            ReportSelfCheck(L"{\"type\":\"selfcheck\",\"passed\":0,\"total\":0,"
                            L"\"lines\":\"[FAIL] TIMEOUT - messages seen:\"}");
        }
        return 0;

    case WM_SIZE:
        if (webview_) webview_->Resize();
        return 0;

    case WM_DESTROY:
        UnregisterHotKey(hwnd_, kHotkeyId);
        webview_.reset();   // release WebView2 before the loop exits
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}
