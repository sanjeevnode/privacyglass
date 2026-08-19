#include "update/Updater.h"
#include "AppIdentity.h"

#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>
#include <shlobj.h>

#include <string>
#include <vector>

namespace {

// --- tiny helpers ----------------------------------------------------------

std::wstring Widen(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

// Extracts a JSON string value: "key": "value". Good enough for the handful of
// fields we need out of a response the GitHub API controls.
std::string JsonString(const std::string& json, const char* key, size_t from = 0) {
    const std::string needle = std::string("\"") + key + "\"";
    size_t p = json.find(needle, from);
    if (p == std::string::npos) return "";
    p = json.find(':', p + needle.size());
    if (p == std::string::npos) return "";
    p = json.find('"', p);
    if (p == std::string::npos) return "";
    const size_t end = json.find('"', ++p);
    return end == std::string::npos ? "" : json.substr(p, end - p);
}

// Numeric compare, so 0.1.10 correctly beats 0.1.9 (a string compare would not).
bool IsNewer(const std::wstring& candidate, const std::wstring& current) {
    auto parts = [](const std::wstring& v) {
        std::vector<int> out;
        int cur = 0; bool any = false;
        size_t i = 0;
        if (i < v.size() && (v[i] == L'v' || v[i] == L'V')) ++i;   // tolerate "v1.2.3"
        for (; i < v.size(); ++i) {
            const wchar_t c = v[i];
            if (c >= L'0' && c <= L'9') { cur = cur * 10 + (c - L'0'); any = true; }
            else if (c == L'.') { out.push_back(any ? cur : 0); cur = 0; any = false; }
            else break;   // stop at any suffix, e.g. "-beta"
        }
        out.push_back(any ? cur : 0);
        return out;
    };
    const std::vector<int> a = parts(candidate), b = parts(current);
    for (size_t i = 0; i < 3; ++i) {
        const int x = i < a.size() ? a[i] : 0;
        const int y = i < b.size() ? b[i] : 0;
        if (x != y) return x > y;
    }
    return false;
}

std::wstring CurrentVersion() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    DWORD dummy = 0;
    const DWORD size = GetFileVersionInfoSizeW(path, &dummy);
    if (!size) return L"0.0.0";
    std::vector<BYTE> buf(size);
    if (!GetFileVersionInfoW(path, 0, size, buf.data())) return L"0.0.0";
    VS_FIXEDFILEINFO* fi = nullptr; UINT len = 0;
    if (!VerQueryValueW(buf.data(), L"\\", reinterpret_cast<LPVOID*>(&fi), &len) || !fi)
        return L"0.0.0";
    wchar_t out[64];
    swprintf_s(out, L"%u.%u.%u", HIWORD(fi->dwFileVersionMS),
               LOWORD(fi->dwFileVersionMS), HIWORD(fi->dwFileVersionLS));
    return out;
}

// GETs a URL over HTTPS, following redirects. Returns "" on any failure.
std::string HttpGet(const std::wstring& host, const std::wstring& path) {
    std::string result;

    HINTERNET session = WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return result;

    // Don't let a hung server wedge the UI thread.
    WinHttpSetTimeouts(session, 10000, 10000, 15000, 15000);

    if (HINTERNET conn = WinHttpConnect(session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0)) {
        if (HINTERNET req = WinHttpOpenRequest(conn, L"GET", path.c_str(), nullptr,
                                               WINHTTP_NO_REFERER,
                                               WINHTTP_DEFAULT_ACCEPT_TYPES,
                                               WINHTTP_FLAG_SECURE)) {
            if (WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(req, nullptr)) {
                DWORD avail = 0;
                while (WinHttpQueryDataAvailable(req, &avail) && avail) {
                    std::string chunk(avail, '\0');
                    DWORD read = 0;
                    if (!WinHttpReadData(req, chunk.data(), avail, &read)) break;
                    chunk.resize(read);
                    result += chunk;
                    // Sanity cap: the releases JSON is a few KB.
                    if (result.size() > 4u * 1024 * 1024) break;
                }
            }
            WinHttpCloseHandle(req);
        }
        WinHttpCloseHandle(conn);
    }
    WinHttpCloseHandle(session);
    return result;
}

}  // namespace

bool Updater::IsNewerVersion(const std::wstring& candidate, const std::wstring& current) {
    return IsNewer(candidate, current);
}

Updater::Release Updater::Check() {
    Release r;

    const std::string json = HttpGet(L"api.github.com",
                                     L"/repos/" kRepoSlugW L"/releases/latest");
    if (json.empty()) return r;

    const std::wstring tag = Widen(JsonString(json, "tag_name"));   // e.g. "v0.1.7"
    if (tag.empty()) return r;

    std::wstring version = tag;
    if (!version.empty() && (version[0] == L'v' || version[0] == L'V'))
        version.erase(0, 1);

    if (!IsNewer(version, CurrentVersion())) return r;   // already current

    // Find the installer asset. browser_download_url appears once per asset;
    // take the first .exe.
    std::string url;
    size_t pos = 0;
    while (true) {
        const std::string candidate = JsonString(json, "browser_download_url", pos);
        if (candidate.empty()) break;
        if (candidate.size() > 4 &&
            _stricmp(candidate.c_str() + candidate.size() - 4, ".exe") == 0) {
            url = candidate;
            break;
        }
        pos = json.find(candidate, pos) + candidate.size();
    }
    if (url.empty()) return r;   // release exists but has no installer yet

    r.available    = true;
    r.version      = version;
    r.installerUrl = Widen(url);
    r.pageUrl      = Widen(JsonString(json, "html_url"));
    return r;
}

bool Updater::DownloadAndRun(HWND parent, const Release& rel) {
    // Split the download URL into host + path for WinHttp.
    std::wstring url = rel.installerUrl;
    const std::wstring scheme = L"https://";
    if (url.compare(0, scheme.size(), scheme) != 0) return false;
    url.erase(0, scheme.size());
    const size_t slash = url.find(L'/');
    if (slash == std::wstring::npos) return false;

    const std::wstring host = url.substr(0, slash);
    const std::wstring path = url.substr(slash);

    const std::string body = HttpGet(host, path);
    if (body.size() < 100000) return false;   // an installer is ~1 MB; too small means failure

    wchar_t tmp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tmp);
    const std::wstring out =
        std::wstring(tmp) + L"PrivacyGlass-" + rel.version + L"-Setup.exe";

    HANDLE f = CreateFileW(out.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const BOOL ok = WriteFile(f, body.data(), static_cast<DWORD>(body.size()),
                              &written, nullptr);
    CloseHandle(f);
    if (!ok || written != body.size()) return false;

    // Launch the installer, then the caller exits so it can replace this exe.
    const auto res = reinterpret_cast<INT_PTR>(
        ShellExecuteW(parent, L"open", out.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    return res > 32;
}
