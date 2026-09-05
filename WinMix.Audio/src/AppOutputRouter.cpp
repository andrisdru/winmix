#include "winmix/audio/AppOutputRouter.h"
#include "winmix/audio/AudioPolicyConfigFactory.h"

#include <windows.h>
#include <mmdeviceapi.h>
#include <winstring.h>

namespace winmix::audio {

namespace {

// IMMDevice::GetId() returns only the middle portion of the full device
// interface path SetPersistedDefaultAudioEndpoint actually expects.
constexpr wchar_t kDeviceInterfaceToken[] = LR"(\\?\SWD#MMDEVAPI#)";
constexpr wchar_t kRenderInterfaceSuffix[] = L"#{e6327cad-dcec-4949-ae8a-991e976a79d2}";

} // namespace

void AppOutputRouter::Set(uint32_t pid, const std::optional<std::wstring>& deviceId)
{
    HSTRING packed = nullptr;
    if (deviceId && !deviceId->empty())
    {
        const std::wstring full = kDeviceInterfaceToken + *deviceId + kRenderInterfaceSuffix;
        WindowsCreateString(full.c_str(), static_cast<UINT32>(full.size()), &packed);
    }

    // Best-effort, matching the original: neither call's result is checked
    // here (a genuinely broken activation would already have failed loudly
    // during earlier use of this machinery, and a transient failure here
    // just means the pin silently doesn't take -- the next poll shows
    // reality either way).
    AudioPolicyConfigFactory::SetPersistedDefaultAudioEndpoint(pid, eRender, eConsole, packed);
    AudioPolicyConfigFactory::SetPersistedDefaultAudioEndpoint(pid, eRender, eMultimedia, packed);

    if (packed)
    {
        WindowsDeleteString(packed);
    }
}

} // namespace winmix::audio
