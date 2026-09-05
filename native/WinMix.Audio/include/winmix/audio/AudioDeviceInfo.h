#pragma once

#include <string>

namespace winmix::audio {

// One active endpoint (render or capture), for a device picker.
struct AudioDeviceInfo
{
    std::wstring id;
    std::wstring friendlyName;
    bool isDefault = false;
};

} // namespace winmix::audio
