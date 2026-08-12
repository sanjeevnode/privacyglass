#pragma once

// Owns COM init, the main window, and the message loop.
class Application {
public:
    // selfCheck: run the headless privacy-engine tests and exit.
    int Run(int showCmd, bool selfCheck = false);
};
