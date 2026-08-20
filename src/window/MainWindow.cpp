#include "window/MainWindow.h"
#include "webview/WebViewManager.h"
#include "settings/Settings.h"
#include "update/Updater.h"
#include "AppIdentity.h"

#include <windowsx.h>    // GET_X_LPARAM / GET_Y_LPARAM
#include <dwmapi.h>      // DwmSetWindowAttribute (dark title bar)
#include <commctrl.h>    // TaskDialogIndirect
#include <shellapi.h>    // ShellExecuteW
#include <vector>

namespace {
// Shared with main.cpp's single-instance lookup; see AppIdentity.h.
constexpr const wchar_t* kClassName = kWindowClassName;
constexpr wchar_t kTitle[]     = L"PrivacyGlass";
// Posted by the keyboard hook; handled on the message loop so the hook proc
// itself returns immediately.
constexpr UINT WM_APP_TOGGLE_PRIVACY    = WM_APP + 1;
constexpr UINT WM_APP_DUMP_DIAGNOSTICS  = WM_APP + 2;
constexpr UINT_PTR kSelfCheckTimeoutId = 2;


// Control ids.
// System-menu command ids. Windows reserves >= 0xF000 for SC_* and masks the low
// 4 bits of wParam in WM_SYSCOMMAND, so these must be below 0xF000 and 16-aligned.
constexpr int kIdMaster        = 0x0010;
constexpr int kIdFirstCategory = 0x0020;   // 0x20,0x30,0x40,0x50
constexpr int kIdHover         = 0x0060;
constexpr int kIdAbout         = 0x0070;
constexpr int kIdUpdate        = 0x0080;
constexpr int kIdHotkey        = 0x0090;

constexpr const wchar_t* kRepoUrl = kRepoUrlW;   // shared; see AppIdentity.h

struct Category { const char* key; const wchar_t* label; };
constexpr Category kCategories[4] = {
    { "names",    L"Names"    },
    { "messages", L"Messages" },
    { "pictures", L"Photos"   },
    { "previews", L"Previews" },
};
}

MainWindow::MainWindow() = default;

MainWindow::~MainWindow() = default;

// No toolbar and no checkboxes: the WebView owns the whole client area, and the
// privacy options live in the window menu (right-click the title bar, or press
// Alt+Space). Custom-painted caption buttons are not viable here -- Windows 11
// composites the frame through DWM and paints over anything drawn via
// GetWindowDC.
void MainWindow::CreateToolbar() {
    HMENU sys = GetSystemMenu(hwnd_, FALSE);
    if (!sys) return;

    // Do NOT remove SC_CLOSE here. Windows drives the title-bar X button from
    // this menu entry, so deleting it greys out the X and leaves Alt+F4 as the
    // only way to quit.
    AppendMenuW(sys, MF_SEPARATOR, 0, nullptr);
    // Label carries the live shortcut, so it stays correct after a change.
    const std::wstring master = L"Privacy Mode\t" + Hotkey::Format(ToggleHotkey());
    AppendMenuW(sys, MF_STRING, kIdMaster, master.c_str());
    for (int i = 0; i < 4; ++i)
        AppendMenuW(sys, MF_STRING, kIdFirstCategory + i * 16, kCategories[i].label);
    AppendMenuW(sys, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(sys, MF_STRING, kIdHover, L"Hover to reveal");
    AppendMenuW(sys, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(sys, MF_STRING, kIdHotkey, L"Change shortcut...");
    AppendMenuW(sys, MF_STRING, kIdUpdate, L"Check for updates...");
    AppendMenuW(sys, MF_STRING, kIdAbout, L"About...");

    SyncSystemMenu();
}

// Rebuilds the menu from scratch. Needed after a shortcut change, since the
// Privacy Mode label carries the shortcut text.
void MainWindow::RebuildSystemMenu() {
    GetSystemMenu(hwnd_, TRUE);   // restore the stock menu
    CreateToolbar();              // then re-append our items
}

// Check marks are set on demand (WM_INITMENUPOPUP) so they always match state.
void MainWindow::SyncSystemMenu() {
    HMENU sys = GetSystemMenu(hwnd_, FALSE);
    if (!sys) return;

    const auto& s = privacy_.Get();
    CheckMenuItem(sys, kIdMaster, MF_BYCOMMAND | (s.on ? MF_CHECKED : MF_UNCHECKED));

    const bool flags[4] = { s.names, s.messages, s.pictures, s.previews };
    for (int i = 0; i < 4; ++i) {
        CheckMenuItem(sys, kIdFirstCategory + i * 16,
                      MF_BYCOMMAND | (flags[i] ? MF_CHECKED : MF_UNCHECKED));
        // Categories are gated by the master toggle anyway.
        EnableMenuItem(sys, kIdFirstCategory + i * 16,
                       MF_BYCOMMAND | (s.on ? MF_ENABLED : MF_GRAYED));
    }
    CheckMenuItem(sys, kIdHover,
                  MF_BYCOMMAND | (s.hoverReveal ? MF_CHECKED : MF_UNCHECKED));
    EnableMenuItem(sys, kIdHover, MF_BYCOMMAND | (s.on ? MF_ENABLED : MF_GRAYED));
}

void MainWindow::LayoutChildren() {
    if (webview_) webview_->Resize();
}

// Returns the configured toggle hotkey, falling back to the default when the
// settings file has none (first run, or an older file).
Hotkey::Combo MainWindow::ToggleHotkey() const {
    const auto& s = privacy_.Get();
    Hotkey::Combo c{ s.hotkeyMods, s.hotkeyVk };
    return c.valid() ? c : Hotkey::Default();
}

// The shortcut is deliberately NOT registered with RegisterHotKey.
//
// RegisterHotKey reserves a combination across the whole system: every other
// application stops receiving it for as long as this app runs. That broke
// editors and anything else with its own bindings (issue #2).
//
// Instead a WH_KEYBOARD hook on our own thread sees keys delivered to this
// process, which includes the WebView2 child that would otherwise swallow them.
// Other applications are unaffected, and the same combination keeps working in
// them exactly as their own bindings define.
bool MainWindow::RegisterHotkeys() {
    if (keyboardHook_) return true;   // already installed

    s_hookOwner = this;
    keyboardHook_ = SetWindowsHookExW(WH_KEYBOARD, &MainWindow::KeyboardHookProc,
                                      nullptr, GetCurrentThreadId());
    return keyboardHook_ != nullptr;
}

// Thread-local hook: only sees input already destined for this process, so it
// is not a keylogger and cannot observe other applications.
MainWindow* MainWindow::s_hookOwner = nullptr;

LRESULT CALLBACK MainWindow::KeyboardHookProc(int code, WPARAM wp, LPARAM lp) {
    MainWindow* self = s_hookOwner;
    if (code != HC_ACTION || !self)
        return CallNextHookEx(nullptr, code, wp, lp);

    // Bit 31 set means key-up; bit 30 means it was already down (auto-repeat).
    const bool keyUp  = (lp & (1 << 31)) != 0;
    const bool repeat = (lp & (1 << 30)) != 0;
    if (keyUp || repeat)
        return CallNextHookEx(nullptr, code, wp, lp);

    const unsigned vk = static_cast<unsigned>(wp);

    unsigned mods = 0;
    if (GetKeyState(VK_CONTROL) & 0x8000) mods |= MOD_CONTROL;
    if (GetKeyState(VK_SHIFT)   & 0x8000) mods |= MOD_SHIFT;
    if (GetKeyState(VK_MENU)    & 0x8000) mods |= MOD_ALT;
    if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) mods |= MOD_WIN;

    const Hotkey::Combo toggle = self->ToggleHotkey();
    const Hotkey::Combo probe  = Hotkey::DefaultDiagnostics();

    if (vk == toggle.vk && mods == toggle.mods) {
        // Post rather than act inline: a hook proc must return promptly, and
        // the toggle ends up pushing state through the WebView.
        PostMessageW(self->hwnd_, WM_APP_TOGGLE_PRIVACY, 0, 0);
        return 1;   // consume, so the page never sees the combination
    }
    if (vk == probe.vk && mods == probe.mods) {
        PostMessageW(self->hwnd_, WM_APP_DUMP_DIAGNOSTICS, 0, 0);
        return 1;
    }

    return CallNextHookEx(nullptr, code, wp, lp);
}

// True when the user has chosen dark mode for apps. The key is absent on older
// builds, where light is the correct assumption.
static bool SystemUsesDarkMode() {
    DWORD value = 1;          // 1 = light
    DWORD size  = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr,
                     &value, &size) != ERROR_SUCCESS)
        return false;
    return value == 0;
}

// Paints the title bar to match the system theme. Windows does not do this
// automatically for Win32 apps -- without it the caption stays light even in
// dark mode, which looks wrong against WhatsApp's dark UI.
void MainWindow::ApplyTitleBarTheme() {
    const BOOL dark = SystemUsesDarkMode() ? TRUE : FALSE;

    // 20 is DWMWA_USE_IMMERSIVE_DARK_MODE on Windows 10 2004+ and Windows 11.
    // Build 18985 and earlier used 19. Trying 20 first and falling back costs
    // nothing and covers both.
    if (FAILED(DwmSetWindowAttribute(hwnd_, 20, &dark, sizeof(dark))))
        DwmSetWindowAttribute(hwnd_, 19, &dark, sizeof(dark));
}

// Reads FileVersion out of our own version resource, so the dialog always shows
// the version CI stamped rather than a hardcoded string.
static std::wstring AppVersion() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);

    DWORD dummy = 0;
    const DWORD size = GetFileVersionInfoSizeW(path, &dummy);
    if (!size) return L"unknown";

    std::vector<BYTE> buf(size);
    if (!GetFileVersionInfoW(path, 0, size, buf.data())) return L"unknown";

    VS_FIXEDFILEINFO* fi = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(buf.data(), L"\\", reinterpret_cast<LPVOID*>(&fi), &len) || !fi)
        return L"unknown";

    wchar_t out[64];
    swprintf_s(out, L"%u.%u.%u",
               HIWORD(fi->dwFileVersionMS), LOWORD(fi->dwFileVersionMS),
               HIWORD(fi->dwFileVersionLS));
    return out;
}

// The badge shows regardless of Privacy Mode. It carries only a number -- never
// a name or message text -- so it stays useful on a shared screen without
// exposing anything the blur is protecting.
void MainWindow::RefreshBadge() {
    badge_.Show(hwnd_, unread_);
}

namespace {

// State passed through the dialog procedure.
struct PromptData {
    const wchar_t* body;
    wchar_t*       buffer;
    int            capacity;
};

constexpr int kPromptEdit  = 200;
constexpr int kPromptLabel = 201;

// Subclass for the edit box: turns an actual keypress into the shortcut text,
// so the user presses the combination rather than spelling it out.
//
// A combination another application has registered globally never reaches this
// dialog and so cannot be captured here.
WNDPROC g_prevEditProc = nullptr;

LRESULT CALLBACK CaptureEditProc(HWND edit, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
        const unsigned vk = static_cast<unsigned>(wp);

        // Let the dialog handle these rather than capturing them.
        if (vk == VK_TAB || vk == VK_RETURN || vk == VK_ESCAPE)
            return CallWindowProcW(g_prevEditProc, edit, msg, wp, lp);

        // A modifier on its own is not a shortcut; wait for the real key.
        if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU ||
            vk == VK_LWIN  || vk == VK_RWIN)
            return 0;

        Hotkey::Combo c;
        if (GetKeyState(VK_CONTROL) & 0x8000) c.mods |= MOD_CONTROL;
        if (GetKeyState(VK_SHIFT)   & 0x8000) c.mods |= MOD_SHIFT;
        if (GetKeyState(VK_MENU)    & 0x8000) c.mods |= MOD_ALT;
        if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) c.mods |= MOD_WIN;
        c.vk = vk;

        // Show it either way: a modifier-less press is displayed so the user
        // sees the dialog reacting, and validation rejects it on OK.
        SetWindowTextW(edit, Hotkey::Format(c).c_str());
        return 0;   // swallow, so the raw character is never inserted
    }

    // Block character insertion from captured keys; typing is handled by
    // WM_KEYDOWN above only when it did not produce a valid combo.
    if (msg == WM_CHAR || msg == WM_SYSCHAR) return 0;

    return CallWindowProcW(g_prevEditProc, edit, msg, wp, lp);
}

INT_PTR CALLBACK PromptProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        auto* d = reinterpret_cast<PromptData*>(lp);
        SetWindowLongPtrW(dlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(d));
        SetDlgItemTextW(dlg, kPromptLabel, d->body);
        SetDlgItemTextW(dlg, kPromptEdit, d->buffer);
        // Capture keypresses instead of accepting typed text.
        HWND edit = GetDlgItem(dlg, kPromptEdit);
        g_prevEditProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(edit, GWLP_WNDPROC,
                              reinterpret_cast<LONG_PTR>(CaptureEditProc)));
        SetFocus(edit);
        return FALSE;   // focus set explicitly
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            auto* d = reinterpret_cast<PromptData*>(GetWindowLongPtrW(dlg, GWLP_USERDATA));
            if (d) GetDlgItemTextW(dlg, kPromptEdit, d->buffer, d->capacity);
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) { EndDialog(dlg, IDCANCEL); return TRUE; }
        break;
    }
    return FALSE;
}

// Appends a control to an in-memory DLGTEMPLATE. Building the template by hand
// avoids adding a .rc dialog resource for one prompt.
void AddControl(std::vector<BYTE>& t, DWORD style, short x, short y,
                short cx, short cy, WORD id, WORD cls, const wchar_t* text) {
    while (t.size() % 4) t.push_back(0);          // DWORD-align each item

    DLGITEMTEMPLATE item{};
    item.style = style | WS_CHILD | WS_VISIBLE;
    item.x = x; item.y = y; item.cx = cx; item.cy = cy;
    item.id = id;
    const auto* p = reinterpret_cast<const BYTE*>(&item);
    t.insert(t.end(), p, p + sizeof(item));

    const WORD marker = 0xFFFF;
    const auto* m = reinterpret_cast<const BYTE*>(&marker);
    t.insert(t.end(), m, m + 2);
    const auto* c = reinterpret_cast<const BYTE*>(&cls);
    t.insert(t.end(), c, c + 2);

    const size_t bytes = (wcslen(text) + 1) * sizeof(wchar_t);
    const auto* txt = reinterpret_cast<const BYTE*>(text);
    t.insert(t.end(), txt, txt + bytes);

    t.push_back(0); t.push_back(0);               // no creation data
}

}  // namespace

bool MainWindow::PromptForText(const wchar_t* title, const wchar_t* body,
                               wchar_t* buffer, int capacity) {
    std::vector<BYTE> tmpl;

    DLGTEMPLATE header{};
    header.style = DS_MODALFRAME | DS_CENTER | DS_SETFONT | WS_POPUP |
                   WS_CAPTION | WS_SYSMENU;
    header.cdit = 5;
    header.cx = 290; header.cy = 176;
    const auto* h = reinterpret_cast<const BYTE*>(&header);
    tmpl.insert(tmpl.end(), h, h + sizeof(header));

    tmpl.push_back(0); tmpl.push_back(0);         // no menu
    tmpl.push_back(0); tmpl.push_back(0);         // default class
    const size_t titleBytes = (wcslen(title) + 1) * sizeof(wchar_t);
    const auto* tb = reinterpret_cast<const BYTE*>(title);
    tmpl.insert(tmpl.end(), tb, tb + titleBytes);

    const WORD pt = 9;                            // DS_SETFONT: point size...
    const auto* ptp = reinterpret_cast<const BYTE*>(&pt);
    tmpl.insert(tmpl.end(), ptp, ptp + 2);
    const wchar_t face[] = L"Segoe UI";           // ...then typeface
    const auto* fp = reinterpret_cast<const BYTE*>(face);
    tmpl.insert(tmpl.end(), fp, fp + sizeof(face));

    constexpr WORD kStatic = 0x0082, kEdit = 0x0081, kButton = 0x0080;
    AddControl(tmpl, SS_LEFT,                   10,  10, 270, 104, kPromptLabel, kStatic, L"");
    // Centred and bold-ish by being the sole focus; ES_READONLY would grey it,
    // so it stays editable-looking but the subclass swallows typed characters.
    AddControl(tmpl, WS_BORDER | WS_TABSTOP | ES_CENTER,
                                                10, 120, 270, 16, kPromptEdit,  kEdit,   L"");
    AddControl(tmpl, BS_DEFPUSHBUTTON | WS_TABSTOP, 160, 146, 58, 16, IDOK,     kButton, L"OK");
    AddControl(tmpl, BS_PUSHBUTTON | WS_TABSTOP,    222, 146, 58, 16, IDCANCEL, kButton, L"Cancel");
    AddControl(tmpl, SS_LEFT,                        10, 168,  0,  0, 202,      kStatic, L"");

    PromptData data{ body, buffer, capacity };
    const INT_PTR r = DialogBoxIndirectParamW(
        GetModuleHandleW(nullptr),
        reinterpret_cast<LPCDLGTEMPLATEW>(tmpl.data()),
        hwnd_, PromptProc, reinterpret_cast<LPARAM>(&data));

    return r == IDOK;
}

// Prompts for a new global shortcut and applies it.
//
// The combination is typed rather than captured by keypress: capturing would
// need a dialog that intercepts every key, and the combination we most need to
// change is one already stolen by another app -- which would never reach us.
void MainWindow::ChangeHotkey() {
    const Hotkey::Combo current = ToggleHotkey();

    // TaskDialog cannot host an edit box, so use a small dialog built in code.
    wchar_t buffer[128]{};
    lstrcpynW(buffer, Hotkey::Format(current).c_str(), 128);

    const std::wstring prompt =
        L"Click the box below, then press the keys you want.\n\n"
        L"Hold one or more of Ctrl, Shift, Alt or Win and press a letter, "
        L"number or function key. For example, hold Shift and Alt, then press W.\n\n"
        L"Current shortcut:  " + Hotkey::Format(current) + L"\n\n"
        L"It works only while this window is focused, so it will not interfere "
        L"with other applications. Press OK to save, Esc to cancel.";

    if (!PromptForText(L"Change shortcut", prompt.c_str(), buffer, 128))
        return;   // cancelled

    const Hotkey::Combo next = Hotkey::Parse(buffer);
    if (!next.valid()) {
        MessageBoxW(hwnd_,
            L"That is not a usable shortcut.\n\n"
            L"Hold at least one modifier (Ctrl, Shift, Alt or Win) and press a "
            L"key -- for example Shift+Alt+W. Without a modifier the key would "
            L"be captured from every application.",
            L"PrivacyGlass", MB_OK | MB_ICONWARNING);
        return;
    }

    auto s = privacy_.Get();
    s.hotkeyMods = next.mods;
    s.hotkeyVk   = next.vk;
    privacy_.Set(s);            // persists via the settings sink

    // No re-registration needed: the hook reads the current combination on each
    // keypress, and nothing is reserved system-wide, so there is no clash to
    // detect or roll back.

    // Rebuild the menu so the Privacy Mode row shows the new shortcut.
    RebuildSystemMenu();
    MessageBoxW(hwnd_,
        (L"Shortcut changed to " + Hotkey::Format(next) + L".").c_str(),
        L"PrivacyGlass", MB_OK | MB_ICONINFORMATION);
}

// Manual check only -- nothing is downloaded or executed without a yes.
void MainWindow::CheckForUpdates() {
    // The request runs on the UI thread and can take a few seconds; at least
    // show that something is happening.
    HCURSOR prev = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    const Updater::Release rel = Updater::Check();
    SetCursor(prev);

    if (!rel.available) {
        MessageBoxW(hwnd_, L"You are running the latest version.",
                    L"PrivacyGlass", MB_OK | MB_ICONINFORMATION);
        return;
    }

    const std::wstring msg =
        L"Version " + rel.version + L" is available.\n\n"
        L"Download and run the installer now?\n\n"
        L"PrivacyGlass will close so the installer can replace it. Your settings "
        L"and WhatsApp login are kept.";

    if (MessageBoxW(hwnd_, msg.c_str(), L"Update available",
                    MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;

    HCURSOR busy = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    const bool ok = Updater::DownloadAndRun(hwnd_, rel);
    SetCursor(busy);

    if (ok) {
        DestroyWindow(hwnd_);   // let the installer overwrite this exe
    } else {
        // Downloading failed; offer the release page rather than a dead end.
        if (MessageBoxW(hwnd_,
                L"The download failed.\n\nOpen the release page in your browser?",
                L"PrivacyGlass", MB_YESNO | MB_ICONWARNING) == IDYES) {
            ShellExecuteW(hwnd_, L"open",
                          rel.pageUrl.empty() ? kRepoUrl : rel.pageUrl.c_str(),
                          nullptr, nullptr, SW_SHOWNORMAL);
        }
    }
}

void MainWindow::ShowAbout() {
    const std::wstring version = L"Version " + AppVersion();
    const std::wstring body =
        L"Blurs names, messages, photos and previews in WhatsApp Web.\n\n" +
        // Live shortcut, not a hardcoded one -- it is user-configurable.
        Hotkey::Format(ToggleHotkey()) +
        L" toggles privacy instantly, from anywhere in the window.\n"
        L"Right-click the title bar (or press Alt+Space) for per-category options.\n"
        L"Hover over anything blurred to peek at it.\n\n"
        L"Everything runs locally: no server, no accounts, and chat content is "
        L"never written to disk or logged.\n\n"
        L"Not affiliated with or endorsed by WhatsApp or Meta. WhatsApp is a "
        L"trademark of WhatsApp LLC.\n\n"
        // ASCII only: this file is UTF-8 but MSVC reads sources as ANSI unless
        // told otherwise, so non-ASCII literals arrive as mojibake.
        L"<A HREF=\"https://sanjeevnode.in\">sanjeevnode.in</A>"
        L"          "
        L"<A HREF=\"" + std::wstring(kRepoUrl) + L"\">Source code</A>";

    TASKDIALOGCONFIG cfg{ sizeof(cfg) };
    cfg.hwndParent           = hwnd_;
    cfg.dwFlags              = TDF_ENABLE_HYPERLINKS | TDF_USE_HICON_MAIN;
    cfg.hMainIcon            = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),
                                   MAKEINTRESOURCEW(1), IMAGE_ICON, 48, 48, LR_SHARED));
    cfg.dwCommonButtons      = TDCBF_CLOSE_BUTTON;
    cfg.pszWindowTitle       = L"About PrivacyGlass";
    cfg.pszMainInstruction   = L"PrivacyGlass";
    cfg.pszContent           = body.c_str();
    cfg.pszFooter            = version.c_str();
    // Hyperlinks are inert unless the app opens them itself.
    cfg.pfCallback = [](HWND, UINT msg, WPARAM, LPARAM lp, LONG_PTR) -> HRESULT {
        if (msg == TDN_HYPERLINK_CLICKED)
            ShellExecuteW(nullptr, L"open", reinterpret_cast<LPCWSTR>(lp),
                          nullptr, nullptr, SW_SHOWNORMAL);
        return S_OK;
    };

    if (FAILED(TaskDialogIndirect(&cfg, nullptr, nullptr, nullptr))) {
        // TaskDialog needs a comctl32 v6 manifest; fall back if it is missing.
        MessageBoxW(hwnd_,
            (L"PrivacyGlass " + AppVersion() +
             L"\n\nhttps://sanjeevnode.in\n" + kRepoUrl).c_str(),
            L"About PrivacyGlass", MB_OK | MB_ICONINFORMATION);
    }
}

// Writes the JS self-check output to selfcheck.txt beside the exe and quits with
// 0 (all passed) or 1 (any failure), so CI and the build script can gate on it.
void MainWindow::ReportSelfCheck(const std::wstring& json) {
    KillTimer(hwnd_, kSelfCheckTimeoutId);

    auto number = [&](const wchar_t* key) -> int {
        size_t p = json.find(key);
        if (p == std::wstring::npos) return -1;
        p = json.find(L':', p);
        return (p == std::wstring::npos) ? -1 : _wtoi(json.c_str() + p + 1);
    };
    const int passed = number(L"\"passed\"");
    const int total  = number(L"\"total\"");

    // "lines" holds \n-escaped [PASS]/[FAIL] rows; unescape for readability.
    std::wstring body;
    if (size_t p = json.find(L"\"lines\""); p != std::wstring::npos) {
        p = json.find(L'"', json.find(L':', p));
        for (size_t i = p + 1; i + 1 < json.size() && json[i] != L'"'; ++i) {
            if (json[i] == L'\\' && json[i + 1] == L'n') { body += L"\r\n"; ++i; }
            else body += json[i];
        }
    }

    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring out(exePath);
    out = out.substr(0, out.find_last_of(L'\\') + 1) + L"selfcheck.txt";

    std::wstring text = body + L"\r\n" + std::to_wstring(passed) + L"/" +
                        std::to_wstring(total) + L" passed\r\n" +
                        L"--- bridge messages ---\r\n" + selfCheckLog_;
    if (HANDLE f = CreateFileW(out.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        f != INVALID_HANDLE_VALUE) {
        // UTF-8 so CI logs render it correctly.
        int n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string utf8(n > 0 ? n - 1 : 0, '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, utf8.data(), n, nullptr, nullptr);
        DWORD written = 0;
        WriteFile(f, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
        CloseHandle(f);
    }

    OutputDebugStringW(text.c_str());
    PostQuitMessage(total > 0 && passed == total ? 0 : 1);
}

bool MainWindow::Create() {
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = &MainWindow::WndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kClassName;
    // Resource id 1 is the app icon (see version.rc.in). LoadIconW picks the
    // large size; hIconSm gets the 16x16 frame for the title bar.
    wc.hIcon   = static_cast<HICON>(LoadImageW(wc.hInstance, MAKEINTRESOURCEW(1),
                     IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED));
    wc.hIconSm = static_cast<HICON>(LoadImageW(wc.hInstance, MAKEINTRESOURCEW(1),
                     IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                     GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(
        0, kClassName, kTitle, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1100, 800,
        nullptr, nullptr, wc.hInstance, this);

    if (hwnd_) {
        // Applied here rather than only in WM_CREATE: DWM ignores the attribute
        // while the window is still being created, so setting it there alone
        // leaves the caption light.
        ApplyTitleBarTheme();
        // The caption is painted during creation, so force a non-client repaint
        // for the new colour to take effect immediately.
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }

    return hwnd_ != nullptr;
}

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    MainWindow* self = nullptr;

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<MainWindow*>(cs->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) return self->HandleMessage(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT MainWindow::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        ApplyTitleBarTheme();
        // Restore saved preferences before the WebView exists, so the very first
        // state pushed to the page is the user's, not the defaults.
        // Skipped under --selfcheck: a test run must not read or write the real
        // settings file.
        if (!selfCheck_) {
            privacy_.Set(Settings::Load());
            privacy_.SetOnChange([](const PrivacyManager::State& s) { Settings::Save(s); });
            CreateToolbar();
        }
        webview_ = std::make_unique<WebViewManager>(hwnd_, &privacy_);
        webview_->SetSelfCheck(selfCheck_);
        if (!selfCheck_) {
            webview_->SetTitleHandler([this](const std::wstring& title) {
                unread_ = ParseUnreadCount(title.c_str());
                RefreshBadge();
            });
        }
        // No toolbar any more: the WebView owns the entire client area.
        webview_->SetTopInset(0);
        if (selfCheck_) {
            webview_->SetMessageHandler([this](const std::wstring& json) {
                selfCheckLog_ += json + L"\r\n";
                if (json.find(L"\"selfcheck\"") == std::wstring::npos) return;
                ReportSelfCheck(json);
            });
            // Don't hang forever if the page never reports.
            SetTimer(hwnd_, kSelfCheckTimeoutId, 30000, nullptr);
        }
        webview_->Initialize();
        // Keyboard hook, not a global hotkey: works wherever focus sits inside
        // this window (including the WebView2 child) without taking the
        // combination away from other applications.
        RegisterHotkeys();
        return 0;

    // Right-click on the title bar (and Alt+Space) opens the window menu; the
    // privacy items are appended to it in CreateToolbar(). Custom-painted
    // caption buttons do not survive DWM composition on Windows 11, so the
    // system menu is the reliable place for this.
    case WM_SYSCOMMAND:
        if (wp == kIdMaster) { privacy_.Toggle(); return 0; }
        for (int i = 0; i < 4; ++i) {
            if (wp == static_cast<WPARAM>(kIdFirstCategory + i * 16)) {
                privacy_.ToggleCategory(kCategories[i].key);
                return 0;
            }
        }
        if (wp == kIdHover) { privacy_.ToggleCategory("hoverReveal"); return 0; }
        if (wp == kIdAbout)  { ShowAbout(); return 0; }
        if (wp == kIdUpdate) { CheckForUpdates(); return 0; }
        if (wp == kIdHotkey) { ChangeHotkey(); return 0; }
        break;

    case WM_INITMENUPOPUP:
        SyncSystemMenu();   // refresh check marks before the menu is shown
        break;              // must still reach DefWindowProc

    case WM_APP_TOGGLE_PRIVACY:
        privacy_.Toggle();   // sink pushes it to the page
        SyncSystemMenu();    // shortcut and menu share one source of truth
        return 0;

    case WM_APP_DUMP_DIAGNOSTICS:
        if (webview_) webview_->DumpDiagnostics();
        return 0;

    case WM_TIMER:
        if (wp == kSelfCheckTimeoutId) {
            ReportSelfCheck(L"{\"type\":\"selfcheck\",\"passed\":0,\"total\":0,"
                            L"\"lines\":\"[FAIL] TIMEOUT - messages seen:\"}");
        }
        return 0;

    case WM_SETTINGCHANGE:
        // Broadcast when the user flips the light/dark setting; lParam names
        // the changed area. Re-apply so the caption follows without a restart.
        if (lp && !lstrcmpiW(reinterpret_cast<LPCWSTR>(lp), L"ImmersiveColorSet"))
            ApplyTitleBarTheme();
        return 0;

    case WM_SIZE:
        LayoutChildren();
        return 0;

    case WM_DESTROY:
        if (keyboardHook_) {
            UnhookWindowsHookEx(keyboardHook_);
            keyboardHook_ = nullptr;
            s_hookOwner = nullptr;
        }
        webview_.reset();   // release WebView2 before the loop exits
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}
