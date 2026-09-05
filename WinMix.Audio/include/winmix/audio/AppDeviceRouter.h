#pragma once

#include <mmdeviceapi.h>
#include <cstdint>
#include <optional>
#include <string>

namespace winmix::audio {

// Per-process Windows preferences. Capture includes the Communications role
// so voice-call apps using the default communications microphone also follow.
class AppDeviceRouter
{
public:
    static HRESULT Set(uint32_t pid, EDataFlow flow, const std::optional<std::wstring>& deviceId);
    static HRESULT Get(uint32_t pid, EDataFlow flow, std::optional<std::wstring>& deviceId,
                       ERole role = eMultimedia);
};

} // namespace winmix::audio
