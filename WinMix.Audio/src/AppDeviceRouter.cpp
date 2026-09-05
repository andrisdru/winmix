#include "winmix/audio/AppDeviceRouter.h"
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
constexpr wchar_t kCaptureInterfaceSuffix[] = L"#{2eef81be-33fa-4800-9670-1cd474972c3f}";

} // namespace

HRESULT AppDeviceRouter::Set(uint32_t pid, EDataFlow flow, const std::optional<std::wstring>& deviceId)
{
    if (flow != eRender && flow != eCapture) return E_INVALIDARG;
    const auto* suffix = flow == eCapture ? kCaptureInterfaceSuffix : kRenderInterfaceSuffix;
    HSTRING packed = nullptr;
    if (deviceId && !deviceId->empty())
    {
        const std::wstring full = kDeviceInterfaceToken + *deviceId + suffix;
        const HRESULT hr = WindowsCreateString(full.c_str(), static_cast<UINT32>(full.size()), &packed);
        if (FAILED(hr))
        {
            return hr;
        }
    }

    const HRESULT multimediaHr = AudioPolicyConfigFactory::SetPersistedDefaultAudioEndpoint(pid, flow, eMultimedia, packed);
    const HRESULT consoleHr = AudioPolicyConfigFactory::SetPersistedDefaultAudioEndpoint(pid, flow, eConsole, packed);

    const HRESULT communicationsHr = flow == eCapture
        ? AudioPolicyConfigFactory::SetPersistedDefaultAudioEndpoint(pid, flow, eCommunications, packed) : S_OK;

    if (packed)
    {
        WindowsDeleteString(packed);
    }
    if (FAILED(multimediaHr)) return multimediaHr;
    return FAILED(consoleHr) ? consoleHr : communicationsHr;
}

HRESULT AppDeviceRouter::Get(uint32_t pid, EDataFlow flow, std::optional<std::wstring>& deviceId, ERole role)
{
    deviceId.reset();
    if (flow != eRender && flow != eCapture) return E_INVALIDARG;
    const auto* suffix = flow == eCapture ? kCaptureInterfaceSuffix : kRenderInterfaceSuffix;
    HSTRING packed = nullptr;
    const HRESULT hr = AudioPolicyConfigFactory::GetPersistedDefaultAudioEndpoint(pid, flow, role, &packed);
    if (SUCCEEDED(hr))
    {
        UINT32 length = 0;
        const wchar_t* raw = WindowsGetStringRawBuffer(packed, &length);
        if (length)
        {
            std::wstring id(raw, length);
            if (id.starts_with(kDeviceInterfaceToken))
            {
                id.erase(0, wcslen(kDeviceInterfaceToken));
            }
            if (id.ends_with(suffix))
            {
                id.resize(id.size() - wcslen(suffix));
            }
            deviceId = std::move(id);
        }
    }
    WindowsDeleteString(packed);
    return hr;
}

} // namespace winmix::audio
