#pragma once

namespace winmix::app::Autostart {

// Both read HKCU\...\Run, not HKLM -- matches the manifest's asInvoker
// level (see CLAUDE.md) and needs no elevation.
bool IsEnabled();
void SetEnabled(bool enabled);

} // namespace winmix::app::Autostart
