#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace winmix::audio {

// Mirrors the native AudioSessionState enum (audiopolicy.h) by value, under
// a different name to avoid colliding with it in translation units that
// include both this header and audiopolicy.h.
enum class SessionState
{
    Inactive = 0,
    Active = 1,
    Expired = 2,
};

// An immutable view of one application's audio session at a point in time.
//
// The UI should read these rather than holding live IAudioSessionControl2
// pointers directly: those are keyed and released every refresh cycle, and
// letting a view model reach into one past its refresh invites
// use-after-release.
//
// instanceId is unique per session instance -- two windows of the same app
// share a session identifier but get distinct instance ids, so this is what
// callers key on. volume is a WASAPI amplitude scalar in 0..1, not a slider
// position (see VolumeCurve).
struct AudioSessionSnapshot
{
    std::wstring instanceId;
    uint32_t pid = 0;
    std::wstring displayName;
    std::optional<std::wstring> executablePath;
    float volume = 0.0f;
    bool isMuted = false;
    float peakLevel = 0.0f;
    bool isSystemSounds = false;
    SessionState state = SessionState::Inactive;

    bool IsActive() const { return state == SessionState::Active; }
};

} // namespace winmix::audio
