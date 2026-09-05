using System.Windows;

// Enabling UseWindowsForms for the tray icon puts two Application types in scope
// via implicit usings. Alias the WPF one rather than dropping implicit usings,
// which would ripple through every file in the project.
using Application = System.Windows.Application;

namespace WinMix.App;

public partial class App : Application
{
    /// <summary>Session-local, so one name per logged-in user rather than per machine.</summary>
    private const string InstanceMutexName = @"Local\WinMix.SingleInstance";

    private Mutex? _instanceMutex;
    private bool _ownsInstance;
    private TrayIcon? _tray;
    private MainWindow? _window;

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);

        // A second copy would double the polling traffic and put a second icon in
        // the tray, so hand over to the running instance instead.
        _instanceMutex = new Mutex(true, InstanceMutexName, out _ownsInstance);
        if (!_ownsInstance)
        {
            _instanceMutex.Dispose();
            _instanceMutex = null;
            Shutdown();
            return;
        }

        _window = new MainWindow();
        _tray = new TrayIcon(
            onOpen: () => _window.ShowMixer(),
            onExit: Shutdown,
            listInputDevices: () => _window.ListInputDevices(),
            setDefaultInputDevice: id => _window.SetDefaultInputDevice(id));
        _window.ShowMixer();
    }

    protected override void OnExit(ExitEventArgs e)
    {
        _tray?.Dispose();
        _window?.ReleaseResources();

        if (_instanceMutex is not null)
        {
            if (_ownsInstance)
            {
                _instanceMutex.ReleaseMutex();
            }

            _instanceMutex.Dispose();
        }

        base.OnExit(e);
    }
}
