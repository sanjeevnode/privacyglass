# WhatsApp Privacy (Windows POC)

Native Win32 app embedding WhatsApp Web via WebView2, with a local-only privacy layer
that blurs names, messages, avatars, and previews. See [docs/plan.md](docs/plan.md).

## Build

```powershell
.\build.ps1
```

Output: `build\Release\WhatsAppPrivacy.exe`

`cmake` and `nuget` are not required on PATH — the script uses the CMake bundled with
Visual Studio 2022, and CMake fetches the WebView2 SDK (a NuGet `.nupkg` is just a zip)
via `FetchContent`. The static loader is linked, so the `.exe` is self-contained.

**Requires:** VS2022 with the "Desktop development with C++" workload, and the
Microsoft Edge WebView2 Evergreen Runtime (preinstalled on current Windows 11).

## Status

- [x] **Phase 1** — native window skeleton
- [x] **Phase 2** — WebView2 + persistent session (QR login survives restart)
- [ ] Phase 3 — JS injection pipeline
- [ ] Phase 4 — privacy engine
- [ ] Phase 5 — native controls
- [ ] Phase 6 — global hotkey
- [ ] Phase 7 — settings + default-on
- [ ] Phase 8 — polish

The WhatsApp session lives in `%LOCALAPPDATA%\WhatsAppPrivacy\WebView2`.
Delete that folder to log out.
