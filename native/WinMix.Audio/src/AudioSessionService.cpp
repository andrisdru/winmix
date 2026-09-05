#include "winmix/audio/AudioSessionService.h"
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
    IMMDevice* device = Device();

    ComPtr<IAudioSessionManager2> manager;
    ThrowIfFailed(
        device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, &manager),
        "Activate(IAudioSessionManager2)");

    // Unlike NAudio's AudioSessionManager.RefreshSessions() (a managed-side
    // convenience with no native counterpart), IAudioSessionManager2 has no
    // RefreshSessions method -- GetSessionEnumerator() itself always returns
    // the current session set.
    ComPtr<IAudioSessionEnumerator> sessionEnum;
    ThrowIfFailed(manager->GetSessionEnumerator(&sessionEnum), "GetSessionEnumerator");

    // The previous batch of controls is released here (ComPtr's destructor
    // does the Release) before the map is repopulated below.
    ReleaseControls();

    int count = 0;
    ThrowIfFailed(sessionEnum->GetCount(&count), "GetCount");

    std::vector<AudioSessionSnapshot> snapshots;
    snapshots.reserve(static_cast<size_t>(count));
    std::unordered_set<uint32_t> livePids;

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

        DWORD pid = 0;
        control->GetProcessId(&pid);
        livePids.insert(pid);

        const ProcessInfo& info = names_.Get(pid);
        const bool isSystemSounds = control->IsSystemSoundsSession() == S_OK;

        float volume = 0.0f;
        BOOL muted = FALSE;
        ComPtr<ISimpleAudioVolume> simpleVolume;
        if (SUCCEEDED(control->QueryInterface(IID_PPV_ARGS(&simpleVolume))))
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

        controls_.emplace(instanceId, control);
        snapshots.push_back(std::move(snapshot));
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

    return snapshots;
}

void AudioSessionService::SetVolume(const std::wstring& instanceId, float scalar)
{
    ComPtr<IAudioSessionControl2> control;
    if (!TryGetControl(instanceId, control))
    {
        return;
    }

    scalar = std::clamp(scalar, 0.0f, 1.0f);
    Guarded([&]()
    {
        ComPtr<ISimpleAudioVolume> simpleVolume;
        ThrowIfFailed(control->QueryInterface(IID_PPV_ARGS(&simpleVolume)), "QI(ISimpleAudioVolume)");
        ThrowIfFailed(simpleVolume->SetMasterVolume(scalar, nullptr), "SetMasterVolume");
    });
}

void AudioSessionService::SetMute(const std::wstring& instanceId, bool muted)
{
    ComPtr<IAudioSessionControl2> control;
    if (!TryGetControl(instanceId, control))
    {
        return;
    }

    Guarded([&]()
    {
        ComPtr<ISimpleAudioVolume> simpleVolume;
        ThrowIfFailed(control->QueryInterface(IID_PPV_ARGS(&simpleVolume)), "QI(ISimpleAudioVolume)");
        ThrowIfFailed(simpleVolume->SetMute(muted ? TRUE : FALSE, nullptr), "SetMute");
    });
}

std::vector<AudioDeviceInfo> AudioSessionService::ListOutputDevices()
{
    IMMDevice* device = Device();

    LPWSTR rawDefaultId = nullptr;
    ThrowIfFailed(device->GetId(&rawDefaultId), "GetId");
    const std::wstring defaultId = *TakeCoString(rawDefaultId);

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

        devices.push_back(AudioDeviceInfo{id, GetCachedFriendlyName(item.Get(), id), id == defaultId});
    }

    return devices;
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
    // Re-resolve when the default endpoint changes under us (headphones
    // plugged in, device disabled); the old device pointer keeps returning
    // stale sessions rather than failing loudly.
    if (device_ && IsUsable(device_.Get()))
    {
        return device_.Get();
    }

    ReleaseControls();
    device_.Reset();

    ThrowIfFailed(
        enumerator_->GetDefaultAudioEndpoint(eRender, eMultimedia, &device_),
        "GetDefaultAudioEndpoint(render)");

    return device_.Get();
}

bool AudioSessionService::IsUsable(IMMDevice* device)
{
    DWORD state = 0;
    if (FAILED(device->GetState(&state)))
    {
        return false;
    }
    return state == DEVICE_STATE_ACTIVE;
}

} // namespace winmix::audio
