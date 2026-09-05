# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

WinMix is a Windows tray utility for **per-application** audio control: a volume
mixer today, a per-application equalizer next. C# on `net8.0-windows`, WPF shell,
WASAPI underneath via NAudio.

## Commands

```powershell
dotnet build WinMix.sln
dotnet run --project src/WinMix.App     # launches the tray app
dotnet test WinMix.sln

# One test or one class
dotnet test --filter "FullyQualifiedName~VolumeCurveTests.CurveIsMonotonic"
dotnet test --filter "FullyQualifiedName~SessionNamingTests"
```

Both projects build with `TreatWarningsAsErrors`, so a warning fails the build.

**A running instance silently swallows the next launch.** `App` holds a
single-instance mutex, so a second copy calls `Shutdown()` in `OnStartup` and
exits with code 0 — a `dotnet run` that appears to do nothing usually means one is
already resident in the tray. Kill it first:

```powershell
Stop-Process -Name WinMix -Force -ErrorAction SilentlyContinue
```

Closing the window only hides it. Exit lives in the tray context menu.

## Architecture

Three projects, and the split is load-bearing:

- **`src/WinMix.Audio`** — all Core Audio interaction. No UI types, no WPF
  reference. This is where the equalizer DSP will land, so it must stay
  presentation-free.
- **`src/WinMix.App`** — WPF shell, view models, tray icon.
- **`tests/WinMix.Audio.Tests`** — xunit against the audio project, whose
  internals are exposed via `InternalsVisibleTo`.

### Snapshots, not live COM objects

[AudioSessionService.Refresh()](src/WinMix.Audio/AudioSessionService.cs) returns
immutable [AudioSessionSnapshot](src/WinMix.Audio/AudioSessionSnapshot.cs) records
and keeps the live `AudioSessionControl` objects private, keyed by session instance
id. Those controls wrap COM pointers whose lifetime ends at the next refresh —
letting XAML bind straight to them invites use-after-dispose. Mutations go back
through `SetVolume(instanceId, …)` / `SetMute(instanceId, …)`.

NAudio hands out a **fresh wrapper and a fresh COM reference per indexer access**,
so `Refresh()` disposes the previous batch. Removing that would leak steadily.

### Polling, not notifications

Discovery is a 100 ms `DispatcherTimer` in
[MixerViewModel](src/WinMix.App/ViewModels/MixerViewModel.cs), not
`IAudioSessionNotification`. WASAPI delivers those callbacks on an MTA thread and
re-entering the session manager from inside one deadlocks. Polling sidesteps it
and is imperceptible for a mixer. Do not "upgrade" this to events without solving
the marshalling problem first.

### Threading rule

Everything in `WinMix.Audio` is single-threaded and, for the WPF shell, that
thread is the UI thread. The Core Audio objects are apartment-bound and a refresh
is only a few COM calls. **The equalizer breaks this rule** and will need its own
real-time thread — see below.

### Two feedback loops worth understanding

Both the session rows and the master control echo device state back into bound
properties, so both guard against fighting the user:

- A `_syncing` flag marks writes that originate from a device reading, so the
  setter does not push them back to WASAPI.
- `SessionViewModel.Sync` only adopts the device's scalar when it differs by more
  than `ScalarEpsilon`. WASAPI quantises what it stores, so a strict comparison
  never matches and every poll would yank the slider out from under a live drag.

### Slider position is not amplitude

WASAPI wants linear amplitude; loudness is perceived logarithmically. All
conversion goes through [VolumeCurve](src/WinMix.Audio/VolumeCurve.cs), which
treats slider travel as linear across −60 dB…0 dB. Position 0 maps to true silence
rather than the floor. Never assign a slider value straight to
`SimpleAudioVolume.Volume`.

### Row naming

[SessionNaming](src/WinMix.Audio/SessionNaming.cs) mirrors the Windows mixer's
precedence: `IsSystemSoundsSession` → the session's own `DisplayName` → the
executable's `FileDescription` → its file name. System sessions report display
names as indirect resource references (`@%SystemRoot%\System32\AudioSrv.Dll,-202`)
that need `SHLoadIndirectString`; without that step rows read as bare `svchost`.

[ProcessInfoCache](src/WinMix.Audio/ProcessInfoCache.cs) resolves paths with
`QueryFullProcessImageName`, not `Process.MainModule` — MainModule needs
`PROCESS_VM_READ` and is denied for anything more elevated than us. Its `Trim`
must keep being called each refresh, because Windows recycles pids and a stale
entry would mislabel a new session.

## Next phase: the per-application equalizer

The decision on record: **process loopback capture**, no driver. Windows offers no
API to insert DSP into another application's session, and the alternatives were
rejected — a system-wide APO applies per output *device* rather than per app, and a
virtual audio driver needs an EV certificate plus WHQL.

The shape of it:

1. Capture the target pid with `ActivateAudioInterfaceAsync` against the
   `VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK` device path, passing
   `AUDIOCLIENT_ACTIVATION_PARAMS` with
   `AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK` and
   `PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE`.
2. Run the captured frames through a cascade of biquad peaking filters (RBJ
   cookbook coefficients), one per band, in `WinMix.Audio/Dsp`.
3. Render the processed stream ourselves, and **mute the app's original session**
   via the existing `SetMute` or the audio plays twice.

Constraints to design against:

- Needs Windows 10 build 20348+. This machine is 26200, so it is available.
- Costs roughly 20–60 ms of latency: fine for music, visible as A/V drift on
  video and games. Expect to expose it per-app rather than globally.
- **NAudio does not wrap any of this.** Add CsWin32 or hand-roll the COM interop.
  The Windows SDK `ApplicationLoopback` sample is the reference implementation;
  verify the exact struct layout and flag names against current docs rather than
  trusting the names above.
- The render loop is real-time. Preallocate every buffer, allocate nothing per
  callback, and keep it off the UI thread.

## Platform gotchas already paid for

- **`Application` is ambiguous.** `UseWPF` and `UseWindowsForms` are both on (the
  tray icon needs WinForms `NotifyIcon`), and their implicit usings each define
  `Application`. [App.xaml.cs](src/WinMix.App/App.xaml.cs) aliases the WPF one.
- **`LibraryImport` cannot marshal `char[]` or `ref` primitives** without the whole
  assembly opting out of runtime marshalling (SYSLIB1051). Declare raw pointers and
  `stackalloc` the buffer; `AllowUnsafeBlocks` is enabled for this.
- **`AudioSessionState` lives in `NAudio.CoreAudioApi.Interfaces`**, not
  `NAudio.CoreAudioApi` beside everything else.
- **The manifest requests `asInvoker` deliberately.** Elevating would *lose* access
  to the session list of the user's normal-integrity apps. Do not add
  `requireAdministrator`.
- **WFAC010 is suppressed** in the app project: the WinForms analyzer wants DPI
  configured in code, but for a WPF app the manifest is correct.
- **The SDK writes `.slnx` by default.** This repo keeps a classic
  `WinMix.sln` (`dotnet new sln --format sln`) for tooling compatibility.

## Seeing the app

To confirm a UI change, capture the window rather than trusting the build.
`SetForegroundWindow` does not work from a background process, so use
`PrintWindow` with flag `2` (`PW_RENDERFULLCONTENT`) — flag `0` returns black for
composited WPF windows. Call
`SetProcessDpiAwarenessContext(-4)` first, or `GetWindowRect` returns virtualized
coordinates and the capture is cropped to the top-left of a scaled window.
