#include "webview/WebViewManager.h"
#include "privacy/PrivacyManager.h"
#include "WebAssets.h"          // generated: kWebAsset_selectorsJs, _privacyCss, _privacyJs

#include <WebView2EnvironmentOptions.h>

#include <shlobj.h>
#include <string>

using namespace Microsoft::WRL;

namespace {

// %LOCALAPPDATA%\WhatsAppPrivacy\WebView2 -- persistent so the WhatsApp session
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

std::wstring Widen(const char* utf8) {
    if (!utf8 || !*utf8) return L"";
    const int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (len <= 0) return L"";
    std::wstring out(static_cast<size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out.data(), len);
    return out;
}

// Embeds a UTF-8 asset as a JS string literal assigned to `varName`.
std::wstring AsJsStringAssignment(const wchar_t* varName, const char* utf8) {
    std::wstring src = Widen(utf8);
    std::wstring escaped;
    escaped.reserve(src.size() + 64);
    for (wchar_t c : src) {
        switch (c) {
        case L'\\': escaped += L"\\\\"; break;
        case L'"':  escaped += L"\\\""; break;
        case L'\n': escaped += L"\\n";  break;
        case L'\r': break;
        // </script> inside a string would terminate an inline script block.
        case L'<':  escaped += L"\\x3c"; break;
        default:    escaped += c;        break;
        }
    }
    return std::wstring(L"window.") + varName + L" = \"" + escaped + L"\";\n";
}

} // namespace

WebViewManager::WebViewManager(HWND host, PrivacyManager* privacy)
    : host_(host), privacy_(privacy) {}

void WebViewManager::Initialize() {
    const std::wstring udf = UserDataFolder();

    // WebView2 defaults to a process-per-site model sized for a browser, which
    // costs ~1.5 GB across ~25 processes for what is effectively one page.
    // Sharing one renderer and capping the JS heap brings that down a long way.
    auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
    options->put_AdditionalBrowserArguments(
        L"--process-per-site "                    // one renderer for web.whatsapp.com
        L"--renderer-process-limit=2 "
        L"--disable-features=SpareRendererForSitePerProcess "  // no idle spare renderer
        L"--js-flags=--max-old-space-size=256");

    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, udf.empty() ? nullptr : udf.c_str(), options.Get(),
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
    if (!webview_) return E_FAIL;

    InjectPrivacyAssets();

    // Inbound bridge messages.
    EventRegistrationToken token{};
    webview_->add_WebMessageReceived(
        Callback<ICoreWebView2WebMessageReceivedEventHandler>(
            [this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                LPWSTR raw = nullptr;
                if (SUCCEEDED(args->TryGetWebMessageAsString(&raw)) && raw) {
                    HandleWebMessage(raw);
                    CoTaskMemFree(raw);
                }
                return S_OK;
            }).Get(), &token);

    // Push state to every new document, including SPA reloads and re-logins.
    EventRegistrationToken navToken{};
    webview_->add_NavigationCompleted(
        Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [this](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
                if (privacy_) PostJson(privacy_->ToJson());
                if (selfCheck_)
                    webview_->ExecuteScript(Widen(kWebAsset_test_privacyJs).c_str(), nullptr);
                return S_OK;
            }).Get(), &navToken);

    // Native state changes -> page.
    if (privacy_) {
        privacy_->SetSink([this](const PrivacyManager::State&) {
            if (privacy_) PostJson(privacy_->ToJson());
        });
    }

    if (selfCheck_) {
        // Blank page + mock DOM; the engine still injects on document-created,
        // so this exercises the real injection path.
        webview_->Navigate(L"about:blank");
    } else {
        webview_->Navigate(L"https://web.whatsapp.com");
    }
    Resize();
    return S_OK;
}

void WebViewManager::InjectPrivacyAssets() {
    // AddScriptToExecuteOnDocumentCreated runs before any page script on EVERY
    // document -- this is what makes the boot blur beat WhatsApp's first paint.
    std::wstring bootstrap;
    bootstrap += AsJsStringAssignment(L"__wapPrivacyCss", kWebAsset_privacyCss);
    bootstrap += Widen(kWebAsset_selectorsJs);
    bootstrap += L"\n";
    // Record a bootstrap throw so the self-check can report it; without this the
    // engine just silently fails to define its globals.
    bootstrap += L"try{\n";
    bootstrap += Widen(kWebAsset_privacyJs);
    bootstrap += L"\n}catch(e){window.__wapBootError=(e&&e.stack)||String(e);}\n";

    webview_->AddScriptToExecuteOnDocumentCreated(bootstrap.c_str(), nullptr);
}

void WebViewManager::HandleWebMessage(const std::wstring& json) {
    if (onMessage_) onMessage_(json);

    // On {"type":"ready"} the page has a fresh engine; re-push authoritative state.
    if (json.find(L"\"ready\"") != std::wstring::npos && privacy_)
        PostJson(privacy_->ToJson());
}

void WebViewManager::PostJson(const std::wstring& json) {
    if (webview_) webview_->PostWebMessageAsString(json.c_str());
}

void WebViewManager::Resize() {
    if (!controller_) return;
    RECT bounds{};
    GetClientRect(host_, &bounds);
    bounds.top = topInset_;                       // leave room for the toolbar
    if (bounds.bottom < bounds.top) bounds.bottom = bounds.top;
    controller_->put_Bounds(bounds);
}
