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

    HWND hwnd_ = nullptr;
    bool selfCheck_ = false;
    std::wstring selfCheckLog_;
    PrivacyManager privacy_;
    std::unique_ptr<WebViewManager> webview_;
};
