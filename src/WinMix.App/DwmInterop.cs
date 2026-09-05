using System.Runtime.InteropServices;

namespace WinMix.App;

/// <summary>
/// Talks the DWM into painting the native title bar dark instead of the default
/// light chrome, so it matches the rest of the (dark-themed) window.
/// </summary>
internal static partial class DwmInterop
{
    private const int DwmwaUseImmersiveDarkMode = 20;

    /// <summary>
    /// Best-effort: builds of Windows 10 older than 2004 silently ignore the
    /// attribute rather than failing, so there is nothing to fall back to.
    /// </summary>
    public static unsafe void EnableDarkTitleBar(nint hwnd)
    {
        int enabled = 1;
        DwmSetWindowAttribute(hwnd, DwmwaUseImmersiveDarkMode, &enabled, sizeof(int));
    }

    // Declared with a raw pointer rather than ref int: the source-generated
    // marshaller refuses by-ref primitives unless the whole assembly opts out of
    // runtime marshalling, and a pointer is already blittable.
    [LibraryImport("dwmapi.dll")]
    private static unsafe partial int DwmSetWindowAttribute(nint hwnd, int dwAttribute, int* pvAttribute, int cbAttribute);
}
