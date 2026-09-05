namespace WinMix.App.ViewModels;

/// <summary>
/// One entry in the input-device picker: a capture device (microphone). Unlike
/// <see cref="OutputDeviceViewModel"/> there is no "Default" sentinel -- input
/// switching is system-wide only, with no per-app override.
/// </summary>
public sealed class InputDeviceViewModel : ObservableObject
{
    private bool _isDefault;

    public InputDeviceViewModel(string id, string friendlyName)
    {
        Id = id;
        FriendlyName = friendlyName;
    }

    public string Id { get; }

    public string FriendlyName { get; }

    /// <summary>True when this is the system's current default capture device.</summary>
    public bool IsDefault
    {
        get => _isDefault;
        set => SetField(ref _isDefault, value);
    }
}
