---
description: Close any running WinMix instance, build the native app, and launch it fresh
---

Build and run WinMix, restarting it if an instance is already running.

1. Close any running instance first. WinMix holds a single-instance mutex, so
   launching a second copy while one is already running just hands off to it
   and exits immediately with no visible effect (see CLAUDE.md, "A running
   instance silently swallows the next launch") — so the running copy must be
   killed before a rebuild can be observed:
   ```
   Stop-Process -Name WinMix -Force -ErrorAction SilentlyContinue
   ```
   No matching process is fine — it just means nothing needed closing.

2. Build the `x64-debug` preset and fix any errors before continuing. If
   `cmake` isn't on `PATH`, add the VS Build Tools' bundled copy first:
   ```powershell
   $env:PATH += ";C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
   cmake --build --preset x64-debug
   ```
   Do not proceed to launch a build that failed.

3. Launch the app in the background so this command can finish:
   ```
   native\build\x64-debug\WinMix.App\Debug\WinMix.exe
   ```

4. Confirm it actually started: check `tasklist` for `WinMix.exe` and check the
   launch output/log for exceptions.

Report back concisely: whether a prior instance was closed, whether the build
succeeded, and the new process's PID.
