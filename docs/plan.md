# WhatsApp Privacy — Windows POC Build Plan

**Target:** Native Windows app embedding WhatsApp Web via WebView2, with a real-time
privacy layer (blur names/messages/photos/previews) toggled by hotkey or UI.
**Scope:** 100% local. No backend, no DB, no accounts, no interception of encrypted
traffic. Pure DOM/CSS manipulation of the rendered WhatsApp Web page.
**Handoff target:** Claude Code, phase-by-phase. Do not start Phase N+1 until
Phase N's exit criteria pass.

---

## 0. Repo Layout

```
whatsapp-privacy/
├── CMakeLists.txt
├── README.md
├── src/
│   ├── main.cpp
│   ├── app/            (Application.cpp/.h)
│   ├── window/         (MainWindow.cpp/.h)
│   ├── webview/        (WebViewManager.cpp/.h)
│   ├── privacy/        (PrivacyManager.cpp/.h)
│   ├── hotkey/         (HotkeyManager.cpp/.h)
│   └── settings/       (Settings.cpp/.h)
├── web/
│   ├── privacy.js
│   └── privacy.css
└── assets/icons/
```

## 1. Stack (locked)

| Component | Choice |
|---|---|
| Language | C++20 |
| UI | Win32 API |
| Web engine | Microsoft WebView2 (Evergreen runtime) |
| Native↔Web bridge | WebView2 `PostWebMessageAsString` / `WebMessageReceived` |
| Build | CMake + MSVC (VS2022) |
| Settings | Local JSON file (`%APPDATA%\WhatsAppPrivacy\settings.json`) |
| Packaging | Deferred to Phase 8 (MSIX) |

## 2. Global Rules for Claude Code

- Never handle WhatsApp credentials, tokens, or message content server-side — there is no server.
- Never write chat content to disk, logs, or telemetry. Privacy engine operates only on live DOM.
- Each phase ends in a runnable `.exe` with a manual test checklist. Do not merge phases.
- Treat WhatsApp Web's DOM selectors as unstable — isolate them in one file (`web/selectors.js`, added Phase 4) so future breakage is a one-file fix.
- No modification of WhatsApp's network layer, service worker, or WebSocket traffic. Privacy = visual layer only, applied post-render.

---

## Phase 1 — Native Window Skeleton

**Goal:** `WhatsAppPrivacy.exe` opens a blank native window.

**Build:**
- CMakeLists.txt targeting Win32 desktop app (not console)
- `main.cpp` → `Application` → `MainWindow` (WNDCLASS, message loop)
- App icon placeholder, window title "WhatsApp Privacy"
- Minimal menu bar stub (File/Settings/Help — no logic yet)

**Exit criteria:**
- [ ] `cmake --build` produces a working `.exe`
- [ ] Window opens, resizes, closes cleanly (no leaked handles)

---

## Phase 2 — WebView2 Integration

**Goal:** Window loads `https://web.whatsapp.com` and QR login works.

**Build:**
- Add WebView2 SDK (NuGet or manual) to CMake
- `WebViewManager`: creates `CoreWebView2Environment` with a **persistent user data folder** (so session survives restart — this is what keeps the QR login from repeating)
- Navigate to `web.whatsapp.com` on launch
- Resize WebView2 to fill client area on `WM_SIZE`

**Exit criteria:**
- [ ] QR code renders inside the native window
- [ ] After scanning, WhatsApp Web functions normally (send/receive, media, scroll)
- [ ] Close and relaunch app → still logged in, no re-scan

---

## Phase 3 — JS Injection Pipeline

**Goal:** Prove C++ → WebView2 → JS → WhatsApp DOM round-trip.

**Build:**
- `AddScriptToExecuteOnDocumentCreatedAsync` to inject `web/privacy.js` on every page load (handles WhatsApp's SPA reloads)
- Trivial smoke test first: JS changes page background color on load
- Add `WebMessageReceived` handler in C++ and `chrome.webview.postMessage` in JS for two-way ping/pong
- Confirm injection survives WhatsApp's internal route changes (it's a single-page app — injected script must persist, not just fire once)

**Exit criteria:**
- [ ] Background-color test visibly works on load
- [ ] C++ can call a JS function and get a string back via WebMessage
- [ ] Injection still active after switching chats / scrolling (no re-injection needed)

---

## Phase 4 — Privacy Engine (core value)

**Goal:** Toggling a class on `<body>` hides all sensitive content, live.

**Build:**
- `web/selectors.js` — isolated, commented WhatsApp DOM selectors (chat list names, message bubbles, avatars, preview text). This is the file that will need maintenance when WhatsApp updates its DOM.
- `web/privacy.css` — `.privacy-mode` scoped rules using `filter: blur()` on selector targets, not `display:none` (blur preserves layout, avoids jank)
- `web/privacy.js`:
  - `togglePrivacyMode(on: bool)` → adds/removes `privacy-mode` class on `<body>`
  - `MutationObserver` on the chat list and active chat pane — new incoming messages and newly opened chats must inherit blur automatically without a manual re-toggle
- `PrivacyManager.cpp` — native-side state holder, calls JS toggle via WebMessage

**Exit criteria:**
- [ ] Toggling `privacy-mode` blurs: contact names, message text, avatars, chat-list previews
- [ ] New incoming message while privacy is ON is blurred immediately (MutationObserver proof)
- [ ] Opening a different chat while ON keeps content blurred with no flash of plaintext

---

## Phase 5 — Native Controls

**Goal:** Native Win32 UI drives the privacy engine (not just JS console testing).

**Build:**
- Toolbar/panel: Privacy Mode ON/OFF toggle button
- Checkboxes: Hide Names / Hide Messages / Hide Pictures / Hide Chat Preview — each maps to a granular CSS class so they can be toggled independently
- Wire each control → `PrivacyManager` → WebMessage → `privacy.js`

**Exit criteria:**
- [ ] Each checkbox independently toggles its own content category
- [ ] Master toggle overrides/syncs all four
- [ ] No UI freeze or visible lag on toggle (WebMessage round-trip should be near-instant)

---

## Phase 6 — Global Hotkey

**Goal:** `Ctrl+Shift+P` toggles privacy mode even if focus isn't on the WebView.

**Build:**
- `HotkeyManager` using `RegisterHotKey` on the main window, handled in `WM_HOTKEY`
- Debounce so rapid repeats don't desync native/JS state
- Sync hotkey state with the Phase 5 UI toggle (single source of truth in `PrivacyManager`)

**Exit criteria:**
- [ ] Hotkey works regardless of which control has focus
- [ ] UI toggle button reflects hotkey-triggered state changes and vice versa

---

## Phase 7 — Settings + Default-On Behavior

**Goal:** Preferences persist; privacy defaults ON at launch.

**Build:**
- `Settings.cpp` — read/write local JSON (`%APPDATA%\WhatsAppPrivacy\settings.json`): four category flags, hover-reveal on/off, hotkey binding
- On launch: load settings → apply to `PrivacyManager` → privacy mode active *before* WhatsApp Web finishes rendering (inject as part of Phase 3's on-document-created script, not after)
- Reveal-on-hover (CSS `:hover` removing blur on the specific hovered element, re-applied on `mouseleave`) — implement in `privacy.css`/`privacy.js`, gated by a settings flag

**Exit criteria:**
- [ ] Fresh launch → privacy ON by default, before any chat content is visible even momentarily
- [ ] Settings persist across restarts
- [ ] Hover reveal works per-element and re-hides on mouse-leave

---

## Phase 8 — Polish (deferred, only after 1–7 pass)

- System tray + minimize-to-tray
- Launch at Windows startup
- Light/dark theme matching WhatsApp's own theme
- Crash handling + local-only debug logging (never log message content)
- MSIX installer

---

## POC-Wide Success Test (run after Phase 7)

| # | Test | Expected |
|---|---|---|
| 1 | Launch exe | WhatsApp Web appears in native window |
| 2 | Scan QR | Normal WhatsApp function |
| 3 | Privacy ON (default) | Names, messages, avatars, previews all blurred |
| 4 | New incoming message | Auto-blurred, no manual refresh |
| 5 | Switch chats | New chat content also auto-blurred |
| 6 | `Ctrl+Shift+P` | Instant reveal, all categories |
| 7 | `Ctrl+Shift+P` again | Instant re-hide |
| 8 | Restart app | Still logged in (persistent WebView2 profile), privacy still defaults ON |

## Explicitly Out of Scope for POC

Login system, backend, database, cloud sync, user accounts, subscriptions, AI features, Chrome extension, mobile app, payments, installer polish — all deferred past Phase 8 or cut entirely for this POC.