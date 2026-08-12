#include "app/Application.h"
#include <windows.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int showCmd) {
    Application app;
    return app.Run(showCmd);
}
