# WinMix

A lightweight Windows tray utility for **per-application** audio control — a
volume mixer today, with a per-application equalizer planned next.

WinMix sits in the system tray and gives you a compact mixer window: an
independent volume slider, mute button, and peak meter for every app currently
playing audio, plus quick pickers for the system's default playback and
recording (microphone) devices — all without leaving the tray.

## Features

- Per-app volume sliders, mute, and live peak meters, similar to the native
  Windows volume mixer but always one click away in the tray
- Pin an individual app's audio output to a specific playback device,
  independent of the system default
- System-wide output and input (microphone) device pickers, available both in
  the main window and the tray icon's context menu
- Runs quietly in the tray; closing the window hides it rather than exiting —
  use "Exit" in the tray menu to actually quit

## Requirements

- Windows 10 (build 20348 or later) or Windows 11
- [.NET 8 SDK](https://dotnet.microsoft.com/download/dotnet/8.0) to build
- The built app needs the .NET 8 Desktop Runtime installed, unless published
  as self-contained (see below)

## Building

```powershell
dotnet build WinMix.sln
```

Both projects build with warnings treated as errors, so a clean build means a
clean build.

## Running

```powershell
dotnet run --project src/WinMix.App
```

WinMix only allows a single running instance — launching a second copy while
one is already running silently hands off to it and exits, so you won't see
your changes. If you're not sure one isn't already running, close it first:

```powershell
Stop-Process -Name WinMix -Force -ErrorAction SilentlyContinue
```

## Testing

```powershell
dotnet test WinMix.sln

# Run one test or one class
dotnet test --filter "FullyQualifiedName~VolumeCurveTests"
```

## Producing a standalone build

```powershell
dotnet publish src/WinMix.App/WinMix.App.csproj -c Release
```

This produces a framework-dependent build at
`src/WinMix.App/bin/Release/net8.0-windows/publish/WinMix.exe`, which needs the
.NET 8 Desktop Runtime on the machine that runs it. To produce a single,
self-contained `.exe` that needs nothing preinstalled, add:

```powershell
dotnet publish src/WinMix.App/WinMix.App.csproj -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true
```

## Project layout

- `src/WinMix.Audio` — all Core Audio / WASAPI interaction, via NAudio plus
  hand-rolled COM interop for the undocumented Windows APIs NAudio doesn't
  cover. No UI dependencies, so it stays reusable once the equalizer lands.
- `src/WinMix.App` — the WPF shell: the mixer window, view models, and tray
  icon.
- `tests/WinMix.Audio.Tests` — xunit tests against the audio project.

## Roadmap

A per-application equalizer, built on WASAPI process loopback capture rather
than a system-wide audio driver, so EQ can be applied to one app's audio
without affecting anything else.
