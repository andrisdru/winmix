using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;

namespace WinMix.App;

/// <summary>
/// Blocks the user from resizing a window's width by dragging its left/right
/// edges or corners, while leaving height freely resizable via the top/bottom
/// edges.
///
/// WPF has no <c>ResizeMode</c> for this: <c>SizeToContent="Width"</c> only
/// controls the window's *automatic* width -- it does nothing to stop a manual
/// drag from overriding it. Remapping <c>WM_NCHITTEST</c>'s horizontal-resize
/// hit codes to <c>HTBORDER</c> (present, but not interactive) stops both the
/// drag itself and the resize-cursor affordance that would otherwise
/// misleadingly appear there.
/// </summary>
internal static partial class WindowResize
{
    private const int WmNcHitTest = 0x0084;

    private const int HtLeft = 10;
    private const int HtRight = 11;
    private const int HtTopLeft = 13;
    private const int HtTopRight = 14;
    private const int HtBottomLeft = 16;
    private const int HtBottomRight = 17;
    private const int HtBorder = 18;

    public static void LockWidth(Window window)
    {
        var source = (HwndSource)PresentationSource.FromVisual(window)!;
        source.AddHook(Hook);
    }

    private static nint Hook(nint hwnd, int msg, nint wParam, nint lParam, ref bool handled)
    {
        if (msg != WmNcHitTest)
        {
            return nint.Zero;
        }

        var result = (int)DefWindowProc(hwnd, msg, wParam, lParam);
        if (result is HtLeft or HtRight or HtTopLeft or HtTopRight or HtBottomLeft or HtBottomRight)
        {
            handled = true;
            return HtBorder;
        }

        return nint.Zero;
    }

    [LibraryImport("user32.dll", EntryPoint = "DefWindowProcW")]
    private static partial nint DefWindowProc(nint hWnd, int msg, nint wParam, nint lParam);
}
