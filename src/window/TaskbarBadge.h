#pragma once
#include <windows.h>

// Draws an unread-count overlay on the taskbar icon (ITaskbarList3).
//
// The count comes from WhatsApp's own document title, which it maintains as
// "(3) WhatsApp" -- no DOM scraping, so this survives WhatsApp restyling its
// page. See WebViewManager's DocumentTitleChanged handler.
class TaskbarBadge {
public:
    ~TaskbarBadge();

    // count 0 clears the badge. Values above 9 render as "9+" because the
    // overlay is only 16x16.
    void Show(HWND hwnd, int count);
    void Clear(HWND hwnd);

private:
    bool EnsureTaskbarList();

    struct ITaskbarList3* taskbar_ = nullptr;
    int   shown_ = -1;      // last count drawn; avoids redundant icon churn
    HICON icon_  = nullptr; // owned; replaced on each change
};

// Parses the leading "(n)" from a WhatsApp document title.
// Returns 0 when there is no badge to show. Exposed for --selfcheck.
int ParseUnreadCount(const wchar_t* title);
