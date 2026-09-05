using System.Collections.ObjectModel;
using System.Windows.Media;
using WinMix.Audio;

namespace WinMix.App.ViewModels;

/// <summary>One row in the mixer: an application, its slider, its mute button.</summary>
public sealed class SessionViewModel : ObservableObject
{
    /// <summary>
    /// How far the device's reported scalar may drift from what our slider implies
    /// before we believe the change came from outside WinMix. WASAPI quantises the
    /// value it stores, so a strict comparison would never match and every refresh
    /// would yank the slider out from under the user's cursor mid-drag.
    /// </summary>
    private const float ScalarEpsilon = 0.01f;

    private readonly AudioSessionService _service;

    private bool _syncing;
    private double _sliderPosition;
    private bool _isMuted;
    private double _peakLevel;
    private bool _isActive;
    private string _displayName = string.Empty;
    private ImageSource? _icon;
    private OutputDeviceViewModel? _selectedOutputOption;

    public SessionViewModel(
        AudioSessionService service,
        AudioSessionSnapshot snapshot,
        ObservableCollection<OutputDeviceViewModel> outputOptions)
    {
        _service = service;
        InstanceId = snapshot.InstanceId;
        Pid = snapshot.Pid;
        OutputOptions = outputOptions;
        _selectedOutputOption = outputOptions.Count > 0 ? outputOptions[0] : null;
        Sync(snapshot);
    }

    /// <summary>Stable per-session key used to match rows across refreshes.</summary>
    public string InstanceId { get; }

    /// <summary>Owning process id, for routing this app's output to a specific device.</summary>
    public uint Pid { get; }

    /// <summary>
    /// The "Default" sentinel followed by every active render device, shared
    /// across every row so they all offer the same choices.
    /// </summary>
    public ObservableCollection<OutputDeviceViewModel> OutputOptions { get; }

    /// <summary>
    /// This app's pinned output device, or the "Default" sentinel (<see cref="OutputDeviceViewModel.Id"/>
    /// null) to follow the system default. Never read back from Windows -- see
    /// <see cref="WinMix.Audio.AudioSessionService.SetAppOutputDevice"/>.
    /// </summary>
    public OutputDeviceViewModel? SelectedOutputOption
    {
        get => _selectedOutputOption;
        set
        {
            if (SetField(ref _selectedOutputOption, value))
            {
                _service.SetAppOutputDevice(Pid, value?.Id);
            }
        }
    }

    public string DisplayName
    {
        get => _displayName;
        private set => SetField(ref _displayName, value);
    }

    public ImageSource? Icon
    {
        get => _icon;
        private set => SetField(ref _icon, value);
    }

    /// <summary>True while the app is actually producing audio.</summary>
    public bool IsActive
    {
        get => _isActive;
        private set => SetField(ref _isActive, value);
    }

    /// <summary>Current output level in 0..1, for the meter.</summary>
    public double PeakLevel
    {
        get => _peakLevel;
        private set => SetField(ref _peakLevel, value);
    }

    /// <summary>
    /// Slider travel in 0..1, perceptually tapered. Writing this pushes straight
    /// through to WASAPI unless we are the ones echoing the device's own state back.
    /// </summary>
    public double SliderPosition
    {
        get => _sliderPosition;
        set
        {
            if (!SetField(ref _sliderPosition, value))
            {
                return;
            }

            OnPropertyChanged(nameof(VolumePercent));

            if (!_syncing)
            {
                _service.SetVolume(InstanceId, VolumeCurve.ToScalar(value));
            }
        }
    }

    /// <summary>Slider travel as a whole percentage, for the row's numeric label.</summary>
    public int VolumePercent => (int)Math.Round(_sliderPosition * 100.0);

    public bool IsMuted
    {
        get => _isMuted;
        set
        {
            if (SetField(ref _isMuted, value) && !_syncing)
            {
                _service.SetMute(InstanceId, value);
            }
        }
    }

    /// <summary>Folds a fresh device reading into this row without echoing it back to WASAPI.</summary>
    public void Sync(AudioSessionSnapshot snapshot)
    {
        _syncing = true;
        try
        {
            DisplayName = snapshot.DisplayName;
            IsActive = snapshot.IsActive;
            PeakLevel = snapshot.PeakLevel;
            IsMuted = snapshot.IsMuted;
            Icon ??= IconLoader.ForExecutable(snapshot.ExecutablePath, snapshot.IsSystemSounds);

            // Only adopt the device's scalar when it genuinely disagrees with us,
            // so an in-flight drag is not fought by our own polling.
            if (Math.Abs(VolumeCurve.ToScalar(_sliderPosition) - snapshot.Volume) > ScalarEpsilon)
            {
                SliderPosition = VolumeCurve.ToPosition(snapshot.Volume);
            }
        }
        finally
        {
            _syncing = false;
        }
    }
}
