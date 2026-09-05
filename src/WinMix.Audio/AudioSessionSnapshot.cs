using NAudio.CoreAudioApi;
using NAudio.CoreAudioApi.Interfaces;

namespace WinMix.Audio;

/// <summary>
/// An immutable view of one application's audio session at a point in time.
///
/// The UI binds to these rather than to live <see cref="AudioSessionControl"/>
/// objects: those wrap COM pointers with a lifetime tied to a refresh cycle, and
/// letting XAML data-binding reach into them invites use-after-dispose.
/// </summary>
/// <param name="InstanceId">
/// Unique per session instance. Two windows of the same app share a
/// <c>SessionIdentifier</c> but get distinct instance ids, so this is what we key on.
/// </param>
/// <param name="Volume">WASAPI amplitude scalar in 0..1, not a slider position.</param>
public sealed record AudioSessionSnapshot(
    string InstanceId,
    uint Pid,
    string DisplayName,
    string? ExecutablePath,
    float Volume,
    bool IsMuted,
    float PeakLevel,
    bool IsSystemSounds,
    AudioSessionState State)
{
    public bool IsActive => State == AudioSessionState.AudioSessionStateActive;
}
