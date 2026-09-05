using System.ComponentModel;
using System.Windows;
using System.Windows.Interop;
using WinMix.App.ViewModels;
using WinMix.Audio;

namespace WinMix.App;

public partial class MainWindow : Window
{
    public MainWindow() => InitializeComponent();

    private MixerViewModel? ViewModel => DataContext as MixerViewModel;

    protected override void OnSourceInitialized(EventArgs e)
    {
        base.OnSourceInitialized(e);
        DwmInterop.EnableDarkTitleBar(new WindowInteropHelper(this).Handle);
        WindowResize.LockWidth(this);

        // SizeToContent="Width" lets the window grow with the channel-strip count;
        // cap it so a long session list still fits the screen instead of running
        // off the edge, leaving the strips ScrollViewer as the overflow fallback.
        MaxWidth = Math.Max(MinWidth, SystemParameters.WorkArea.Width - 60);
    }

    /// <summary>Reveals the window and resumes polling.</summary>
    public void ShowMixer()
    {
        Show();

        if (WindowState == WindowState.Minimized)
        {
            WindowState = WindowState.Normal;
        }

        Activate();

        // Keyboard navigation focuses the first focusable channel strip on
        // activation, which otherwise drags the horizontal scroll away from the
        // device fader before the user has touched anything.
        StripsScroll.ScrollToLeftEnd();

        ViewModel?.Start();
    }

    /// <summary>
    /// The close button hides to the tray instead of exiting, which is the
    /// convention for a mixer you want available but not on screen. Exit from the
    /// tray menu is the only path to real shutdown.
    /// </summary>
    protected override void OnClosing(CancelEventArgs e)
    {
        e.Cancel = true;
        ViewModel?.Stop();
        Hide();
        base.OnClosing(e);
    }

    /// <summary>Tears down the audio session service. Called once, on real shutdown.</summary>
    public void ReleaseResources() => ViewModel?.Dispose();

    /// <summary>
    /// Active capture devices, for the tray menu's input-device submenu. Queried
    /// directly rather than through the (possibly stopped) poll timer, since the
    /// tray must work while the mixer window is hidden.
    /// </summary>
    public IReadOnlyList<AudioDeviceInfo> ListInputDevices() => ViewModel?.ListInputDevices() ?? Array.Empty<AudioDeviceInfo>();

    /// <summary>Sets the system default capture device from the tray menu.</summary>
    public void SetDefaultInputDevice(string deviceId) => ViewModel?.SetDefaultInputDevice(deviceId);
}
