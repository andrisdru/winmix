using System.Drawing;
using System.IO;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace WinMix.App;

/// <summary>
/// Pulls application icons off disk for the mixer rows, caching by path.
/// Every result is frozen so the icons cost nothing to re-render and are safe to
/// hand to any thread.
/// </summary>
internal static class IconLoader
{
    private static readonly Dictionary<string, ImageSource?> Cache = new(StringComparer.OrdinalIgnoreCase);
    private static ImageSource? _systemSoundsIcon;

    public static ImageSource? ForExecutable(string? executablePath, bool isSystemSounds)
    {
        if (isSystemSounds)
        {
            return _systemSoundsIcon ??= FromIcon(SystemIcons.Information);
        }

        if (string.IsNullOrWhiteSpace(executablePath))
        {
            return null;
        }

        if (Cache.TryGetValue(executablePath, out var cached))
        {
            return cached;
        }

        var loaded = Load(executablePath);
        Cache[executablePath] = loaded;
        return loaded;
    }

    private static ImageSource? Load(string executablePath)
    {
        try
        {
            using var icon = Icon.ExtractAssociatedIcon(executablePath);
            return icon is null ? null : FromIcon(icon);
        }
        catch (Exception ex) when (ex is IOException or ArgumentException or UnauthorizedAccessException)
        {
            // Unreadable image (deleted, or a path we cannot open). A missing icon
            // is cosmetic, so degrade rather than surface it.
            return null;
        }
    }

    private static ImageSource? FromIcon(Icon icon)
    {
        try
        {
            var source = Imaging.CreateBitmapSourceFromHIcon(
                icon.Handle, Int32Rect.Empty, BitmapSizeOptions.FromEmptyOptions());
            source.Freeze();
            return source;
        }
        catch (Exception ex) when (ex is System.ComponentModel.Win32Exception or ArgumentException)
        {
            return null;
        }
    }
}
