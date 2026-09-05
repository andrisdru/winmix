using System.Runtime.InteropServices;

namespace WinMix.Audio;

/// <summary>
/// Turns the display name an audio session reports about itself into something
/// worth showing a user.
///
/// Most applications leave the field empty and are better identified by their
/// executable, but Windows' own sessions populate it with an indirect resource
/// reference such as <c>@%SystemRoot%\System32\AudioSrv.Dll,-202</c>. Left
/// unresolved those render as raw gibberish, which is why naive mixers show rows
/// labelled "svchost" where the Windows mixer shows a real name.
/// </summary>
internal static partial class SessionNaming
{
    /// <summary>
    /// Returns a display-worthy session name, or null when the session offers
    /// nothing useful and the caller should fall back to the process identity.
    /// </summary>
    public static string? Resolve(string? reportedName)
    {
        if (string.IsNullOrWhiteSpace(reportedName))
        {
            return null;
        }

        var trimmed = reportedName.Trim();

        // Only strings beginning with '@' are indirect references; everything
        // else is already literal text the app chose for itself.
        if (!trimmed.StartsWith('@'))
        {
            return trimmed;
        }

        var expanded = LoadIndirectString(trimmed);
        return string.IsNullOrWhiteSpace(expanded) ? null : expanded.Trim();
    }

    private static string? LoadIndirectString(string source)
    {
        try
        {
            unsafe
            {
                const int capacity = 512;
                var buffer = stackalloc char[capacity];

                // Returns a failure HRESULT when the module or resource id cannot
                // be found, which happens routinely for third-party sessions.
                if (SHLoadIndirectString(source, buffer, capacity, IntPtr.Zero) != 0)
                {
                    return null;
                }

                return new string(buffer);
            }
        }
        catch (DllNotFoundException)
        {
            return null;
        }
    }

    [LibraryImport("shlwapi.dll", StringMarshalling = StringMarshalling.Utf16)]
    private static unsafe partial int SHLoadIndirectString(
        string pszSource,
        char* pszOutBuf,
        uint cchOutBuf,
        IntPtr ppvReserved);
}
