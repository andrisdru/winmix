#include "winmix/audio/AudioPolicyConfigFactory.h"

#include <windows.h>
#include <inspectable.h>
#include <roapi.h>
#include <winternl.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace winmix::audio {

namespace {

// Undocumented. 3 IUnknown + 3 IInspectable slots come from the IInspectable
// base class below, then 19 undocumented slots this app never calls, then
// the one method it does -- Windows 11's newer shape (see kNewShapeMinBuild)
// only appends further slots after this one, it never renumbers it. Slot
// numbers cross-checked against SoundSwitch (github.com/Belphemur/SoundSwitch)
// rather than derived from any header, since this interface is undocumented.
MIDL_INTERFACE("AB3D4648-E242-459F-B02F-541C70306324")
IAudioPolicyConfigInternal : public IInspectable
{
public:
    virtual HRESULT STDMETHODCALLTYPE Unused01() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused02() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused03() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused04() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused05() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused06() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused07() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused08() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused09() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused10() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused11() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused12() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused13() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused14() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused15() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused16() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused17() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused18() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unused19() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPersistedDefaultAudioEndpoint(
        DWORD processId, EDataFlow flow, ERole role, HSTRING deviceId) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPersistedDefaultAudioEndpoint(
        DWORD processId, EDataFlow flow, ERole role, HSTRING* deviceId) = 0;
};

// Below this build, Windows exposes the pre-21H2 shape of this class under a
// different IID; the vtable slots above are identical either way. 21390 was
// an Insider build during the Windows 10 21H2 development cycle; nothing
// shipped at exactly that number, but every released Windows 10 21H2
// (19044) and every Windows 11 build (22000+) compares correctly against it.
//
// This depends on WinMix.App's app.manifest declaring a Windows 10
// <supportedOS> entry: without one, the OS's version-lie compatibility shim
// reports a stale build number to any app that doesn't explicitly claim
// support for it -- to every version-query API uniformly (RtlGetVersion
// included, see GetOsBuildNumber below), not just the deprecated ones --
// which would pick the wrong IID here.
constexpr uint32_t kNewShapeMinBuild = 21390;

constexpr GUID kNewShapeIid = {0xAB3D4648, 0xE242, 0x459F, {0xB0, 0x2F, 0x54, 0x1C, 0x70, 0x30, 0x63, 0x24}};
constexpr GUID kLegacyShapeIid = {0x2A59116D, 0x6C4F, 0x45E0, {0xA7, 0x4F, 0x70, 0x7E, 0x3F, 0xEF, 0x92, 0x58}};

constexpr wchar_t kClassId[] = L"Windows.Media.Internal.AudioPolicyConfig";

uint32_t GetOsBuildNumber()
{
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);

    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll)
    {
        return 0;
    }

    auto rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"));
    if (!rtlGetVersion)
    {
        return 0;
    }

    RTL_OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    if (rtlGetVersion(&info) != 0) // STATUS_SUCCESS == 0
    {
        return 0;
    }

    return info.dwBuildNumber;
}

const GUID& ClassIid()
{
    return GetOsBuildNumber() >= kNewShapeMinBuild ? kNewShapeIid : kLegacyShapeIid;
}

} // namespace

HRESULT AudioPolicyConfigFactory::SetPersistedDefaultAudioEndpoint(
    uint32_t pid, EDataFlow flow, ERole role, HSTRING deviceId)
{
    HSTRING classId = nullptr;
    HRESULT hr = WindowsCreateString(kClassId, static_cast<UINT32>(wcslen(kClassId)), &classId);
    if (FAILED(hr))
    {
        return hr;
    }

    ComPtr<IAudioPolicyConfigInternal> factory;
    hr = RoGetActivationFactory(classId, ClassIid(), reinterpret_cast<void**>(factory.GetAddressOf()));
    WindowsDeleteString(classId);
    if (FAILED(hr))
    {
        return hr;
    }

    return factory->SetPersistedDefaultAudioEndpoint(static_cast<DWORD>(pid), flow, role, deviceId);
}

HRESULT AudioPolicyConfigFactory::GetPersistedDefaultAudioEndpoint(
    uint32_t pid, EDataFlow flow, ERole role, HSTRING* deviceId)
{
    *deviceId = nullptr;
    HSTRING classId = nullptr;
    HRESULT hr = WindowsCreateString(kClassId, static_cast<UINT32>(wcslen(kClassId)), &classId);
    if (FAILED(hr))
    {
        return hr;
    }
    ComPtr<IAudioPolicyConfigInternal> factory;
    hr = RoGetActivationFactory(classId, ClassIid(), reinterpret_cast<void**>(factory.GetAddressOf()));
    WindowsDeleteString(classId);
    return FAILED(hr) ? hr : factory->GetPersistedDefaultAudioEndpoint(pid, flow, role, deviceId);
}

} // namespace winmix::audio
