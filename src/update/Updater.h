#pragma once
#include <windows.h>
#include <string>

// Checks GitHub Releases for a newer build and, if the user agrees, downloads
// the installer and runs it.
//
// No auto-update and no background polling: the check happens only when the
// user picks "Check for updates", and nothing is downloaded or run without an
// explicit yes. An app that silently fetches and executes a binary is a far
// bigger security surface than one that asks.
namespace Updater {

struct Release {
    bool         available = false;   // a NEWER version exists
    std::wstring version;             // e.g. "0.1.7"
    std::wstring installerUrl;        // .exe asset from that release
    std::wstring pageUrl;             // human-readable release page
};

// Queries the API. Returns available=false on any network/parse failure --
// a failed check must never block or crash the app.
Release Check();

// Downloads to %TEMP% and launches it, then asks the caller to quit so the
// installer can replace the running exe. Returns false if anything failed.
bool DownloadAndRun(HWND parent, const Release& r);

// True if `candidate` is a strictly newer version than `current`. Exposed for
// --selfcheck: a string compare here would rank 0.1.10 below 0.1.9, so it needs
// a test rather than trust.
bool IsNewerVersion(const std::wstring& candidate, const std::wstring& current);

}  // namespace Updater
