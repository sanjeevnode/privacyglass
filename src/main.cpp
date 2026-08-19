#include "app/Application.h"
#include "settings/Settings.h"

#include <windows.h>
#include <shellapi.h>
#include <cwchar>

namespace {

// Round-trips settings through the real file and reports via exit code.
// Runs as part of --selfcheck so the persistence path is covered too.
bool SettingsRoundTripOk() {
    const PrivacyManager::State original = Settings::Load();

    PrivacyManager::State probe;
    probe.on = false;  probe.names = true;   probe.messages = false;
    probe.pictures = true; probe.previews = false; probe.hoverReveal = true;

    Settings::Save(probe);
    const PrivacyManager::State back = Settings::Load();

    Settings::Save(original);   // never clobber the user's real preferences

    return back.on == probe.on && back.names == probe.names &&
           back.messages == probe.messages && back.pictures == probe.pictures &&
           back.previews == probe.previews && back.hoverReveal == probe.hoverReveal;
}

// Hands focus to an already-running instance, then reports whether it found one.
//
// A taskbar click on a second virtual desktop would otherwise start a whole new
// process (and a second WebView2 profile). SwitchToThisWindow is what makes this
// work across desktops: SetForegroundWindow alone cannot pull a window off
// another virtual desktop, it just flashes the taskbar button.
bool ActivateExistingInstance() {
    // Must match MainWindow's registered class name.
    HWND existing = FindWindowW(L"PrivacyGlassWindow", nullptr);
    if (!existing) return false;

    if (IsIconic(existing)) ShowWindow(existing, SW_RESTORE);
    SwitchToThisWindow(existing, TRUE);
    SetForegroundWindow(existing);
    return true;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int showCmd) {
    bool selfCheck = false;
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        for (int i = 1; i < argc; ++i)
            if (!std::wcscmp(argv[i], L"--selfcheck")) selfCheck = true;
        LocalFree(argv);
    }

    if (selfCheck && !SettingsRoundTripOk())
        return 3;   // distinct from the JS self-check's 1/2

    // Single instance -- but not for --selfcheck, which must be able to run
    // while a normal instance is open (CI and build.ps1 depend on that).
    if (!selfCheck) {
        // Named mutex, not FindWindow alone: two instances launched together
        // could both search before either has created its window. The mutex is
        // per-user (Local\) so it does not clash across fast-user-switching.
        HANDLE lock = CreateMutexW(nullptr, FALSE, L"Local\\PrivacyGlass.SingleInstance");
        if (lock && GetLastError() == ERROR_ALREADY_EXISTS) {
            // The window may not exist yet if the first instance is still
            // starting; give it a moment rather than silently doing nothing.
            for (int i = 0; i < 40; ++i) {
                if (ActivateExistingInstance()) return 0;
                Sleep(50);
            }
            return 0;   // it is starting up; two windows is worse than none
        }
        // Deliberately not closing `lock`: it must outlive main so the OS
        // releases it on exit, which is exactly the lifetime we want.
    }

    Application app;
    return app.Run(showCmd, selfCheck);
}
