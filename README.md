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

`.\build.ps1 -SkipTests` skips the self-check.

## Testing

`Ctrl+Shift+P` toggles privacy mode. Privacy is **ON at launch** — content is
blurred before WhatsApp finishes rendering, so there is no flash of plaintext.

The engine has a headless self-check (20 assertions, no login or network needed):

```powershell
.\build\Release\WhatsAppPrivacy.exe --selfcheck   # exit 0 = pass
```

Results are written to `build\Release\selfcheck.txt`. `build.ps1` and CI both
fail the build if it regresses.

## Status

- [x] **Phase 1** — native window skeleton
- [x] **Phase 2** — WebView2 + persistent session (QR login survives restart)
- [x] **Phase 3** — JS injection pipeline + two-way bridge
- [x] **Phase 4** — privacy engine (blur, MutationObserver, boot fail-safe)
- [ ] Phase 5 — native controls (per-category checkboxes)
- [x] **Phase 6** — global hotkey (`Ctrl+Shift+P`)
- [ ] Phase 7 — settings persistence + hover reveal
- [ ] Phase 8 — polish (tray, autostart, theme)

The WhatsApp session lives in `%LOCALAPPDATA%\WhatsAppPrivacy\WebView2` and
persists like a normal browser profile. Delete that folder to log out.

## Releases

Pushing to `master` builds an installer via GitHub Actions and publishes it as
`v0.1.<run-number>`. The installer is per-user (no UAC), registers in
**Settings → Apps** for uninstall, and removes the WhatsApp session folder when
uninstalled so no chat data is left behind.

## Maintenance

WhatsApp ships obfuscated class names that change without notice. When blurring
breaks, [web/selectors.js](web/selectors.js) is the only file to edit — the CSS
keys off our own `wap-t-*` classes, never WhatsApp's.
