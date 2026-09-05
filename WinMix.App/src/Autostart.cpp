#include "Autostart.h"

#include <windows.h>

#include <string>

namespace winmix::app::Autostart {

namespace {

constexpr wchar_t kRunKeyPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kValueName[] = L"WinMix";

} // namespace

bool IsEnabled()
{
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
    {
        return false;
    }

    const LSTATUS status = RegQueryValueExW(key, kValueName, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

void SetEnabled(bool enabled)
{
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
    {
        return;
    }

    if (enabled)
    {
        wchar_t path[MAX_PATH];
        const DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (len > 0 && len < MAX_PATH)
        {
            // Quoted so a launch survives spaces in the install path (e.g.
            // "C:\Program Files\WinMix\WinMix.exe"). --minimized tells
            // WinMain (see WinMain.cpp) this is a sign-in launch, not the
            // user double-clicking the exe, so it should stay tray-only
            // instead of popping the mixer window open unasked.
            const std::wstring command = L"\"" + std::wstring(path) + L"\" --minimized";
            RegSetValueExW(
                key, kValueName, 0, REG_SZ,
                reinterpret_cast<const BYTE*>(command.c_str()),
                static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
        }
    }
    else
    {
        RegDeleteValueW(key, kValueName);
    }

    RegCloseKey(key);
}

} // namespace winmix::app::Autostart
