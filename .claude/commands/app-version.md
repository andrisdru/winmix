---
description: Bump WinMix's version number and confirm it shows up in the app GUI
---

Assign WinMix a new version. The version lives in a single place, the `VERSION`
file at the repo root (currently a bare `MAJOR.MINOR.PATCH` string, e.g.
`1.0.0`) -- CMake reads it into `PROJECT_VERSION` and
`WinMix.App/resources/Version.h.in` gets configured into a generated
`Version.h` from that, which `MainWindow.cpp` (window title bar) and
`TrayIcon.cpp` (tray tooltip and the disabled "WinMix vX.Y.Z" tray-menu row)
both include. Bumping the `VERSION` file is the only edit needed -- the GUI
strings update automatically on the next build.

1. Read `VERSION` and parse the three numbers.

2. Decide the new version from the argument given to this skill:
   - No argument, or `patch`: increment PATCH, reset nothing (`1.2.3` -> `1.2.4`).
   - `minor`: increment MINOR, reset PATCH to 0 (`1.2.3` -> `1.3.0`).
   - `major`: increment MAJOR, reset MINOR and PATCH to 0 (`1.2.3` -> `2.0.0`).
   - An explicit `X.Y.Z` string: use it as-is after validating it's three
     non-negative integers separated by dots (this is fed to CMake's
     `project(... VERSION ...)`, which rejects anything else).

3. Write the new version (bare string, trailing newline) to `VERSION`.

4. Rebuild the `x64-debug` preset to confirm the reconfigure and generated
   header pick up the change cleanly. If `cmake`/`ninja` aren't on `PATH`:
   ```powershell
   $env:PATH += ";C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
   ```
   Then:
   ```powershell
   cmake --build --preset x64-debug
   ```
   Do not report success if this fails -- fix it first (the most likely
   break is `VERSION` no longer parsing as three dotted integers).

5. Spot-check the generated header actually picked up the new number:
   ```powershell
   Get-Content build\x64-debug\WinMix.App\generated\Version.h
   ```

This skill does not commit or push -- run `/commit-and-push` afterward if the
bump should be saved to git.

Report the old version, the new version, and confirm the rebuild succeeded.
