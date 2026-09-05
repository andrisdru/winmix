# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

WinMix is a Windows tray utility for **per-application** audio control: a
volume mixer. Native C++20, Win32 + Direct2D/DirectWrite for the UI, WASAPI
via hand-rolled COM interop for audio (no NAudio, no .NET) — a single
dependency-free `WinMix.exe`.

## Commands

Requires Visual Studio Build Tools 2022 ("Desktop development with C++"
workload, plus the "C++ CMake tools for Windows" optional component, which
bundles the `cmake.exe`/`ninja.exe` used below) and Windows 10 build 20348+.

```powershell
# Configure + build (Debug)
cmake --build --preset x64-debug

# Release / packaging presets
cmake --build --preset x64-release
cmake --build --preset x64-package    # tools/tests off, matches the shipped exe

# Run
native/build/x64-debug/WinMix.App/Debug/WinMix.exe

# Unit tests (doctest)
native/build/x64-debug/tests/Debug/winmix_audio_tests.exe
native/build/x64-debug/tests/Debug/winmix_audio_tests.exe --test-case="VolumeCurve*"
```

If `cmake`/`ninja` aren't on `PATH`, add the VS Build Tools' bundled copies
(adjust the VS year/edition if different):

```powershell
$env:PATH += ";C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
```

Both `WinMix.Audio` and `WinMix.App` build as C++20 with the VS generator
(`CMakePresets.json`), producing a multi-config tree under `native/build/<preset>/`.

**A running instance silently swallows the next launch.** `WinMain` holds a
single-instance mutex (`Local\WinMix.SingleInstance`), so a second launch
exits immediately with no visible effect — a rebuild that appears to do
nothing usually means one is already resident in the tray. Kill it first:

```powershell
Stop-Process -Name WinMix -Force -ErrorAction SilentlyContinue
```

Closing the window only hides it. Exit lives in the tray context menu.

## Architecture

Three CMake targets under `native/`, and the split is load-bearing:

- **`WinMix.Audio`** (static lib) — all Core Audio/WASAPI interaction via
  raw COM (`Microsoft::WRL::ComPtr`, no wrapper library). No UI types, no
  Win32 window/message-loop code, so it stays independently testable.
- **`WinMix.App`** (exe) — the Win32 shell: `WinMain`, the message loop,
  Direct2D/DirectWrite rendering, custom controls (`FaderControl`,
  `MuteToggle`, `PeakMeter`, `ComboBox`), the tray icon.
- **`tests/`** — doctest against `winmix_audio`, run via `enable_testing()`.
- **`tools/AudioSmokeTest`** — a throwaway console exe that polls sessions
  once a second and prints name/volume/mute/peak, for verifying the audio
  layer against real playback without the UI. `WINMIX_BUILD_TOOLS`/
  `WINMIX_BUILD_TESTS` CMake options (default `ON`) turn both off for
  `x64-package`.

### Snapshots, not live COM objects

[AudioSessionService::Refresh()](native/WinMix.Audio/src/AudioSessionService.cpp)
returns immutable `AudioSessionSnapshot` values and keeps the live
`IAudioSessionControl2`/`ISimpleAudioVolume` COM pointers private, keyed by
session instance ID. Each `Refresh()` releases the previous batch of
`ComPtr`s before re-enumerating — skipping that would leak COM references
steadily. Mutations go back through `SetVolume(instanceId, …)` /
`SetMute(instanceId, …)`, never by holding a pointer across a refresh.

### Polling, not notifications

Discovery is a 100 ms `SetTimer` (`kPollTimerId`/`kPollIntervalMs` in
[MainWindow.cpp](native/WinMix.App/src/MainWindow.cpp)), not
`IAudioSessionNotification`. WASAPI delivers those callbacks off-thread, and
re-entering the session manager from inside one deadlocks. Polling
sidesteps it and is imperceptible for a mixer. Do not "upgrade" this to
events without solving the marshalling problem first.

### Threading rule

Everything here is single-threaded: one apartment-threaded COM thread
(`CoInitializeEx(COINIT_APARTMENTTHREADED)` in `WinMain`), which is also the
window's message-loop thread. A refresh is only a few COM calls; nothing in
this codebase should block that thread for long.

### The feedback-loop guard

The fader/mute controls and the poll loop both touch the same on-screen
state, so both must avoid fighting the user:

- `FaderControl::SetValue()` / `MuteToggle::SetMuted()` (used when a poll
  adopts the device's own reading) deliberately do **not** invoke
  `onChange` — only a direct user click/drag does. This is a structural
  guard (the poll path and the user-input path are different methods),
  not a runtime flag.
- `SyncMaster()`/`SyncStrip()` only adopt the device's scalar when it
  differs from the current fader value by more than `kScalarEpsilon`
  (~0.01). WASAPI quantizes what it stores, so a strict comparison never
  matches and every poll would yank the slider out from under a live drag.

### Slider position is not amplitude

WASAPI wants linear amplitude; loudness is perceived logarithmically. All
conversion goes through [VolumeCurve](native/WinMix.Audio/src/VolumeCurve.cpp),
which treats slider travel as linear across −60 dB…0 dB. Position 0 maps to
true silence rather than the floor. Never assign a slider value straight to
`ISimpleAudioVolume::SetMasterVolume`.

### Row naming

[SessionNaming](native/WinMix.Audio/src/SessionNaming.cpp) mirrors the
Windows mixer's precedence: `IsSystemSoundsSession` → the session's own
display name → the executable's `FileDescription` → its file name. System
sessions report display names as indirect resource references
(`@%SystemRoot%\System32\AudioSrv.Dll,-202`) that need
`SHLoadIndirectString`; without that step rows read as bare `svchost`.

[ProcessInfoCache](native/WinMix.Audio/src/ProcessInfoCache.cpp) resolves
paths with `QueryFullProcessImageNameW` under
`PROCESS_QUERY_LIMITED_INFORMATION`, not `PROCESS_VM_READ` — that's denied
for anything more elevated than us. It must keep trimming stale pids each
refresh, since Windows recycles them and a stale entry would mislabel a new
session.

### The two undocumented COM interop shims

- [AudioPolicyConfigFactory](native/WinMix.Audio/src/AudioPolicyConfigFactory.cpp)
  and [PolicyConfigInterop](native/WinMix.Audio/src/PolicyConfigInterop.cpp)
  are hand-declared `IInspectable`/`IUnknown`-derived vtable structs for
  undocumented Windows interfaces — there is no header for either. The
  vtable slot counts must be exact: a past bug here (fixed) had 9 leading
  placeholder slots before `SetDefaultEndpoint` when 10 are needed (a
  missing `ResetDeviceFormat`), which doesn't fail to compile — it silently
  calls the wrong method and segfaults or corrupts state at runtime. If you
  touch either file, verify slot counts against an independent reference
  (e.g. EarTrumpet, SoundSwitch), not just by re-reading the existing code.
- `AudioPolicyConfigFactory`'s class IID branches on the OS build number
  (`AB3D4648-…` for build ≥ 21390, else `2A59116D-…`), which depends on the
  manifest's `<supportedOS>` Windows-10 GUID being present — without it, a
  compatibility shim reports a stale build number and picks the wrong
  branch.

## DPI and layout (the part most recently debugged)

This app renders through Direct2D pinned to a flat 96 DPI
([DeviceResources.cpp](native/WinMix.App/src/render/DeviceResources.cpp),
`SetDpi(96, 96)` deliberately, not `GetDpiForWindow`) so that D2D's
coordinate space always equals physical pixels — every layout constant and
every mouse-derived hit-test rect is already a raw physical-pixel value, and
letting D2D additionally scale its own rendering by the real monitor DPI
would misalign clicks from what's drawn (this was a real, user-reported bug:
sliders and dropdowns only responded "between" where they visually were).

Because D2D itself no longer compensates for DPI, `MainWindow` has to do it
manually, unlike WPF which scales DIPs automatically:

- `dpiScale_` (`GetDpiForWindow(hwnd_) / 96.0`), recomputed on
  `WM_DPICHANGED`, scales every layout constant, font size, and control
  metric in `Layout()`/`LayoutStrip()` and each control's `SetScale()`.
- **The fader's height is computed, not fixed** — `LayoutStrip` gives it
  whatever vertical room is left after every other (fixed-size) row, the
  same way the equivalent WPF `Grid` row used `Height="*"`. Without this,
  the card either clips at the bottom (window too short for the scaled
  content) or leaves a dead gap (window taller than the content needs).
- **The window width tracks the strip count** (`SyncWindowWidthToContent`,
  called at the end of every `Layout()`), matching WPF's
  `SizeToContent="Width"` — otherwise the window sits at a fixed width with
  empty space to the right when only a few apps are playing audio. Manual
  horizontal resizing is intentionally blocked (see the `WM_NCHITTEST`
  remap below), so this never fights a user drag; it's guarded against
  reentrancy (`syncingWindowWidth_`) because `SetWindowPos` synchronously
  re-enters `WM_SIZE` → `Layout()` on the same call stack.
- `nameFormat_`/`comboFormat_` need `IDWriteTextFormat::SetTrimming` with an
  ellipsis sign set explicitly — unlike WPF's `TextTrimming`, DirectWrite's
  default behavior for text that overflows a `DRAW_TEXT_OPTIONS_CLIP` rect
  is a hard, silent clip: LEADING-aligned text (the per-app output combo)
  chops off mid-word ("Defa" for "Default"), and CENTER-aligned text (the
  app name label) clips *both* ends at once, showing a garbled slice from
  the middle of the string. The app name label additionally wraps
  (`DWRITE_WORD_WRAPPING_WRAP`, two lines' worth of height) rather than
  truncating a long name down to almost nothing.

## Verifying UI changes

No UI test harness exists. To confirm a change actually rendered correctly:

1. `SetProcessDpiAwarenessContext(-4)` (`DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2`)
   at the top of *every* verification script — each PowerShell invocation is
   a fresh process, so this doesn't carry over, and without it
   `GetWindowRect`/`ClientToScreen` return virtualized (wrong) coordinates.
2. Capture with `PrintWindow` and flag `2` (`PW_RENDERFULLCONTENT`) —
   flag `0` returns black for composited D2D windows.
3. **The ComboBox dropdown is a separate, real Win32 popup window**
   (GDI-rendered, not D2D — see `ComboBox::OpenPopup`), not a child of the
   main window. `PrintWindow` on the main window's HWND alone cannot
   capture it; verifying an open dropdown needs a full-screen
   (`CopyFromScreen`) capture instead.
4. `SendMessageW`/`PostMessageW` with `WM_LBUTTONDOWN`/`WM_LBUTTONUP` and a
   `MAKELPARAM(x, y)` in **client** coordinates can drive clicks
   programmatically without needing real cursor movement or window focus —
   useful for scripted verification of fader drags and combo clicks.

## Platform gotchas already paid for

- **`LibraryImport`/marshalling restrictions don't apply here** (that was a
  C#/.NET-interop constraint) — declare COM vtables and Win32 calls
  directly; no attribute dance needed.
- **The manifest requests `asInvoker` deliberately.** Elevating would *lose*
  access to the session list of the user's normal-integrity apps. Do not add
  `requireAdministrator`.
- **A hand-authored `RT_MANIFEST` resource collides with the MSVC linker's
  own auto-generated manifest** at the same resource ID unless
  `/MANIFEST:NO` is set explicitly (already set in
  `WinMix.App/CMakeLists.txt`).
- **Static CRT linking** goes through `CMAKE_MSVC_RUNTIME_LIBRARY` +
  `cmake_policy(SET CMP0091 NEW)` (set before `project()` in the top-level
  `CMakeLists.txt`), not hand-edited `/MT` flags — this is what keeps the
  shipped exe free of a VCRUNTIME/MSVCP/UCRT dependency. Verify with
  `dumpbin /dependents` on the `x64-package` output.
- **`HICON` → D2D bitmap conversion has no one-liner.** `IconLoader` goes
  through `GetIconInfo` → select `hbmColor`/`hbmMask` into a memory DC →
  `GetDIBits` into a top-down 32bpp BGRA buffer → `CreateBitmap`, with a
  legacy AND-mask fallback when there's no per-pixel alpha.

## Seeing the app

See "Verifying UI changes" above for the concrete steps; the summary is
build, run, kill any existing instance first (single-instance mutex), then
screenshot rather than trusting compilation alone.
