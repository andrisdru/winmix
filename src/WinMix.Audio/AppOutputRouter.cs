using NAudio.CoreAudioApi;

namespace WinMix.Audio;

/// <summary>
/// Pins one process's playback to a specific render device (or clears the pin,
/// letting it follow the system default again) via
/// <see cref="AudioPolicyConfigFactory"/>. This is the same undocumented
/// mechanism behind Settings > System > Sound > "App volume and device
/// preferences".
/// </summary>
internal static class AppOutputRouter
{
    // IMMDevice.Id (what MMDevice.ID returns) is only the middle of the device
    // interface path this API actually expects; the prefix and the render
    // interface class suffix have to be glued back on.
    private const string DeviceInterfaceToken = @"\\?\SWD#MMDEVAPI#";
    private const string RenderInterfaceSuffix = "#{e6327cad-dcec-4949-ae8a-991e976a79d2}";

    public static void Set(uint pid, string? deviceId)
    {
        nint hstring = 0;
        try
        {
            if (!string.IsNullOrEmpty(deviceId))
            {
                var packed = DeviceInterfaceToken + deviceId + RenderInterfaceSuffix;
                Combase.WindowsCreateString(packed, (uint)packed.Length, out hstring);
            }

            // Console and Multimedia are the two roles an app's own render
            // stream can resolve through; Communications is left alone since a
            // per-app override has no bearing on a separate voice-call device.
            AudioPolicyConfigFactory.SetPersistedDefaultAudioEndpoint(pid, DataFlow.Render, Role.Console, hstring);
            AudioPolicyConfigFactory.SetPersistedDefaultAudioEndpoint(pid, DataFlow.Render, Role.Multimedia, hstring);
        }
        finally
        {
            if (hstring != 0)
            {
                Combase.WindowsDeleteString(hstring);
            }
        }
    }
}
