#include "app/Application.h"
#include <windows.h>
#include <shellapi.h>
#include <cwchar>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int showCmd) {
    bool selfCheck = false;
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        for (int i = 1; i < argc; ++i)
            if (!std::wcscmp(argv[i], L"--selfcheck")) selfCheck = true;
        LocalFree(argv);
    }

    Application app;
    return app.Run(showCmd, selfCheck);
}
