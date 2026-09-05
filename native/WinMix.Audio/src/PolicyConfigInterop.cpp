#include "winmix/audio/PolicyConfigInterop.h"

#include <mmdeviceapi.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace winmix::audio {

namespace {

// Undocumented (IID F8679F50-850A-41CF-9C72-430F290290C8). The leading
// placeholder methods stand in for real vtable methods (GetMixFormat,
// GetDeviceFormat, ResetDeviceFormat, SetDeviceFormat, GetProcessingPeriod,
// SetProcessingPeriod, GetShareMode, SetShareMode, GetPropertyValue,
// SetPropertyValue) this app never calls -- removing any of them would
// shift SetDefaultEndpoint to the wrong slot and silently invoke a
// different method. A trailing placeholder (SetEndpointVisibility) follows
// the one method actually used, for the same reason.
//
// The original reference source (WinMix.Audio/PolicyConfigInterop.cs) was
// missing ResetDeviceFormat (9 unused methods instead of 10), which put
// SetDefaultEndpoint at the wrong slot -- confirmed by a crash on the very
// first call in an isolated repro (tools/AudioSmokeTest --set-default),
// diagnosed here and cross-checked against the widely-used community
// IPolicyConfig declaration (e.g. the one behind EarTrumpet/SoundVolumeView-
// style tools), which includes it.
MIDL_INTERFACE("F8679F50-850A-41CF-9C72-430F290290C8")
IPolicyConfig : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE Unused1() = 0; // GetMixFormat
    virtual HRESULT STDMETHODCALLTYPE Unused2() = 0; // GetDeviceFormat
    virtual HRESULT STDMETHODCALLTYPE Unused3() = 0; // ResetDeviceFormat
    virtual HRESULT STDMETHODCALLTYPE Unused4() = 0; // SetDeviceFormat
    virtual HRESULT STDMETHODCALLTYPE Unused5() = 0; // GetProcessingPeriod
    virtual HRESULT STDMETHODCALLTYPE Unused6() = 0; // SetProcessingPeriod
    virtual HRESULT STDMETHODCALLTYPE Unused7() = 0; // GetShareMode
    virtual HRESULT STDMETHODCALLTYPE Unused8() = 0; // SetShareMode
    virtual HRESULT STDMETHODCALLTYPE Unused9() = 0; // GetPropertyValue
    virtual HRESULT STDMETHODCALLTYPE Unused10() = 0; // SetPropertyValue
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(LPCWSTR deviceId, ERole role) = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused11() = 0; // SetEndpointVisibility
};

// CLSID 870AF99C-171D-4F9E-AF0D-E63DF40C2BC9 ("PolicyConfigClient").
constexpr CLSID kPolicyConfigClientClsid = {
    0x870AF99C, 0x171D, 0x4F9E, {0xAF, 0x0D, 0xE6, 0x3D, 0xF4, 0x0C, 0x2B, 0xC9}};

} // namespace

HRESULT DefaultEndpointSwitcher::SetDefault(const std::wstring& deviceId)
{
    ComPtr<IPolicyConfig> policyConfig;
    HRESULT hr = CoCreateInstance(
        kPolicyConfigClientClsid, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&policyConfig));
    if (FAILED(hr))
    {
        return hr;
    }

    // The system default must move games/notifications, music, and calls
    // together, or they would point at different, now-stale devices --
    // unlike AppOutputRouter's per-app pin, this one does include
    // Communications.
    for (const ERole role : {eConsole, eMultimedia, eCommunications})
    {
        hr = policyConfig->SetDefaultEndpoint(deviceId.c_str(), role);
        if (FAILED(hr))
        {
            return hr;
        }
    }

    return S_OK;
}

} // namespace winmix::audio
