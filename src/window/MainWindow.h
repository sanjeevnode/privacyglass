#pragma once
#include <windows.h>
#include <memory>
#include <string>

#include "privacy/PrivacyManager.h"

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
    void    CreateToolbar();
    void    LayoutChildren();
    void    SyncToolbar();          // reflect PrivacyManager state in the checkboxes

    HWND hwnd_ = nullptr;
    HWND toolbar_ = nullptr;
    HWND master_ = nullptr;
    HWND checks_[4]{};              // names, messages, pictures, previews
    HWND hover_ = nullptr;
    HFONT font_ = nullptr;
    bool selfCheck_ = false;
    bool syncing_ = false;          // guards against feedback while setting checks
    std::wstring selfCheckLog_;
    PrivacyManager privacy_;
    std::unique_ptr<WebViewManager> webview_;
};
