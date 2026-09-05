using System.Runtime.InteropServices;
using NAudio.CoreAudioApi;

namespace WinMix.Audio;

/// <summary>
/// Calls the undocumented WinRT class <c>Windows.Media.Internal.AudioPolicyConfig</c>
/// -- the same mechanism behind Settings > System > Sound > "App volume and device
/// preferences" -- via hand-rolled vtable dispatch rather than <c>[ComImport]</c>
/// interfaces.
///
/// That is not a style choice: .NET (Core) has no support for marshalling
/// IInspectable-based WinRT objects through interop attributes at all (only .NET
/// Framework, or the CsWinRT source generator, can do that -- see the remarks on
/// <see cref="Combase.RoGetActivationFactory"/>). Reading the object's vtable and
/// invoking a slot by index sidesteps that entirely, and is the same technique
/// SoundSwitch (github.com/Belphemur/SoundSwitch) ships for this identical
/// problem; the slot numbers below are cross-checked against its implementation
/// rather than guessed.
///
/// The real method sits at the same vtable slot on every Windows version
/// SoundSwitch supports, including both known shapes of this class: 3 IUnknown
/// slots (QueryInterface/AddRef/Release), 3 IInspectable slots
/// (GetIids/GetRuntimeClassName/GetTrustLevel), then 19 undocumented slots for
/// features this app has no reason to call, before
/// <see cref="SetPersistedDefaultAudioEndpoint"/> (25). Windows 11's newer shape
/// only ever appends further slots after that, under a different class IID (see
/// <see cref="ClassIid"/>) -- it does not renumber it.
/// </summary>
internal static class AudioPolicyConfigFactory
{
    private const string ClassId = "Windows.Media.Internal.AudioPolicyConfig";

    private const int SetPersistedDefaultAudioEndpointSlot = 25;

    // Below this build, Windows exposes the pre-21H2 shape of this class under a
    // different IID; the vtable slots used above are identical either way. 21390
    // was an Insider build during the Windows 10 21H2 development cycle; nothing
    // shipped at exactly that number, but every released Windows 10 21H2 (19044)
    // and every Windows 11 build (22000+) compares correctly against it.
    //
    // This depends on WinMix.App's app.manifest declaring a Windows 10
    // <supportedOS> entry: without one, Windows' compatibility shim makes
    // Environment.OSVersion report an old, wrong build to any app that doesn't
    // explicitly claim support, which would pick the wrong IID below.
    private const int NewShapeMinBuild = 21390;

    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    private delegate int SetPersistedDefaultAudioEndpointFn(nint self, uint processId, DataFlow flow, Role role, nint deviceId);

    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    private delegate uint ReleaseFn(nint self);

    public static int SetPersistedDefaultAudioEndpoint(uint pid, DataFlow flow, Role role, nint deviceId)
    {
        var factory = Activate();
        try
        {
            var setFn = VtableMethod<SetPersistedDefaultAudioEndpointFn>(factory, SetPersistedDefaultAudioEndpointSlot);
            return setFn(factory, pid, flow, role, deviceId);
        }
        finally
        {
            Release(factory);
        }
    }

    /// <summary>Activates the class, requesting its interface directly so no separate QueryInterface is needed.</summary>
    private static nint Activate()
    {
        var iid = ClassIid;
        Combase.WindowsCreateString(ClassId, (uint)ClassId.Length, out var classId);
        try
        {
            Combase.RoGetActivationFactory(classId, ref iid, out var factory);
            return factory;
        }
        finally
        {
            Combase.WindowsDeleteString(classId);
        }
    }

    private static Guid ClassIid => Environment.OSVersion.Version.Build >= NewShapeMinBuild
        ? new Guid("AB3D4648-E242-459F-B02F-541C70306324")
        : new Guid("2A59116D-6C4F-45E0-A74F-707E3FEF9258");

    private static T VtableMethod<T>(nint self, int slot) where T : Delegate
    {
        var vtable = Marshal.ReadIntPtr(self);
        var slotPtr = Marshal.ReadIntPtr(vtable, slot * nint.Size);
        return Marshal.GetDelegateForFunctionPointer<T>(slotPtr);
    }

    private static void Release(nint self)
    {
        var vtable = Marshal.ReadIntPtr(self);
        var releaseSlotPtr = Marshal.ReadIntPtr(vtable, 2 * nint.Size);
        Marshal.GetDelegateForFunctionPointer<ReleaseFn>(releaseSlotPtr)(self);
    }
}
