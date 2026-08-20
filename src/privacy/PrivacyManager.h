#pragma once
#include <functional>
#include <string>

// Single source of truth for privacy state (plan Phase 6: hotkey and UI must
// never desync -- both mutate this, and everything else observes it).
class PrivacyManager {
public:
    struct State {
        bool on          = true;   // default ON per plan Phase 7
        bool names       = true;
        bool messages    = true;
        bool pictures    = true;
        bool previews    = true;
        bool hoverReveal = true;   // hover to peek without toggling privacy off

        // Shortcut for the master toggle, active only while this app is
        // focused. mods is a MOD_* bitmask; key is a virtual-key code.
        unsigned hotkeyMods = 0;   // 0 => use the default
        unsigned hotkeyVk   = 0;
    };

    // Invoked whenever state changes; wired to the WebView JS push.
    using Sink = std::function<void(const State&)>;

    void SetSink(Sink sink);

    // Second observer, independent of the WebView sink: persistence needs to see
    // every change too, and a single sink would mean one overwriting the other.
    void SetOnChange(Sink onChange) { onChange_ = std::move(onChange); }

    const State& Get() const { return state_; }
    void Set(const State& s);
    void Toggle();                         // master on/off
    void SetCategory(const char* key, bool value);
    bool GetCategory(const char* key) const;
    void ToggleCategory(const char* key) { SetCategory(key, !GetCategory(key)); }

    // Serializes current state as the {"type":"state",...} bridge message.
    std::wstring ToJson() const;

private:
    void Publish() const;

    State state_;
    Sink  sink_;
    Sink  onChange_;
};
