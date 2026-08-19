#pragma once
#include "privacy/PrivacyManager.h"

// Persists PrivacyManager state to %APPDATA%\PrivacyGlass\settings.json.
//
// Deliberately hand-rolled rather than pulling in a JSON library: the file holds
// six booleans, and the parser only has to survive a file the app itself wrote
// (or a corrupt/absent one, which falls back to defaults).
namespace Settings {

// Missing or unreadable file returns defaults -- privacy ON, all categories on.
PrivacyManager::State Load();

// Best-effort; a failed write must never take the app down.
void Save(const PrivacyManager::State& s);

}  // namespace Settings
