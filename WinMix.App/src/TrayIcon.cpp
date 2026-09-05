#include "TrayIcon.h"
#include "Version.h"

#include <windowsx.h>

namespace winmix::app {

namespace {
constexpr UINT_PTR kMenuOpenId = 1;
constexpr UINT_PTR kMenuExitId = 2;
constexpr UINT_PTR kMenuAutostartId = 3;
constexpr UINT_PTR kMenuVersionId = 4; // disabled -- never actually selectable
constexpr UINT_PTR kMenuOutputDeviceBase = 100; // + index into the current device list
constexpr UINT_PTR kMenuInputDeviceBase = 200; // + index into the current device list
} // namespace

TrayIcon::TrayIcon(HWND owner, UINT callbackMessage, HICON icon)
    : owner_(owner), callbackMessage_(callbackMessage)
{
    nid_.cbSize = sizeof(nid_);
    nid_.hWnd = owner_;
    nid_.uID = 1;
    nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid_.uCallbackMessage = callbackMessage_;
    nid_.hIcon = icon;
    const std::wstring tip = std::wstring(L"WinMix v") + kWinMixVersion;
    wcsncpy_s(nid_.szTip, tip.c_str(), _TRUNCATE);

    added_ = Shell_NotifyIconW(NIM_ADD, &nid_) != FALSE;
}

TrayIcon::~TrayIcon()
{
    if (added_)
    {
        Shell_NotifyIconW(NIM_DELETE, &nid_);
    }
}

void TrayIcon::OnCallback(LPARAM lParam)
{
    switch (LOWORD(lParam))
    {
    case WM_LBUTTONUP:
        if (onOpen)
        {
            onOpen();
        }
        break;

    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
        ShowContextMenu();
        break;

    default:
        break;
    }
}

void TrayIcon::ShowContextMenu()
{
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kMenuOpenId, L"Open");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    const std::vector<TrayDevice> outputDevices = listOutputDevices ? listOutputDevices() : std::vector<TrayDevice>{};

    HMENU outputMenu = CreatePopupMenu();
    for (size_t i = 0; i < outputDevices.size(); ++i)
    {
        const UINT flags = MF_STRING | (outputDevices[i].isDefault ? MF_CHECKED : 0u);
        AppendMenuW(outputMenu, flags, kMenuOutputDeviceBase + i, outputDevices[i].friendlyName.c_str());
    }

    if (!outputDevices.empty())
    {
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(outputMenu), L"Output");
    }
    else
    {
        DestroyMenu(outputMenu);
    }

    const std::vector<TrayDevice> devices = listInputDevices ? listInputDevices() : std::vector<TrayDevice>{};

    HMENU inputMenu = CreatePopupMenu();
    for (size_t i = 0; i < devices.size(); ++i)
    {
        const UINT flags = MF_STRING | (devices[i].isDefault ? MF_CHECKED : 0u);
        AppendMenuW(inputMenu, flags, kMenuInputDeviceBase + i, devices[i].friendlyName.c_str());
    }

    if (!devices.empty())
    {
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(inputMenu), L"Microphone");
    }
    else
    {
        DestroyMenu(inputMenu);
    }

    const bool autostartEnabled = isAutostartEnabled && isAutostartEnabled();
    AppendMenuW(menu, MF_STRING | (autostartEnabled ? MF_CHECKED : 0u), kMenuAutostartId, L"Start with Windows");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    const std::wstring versionLabel = std::wstring(L"WinMix v") + kWinMixVersion;
    AppendMenuW(menu, MF_STRING | MF_DISABLED, kMenuVersionId, versionLabel.c_str());
    AppendMenuW(menu, MF_STRING, kMenuExitId, L"Exit");

    POINT pt;
    GetCursorPos(&pt);

    // Both lines are the standard, documented workaround for a tray
    // context menu that otherwise fails to dismiss on click-away.
    SetForegroundWindow(owner_);
    const int selected = TrackPopupMenu(
        menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
        pt.x, pt.y, 0, owner_, nullptr);
    PostMessageW(owner_, WM_NULL, 0, 0);

    if (selected == static_cast<int>(kMenuOpenId))
    {
        if (onOpen)
        {
            onOpen();
        }
    }
    else if (selected == static_cast<int>(kMenuExitId))
    {
        if (onExit)
        {
            onExit();
        }
    }
    else if (selected == static_cast<int>(kMenuAutostartId))
    {
        if (setAutostartEnabled)
        {
            setAutostartEnabled(!autostartEnabled);
        }
    }
    else if (selected >= static_cast<int>(kMenuInputDeviceBase))
    {
        const size_t index = static_cast<size_t>(selected) - kMenuInputDeviceBase;
        if (index < devices.size() && setDefaultInputDevice)
        {
            setDefaultInputDevice(devices[index].id);
        }
    }
    else if (selected >= static_cast<int>(kMenuOutputDeviceBase))
    {
        const size_t index = static_cast<size_t>(selected) - kMenuOutputDeviceBase;
        if (index < outputDevices.size() && setDefaultOutputDevice)
        {
            setDefaultOutputDevice(outputDevices[index].id);
        }
    }

    DestroyMenu(menu); // recursively destroys the attached output/input submenus too
}

} // namespace winmix::app
