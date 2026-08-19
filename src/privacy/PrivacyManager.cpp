#include "privacy/PrivacyManager.h"

#include <cstring>

void PrivacyManager::SetSink(Sink sink) {
    sink_ = std::move(sink);
    Publish();   // push current state as soon as a consumer appears
}

void PrivacyManager::Set(const State& s) {
    state_ = s;
    Publish();
}

void PrivacyManager::Toggle() {
    state_.on = !state_.on;
    Publish();
}

void PrivacyManager::SetCategory(const char* key, bool value) {
    if      (!std::strcmp(key, "names"))       state_.names       = value;
    else if (!std::strcmp(key, "messages"))    state_.messages    = value;
    else if (!std::strcmp(key, "pictures"))    state_.pictures    = value;
    else if (!std::strcmp(key, "previews"))    state_.previews    = value;
    else if (!std::strcmp(key, "hoverReveal")) state_.hoverReveal = value;
    else return;
    Publish();
}

bool PrivacyManager::GetCategory(const char* key) const {
    if (!std::strcmp(key, "names"))       return state_.names;
    if (!std::strcmp(key, "messages"))    return state_.messages;
    if (!std::strcmp(key, "pictures"))    return state_.pictures;
    if (!std::strcmp(key, "previews"))    return state_.previews;
    if (!std::strcmp(key, "hoverReveal")) return state_.hoverReveal;
    return false;
}

void PrivacyManager::Publish() const {
    if (sink_) sink_(state_);
    if (onChange_) onChange_(state_);
}

std::wstring PrivacyManager::ToJson() const {
    auto b = [](bool v) -> const wchar_t* { return v ? L"true" : L"false"; };
    std::wstring j = L"{\"type\":\"state\"";
    j += L",\"on\":";          j += b(state_.on);
    j += L",\"names\":";       j += b(state_.names);
    j += L",\"messages\":";    j += b(state_.messages);
    j += L",\"pictures\":";    j += b(state_.pictures);
    j += L",\"previews\":";    j += b(state_.previews);
    j += L",\"hoverReveal\":"; j += b(state_.hoverReveal);
    j += L"}";
    return j;
}
