#include "settings/Hotkey.h"

#include <cwctype>
#include <vector>

namespace {

// Named keys that have no printable single character.
struct NamedKey { const wchar_t* name; unsigned vk; };
const NamedKey kNamedKeys[] = {
    { L"f1", VK_F1 },   { L"f2", VK_F2 },   { L"f3", VK_F3 },   { L"f4", VK_F4 },
    { L"f5", VK_F5 },   { L"f6", VK_F6 },   { L"f7", VK_F7 },   { L"f8", VK_F8 },
    { L"f9", VK_F9 },   { L"f10", VK_F10 }, { L"f11", VK_F11 }, { L"f12", VK_F12 },
    { L"space", VK_SPACE },   { L"tab", VK_TAB },
    { L"insert", VK_INSERT }, { L"delete", VK_DELETE },
    { L"home", VK_HOME },     { L"end", VK_END },
    { L"pageup", VK_PRIOR },  { L"pagedown", VK_NEXT },
    { L"up", VK_UP },   { L"down", VK_DOWN },
    { L"left", VK_LEFT }, { L"right", VK_RIGHT },
};

std::wstring Lower(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
    return s;
}

}  // namespace

Hotkey::Combo Hotkey::Default() {
    // Shift+Alt+W: not a Windows shortcut, and not bound by the editors and
    // browsers people keep open alongside this.
    return { MOD_SHIFT | MOD_ALT, 'W' };
}

Hotkey::Combo Hotkey::DefaultDiagnostics() {
    return { MOD_SHIFT | MOD_ALT, 'D' };
}

std::wstring Hotkey::Format(const Combo& c) {
    if (!c.valid()) return L"(none)";

    std::wstring out;
    if (c.mods & MOD_CONTROL) out += L"Ctrl+";
    if (c.mods & MOD_SHIFT)   out += L"Shift+";
    if (c.mods & MOD_ALT)     out += L"Alt+";
    if (c.mods & MOD_WIN)     out += L"Win+";

    for (const auto& k : kNamedKeys) {
        if (k.vk == c.vk) {
            std::wstring name = k.name;
            // Capitalise for display: "f9" -> "F9", "pageup" -> "Pageup".
            name[0] = static_cast<wchar_t>(towupper(name[0]));
            return out + name;
        }
    }

    // Printable keys map directly from their virtual-key code.
    if ((c.vk >= 'A' && c.vk <= 'Z') || (c.vk >= '0' && c.vk <= '9'))
        return out + static_cast<wchar_t>(c.vk);

    return out + L"?";
}

Hotkey::Combo Hotkey::Parse(const std::wstring& text) {
    Combo c;

    // Split on +, space, or - so "shift+alt+w" and "shift alt w" both work.
    std::vector<std::wstring> tokens;
    std::wstring cur;
    for (wchar_t ch : text) {
        if (ch == L'+' || ch == L' ' || ch == L'-') {
            if (!cur.empty()) { tokens.push_back(Lower(cur)); cur.clear(); }
        } else {
            cur += ch;
        }
    }
    if (!cur.empty()) tokens.push_back(Lower(cur));

    for (const auto& t : tokens) {
        if (t == L"ctrl" || t == L"control") { c.mods |= MOD_CONTROL; continue; }
        if (t == L"shift")                   { c.mods |= MOD_SHIFT;   continue; }
        if (t == L"alt")                     { c.mods |= MOD_ALT;     continue; }
        if (t == L"win")                     { c.mods |= MOD_WIN;     continue; }

        bool named = false;
        for (const auto& k : kNamedKeys) {
            if (t == k.name) { c.vk = k.vk; named = true; break; }
        }
        if (named) continue;

        if (t.size() == 1) {
            const wchar_t ch = static_cast<wchar_t>(towupper(t[0]));
            if ((ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9'))
                c.vk = static_cast<unsigned>(ch);
        }
    }

    // A hotkey with no modifier would swallow a bare keypress system-wide.
    if (c.mods == 0) c.vk = 0;
    return c;
}
