using System.Collections.ObjectModel;
using System.Runtime.InteropServices;
using System.Windows.Threading;
using WinMix.Audio;

namespace WinMix.App.ViewModels;

/// <summary>
/// Drives the mixer window: polls <see cref="AudioSessionService"/> and reconciles
/// the result into a stable row collection.
/// </summary>
public sealed class MixerViewModel : ObservableObject, IDisposable
{
    /// <summary>
    /// Poll interval. Fast enough that the peak meters read as continuous, slow
    /// enough that re-enumerating sessions stays free.
    /// </summary>
    private static readonly TimeSpan PollInterval = TimeSpan.FromMilliseconds(100);

    private readonly AudioSessionService _service = new();
    private readonly DispatcherTimer _timer;

    private bool _syncing;
    private bool _syncingDevice;
    private bool _syncingInputDevice;
    private double _masterPosition;
    private bool _masterMuted;
    private string? _statusMessage;
    private OutputDeviceViewModel? _selectedOutputDevice;
    private InputDeviceViewModel? _selectedInputDevice;
    private bool _disposed;

    public MixerViewModel()
    {
        _timer = new DispatcherTimer(DispatcherPriority.Background) { Interval = PollInterval };
        _timer.Tick += (_, _) => Refresh();

        // Every per-app picker offers this same "follow the system default"
        // sentinel ahead of the real devices synced into it below.
        AppOutputOptions.Add(new OutputDeviceViewModel(null, "Default"));
    }

    public ObservableCollection<SessionViewModel> Sessions { get; } = new();

    /// <summary>Active render devices, for the header's output-device picker.</summary>
    public ObservableCollection<OutputDeviceViewModel> OutputDevices { get; } = new();

    /// <summary>The "Default" sentinel plus <see cref="OutputDevices"/>, shared by every app row.</summary>
    public ObservableCollection<OutputDeviceViewModel> AppOutputOptions { get; } = new();

    /// <summary>Active capture devices (microphones), for the header's input-device picker.</summary>
    public ObservableCollection<InputDeviceViewModel> InputDevices { get; } = new();

    /// <summary>Non-null when the audio stack is unavailable, e.g. no output device.</summary>
    public string? StatusMessage
    {
        get => _statusMessage;
        private set
        {
            if (SetField(ref _statusMessage, value))
            {
                OnPropertyChanged(nameof(HasStatusMessage));
            }
        }
    }

    public bool HasStatusMessage => StatusMessage is not null;

    public double MasterPosition
    {
        get => _masterPosition;
        set
        {
            if (!SetField(ref _masterPosition, value))
            {
                return;
            }

            OnPropertyChanged(nameof(MasterPercent));

            if (!_syncing)
            {
                Guarded(() => _service.MasterVolume = VolumeCurve.ToScalar(value));
            }
        }
    }

    public int MasterPercent => (int)Math.Round(_masterPosition * 100.0);

    public bool MasterMuted
    {
        get => _masterMuted;
        set
        {
            if (SetField(ref _masterMuted, value) && !_syncing)
            {
                Guarded(() => _service.MasterMuted = value);
            }
        }
    }

    /// <summary>
    /// The system's default render device. Setting this switches every app's
    /// audio to the new device, not just this mixer's view of it.
    /// </summary>
    public OutputDeviceViewModel? SelectedOutputDevice
    {
        get => _selectedOutputDevice;
        set
        {
            if (!SetField(ref _selectedOutputDevice, value))
            {
                return;
            }

            if (!_syncingDevice && value?.Id is { } deviceId)
            {
                Guarded(() => _service.SetDefaultOutputDevice(deviceId));
            }
        }
    }

    /// <summary>
    /// The system's default capture device. Setting this switches every app's
    /// microphone input to the new device -- there is no per-app override, unlike
    /// <see cref="SelectedOutputDevice"/>.
    /// </summary>
    public InputDeviceViewModel? SelectedInputDevice
    {
        get => _selectedInputDevice;
        set
        {
            if (!SetField(ref _selectedInputDevice, value))
            {
                return;
            }

            if (!_syncingInputDevice && value?.Id is { } deviceId)
            {
                Guarded(() => _service.SetDefaultInputDevice(deviceId));
            }
        }
    }

    /// <summary>Active capture devices, for the tray menu's input-device submenu.</summary>
    public IReadOnlyList<AudioDeviceInfo> ListInputDevices() => _service.ListInputDevices();

    /// <summary>Sets the system default capture device from the tray menu.</summary>
    public void SetDefaultInputDevice(string deviceId) => Guarded(() => _service.SetDefaultInputDevice(deviceId));

    public void Start()
    {
        Refresh();
        _timer.Start();
    }

    /// <summary>
    /// Stops polling. Worth calling while the window is hidden to the tray -- there
    /// is nothing to animate, so the COM traffic would be pure waste.
    /// </summary>
    public void Stop() => _timer.Stop();

    private void Refresh()
    {
        if (_disposed)
        {
            return;
        }

        IReadOnlyList<AudioSessionSnapshot> snapshots;
        try
        {
            snapshots = _service.Refresh();
            SyncMaster();
            SyncOutputDevices();
            SyncInputDevices();
            StatusMessage = null;
        }
        catch (COMException)
        {
            // Typically no active render endpoint at all.
            Sessions.Clear();
            StatusMessage = "No audio output device is available.";
            return;
        }

        Reconcile(snapshots);
    }

    /// <summary>
    /// Matches rows to sessions by instance id: updates survivors in place, drops
    /// the departed, appends arrivals.
    ///
    /// New rows go on the end rather than in the service's sort position on purpose.
    /// Re-sorting live would make rows leap around the window every time an app
    /// started or stopped producing sound, which is worse than a stale ordering.
    /// </summary>
    private void Reconcile(IReadOnlyList<AudioSessionSnapshot> snapshots)
    {
        var incoming = new Dictionary<string, AudioSessionSnapshot>(snapshots.Count);
        foreach (var snapshot in snapshots)
        {
            incoming[snapshot.InstanceId] = snapshot;
        }

        for (var i = Sessions.Count - 1; i >= 0; i--)
        {
            if (!incoming.ContainsKey(Sessions[i].InstanceId))
            {
                Sessions.RemoveAt(i);
            }
        }

        var present = new Dictionary<string, SessionViewModel>(Sessions.Count);
        foreach (var row in Sessions)
        {
            present[row.InstanceId] = row;
        }

        foreach (var snapshot in snapshots)
        {
            if (present.TryGetValue(snapshot.InstanceId, out var row))
            {
                row.Sync(snapshot);
            }
            else
            {
                Sessions.Add(new SessionViewModel(_service, snapshot, AppOutputOptions));
            }
        }
    }

    private void SyncMaster()
    {
        _syncing = true;
        try
        {
            MasterMuted = _service.MasterMuted;
            MasterPosition = VolumeCurve.ToPosition(_service.MasterVolume);
        }
        finally
        {
            _syncing = false;
        }
    }

    /// <summary>
    /// Matches <see cref="OutputDevices"/> to the current device list by id, the
    /// same survivors-in-place approach <see cref="Reconcile"/> uses for
    /// sessions -- replacing the collection outright would blow away an open
    /// dropdown mid-click every ~100ms.
    /// </summary>
    private void SyncOutputDevices()
    {
        _syncingDevice = true;
        try
        {
            var devices = _service.ListOutputDevices();

            var incoming = new Dictionary<string, AudioDeviceInfo>(devices.Count);
            foreach (var device in devices)
            {
                incoming[device.Id] = device;
            }

            for (var i = OutputDevices.Count - 1; i >= 0; i--)
            {
                if (!incoming.ContainsKey(OutputDevices[i].Id!))
                {
                    var stale = OutputDevices[i];
                    OutputDevices.RemoveAt(i);
                    AppOutputOptions.Remove(stale);
                }
            }

            var present = new Dictionary<string, OutputDeviceViewModel>(OutputDevices.Count);
            foreach (var row in OutputDevices)
            {
                present[row.Id!] = row;
            }

            foreach (var device in devices)
            {
                if (present.TryGetValue(device.Id, out var row))
                {
                    row.IsDefault = device.IsDefault;
                }
                else
                {
                    var added = new OutputDeviceViewModel(device.Id, device.FriendlyName) { IsDefault = device.IsDefault };
                    OutputDevices.Add(added);
                    AppOutputOptions.Add(added);
                }
            }

            var currentDefault = OutputDevices.FirstOrDefault(d => d.IsDefault);
            if (!ReferenceEquals(SelectedOutputDevice, currentDefault))
            {
                SelectedOutputDevice = currentDefault;
            }
        }
        finally
        {
            _syncingDevice = false;
        }
    }

    /// <summary>
    /// Matches <see cref="InputDevices"/> to the current capture-device list by id,
    /// the same survivors-in-place approach <see cref="SyncOutputDevices"/> uses.
    /// </summary>
    private void SyncInputDevices()
    {
        _syncingInputDevice = true;
        try
        {
            var devices = _service.ListInputDevices();

            var incoming = new Dictionary<string, AudioDeviceInfo>(devices.Count);
            foreach (var device in devices)
            {
                incoming[device.Id] = device;
            }

            for (var i = InputDevices.Count - 1; i >= 0; i--)
            {
                if (!incoming.ContainsKey(InputDevices[i].Id))
                {
                    InputDevices.RemoveAt(i);
                }
            }

            var present = new Dictionary<string, InputDeviceViewModel>(InputDevices.Count);
            foreach (var row in InputDevices)
            {
                present[row.Id] = row;
            }

            foreach (var device in devices)
            {
                if (present.TryGetValue(device.Id, out var row))
                {
                    row.IsDefault = device.IsDefault;
                }
                else
                {
                    InputDevices.Add(new InputDeviceViewModel(device.Id, device.FriendlyName) { IsDefault = device.IsDefault });
                }
            }

            var currentDefault = InputDevices.FirstOrDefault(d => d.IsDefault);
            if (!ReferenceEquals(SelectedInputDevice, currentDefault))
            {
                SelectedInputDevice = currentDefault;
            }
        }
        finally
        {
            _syncingInputDevice = false;
        }
    }

    private static void Guarded(Action action)
    {
        try
        {
            action();
        }
        catch (COMException)
        {
            // Endpoint vanished mid-gesture; the next poll reports the real state.
        }
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        _timer.Stop();
        Sessions.Clear();
        _service.Dispose();
    }
}
