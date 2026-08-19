#include "app/Application.h"
#include "settings/Settings.h"
#include "update/Updater.h"
#include "window/TaskbarBadge.h"
#include "AppIdentity.h"

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
    HWND existing = FindWindowW(kWindowClassName, nullptr);
    if (!existing) return false;

    if (IsIconic(existing)) ShowWindow(existing, SW_RESTORE);
    SwitchToThisWindow(existing, TRUE);
    SetForegroundWindow(existing);
    return true;
}

// Version ordering is easy to get subtly wrong (a string compare ranks 0.1.10
// below 0.1.9), and a wrong answer here either hides updates forever or offers
// a downgrade as an upgrade.
bool VersionCompareOk() {
    struct { const wchar_t* a; const wchar_t* b; bool newer; } cases[] = {
        { L"0.1.10", L"0.1.9",  true  },   // the string-compare trap
        { L"0.1.9",  L"0.1.10", false },
        { L"0.2.0",  L"0.1.99", true  },
        { L"1.0.0",  L"0.9.9",  true  },
        { L"0.1.5",  L"0.1.5",  false },   // equal is not newer
        { L"0.1.4",  L"0.1.5",  false },
        { L"0.1.6",  L"0.1.5",  true  },
        { L"v0.1.6", L"0.1.5",  true  },   // stray prefix must not break parsing
        { L"0.2",    L"0.1.9",  true  },   // short form
    };
    for (const auto& c : cases)
        if (Updater::IsNewerVersion(c.a, c.b) != c.newer) return false;
    return true;
}

// The badge count is parsed from a title WhatsApp controls, so the parser has
// to reject anything that is not a real count rather than show a wrong number.
bool UnreadParseOk() {
    struct { const wchar_t* title; int expect; } cases[] = {
        { L"(3) WhatsApp",   3   },
        { L"(12) WhatsApp",  12  },
        { L"(99+) WhatsApp", 99  },   // WhatsApp's own cap notation
        { L"WhatsApp",       0   },   // no unread
        { L"",               0   },
        { L"(x) WhatsApp",   0   },   // not digits
        { L"(3 WhatsApp",    0   },   // unclosed, must not read as 3
        { L"3) WhatsApp",    0   },   // no opening paren
        { L"  (7) WhatsApp", 7   },   // leading space
        { nullptr,           0   },   // null must not crash
    };
    for (const auto& c : cases)
        if (ParseUnreadCount(c.title) != c.expect) return false;
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
    if (selfCheck && !VersionCompareOk())
        return 4;
    if (selfCheck && !UnreadParseOk())
        return 5;
    if (selfCheck && !UnreadParseOk())
        return 5;

    // Single instance -- but not for --selfcheck, which must be able to run
    // while a normal instance is open (CI and build.ps1 depend on that).
    if (!selfCheck) {
        // Named mutex, not FindWindow alone: two instances launched together
        // could both search before either has created its window. The mutex is
        // per-user (Local\) so it does not clash across fast-user-switching.
        HANDLE lock = CreateMutexW(nullptr, FALSE, kSingleInstanceMutex);
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
