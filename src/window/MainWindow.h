#pragma once
#include <windows.h>
#include <memory>

class WebViewManager;

class MainWindow {
public:
    MainWindow();
    ~MainWindow();

    bool Create();
    HWND Handle() const { return hwnd_; }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);

    HWND hwnd_ = nullptr;
    std::unique_ptr<WebViewManager> webview_;
};
