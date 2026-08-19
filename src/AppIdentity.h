#pragma once

// Names that must be globally unique and must never drift between the two
// places that use them (main.cpp claims them, MainWindow.cpp registers the
// window class). Keeping them here means a rename cannot break single-instance
// activation by leaving the two out of sync.
//
// Both embed a GUID rather than a readable name so that no other program can
// accidentally -- or deliberately -- create the same object. Squatting the
// mutex name would otherwise stop PrivacyGlass from ever starting.
//
// These are identity, not branding: do NOT change them when the app is renamed.
// Changing the mutex lets a second instance run; changing the class name breaks
// activation for anyone whose old copy is still open.

// {C7F1B9A4-3E62-4D80-9A15-6B2D8E4F70C3}
inline constexpr wchar_t kSingleInstanceMutex[] =
    L"Local\\PrivacyGlass.SingleInstance.{C7F1B9A4-3E62-4D80-9A15-6B2D8E4F70C3}";

inline constexpr wchar_t kWindowClassName[] =
    L"PrivacyGlassWindow.{C7F1B9A4-3E62-4D80-9A15-6B2D8E4F70C3}";
