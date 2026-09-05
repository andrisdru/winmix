#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

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
// Raw enumeration uses the WASAPI instance ID; service results use a stable
// app key and carry the current session IDs separately. Volume is a WASAPI
// amplitude scalar in 0..1, not a slider position (see VolumeCurve).
struct AudioSessionSnapshot
{
    std::wstring instanceId;
    // Populated for app snapshots returned by the service. instanceId is
    // then a stable app key; these are the current WASAPI session IDs/PIDs.
    std::vector<std::wstring> sessionInstanceIds;
    std::vector<std::wstring> inputSessionInstanceIds;
    std::vector<uint32_t> processIds;
    std::vector<uint32_t> inputProcessIds;
    std::vector<std::wstring> activeOutputDeviceIds;
    std::vector<std::wstring> activeInputDeviceIds;
    std::optional<std::wstring> outputDeviceId;
    bool outputDeviceKnown = false;
    std::optional<std::wstring> inputDeviceId;
    bool inputDeviceKnown = false;
    // Raw capture snapshots set this false. In groups it means there is
    // playback to control; microphone sessions never drive output volume.
    bool hasOutputSession = true;
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
