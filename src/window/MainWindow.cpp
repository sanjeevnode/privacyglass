#include "window/MainWindow.h"
#include "webview/WebViewManager.h"

namespace {
constexpr wchar_t kClassName[] = L"WhatsAppPrivacyWindow";
constexpr wchar_t kTitle[]     = L"WhatsApp Privacy";
}

MainWindow::MainWindow() = default;
MainWindow::~MainWindow() = default;

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
        webview_ = std::make_unique<WebViewManager>(hwnd_);
        webview_->Initialize();
        return 0;

    case WM_SIZE:
        if (webview_) webview_->Resize();
        return 0;

    case WM_DESTROY:
        webview_.reset();   // release WebView2 before the loop exits
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}
