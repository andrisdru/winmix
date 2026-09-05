using NAudio.CoreAudioApi;
using NAudio.CoreAudioApi.Interfaces;

namespace WinMix.Audio;

/// <summary>
/// Reads and controls per-application volume on the default render device via WASAPI.
///
/// Threading: every member must be called from a single thread, and for the WPF
/// shell that is the UI thread. This is deliberate for the volume-mixer phase --
/// the Core Audio objects are apartment-bound and the work is a few COM calls per
/// refresh, so a dedicated thread would buy nothing but marshalling bugs. The
/// equalizer phase is different and will need its own real-time thread; see
/// <c>Loopback/</c>.
///
/// Discovery is poll-based rather than event-based. WASAPI does offer
/// <c>IAudioSessionNotification</c>, but its callbacks arrive on an MTA thread and
/// re-entering the session manager from inside one deadlocks. Polling at a couple
/// of hertz sidesteps that entirely and is imperceptible for a mixer.
/// </summary>
public sealed class AudioSessionService : IDisposable
{
    private readonly MMDeviceEnumerator _enumerator = new();
    private readonly ProcessInfoCache _names = new();
    private readonly Dictionary<string, AudioSessionControl> _controls = new();

    // MMDevice.FriendlyName is a fresh IPropertyStore COM round trip every
    // access -- measured at 35-50ms per device on this machine, regardless of
    // device -- while .ID is effectively free. A device's name does not change
    // while it stays plugged in, so caching it by id turns every poll after the
    // first into a handful of cheap calls instead of one slow one per device.
    // No trim needed the way ProcessInfoCache needs one: unlike pids, device
    // ids don't cycle, so this cannot grow unbounded in practice.
    private readonly Dictionary<string, string> _deviceNameCache = new();

    private MMDevice? _device;
    private bool _disposed;

    /// <summary>Device-wide output level as an amplitude scalar in 0..1.</summary>
    public float MasterVolume
    {
        get => Device().AudioEndpointVolume.MasterVolumeLevelScalar;
        set => Device().AudioEndpointVolume.MasterVolumeLevelScalar = Math.Clamp(value, 0f, 1f);
    }

    public bool MasterMuted
    {
        get => Device().AudioEndpointVolume.Mute;
        set => Device().AudioEndpointVolume.Mute = value;
    }

    /// <summary>
    /// Re-enumerates sessions and returns a fresh snapshot of each one worth showing.
    /// Invalidates any <see cref="AudioSessionSnapshot.InstanceId"/> not present in
    /// the result -- callers must not cache ids across refreshes.
    /// </summary>
    public IReadOnlyList<AudioSessionSnapshot> Refresh()
    {
        ObjectDisposedException.ThrowIf(_disposed, this);

        var manager = Device().AudioSessionManager;
        manager.RefreshSessions();
        var sessions = manager.Sessions;

        // NAudio hands out a new wrapper (and a new COM reference) per indexer
        // access, so the previous batch has to be released or we leak steadily.
        ReleaseControls();

        var snapshots = new List<AudioSessionSnapshot>(sessions.Count);
        var livePids = new HashSet<uint>();

        for (var i = 0; i < sessions.Count; i++)
        {
            var control = sessions[i];

            if (control.State == AudioSessionState.AudioSessionStateExpired)
            {
                control.Dispose();
                continue;
            }

            var instanceId = control.GetSessionInstanceIdentifier;
            if (string.IsNullOrEmpty(instanceId) || _controls.ContainsKey(instanceId))
            {
                control.Dispose();
                continue;
            }

            _controls[instanceId] = control;

            var pid = control.GetProcessID;
            livePids.Add(pid);

            var info = _names.Get(pid);
            snapshots.Add(new AudioSessionSnapshot(
                InstanceId: instanceId,
                Pid: pid,
                DisplayName: ChooseDisplayName(control, info),
                ExecutablePath: info.ExecutablePath,
                Volume: control.SimpleAudioVolume.Volume,
                IsMuted: control.SimpleAudioVolume.Mute,
                PeakLevel: ReadPeak(control),
                IsSystemSounds: control.IsSystemSoundsSession,
                State: control.State));
        }

        _names.Trim(livePids);

        return snapshots
            .OrderByDescending(s => s.IsActive)
            .ThenBy(s => s.DisplayName, StringComparer.CurrentCultureIgnoreCase)
            .ToList();
    }

    /// <summary>Sets one session's amplitude scalar. No-op if the session has gone away.</summary>
    public void SetVolume(string instanceId, float scalar)
    {
        if (TryGetControl(instanceId, out var control))
        {
            Guarded(() => control.SimpleAudioVolume.Volume = Math.Clamp(scalar, 0f, 1f));
        }
    }

    /// <summary>Mutes or unmutes one session. No-op if the session has gone away.</summary>
    public void SetMute(string instanceId, bool muted)
    {
        if (TryGetControl(instanceId, out var control))
        {
            Guarded(() => control.SimpleAudioVolume.Mute = muted);
        }
    }

    /// <summary>Active render endpoints, for the output-device picker.</summary>
    public IReadOnlyList<AudioDeviceInfo> ListOutputDevices()
    {
        ObjectDisposedException.ThrowIf(_disposed, this);

        var defaultId = Device().ID;
        var collection = _enumerator.EnumerateAudioEndPoints(DataFlow.Render, DeviceState.Active);

        var devices = new List<AudioDeviceInfo>(collection.Count);
        foreach (var device in collection)
        {
            devices.Add(new AudioDeviceInfo(device.ID, GetCachedFriendlyName(device), device.ID == defaultId));
            device.Dispose();
        }

        return devices;
    }

    /// <summary>
    /// Makes <paramref name="deviceId"/> the system default render device, for
    /// every role, so both this mixer and every other app follow it.
    /// </summary>
    public void SetDefaultOutputDevice(string deviceId)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        Guarded(() => DefaultEndpointSwitcher.SetDefault(deviceId));

        // The device we already resolved is still Active, just no longer
        // default -- drop it so the next Device() call re-resolves to the new
        // default instead of quietly continuing to mix the old one.
        ReleaseControls();
        _device?.Dispose();
        _device = null;
    }

    /// <summary>
    /// Pins one app's output to <paramref name="deviceId"/>, or with null clears
    /// the pin so it follows the system default again.
    /// </summary>
    public void SetAppOutputDevice(uint pid, string? deviceId) =>
        Guarded(() => AppOutputRouter.Set(pid, deviceId));

    /// <summary>Active capture endpoints (microphones), for the input-device picker.</summary>
    public IReadOnlyList<AudioDeviceInfo> ListInputDevices()
    {
        ObjectDisposedException.ThrowIf(_disposed, this);

        string? defaultId = null;
        try
        {
            using var defaultDevice = _enumerator.GetDefaultAudioEndpoint(DataFlow.Capture, Role.Multimedia);
            defaultId = defaultDevice.ID;
        }
        catch (System.Runtime.InteropServices.COMException)
        {
            // No active capture device at all; every row below reports IsDefault = false.
        }

        var collection = _enumerator.EnumerateAudioEndPoints(DataFlow.Capture, DeviceState.Active);

        var devices = new List<AudioDeviceInfo>(collection.Count);
        foreach (var device in collection)
        {
            devices.Add(new AudioDeviceInfo(device.ID, GetCachedFriendlyName(device), device.ID == defaultId));
            device.Dispose();
        }

        return devices;
    }

    /// <summary>
    /// Makes <paramref name="deviceId"/> the system default capture device, for
    /// every role -- the same mechanism as <see cref="SetDefaultOutputDevice"/>,
    /// just pointed at a microphone instead of a speaker. Unlike the render side,
    /// no session state depends on which capture device is default, so there is
    /// nothing to re-resolve afterwards.
    /// </summary>
    public void SetDefaultInputDevice(string deviceId) =>
        Guarded(() => DefaultEndpointSwitcher.SetDefault(deviceId));

    /// <summary>
    /// Picks the label for a row, mirroring how the Windows mixer decides: the
    /// session's own name wins when it has one, otherwise the owning executable.
    /// </summary>
    private static string ChooseDisplayName(AudioSessionControl control, ProcessInfo info)
    {
        if (control.IsSystemSoundsSession)
        {
            return "System sounds";
        }

        return SessionNaming.Resolve(TryReadDisplayName(control)) ?? info.FriendlyName;
    }

    private static string? TryReadDisplayName(AudioSessionControl control)
    {
        try
        {
            return control.DisplayName;
        }
        catch (Exception ex) when (IsSessionGone(ex))
        {
            return null;
        }
    }

    private string GetCachedFriendlyName(MMDevice device)
    {
        if (_deviceNameCache.TryGetValue(device.ID, out var cached))
        {
            return cached;
        }

        var name = device.FriendlyName;
        _deviceNameCache[device.ID] = name;
        return name;
    }

    private bool TryGetControl(string instanceId, out AudioSessionControl control)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        return _controls.TryGetValue(instanceId, out control!);
    }

    private static float ReadPeak(AudioSessionControl control)
    {
        try
        {
            return control.AudioMeterInformation.MasterPeakValue;
        }
        catch (Exception ex) when (IsSessionGone(ex))
        {
            return 0f;
        }
    }

    /// <summary>
    /// Runs a Core Audio mutation, swallowing the failure that happens when the
    /// target process exits between our refresh and the user moving its slider.
    /// </summary>
    private static void Guarded(Action action)
    {
        try
        {
            action();
        }
        catch (Exception ex) when (IsSessionGone(ex))
        {
            // The session died underneath us; the next refresh drops the row.
        }
    }

    private static bool IsSessionGone(Exception ex) =>
        ex is System.Runtime.InteropServices.COMException
            or ObjectDisposedException
            or InvalidCastException;

    private MMDevice Device()
    {
        ObjectDisposedException.ThrowIf(_disposed, this);

        // Re-resolve when the default endpoint changes under us (headphones
        // plugged in, device disabled); the old MMDevice keeps returning stale
        // sessions rather than failing loudly.
        if (_device is not null && IsUsable(_device))
        {
            return _device;
        }

        ReleaseControls();
        _device?.Dispose();
        _device = _enumerator.GetDefaultAudioEndpoint(DataFlow.Render, Role.Multimedia);
        return _device;
    }

    private static bool IsUsable(MMDevice device)
    {
        try
        {
            return device.State == DeviceState.Active;
        }
        catch (Exception ex) when (IsSessionGone(ex))
        {
            return false;
        }
    }

    private void ReleaseControls()
    {
        foreach (var control in _controls.Values)
        {
            try
            {
                control.Dispose();
            }
            catch (Exception ex) when (IsSessionGone(ex))
            {
                // Already gone.
            }
        }

        _controls.Clear();
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        ReleaseControls();
        _device?.Dispose();
        _enumerator.Dispose();
    }
}
