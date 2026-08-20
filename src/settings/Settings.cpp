#include "settings/Settings.h"

#include <windows.h>
#include <shlobj.h>

#include <string>
#include <vector>

namespace {

std::wstring SettingsDir() {
    PWSTR roaming = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming)))
        return L"";
    std::wstring dir(roaming);
    CoTaskMemFree(roaming);
    return dir + L"\\PrivacyGlass";
}

std::wstring SettingsPath() {
    const std::wstring dir = SettingsDir();
    return dir.empty() ? L"" : dir + L"\\settings.json";
}

// Finds "key": true|false. Absent key leaves the default untouched, so adding a
// field later does not invalidate an existing file.
void ReadBool(const std::string& json, const char* key, bool& out) {
    const std::string needle = std::string("\"") + key + "\"";
    size_t p = json.find(needle);
    if (p == std::string::npos) return;
    p = json.find(':', p + needle.size());
    if (p == std::string::npos) return;

    // Skip whitespace, then look at the first meaningful character.
    while (++p < json.size() && (json[p] == ' ' || json[p] == '\t')) {}
    if (json.compare(p, 4, "true") == 0)  { out = true;  return; }
    if (json.compare(p, 5, "false") == 0) { out = false; return; }
}

// Finds "key": <number>. Absent key leaves the default untouched.
void ReadUInt(const std::string& json, const char* key, unsigned& out) {
    const std::string needle = std::string("\"") + key + "\"";
    size_t p = json.find(needle);
    if (p == std::string::npos) return;
    p = json.find(':', p + needle.size());
    if (p == std::string::npos) return;

    while (++p < json.size() && (json[p] == ' ' || json[p] == '\t')) {}
    unsigned v = 0;
    bool any = false;
    while (p < json.size() && json[p] >= '0' && json[p] <= '9') {
        if (v < 100000000) v = v * 10 + static_cast<unsigned>(json[p] - '0');
        any = true;
        ++p;
    }
    if (any) out = v;
}

}  // namespace

PrivacyManager::State Settings::Load() {
    PrivacyManager::State s;   // defaults: everything on, hover reveal on

    const std::wstring path = SettingsPath();
    if (path.empty()) return s;

    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return s;   // first run

    LARGE_INTEGER size{};
    // Cap the read: this file should be a few hundred bytes, and a corrupt or
    // hostile one must not make us allocate wildly.
    if (GetFileSizeEx(f, &size) && size.QuadPart > 0 && size.QuadPart < 64 * 1024) {
        std::string buf(static_cast<size_t>(size.QuadPart), '\0');
        DWORD read = 0;
        if (ReadFile(f, buf.data(), static_cast<DWORD>(buf.size()), &read, nullptr)) {
            buf.resize(read);
            ReadBool(buf, "privacyOn",   s.on);
            ReadBool(buf, "names",       s.names);
            ReadBool(buf, "messages",    s.messages);
            ReadBool(buf, "pictures",    s.pictures);
            ReadBool(buf, "previews",    s.previews);
            ReadBool(buf, "hoverReveal", s.hoverReveal);
            ReadUInt(buf, "hotkeyMods",  s.hotkeyMods);
            ReadUInt(buf, "hotkeyVk",    s.hotkeyVk);
        }
    }
    CloseHandle(f);
    return s;
}

void Settings::Save(const PrivacyManager::State& s) {
    const std::wstring dir = SettingsDir();
    if (dir.empty()) return;
    CreateDirectoryW(dir.c_str(), nullptr);   // fine if it already exists

    const std::wstring path = dir + L"\\settings.json";

    auto b = [](bool v) { return v ? "true" : "false"; };
    std::string json;
    json += "{\n";
    json += "  \"privacyOn\":   "; json += b(s.on);          json += ",\n";
    json += "  \"names\":       "; json += b(s.names);       json += ",\n";
    json += "  \"messages\":    "; json += b(s.messages);    json += ",\n";
    json += "  \"pictures\":    "; json += b(s.pictures);    json += ",\n";
    json += "  \"previews\":    "; json += b(s.previews);    json += ",\n";
    json += "  \"hoverReveal\": "; json += b(s.hoverReveal); json += ",\n";
    json += "  \"hotkeyMods\":  "; json += std::to_string(s.hotkeyMods); json += ",\n";
    json += "  \"hotkeyVk\":    "; json += std::to_string(s.hotkeyVk);   json += "\n";
    json += "}\n";

    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;    // best-effort only
    DWORD written = 0;
    WriteFile(f, json.data(), static_cast<DWORD>(json.size()), &written, nullptr);
    CloseHandle(f);
}
