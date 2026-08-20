#pragma once
#include <windows.h>
#include <string>

// Shortcut handling: defaults, display text, and parsing user input.
//
// The combination is matched by a keyboard hook while this app is focused, not
// reserved with RegisterHotKey, so it never takes the keys away from other
// applications. The defaults still avoid widely-used combinations so the
// shortcut does not shadow something inside the WhatsApp page itself.
namespace Hotkey {

struct Combo {
    unsigned mods = 0;    // MOD_* bitmask (without MOD_NOREPEAT)
    unsigned vk   = 0;    // virtual-key code
    bool valid() const { return mods != 0 && vk != 0; }
};

Combo Default();          // Shift+Alt+W
Combo DefaultDiagnostics();

// "Shift+Alt+W" -- for menus and dialogs.
std::wstring Format(const Combo& c);

// Parses "shift+alt+w", "ctrl alt f9", etc. Returns an invalid Combo if the
// text names no key, or only modifiers.
Combo Parse(const std::wstring& text);

}  // namespace Hotkey
