#pragma once
#include <windows.h>
#include <wrl.h>
#include <WebView2.h>

#include <functional>
#include <string>

class PrivacyManager;

// Owns the WebView2 environment/controller for one HWND, injects the privacy
// assets, and carries the two-way native<->JS bridge.
class WebViewManager {
public:
    WebViewManager(HWND host, PrivacyManager* privacy);

    // Self-check mode: load a blank page and run web/test_privacy.js instead of
    // navigating to WhatsApp. Requires no login and no network.
    void SetSelfCheck(bool on) { selfCheck_ = on; }

    void Initialize();
    void Resize();

    // Pixels reserved at the top of the client area for the native toolbar.
    void SetTopInset(int px) { topInset_ = px; }

    // Writes selector/perf diagnostics next to the exe (Ctrl+Shift+D).
    void DumpDiagnostics();

    // Fire-and-forget JSON to the page. Safe before the webview exists (drops).
    void PostJson(const std::wstring& json);

    // Called for each inbound JS message (raw JSON), for diagnostics.
    void SetMessageHandler(std::function<void(const std::wstring&)> h) {
        onMessage_ = std::move(h);
    }

private:
    HRESULT OnEnvironmentReady(HRESULT, ICoreWebView2Environment*);
    HRESULT OnControllerReady(HRESULT, ICoreWebView2Controller*);
    void    InjectPrivacyAssets();
    void    HandleWebMessage(const std::wstring& json);

    HWND host_;
    PrivacyManager* privacy_;
    bool selfCheck_ = false;
    int  topInset_ = 0;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
    Microsoft::WRL::ComPtr<ICoreWebView2> webview_;
    std::function<void(const std::wstring&)> onMessage_;
};
