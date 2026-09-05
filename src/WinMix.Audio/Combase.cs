using System.Runtime.InteropServices;

namespace WinMix.Audio;

/// <summary>
/// The WinRT activation primitives needed to reach
/// <c>Windows.Media.Internal.AudioPolicyConfig</c> (see
/// <see cref="AudioPolicyConfigFactory"/>).
///
/// Declared with classic <c>DllImport</c> rather than this codebase's usual
/// <c>LibraryImport</c>: WinRT activation needs marshalling the source-generated
/// marshaller does not support at all on .NET (Core) -- see the remarks on
/// <see cref="RoGetActivationFactory"/>. <c>PreserveSig = false</c> is deliberate
/// too, on the calls where it appears -- it makes a failing HRESULT surface as a
/// normal .NET exception instead of a silently wrong out-parameter.
///
/// <see cref="RoGetActivationFactory"/> takes the class id as a raw HSTRING
/// handle rather than <c>[MarshalAs(UnmanagedType.HString)] string</c>: that
/// attribute marshals fine on .NET Framework, but .NET (Core) throws
/// <see cref="MarshalDirectiveException"/> for it on a classic DllImport --
/// HString auto-marshalling only works there through CsWinRT-generated
/// projections, which this hand-rolled interop does not use. Every caller must
/// build the HSTRING itself via <see cref="WindowsCreateString"/> first.
///
/// The activated factory itself comes back as a raw <c>nint</c>, not
/// <c>[MarshalAs(UnmanagedType.IInspectable)] out object</c>: .NET (Core) throws
/// "Marshalling as IInspectable is not supported in the .NET runtime" for that,
/// unconditionally, on any target framework. <see cref="AudioPolicyConfigFactory"/>
/// reads the returned object's vtable directly instead -- the same workaround
/// SoundSwitch (github.com/Belphemur/SoundSwitch) uses for this identical problem.
/// </summary>
internal static class Combase
{
    [DllImport("combase.dll", PreserveSig = false)]
    public static extern void RoGetActivationFactory(
        nint activatableClassId,
        [In] ref Guid iid,
        out nint factory);

    [DllImport("combase.dll", PreserveSig = false)]
    public static extern void WindowsCreateString(
        [MarshalAs(UnmanagedType.LPWStr)] string sourceString,
        [In] uint length,
        [Out] out nint hstring);

    [DllImport("combase.dll", PreserveSig = false)]
    public static extern void WindowsDeleteString([In] nint hstring);
}
