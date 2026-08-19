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

    Application app;
    return app.Run(showCmd, selfCheck);
}
