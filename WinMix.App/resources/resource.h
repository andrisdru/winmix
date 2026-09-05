#pragma once

// Shared between WinMix.rc and the C++ source (window title-bar icon, tray
// icon) so both refer to the exact same embedded resource -- one asset,
// three uses (exe icon via being the first ICON in the .rc, title bar via
// WM_SETICON, tray via Shell_NotifyIconW), matching the .NET port's wiring.
#define IDI_APPICON 101
