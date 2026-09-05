namespace WinMix.Audio;

/// <summary>One active endpoint (render or capture), for a device picker.</summary>
public sealed record AudioDeviceInfo(string Id, string FriendlyName, bool IsDefault);
