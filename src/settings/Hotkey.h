#pragma once
#include <windows.h>
#include <string>

// Global hotkey handling: defaults, display text, and parsing user input.
//
// RegisterHotKey takes a combination away from EVERY other application, so the
// defaults here avoid anything commonly bound elsewhere. Shift+Alt+W rather
// than Ctrl+Shift+P, which is VS Code's command palette.
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
