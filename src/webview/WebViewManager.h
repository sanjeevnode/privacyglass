#pragma once
#include <windows.h>
#include <wrl.h>
#include <WebView2.h>

// Owns the WebView2 environment/controller for one HWND.
// Creation is async; Resize() is a no-op until the controller exists.
class WebViewManager {
public:
    explicit WebViewManager(HWND host);

    void Initialize();          // kicks off async env + controller creation
    void Resize();              // fit webview to host client area

private:
    HRESULT OnEnvironmentReady(HRESULT, ICoreWebView2Environment*);
    HRESULT OnControllerReady(HRESULT, ICoreWebView2Controller*);

    HWND host_;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
    Microsoft::WRL::ComPtr<ICoreWebView2> webview_;
};
