#pragma once

#include <windows.h>
#include <shellapi.h>

#include <functional>
#include <string>
#include <vector>

namespace winmix::app {

struct TrayDevice
{
    std::wstring id;
    std::wstring friendlyName;
    bool isDefault = false;
};

// The tray icon and its right-click menu (Open / output-device submenu /
// input-device submenu / Exit). Routes through a caller-chosen WM_APP
// message on the owning window rather than owning a window of its own --
// the owner's WndProc forwards that message to OnCallback().
class TrayIcon
{
public:
    TrayIcon(HWND owner, UINT callbackMessage, HICON icon);
    ~TrayIcon();

    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    void OnCallback(LPARAM lParam);

    std::function<void()> onOpen;
    std::function<void()> onExit;
    std::function<std::vector<TrayDevice>()> listOutputDevices;
    std::function<void(const std::wstring&)> setDefaultOutputDevice;
    std::function<std::vector<TrayDevice>()> listInputDevices;
    std::function<void(const std::wstring&)> setDefaultInputDevice;
    std::function<bool()> isAutostartEnabled;
    std::function<void(bool)> setAutostartEnabled;

private:
    void ShowContextMenu();

    HWND owner_;
    UINT callbackMessage_;
    NOTIFYICONDATAW nid_{};
    bool added_ = false;
};

} // namespace winmix::app
