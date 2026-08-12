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
        bool hoverReveal = false;
    };

    // Invoked whenever state changes; wired to the WebView JS push.
    using Sink = std::function<void(const State&)>;

    void SetSink(Sink sink);

    const State& Get() const { return state_; }
    void Set(const State& s);
    void Toggle();                         // master on/off
    void SetCategory(const char* key, bool value);

    // Serializes current state as the {"type":"state",...} bridge message.
    std::wstring ToJson() const;

private:
    void Publish() const;

    State state_;
    Sink  sink_;
};
