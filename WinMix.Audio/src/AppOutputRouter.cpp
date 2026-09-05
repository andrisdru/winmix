#include "winmix/audio/AppOutputRouter.h"
#include "winmix/audio/AppDeviceRouter.h"

namespace winmix::audio {
HRESULT AppOutputRouter::Set(uint32_t pid, const std::optional<std::wstring>& deviceId)
{
    return AppDeviceRouter::Set(pid, eRender, deviceId);
}

HRESULT AppOutputRouter::Get(uint32_t pid, std::optional<std::wstring>& deviceId)
{
    return AppDeviceRouter::Get(pid, eRender, deviceId);
}
} // namespace winmix::audio
