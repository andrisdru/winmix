#pragma once

#include <windows.h>

#include <string>

namespace winmix::audio {

// Switches the system's default render/capture device (all roles) via the
// undocumented but stable-since-Vista IPolicyConfig/PolicyConfigClient COM
// object -- the same object Windows' own Settings app uses internally, and
// the well-known "PolicyConfig hack" used by EarTrumpet, SoundSwitch, and
// most other third-party volume-control utilities.
class DefaultEndpointSwitcher
{
public:
    static HRESULT SetDefault(const std::wstring& deviceId);
};

} // namespace winmix::audio
