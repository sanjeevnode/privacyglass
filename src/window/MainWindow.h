#pragma once
#include <windows.h>
#include <memory>
#include <string>

#include "privacy/PrivacyManager.h"
#include "window/TaskbarBadge.h"

class WebViewManager;

class MainWindow {
public:
    MainWindow();
    ~MainWindow();

    void SetSelfCheck(bool on) { selfCheck_ = on; }
    bool Create();
    HWND Handle() const { return hwnd_; }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);
    void    ReportSelfCheck(const std::wstring& json);
    void    CreateToolbar();        // appends privacy items to the window menu
    void    SyncSystemMenu();       // refresh those items' check marks
    void    ShowAbout();
    void    CheckForUpdates();
    void    LayoutChildren();

    HWND hwnd_ = nullptr;
    bool selfCheck_ = false;
    TaskbarBadge badge_;
    int  unread_ = 0;               // last count parsed from the page title
    void RefreshBadge();
    std::wstring selfCheckLog_;
    PrivacyManager privacy_;
    std::unique_ptr<WebViewManager> webview_;
};
