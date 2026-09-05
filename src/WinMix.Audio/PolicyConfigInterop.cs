using System.Runtime.InteropServices;
using NAudio.CoreAudioApi;

namespace WinMix.Audio;

/// <summary>
/// Undocumented but stable since Windows 7: Core Audio has no public API to change
/// which render device is the system default, so Windows' own Settings app drives
/// this same COM object internally.
///
/// The leading placeholder methods stand in for real ones this app never calls --
/// removing them would shift <see cref="SetDefaultEndpoint"/> to the wrong vtable
/// slot and silently invoke something else instead.
/// </summary>
[ComImport]
[Guid("F8679F50-850A-41CF-9C72-430F290290C8")]
[InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
internal interface IPolicyConfig
{
    void Unused1(); // GetMixFormat
    void Unused2(); // GetDeviceFormat
    void Unused3(); // SetDeviceFormat
    void Unused4(); // GetProcessingPeriod
    void Unused5(); // SetProcessingPeriod
    void Unused6(); // GetShareMode
    void Unused7(); // SetShareMode
    void Unused8(); // GetPropertyValue
    void Unused9(); // SetPropertyValue

    void SetDefaultEndpoint([MarshalAs(UnmanagedType.LPWStr)] string deviceId, Role role);

    void Unused10(); // SetEndpointVisibility
}

[ComImport]
[Guid("870AF99C-171D-4F9E-AF0D-E63DF40C2BC9")]
internal class PolicyConfigClient
{
}

/// <summary>Switches the system's default render device via <see cref="IPolicyConfig"/>.</summary>
internal static class DefaultEndpointSwitcher
{
    /// <summary>
    /// Sets <paramref name="deviceId"/> as the default for every role. Windows'
    /// own device switcher does the same -- otherwise games and notification
    /// sounds (Console), music (Multimedia), and calls (Communications) could
    /// each keep pointing at a different, now-stale device.
    /// </summary>
    public static void SetDefault(string deviceId)
    {
        var policyConfig = (IPolicyConfig)new PolicyConfigClient();
        policyConfig.SetDefaultEndpoint(deviceId, Role.Console);
        policyConfig.SetDefaultEndpoint(deviceId, Role.Multimedia);
        policyConfig.SetDefaultEndpoint(deviceId, Role.Communications);
    }
}
