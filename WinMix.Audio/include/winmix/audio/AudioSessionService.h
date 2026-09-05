#pragma once

#include <windows.h>
#include <wrl/client.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "winmix/audio/AudioDeviceInfo.h"
#include "winmix/audio/AudioSessionSnapshot.h"
#include "winmix/audio/ProcessInfoCache.h"
#include "winmix/audio/AppSessionTracker.h"

namespace winmix::audio {

// Discovers playback and recording apps across active endpoints, controls
// playback volume, and reads/writes per-app output and microphone preferences.
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

    // Returns one stable app snapshot across all endpoints. A missing app
    // survives a short stream-recreation gap with no live session IDs.
    std::vector<AudioSessionSnapshot> Refresh();

    // Sets all of an app's playback sessions. Also accepts a raw render ID.
    void SetVolume(const std::wstring& instanceId, float scalar);

    // Mutes or unmutes all of an app's current playback sessions.
    void SetMute(const std::wstring& instanceId, bool muted);

    // Active render endpoints, for the output-device picker.
    std::vector<AudioDeviceInfo> ListOutputDevices();

    // Makes deviceId the system default render device, for every role, so
    // both this mixer and every other app follow it.
    void SetDefaultOutputDevice(const std::wstring& deviceId);

    // Pins one app's output to deviceId, or with nullopt clears the pin so
    // it follows the system default again.
    void SetAppOutputDevice(uint32_t pid, const std::optional<std::wstring>& deviceId);
    void SetAppOutputDevice(const std::wstring& appId, const std::optional<std::wstring>& deviceId);
    // Sets capture preferences, including Communications, without changing
    // system defaults or output volume. nullopt restores the role defaults.
    void SetAppInputDevice(const std::wstring& appId, const std::optional<std::wstring>& deviceId);

    // Active capture endpoints (microphones), for the input-device picker.
    std::vector<AudioDeviceInfo> ListInputDevices();

    // Makes deviceId the system default capture device, for every role --
    // the same mechanism as SetDefaultOutputDevice, just pointed at a
    // microphone. Unlike the render side, no session state depends on which
    // capture device is default, so there is nothing to re-resolve after.
    void SetDefaultInputDevice(const std::wstring& deviceId);

private:
    IMMDevice* Device();
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

    AppSessionTracker tracker_;
    std::unordered_map<std::wstring, AudioSessionSnapshot> apps_;
    struct ControlTransfer
    {
        float volume;
        bool muted;
        AppSessionTracker::Clock::time_point expires;
        std::unordered_set<std::wstring> appliedIds;
    };
    std::unordered_map<std::wstring, ControlTransfer> transfers_;
};

} // namespace winmix::audio
