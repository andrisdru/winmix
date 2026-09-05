#pragma once

#include <windows.h>

#include <cstdint>
#include <optional>
#include <string>

namespace winmix::audio {

// Pins one process's playback to a specific render device, or with an empty
// deviceId clears the pin so it follows the system default again, via
// AudioPolicyConfigFactory. Only Console and Multimedia roles are set --
// deliberately not Communications, since a per-app override has no bearing
// on a separate voice-call device.
class AppOutputRouter
{
public:
    static HRESULT Set(uint32_t pid, const std::optional<std::wstring>& deviceId);
    static HRESULT Get(uint32_t pid, std::optional<std::wstring>& deviceId);
};

} // namespace winmix::audio
