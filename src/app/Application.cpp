#include "app/Application.h"
#include "window/MainWindow.h"

#include <windows.h>
#include <objbase.h>

int Application::Run(int showCmd, bool selfCheck) {
    // WebView2 requires an STA apartment on the UI thread.
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) return 1;

    int exitCode = 1;
    {
        MainWindow window;
        window.SetSelfCheck(selfCheck);

        if (window.Create()) {
            // Keep the self-check off-screen; it is a test run, not a UI.
            ShowWindow(window.Handle(), selfCheck ? SW_HIDE : showCmd);
            UpdateWindow(window.Handle());

            MSG msg{};
            while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            exitCode = static_cast<int>(msg.wParam);
        }
    }

    CoUninitialize();
    return exitCode;
}
