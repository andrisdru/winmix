namespace WinMix.App.ViewModels;

/// <summary>
/// One entry in an output-device picker: either a real playback device, or (when
/// <see cref="Id"/> is null) the "Default" sentinel a per-app picker uses to mean
/// "no override, follow the system default".
/// </summary>
public sealed class OutputDeviceViewModel : ObservableObject
{
    private bool _isDefault;

    public OutputDeviceViewModel(string? id, string friendlyName)
    {
        Id = id;
        FriendlyName = friendlyName;
    }

    public string? Id { get; }

    public string FriendlyName { get; }

    /// <summary>True when this is the system's current default render device.</summary>
    public bool IsDefault
    {
        get => _isDefault;
        set => SetField(ref _isDefault, value);
    }
}
