#include "webview/WebViewManager.h"

#include <shlobj.h>
#include <string>

using namespace Microsoft::WRL;

namespace {

// %LOCALAPPDATA%\WhatsAppPrivacy\WebView2 — persistent so the WhatsApp session
// (and therefore the QR login) survives restarts.
std::wstring UserDataFolder() {
    PWSTR local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)))
        return L"";
    std::wstring path(local);
    CoTaskMemFree(local);
    path += L"\\WhatsAppPrivacy\\WebView2";
    return path;
}

} // namespace

WebViewManager::WebViewManager(HWND host) : host_(host) {}

void WebViewManager::Initialize() {
    const std::wstring udf = UserDataFolder();

    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, udf.empty() ? nullptr : udf.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this](HRESULT r, ICoreWebView2Environment* env) {
                return OnEnvironmentReady(r, env);
            }).Get());

    if (FAILED(hr)) {
        MessageBoxW(host_,
            L"Failed to create the WebView2 environment.\n\n"
            L"Ensure the Microsoft Edge WebView2 Runtime is installed.",
            L"WhatsApp Privacy", MB_ICONERROR | MB_OK);
    }
}

HRESULT WebViewManager::OnEnvironmentReady(HRESULT result, ICoreWebView2Environment* env) {
    if (FAILED(result) || !env) return result;

    return env->CreateCoreWebView2Controller(host_,
        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
            [this](HRESULT r, ICoreWebView2Controller* c) {
                return OnControllerReady(r, c);
            }).Get());
}

HRESULT WebViewManager::OnControllerReady(HRESULT result, ICoreWebView2Controller* controller) {
    if (FAILED(result) || !controller) return result;

    controller_ = controller;
    controller_->get_CoreWebView2(&webview_);

    if (webview_) {
        // WhatsApp Web refuses to load if it thinks it's in an unsupported browser,
        // so leave the UA alone and just navigate.
        webview_->Navigate(L"https://web.whatsapp.com");
    }

    Resize();
    return S_OK;
}

void WebViewManager::Resize() {
    if (!controller_) return;
    RECT bounds{};
    GetClientRect(host_, &bounds);
    controller_->put_Bounds(bounds);
}
