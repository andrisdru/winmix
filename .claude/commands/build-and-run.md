---
description: Close any running WinMix instance, build the solution, and launch it fresh
---

Build and run WinMix, restarting it if an instance is already running.

1. Close any running instance first. WinMix holds a single-instance mutex, so
   launching a second copy while one is already running just hands off to it
   and exits immediately with no visible effect (see CLAUDE.md, "A running
   instance silently swallows the next launch") — so the running copy must be
   killed before a rebuild can be observed:
   ```
   taskkill //F //IM WinMix.exe
   ```
   A "no tasks running" result is fine — it just means nothing needed closing.

2. Build the solution and fix any errors before continuing:
   ```
   dotnet build WinMix.sln
   ```
   Do not proceed to launch a build that failed.

3. Launch the app in the background so this command can finish:
   ```
   dotnet run --project src/WinMix.App
   ```

4. Confirm it actually started: check `tasklist` for `WinMix.exe` and check the
   launch output/log for exceptions.

Report back concisely: whether a prior instance was closed, whether the build
succeeded, and the new process's PID.
