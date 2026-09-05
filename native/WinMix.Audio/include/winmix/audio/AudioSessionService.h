#pragma once

#include <windows.h>
#include <wrl/client.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "winmix/audio/AudioDeviceInfo.h"
#include "winmix/audio/AudioSessionSnapshot.h"
#include "winmix/audio/ProcessInfoCache.h"

namespace winmix::audio {

// Reads and controls per-application volume on the default render device via
// WASAPI.
//
// Threading: every member must be called from a single COM-initialized
// (STA) thread -- the constructor calls CoCreateInstance, so the caller must
// have already called CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED) on
// this thread. The Core Audio objects are apartment-bound and the work is a
// few COM calls per refresh, so a dedicated thread buys nothing but
// marshalling bugs.
//
// Discovery is poll-based rather than event-based: WASAPI's
// IAudioSessionNotification callbacks arrive on an MTA thread, and
// re-entering the session manager from inside one deadlocks. Polling at a
// couple of hertz sidesteps that entirely and is imperceptible for a mixer.
//
// Per-app/system output-device switching (AppOutputRouter,
// PolicyConfigInterop) lands in a later stage -- this class currently only
// reads/enumerates devices and controls per-session and master volume.
class AudioSessionService
{
public:
    AudioSessionService();
    ~AudioSessionService();

    AudioSessionService(const AudioSessionService&) = delete;
    AudioSessionService& operator=(const AudioSessionService&) = delete;

    // Device-wide output level as an amplitude scalar in 0..1.
    float GetMasterVolume();
    void SetMasterVolume(float scalar);

    bool GetMasterMuted();
    void SetMasterMuted(bool muted);

    // Re-enumerates sessions and returns a fresh snapshot of each one worth
    // showing. Invalidates any AudioSessionSnapshot::instanceId not present
    // in the result -- callers must not cache ids across refreshes.
    std::vector<AudioSessionSnapshot> Refresh();

    // Sets one session's amplitude scalar. No-op if the session has gone away.
    void SetVolume(const std::wstring& instanceId, float scalar);

    // Mutes or unmutes one session. No-op if the session has gone away.
    void SetMute(const std::wstring& instanceId, bool muted);

    // Active render endpoints, for the output-device picker.
    std::vector<AudioDeviceInfo> ListOutputDevices();

    // Active capture endpoints (microphones), for the input-device picker.
    std::vector<AudioDeviceInfo> ListInputDevices();

private:
    IMMDevice* Device();
    static bool IsUsable(IMMDevice* device);
    void ReleaseControls();
    std::wstring GetCachedFriendlyName(IMMDevice* device, const std::wstring& id);

    static std::wstring ChooseDisplayName(IAudioSessionControl2* control, const ProcessInfo& info, bool isSystemSounds);
    static std::optional<std::wstring> TryReadDisplayName(IAudioSessionControl2* control);
    static float ReadPeak(IAudioSessionControl2* control);

    bool TryGetControl(const std::wstring& instanceId, Microsoft::WRL::ComPtr<IAudioSessionControl2>& out);

    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator_;
    Microsoft::WRL::ComPtr<IMMDevice> device_;
    ProcessInfoCache names_;
    std::unordered_map<std::wstring, Microsoft::WRL::ComPtr<IAudioSessionControl2>> controls_;
    std::unordered_map<std::wstring, std::wstring> deviceNameCache_;
};

} // namespace winmix::audio
