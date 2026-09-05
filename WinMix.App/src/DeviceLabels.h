#pragma once

#include <string>
#include <vector>

namespace winmix::app {

// Labels only: indices and the endpoint IDs used for routing never change.
// Unknown/custom names are retained. Ambiguous short labels fall back to
// their full names; identical full names get an ordinal in this device list.
std::vector<std::wstring> CompactDeviceLabels(const std::vector<std::wstring>& names);

} // namespace winmix::app
