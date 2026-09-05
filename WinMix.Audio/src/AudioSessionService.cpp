#include "winmix/audio/AudioSessionService.h"
#include "winmix/audio/AppOutputRouter.h"
#include "winmix/audio/AppDeviceRouter.h"
#include "winmix/audio/PolicyConfigInterop.h"
#include "winmix/audio/SessionNaming.h"

#include <endpointvolume.h>
#include <propsys.h>
#include <propidl.h>
#include <functiondiscoverykeys_devpkey.h>

#include <algorithm>
#include <functional>
#include <stdexcept>
#include <unordered_set>

using Microsoft::WRL::ComPtr;

namespace winmix::audio {

namespace {

// Native COM failures surface uniformly as a failed HRESULT -- there is no
// native equivalent of .NET's separate COMException/ObjectDisposedException/
// InvalidCastException distinction, so Guarded() below swallows any
// ComException the same way the original code swallowed all three.
struct ComException : std::runtime_error
{
    HRESULT hr;
    ComException(HRESULT hr_, const char* what) : std::runtime_error(what), hr(hr_) {}
};

void ThrowIfFailed(HRESULT hr, const char* what)
{
    if (FAILED(hr))
    {
        throw ComException(hr, what);
    }
}

// Runs a Core Audio mutation, swallowing the failure that happens when the
// target process exits between a refresh and the user moving its slider.
void Guarded(const std::function<void()>& action)
{
    try
    {
        action();
    }
    catch (const ComException&)
    {
        // The session died underneath us; the next refresh drops the row.
    }
}

std::optional<std::wstring> TakeCoString(LPWSTR raw)
{
    if (raw == nullptr)
    {
        return std::nullopt;
    }
    std::wstring result(raw);
    CoTaskMemFree(raw);
    return result;
}

// Culture-aware, case-insensitive comparison, matching
// StringComparer.CurrentCultureIgnoreCase closely enough for row ordering.
int CompareDisplayNames(const std::wstring& a, const std::wstring& b)
{
    const int result = CompareStringEx(
        LOCALE_NAME_USER_DEFAULT, LINGUISTIC_IGNORECASE,
        a.c_str(), static_cast<int>(a.size()),
        b.c_str(), static_cast<int>(b.size()),
        nullptr, nullptr, 0);
    return result - CSTR_EQUAL;
}

} // namespace

AudioSessionService::AudioSessionService()
{
    ThrowIfFailed(
        CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator_)),
        "CoCreateInstance(MMDeviceEnumerator)");
}

AudioSessionService::~AudioSessionService() = default;

float AudioSessionService::GetMasterVolume()
{
    ComPtr<IAudioEndpointVolume> endpointVolume;
    ThrowIfFailed(
        Device()->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, &endpointVolume),
        "Activate(IAudioEndpointVolume)");

    float level = 0.0f;
    ThrowIfFailed(endpointVolume->GetMasterVolumeLevelScalar(&level), "GetMasterVolumeLevelScalar");
    return level;
}

void AudioSessionService::SetMasterVolume(float scalar)
{
    scalar = std::clamp(scalar, 0.0f, 1.0f);

    ComPtr<IAudioEndpointVolume> endpointVolume;
    ThrowIfFailed(
        Device()->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, &endpointVolume),
        "Activate(IAudioEndpointVolume)");

    ThrowIfFailed(endpointVolume->SetMasterVolumeLevelScalar(scalar, nullptr), "SetMasterVolumeLevelScalar");
}

bool AudioSessionService::GetMasterMuted()
{
    ComPtr<IAudioEndpointVolume> endpointVolume;
    ThrowIfFailed(
        Device()->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, &endpointVolume),
        "Activate(IAudioEndpointVolume)");

    BOOL muted = FALSE;
    ThrowIfFailed(endpointVolume->GetMute(&muted), "GetMute");
    return muted != FALSE;
}

void AudioSessionService::SetMasterMuted(bool muted)
{
    ComPtr<IAudioEndpointVolume> endpointVolume;
    ThrowIfFailed(
        Device()->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, &endpointVolume),
        "Activate(IAudioEndpointVolume)");

    ThrowIfFailed(endpointVolume->SetMute(muted ? TRUE : FALSE, nullptr), "SetMute");
}

std::vector<AudioSessionSnapshot> AudioSessionService::Refresh()
{
    // Include capture endpoints: a recording-only app must still have an
    // input picker, and a browser may capture in a separate worker process.
    std::optional<std::wstring> defaultDeviceId;
    ComPtr<IMMDevice> defaultDevice;
    if (SUCCEEDED(enumerator_->GetDefaultAudioEndpoint(eRender, eMultimedia, &defaultDevice)))
    {
        LPWSTR raw = nullptr;
        if (SUCCEEDED(defaultDevice->GetId(&raw))) defaultDeviceId = TakeCoString(raw);
    }

    ComPtr<IMMDeviceCollection> deviceCollection;
    ThrowIfFailed(
        enumerator_->EnumAudioEndpoints(eAll, DEVICE_STATE_ACTIVE, &deviceCollection),
        "EnumAudioEndpoints(all)");

    UINT deviceCount = 0;
    ThrowIfFailed(deviceCollection->GetCount(&deviceCount), "GetCount(render devices)");

    // The previous batch of controls is released here (ComPtr's destructor
    // does the Release) before the map is repopulated below.
    ReleaseControls();

    std::vector<AudioSessionSnapshot> snapshots;
    std::unordered_set<uint32_t> livePids;

    for (UINT d = 0; d < deviceCount; ++d)
    {
        ComPtr<IMMDevice> device;
        if (FAILED(deviceCollection->Item(d, &device)))
        {
            continue;
        }

        LPWSTR rawId = nullptr;
        if (FAILED(device->GetId(&rawId)))
        {
            continue;
        }
        const std::wstring deviceId = *TakeCoString(rawId);
        const bool isDefaultDevice = defaultDeviceId && deviceId == *defaultDeviceId;
        ComPtr<IMMEndpoint> endpoint;
        EDataFlow flow = eRender;
        if (FAILED(device.As(&endpoint)) || FAILED(endpoint->GetDataFlow(&flow))) continue;
        const bool isInput = flow == eCapture;

        ComPtr<IAudioSessionManager2> manager;
        if (FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, &manager)))
        {
            continue;
        }

        // Unlike NAudio's AudioSessionManager.RefreshSessions() (a
        // managed-side convenience with no native counterpart),
        // IAudioSessionManager2 has no RefreshSessions method --
        // GetSessionEnumerator() itself always returns the current session
        // set.
        ComPtr<IAudioSessionEnumerator> sessionEnum;
        if (FAILED(manager->GetSessionEnumerator(&sessionEnum)))
        {
            continue;
        }

        int count = 0;
        if (FAILED(sessionEnum->GetCount(&count)))
        {
            continue;
        }

        for (int i = 0; i < count; ++i)
        {
            ComPtr<IAudioSessionControl> baseControl;
            if (FAILED(sessionEnum->GetSession(i, &baseControl)))
            {
                continue;
            }

            ComPtr<IAudioSessionControl2> control;
            if (FAILED(baseControl.As(&control)))
            {
                continue;
            }

            ::AudioSessionState state = ::AudioSessionStateInactive;
            if (FAILED(control->GetState(&state)))
            {
                continue;
            }

            if (state == ::AudioSessionStateExpired)
            {
                continue;
            }

            DWORD pid = 0;
            control->GetProcessId(&pid);

            const bool isSystemSounds = control->IsSystemSoundsSession() == S_OK;
            // Keep paused apps on every endpoint. Grouping below coalesces
            // stale and replacement streams into a single app control.
            if (isSystemSounds && (isInput || (!isDefaultDevice && state != ::AudioSessionStateActive)))
            {
                continue;
            }

            LPWSTR rawInstanceId = nullptr;
            if (FAILED(control->GetSessionInstanceIdentifier(&rawInstanceId)))
            {
                continue;
            }
            const auto instanceIdOpt = TakeCoString(rawInstanceId);
            if (!instanceIdOpt || instanceIdOpt->empty() || controls_.contains(*instanceIdOpt))
            {
                continue;
            }
            const std::wstring instanceId = *instanceIdOpt;

            livePids.insert(pid);

            const ProcessInfo& info = names_.Get(pid);
            float volume = 0.0f;
            BOOL muted = FALSE;
            ComPtr<ISimpleAudioVolume> simpleVolume;
            if (!isInput && SUCCEEDED(control->QueryInterface(IID_PPV_ARGS(&simpleVolume))))
            {
                simpleVolume->GetMasterVolume(&volume);
                simpleVolume->GetMute(&muted);
            }

            AudioSessionSnapshot snapshot;
            snapshot.instanceId = instanceId;
            snapshot.pid = pid;
            snapshot.displayName = ChooseDisplayName(control.Get(), info, isSystemSounds);
            snapshot.executablePath = info.executablePath;
            snapshot.volume = volume;
            snapshot.isMuted = muted != FALSE;
            snapshot.peakLevel = ReadPeak(control.Get());
            snapshot.isSystemSounds = isSystemSounds;
            snapshot.state = static_cast<SessionState>(state);
            snapshot.hasOutputSession = !isInput;
            if (snapshot.IsActive())
            {
                if (isInput) snapshot.activeInputDeviceIds.push_back(deviceId);
                else snapshot.activeOutputDeviceIds.push_back(deviceId);
            }

            // Capture controls never enter the playback volume/mute map.
            if (!isInput) controls_.emplace(instanceId, control);
            snapshots.push_back(std::move(snapshot));
        }
    }

    names_.Trim(livePids);

    std::sort(snapshots.begin(), snapshots.end(), [](const AudioSessionSnapshot& a, const AudioSessionSnapshot& b)
    {
        if (a.IsActive() != b.IsActive())
        {
            return a.IsActive() && !b.IsActive();
        }
        return CompareDisplayNames(a.displayName, b.displayName) < 0;
    });

    snapshots = tracker_.Refresh(snapshots, AppSessionTracker::Clock::now());
    const auto now = AppSessionTracker::Clock::now();
    std::erase_if(transfers_, [&](const auto& entry) { return now >= entry.second.expires; });
    apps_.clear();
    for (auto& snapshot : snapshots)
    {
        if (const auto transfer = transfers_.find(snapshot.instanceId); transfer != transfers_.end())
        {
            bool applied = false;
            for (const auto& id : snapshot.sessionInstanceIds)
            {
                if (transfer->second.appliedIds.contains(id)) continue;
                ComPtr<IAudioSessionControl2> control;
                ComPtr<ISimpleAudioVolume> volume;
                if (TryGetControl(id, control) && SUCCEEDED(control.As(&volume)) &&
                    SUCCEEDED(volume->SetMasterVolume(transfer->second.volume, nullptr)) &&
                    SUCCEEDED(volume->SetMute(transfer->second.muted ? TRUE : FALSE, nullptr)))
                {
                    transfer->second.appliedIds.insert(id);
                    applied = true;
                }
            }
            if (applied)
            {
                snapshot.volume = transfer->second.volume;
                snapshot.isMuted = transfer->second.muted;
            }
        }
        if (!snapshot.isSystemSounds && (!snapshot.sessionInstanceIds.empty() || !snapshot.inputSessionInstanceIds.empty()))
        {
            snapshot.outputDeviceKnown = SUCCEEDED(AppOutputRouter::Get(snapshot.pid, snapshot.outputDeviceId));
            const auto inputPid = snapshot.inputProcessIds.empty() ? snapshot.pid : snapshot.inputProcessIds.front();
            snapshot.inputDeviceKnown = SUCCEEDED(AppDeviceRouter::Get(inputPid, eCapture, snapshot.inputDeviceId));
        }
        apps_.emplace(snapshot.instanceId, snapshot);
    }
    return snapshots;
}

void AudioSessionService::SetVolume(const std::wstring& instanceId, float scalar)
{
    const auto app = apps_.find(instanceId);
    const auto ids = app != apps_.end() ? app->second.sessionInstanceIds : std::vector<std::wstring>{instanceId};
    scalar = std::clamp(scalar, 0.0f, 1.0f);
    for (const auto& id : ids)
    {
        ComPtr<IAudioSessionControl2> control;
        if (!TryGetControl(id, control)) continue;
        Guarded([&]()
        {
            ComPtr<ISimpleAudioVolume> volume;
            ThrowIfFailed(control.As(&volume), "QI(ISimpleAudioVolume)");
            ThrowIfFailed(volume->SetMasterVolume(scalar, nullptr), "SetMasterVolume");
        });
    }
    if (app != apps_.end()) app->second.volume = scalar;
    if (const auto transfer = transfers_.find(instanceId); transfer != transfers_.end()) transfer->second.volume = scalar;
}

void AudioSessionService::SetMute(const std::wstring& instanceId, bool muted)
{
    const auto app = apps_.find(instanceId);
    const auto ids = app != apps_.end() ? app->second.sessionInstanceIds : std::vector<std::wstring>{instanceId};
    for (const auto& id : ids)
    {
        ComPtr<IAudioSessionControl2> control;
        if (!TryGetControl(id, control)) continue;
        Guarded([&]()
        {
            ComPtr<ISimpleAudioVolume> volume;
            ThrowIfFailed(control.As(&volume), "QI(ISimpleAudioVolume)");
            ThrowIfFailed(volume->SetMute(muted ? TRUE : FALSE, nullptr), "SetMute");
        });
    }
    if (app != apps_.end()) app->second.isMuted = muted;
    if (const auto transfer = transfers_.find(instanceId); transfer != transfers_.end()) transfer->second.muted = muted;
}

std::vector<AudioDeviceInfo> AudioSessionService::ListOutputDevices()
{
    std::optional<std::wstring> defaultId;
    ComPtr<IMMDevice> defaultDevice;
    if (SUCCEEDED(enumerator_->GetDefaultAudioEndpoint(eRender, eMultimedia, &defaultDevice)))
    {
        LPWSTR raw = nullptr;
        if (SUCCEEDED(defaultDevice->GetId(&raw))) defaultId = TakeCoString(raw);
    }

    ComPtr<IMMDeviceCollection> collection;
    ThrowIfFailed(enumerator_->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection), "EnumAudioEndpoints(render)");

    UINT count = 0;
    ThrowIfFailed(collection->GetCount(&count), "GetCount");

    std::vector<AudioDeviceInfo> devices;
    devices.reserve(count);
    for (UINT i = 0; i < count; ++i)
    {
        ComPtr<IMMDevice> item;
        if (FAILED(collection->Item(i, &item)))
        {
            continue;
        }

        LPWSTR rawId = nullptr;
        if (FAILED(item->GetId(&rawId)))
        {
            continue;
        }
        const std::wstring id = *TakeCoString(rawId);

        devices.push_back(AudioDeviceInfo{id, GetCachedFriendlyName(item.Get(), id), defaultId && id == *defaultId});
    }

    return devices;
}

void AudioSessionService::SetDefaultOutputDevice(const std::wstring& deviceId)
{
    Guarded([&]()
    {
        ThrowIfFailed(DefaultEndpointSwitcher::SetDefault(deviceId), "SetDefault(output)");
    });

    // The device we already resolved is still Active, just no longer
    // default -- drop it so the next Device() call re-resolves to the new
    // default instead of quietly continuing to mix the old one.
    ReleaseControls();
    device_.Reset();
}

void AudioSessionService::SetAppOutputDevice(uint32_t pid, const std::optional<std::wstring>& deviceId)
{
    ThrowIfFailed(AppOutputRouter::Set(pid, deviceId), "Windows could not change the app output device.");
}

void AudioSessionService::SetAppOutputDevice(const std::wstring& appId, const std::optional<std::wstring>& deviceId)
{
    const auto app = apps_.find(appId);
    if (app == apps_.end() || app->second.isSystemSounds)
    {
        throw std::runtime_error("The application no longer has an audio session.");
    }
    std::unordered_set<uint32_t> pids(app->second.processIds.begin(), app->second.processIds.end());
    // Endpoints remember independent levels. Align existing (including idle)
    // streams now, and transfer the level once to any replacement streams
    // created during this switch. Subsequent external changes remain readable.
    ControlTransfer transfer{app->second.volume, app->second.isMuted,
        AppSessionTracker::Clock::now() + std::chrono::seconds(3), {}};
    if (app->second.hasOutputSession)
    {
        SetVolume(appId, transfer.volume);
        SetMute(appId, transfer.muted);
    }
    HRESULT failure = S_OK;
    uint32_t failedPid = 0;
    for (uint32_t pid : pids)
    {
        if (!pid) continue;
        const HRESULT hr = AppOutputRouter::Set(pid, deviceId);
        if (FAILED(hr))
        {
            failure = hr;
            failedPid = pid;
        }
    }
    if (FAILED(failure))
    {
        char message[160];
        sprintf_s(message, "Windows could not change the app output (process %u, error 0x%08lX).",
                  failedPid, static_cast<unsigned long>(failure));
        throw ComException(failure, message);
    }
    if (app->second.hasOutputSession) transfers_[appId] = std::move(transfer);
}

void AudioSessionService::SetAppInputDevice(const std::wstring& appId, const std::optional<std::wstring>& deviceId)
{
    const auto app = apps_.find(appId);
    if (app == apps_.end() || app->second.isSystemSounds)
        throw std::runtime_error("The application no longer has an audio session.");

    // Includes both playback and microphone workers belonging to this app.
    // Only capture policy is changed; playback volume and routing stay intact.
    for (const auto pid : app->second.processIds)
    {
        if (!pid) continue;
        const HRESULT hr = AppDeviceRouter::Set(pid, eCapture, deviceId);
        if (FAILED(hr))
        {
            char message[160];
            sprintf_s(message, "Windows could not change the app input (process %u, error 0x%08lX).",
                      pid, static_cast<unsigned long>(hr));
            throw ComException(hr, message);
        }
    }
}

std::vector<AudioDeviceInfo> AudioSessionService::ListInputDevices()
{
    std::optional<std::wstring> defaultId;
    ComPtr<IMMDevice> defaultDevice;
    if (SUCCEEDED(enumerator_->GetDefaultAudioEndpoint(eCapture, eMultimedia, &defaultDevice)))
    {
        LPWSTR raw = nullptr;
        if (SUCCEEDED(defaultDevice->GetId(&raw)))
        {
            defaultId = TakeCoString(raw);
        }
    }
    // No active capture device at all is not an error here; every row below
    // simply reports isDefault = false.

    ComPtr<IMMDeviceCollection> collection;
    ThrowIfFailed(enumerator_->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &collection), "EnumAudioEndpoints(capture)");

    UINT count = 0;
    ThrowIfFailed(collection->GetCount(&count), "GetCount");

    std::vector<AudioDeviceInfo> devices;
    devices.reserve(count);
    for (UINT i = 0; i < count; ++i)
    {
        ComPtr<IMMDevice> item;
        if (FAILED(collection->Item(i, &item)))
        {
            continue;
        }

        LPWSTR rawId = nullptr;
        if (FAILED(item->GetId(&rawId)))
        {
            continue;
        }
        const std::wstring id = *TakeCoString(rawId);

        devices.push_back(AudioDeviceInfo{id, GetCachedFriendlyName(item.Get(), id), defaultId.has_value() && id == *defaultId});
    }

    return devices;
}

void AudioSessionService::SetDefaultInputDevice(const std::wstring& deviceId)
{
    Guarded([&]()
    {
        ThrowIfFailed(DefaultEndpointSwitcher::SetDefault(deviceId), "SetDefault(input)");
    });
}

std::wstring AudioSessionService::ChooseDisplayName(IAudioSessionControl2* control, const ProcessInfo& info, bool isSystemSounds)
{
    if (isSystemSounds)
    {
        return L"System sounds";
    }

    const auto resolved = SessionNaming::Resolve(TryReadDisplayName(control));
    return resolved ? *resolved : info.friendlyName;
}

std::optional<std::wstring> AudioSessionService::TryReadDisplayName(IAudioSessionControl2* control)
{
    LPWSTR raw = nullptr;
    if (FAILED(control->GetDisplayName(&raw)))
    {
        return std::nullopt;
    }
    return TakeCoString(raw);
}

float AudioSessionService::ReadPeak(IAudioSessionControl2* control)
{
    ComPtr<IAudioMeterInformation> meter;
    if (FAILED(control->QueryInterface(IID_PPV_ARGS(&meter))))
    {
        return 0.0f;
    }

    float peak = 0.0f;
    if (FAILED(meter->GetPeakValue(&peak)))
    {
        return 0.0f;
    }
    return peak;
}

std::wstring AudioSessionService::GetCachedFriendlyName(IMMDevice* device, const std::wstring& id)
{
    auto it = deviceNameCache_.find(id);
    if (it != deviceNameCache_.end())
    {
        return it->second;
    }

    std::wstring name = id;
    ComPtr<IPropertyStore> store;
    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store)))
    {
        PROPVARIANT variant;
        PropVariantInit(&variant);
        if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &variant)) &&
            variant.vt == VT_LPWSTR && variant.pwszVal != nullptr)
        {
            name = variant.pwszVal;
        }
        PropVariantClear(&variant);
    }

    deviceNameCache_[id] = name;
    return name;
}

bool AudioSessionService::TryGetControl(const std::wstring& instanceId, ComPtr<IAudioSessionControl2>& out)
{
    auto it = controls_.find(instanceId);
    if (it == controls_.end())
    {
        return false;
    }
    out = it->second;
    return true;
}

void AudioSessionService::ReleaseControls()
{
    controls_.clear();
}

IMMDevice* AudioSessionService::Device()
{
    // An old default endpoint can remain active after Windows selects a
    // different one. Resolve the actual default, not just its device state.
    ComPtr<IMMDevice> current;
    ThrowIfFailed(
        enumerator_->GetDefaultAudioEndpoint(eRender, eMultimedia, &current),
        "GetDefaultAudioEndpoint(render)");
    device_ = std::move(current);
    return device_.Get();
}

} // namespace winmix::audio
