using System.Drawing;
using System.Runtime.InteropServices;
using System.Windows.Forms;
using WinMix.Audio;

namespace WinMix.App;

/// <summary>
/// Notification-area presence for the app.
///
/// This is the one place we reach into Windows Forms: WPF ships no tray
/// abstraction, and NotifyIcon is the supported way to get one without taking on
/// a third-party dependency.
/// </summary>
internal sealed class TrayIcon : IDisposable
{
    private readonly NotifyIcon _icon;
    private readonly ContextMenuStrip _menu;
    private readonly Icon _appIcon;
    private readonly ToolStripMenuItem _inputDeviceMenu;
    private readonly Func<IReadOnlyList<AudioDeviceInfo>> _listInputDevices;
    private readonly Action<string> _setDefaultInputDevice;

    public TrayIcon(
        Action onOpen,
        Action onExit,
        Func<IReadOnlyList<AudioDeviceInfo>> listInputDevices,
        Action<string> setDefaultInputDevice)
    {
        _listInputDevices = listInputDevices;
        _setDefaultInputDevice = setDefaultInputDevice;

        _inputDeviceMenu = new ToolStripMenuItem("Microphone");
        // Built lazily rather than once at startup, so a mic plugged in or
        // unplugged while the app is running is reflected the next time the
        // user actually opens the submenu instead of going stale.
        _inputDeviceMenu.DropDownOpening += (_, _) => RebuildInputDeviceMenu();

        _menu = new ContextMenuStrip();
        _menu.Items.Add("Open mixer", null, (_, _) => onOpen());
        _menu.Items.Add(new ToolStripSeparator());
        _menu.Items.Add(_inputDeviceMenu);
        _menu.Items.Add(new ToolStripSeparator());
        _menu.Items.Add("Exit", null, (_, _) => onExit());

        _appIcon = LoadAppIcon();

        _icon = new NotifyIcon
        {
            Icon = _appIcon,
            Text = "WinMix",
            Visible = true,
            ContextMenuStrip = _menu,
        };

        _icon.DoubleClick += (_, _) => onOpen();
    }

    /// <summary>
    /// Loads the icon bundled as a WPF resource (see the csproj's Resource item)
    /// rather than a loose file next to the exe, so a single copy of AppIcon.ico
    /// serves the exe's own Win32 icon (via ApplicationIcon), the window's
    /// titlebar (via Window.Icon), and this tray icon.
    /// </summary>
    private static Icon LoadAppIcon()
    {
        var info = System.Windows.Application.GetResourceStream(new Uri("pack://application:,,,/Assets/AppIcon.ico"));
        using var stream = info!.Stream;
        return new Icon(stream);
    }

    private void RebuildInputDeviceMenu()
    {
        _inputDeviceMenu.DropDownItems.Clear();

        IReadOnlyList<AudioDeviceInfo> devices;
        try
        {
            devices = _listInputDevices();
        }
        catch (COMException)
        {
            devices = Array.Empty<AudioDeviceInfo>();
        }

        if (devices.Count == 0)
        {
            _inputDeviceMenu.DropDownItems.Add(new ToolStripMenuItem("No microphones found") { Enabled = false });
            return;
        }

        foreach (var device in devices)
        {
            var item = new ToolStripMenuItem(device.FriendlyName) { Checked = device.IsDefault };
            item.Click += (_, _) => _setDefaultInputDevice(device.Id);
            _inputDeviceMenu.DropDownItems.Add(item);
        }
    }

    public void Dispose()
    {
        // Hiding before disposing matters: a NotifyIcon torn down while visible
        // leaves a dead icon in the tray until the user hovers over it.
        _icon.Visible = false;
        _icon.Dispose();
        _menu.Dispose();
        _appIcon.Dispose();
    }
}
