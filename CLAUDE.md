# Monitone: briefing for AI coding sessions

Monitone is a Windows tray application: every program plays on the speakers of
the screen its window is on. Optional left/right balance for a single speaker
set, per-app rules, and a modern settings window. C++20, Win32, Core Audio,
WebView2. Owner: Nusi42 (experienced C++/Win32 developer).

## Decisions already made (do not reopen without being asked)

- **Windows 10/11 only.** No other platforms. MinGW cross-compile from Linux
  and the Chromium UI preview are kept purely as developer tools.
- **No GitHub Actions / CI.** Releases are built on the owner's PC with
  `tools\build-release.ps1`, tested, then published with
  `tools\publish-release.ps1`. Do not add workflows.
- **Until the first release exists, push directly to `main`.** No PRs needed
  until then. After the first release: ask the owner how to proceed.
- **Commit messages follow Conventional Commits** (`feat:`, `fix:`, `perf:`,
  `refactor:`, `docs:`, `build:`, `chore:`, ...). The release script derives
  the version and changelog from them. End commit messages with the
  attribution trailer your harness asks for.
- **The project had a former name. Never mention it** anywhere: code,
  comments, docs, commit messages, UI.
- **README.md is for end users only** (installation, usage, tips). Developer
  information goes to `docs/BUILDING.md`, `docs/ARCHITECTURE.md`,
  `CONTRIBUTING.md`.
- **Settings UI is WebView2 (HTML/CSS/JS) in `MonitoneSettings.dll`,** loaded
  only while the window is open and unloaded afterwards to free memory.
  WinUI 3 was rejected because it cannot be unloaded from a process.
- **Code quality bar:** C++20, RAII for every resource (`src/Win32.h`), every
  variable initialized, `[[nodiscard]]`/`noexcept` where sensible, no raw
  owning handles, no `const_cast`, warnings as errors
  (`-DMONITONE_WARNINGS_AS_ERRORS=ON`), formatted with `.clang-format`.
  Match the surrounding style.

## Repository layout

| Path | Purpose |
| --- | --- |
| `src/main.cpp`, `src/TrayApp.*` | Entry point, single instance, tray icon/menu, WinEvent hooks, message loop |
| `src/Engine.*` | Per-tick evaluation: audio session -> window -> display -> rule; also the live report |
| `src/AudioRouter.*` | Render endpoints, audio sessions, change notifications |
| `src/PolicyConfig.*` | Per-app default endpoint via undocumented `IAudioPolicyConfigFactory` |
| `src/WindowMap.*` | Process -> representative window (parent chain for helper processes, UWP frames) |
| `src/SettingsModule.*` | Exe side: loads, drives, unloads the settings DLL |
| `src/SettingsApi.h` | Plain C interface between exe and DLL (versioned) |
| `src/Config.*`, `Monitors.*`, `Balance.*`, `Autostart.*`, `Shell.*` | `monitone_core` static lib, shared by exe and DLL |
| `src/settings/` | `MonitoneSettings.dll`: exports, UI thread, WebView2 window, JSON bridge, resource serving, loader |
| `src/IdentifyOverlay.*`, `src/TestTone.*` | Display number overlay, WASAPI test tone (used by the DLL) |
| `ui/` | Settings UI: `index.html`, `app.css`, `app.js` (embedded into the DLL as RCDATA) |
| `res/` | Icons (`.ico`, SVG sources), `.rc` files, manifest, `.def` for DLL exports |
| `tools/build-release.ps1` | Release candidate: next version, notes, MSVC build, zip + NSIS setup + MSI in `build\release-candidate\dist`; no git changes |
| `tools/publish-release.ps1` | Publishes the tested candidate: version.txt, CHANGELOG, commit, tag, push, GitHub release via `gh` |
| `installer/` | `Monitone.nsi` (NSIS 3), `Monitone.wxs` (WiX 5 + Util ext); both per-user, no admin |
| `docs/release-notes/` | Optional `X.Y.Z.md` intro for a release's notes (required for the first release) |
| `tools/ui-preview/` | Renders the UI in Chromium with `mock-bridge.js`; also makes README screenshots |
| `tools/icon/make_icon.py` | Regenerates all icons from SVG (needs rsvg-convert, icotool) |
| `cmake/GenerateVersion.cmake` | `version.h` from `version.txt` + git state (release vs. `+g<hash>[.dirty]`) |

## Technical facts and pitfalls

- **Per-app routing** uses the undocumented `IAudioPolicyConfigFactory`
  (activation class `Windows.Media.Internal.AudioPolicyConfig`). Two IIDs are
  tried (21H2+ and older); the vtable layout is in `src/PolicyConfig.cpp`.
  Device ids are wrapped as `\\?\SWD#MMDEVAPI#<id>#{e6327cad-...}`.
- **Session enumeration** only sees new sessions if
  `RegisterSessionNotification` was called on the session manager.
- **Main thread is MTA** (audio callbacks are free-threaded and only
  `PostMessage` to the tray window).
- **Exe and DLL each link the CRT statically** = separate heaps. Never pass C++
  objects or allocations across the boundary; use `SettingsApi.h` (POD structs,
  strings valid only during a call). Config changes go through the INI file:
  DLL writes, then calls `configChanged`, exe reloads.
- **The DLL runs its UI on its own STA thread** (WebView2 requirement). Host
  callbacks are marshalled to the exe main thread with `SendMessage`
  (`kMsgSettingsCall`); `MonitoneSettingsShutdown` keeps dispatching sent
  messages while it waits, to avoid deadlocks. `closed` is posted
  (`kMsgSettingsClosed`), then the exe calls shutdown and `FreeLibrary`.
- **Before unloading,** the DLL waits until every WebView2 callback object it
  created is released (`g_liveCallbacks` in `src/settings/Handlers.h`); after
  5 s it pins itself instead of risking a crash.
- **WebView2:** MSVC links `WebView2LoaderStatic.lib`; MinGW loads
  `WebView2Loader.dll` at runtime (`src/settings/Loader.cpp`). Use the
  `IID_ICoreWebView2...` constants, not `__uuidof`, for WebView2 interfaces
  (MinGW has no `__uuidof` for them). `cmake/mingw-compat/EventToken.h` fixes a
  header case issue on Linux.
- **UI files** are served from `https://monitone.ui/` via
  `WebResourceRequested`. `index.html` has a strict Content Security Policy:
  no inline scripts or `style` attributes; set styles via CSSOM
  (`element.style`). Build DOM with the `h()` helper in `app.js`, never
  `innerHTML` (exe names and window titles are untrusted).
- **Messages UI -> DLL:** `getState`, `getReport`, `setDisplayDevice`,
  `setOptions`, `setAutostart`, `setAppRule`, `removeAppRule`, `testTone`,
  `identify`, `browseExecutable`, `openConfigFolder`, `openUrl`,
  `resetWindowsDevices`, `close`. DLL -> UI: `state`, `report`, `navigate`,
  `browseResult`. Keep `tools/ui-preview/mock-bridge.js` in sync when you
  change the protocol.
- **Configuration:** `%APPDATA%\Monitone\Monitone.ini` (UTF-16 with BOM,
  written via the profile API). WebView2 cache: `%LOCALAPPDATA%\Monitone`.
- **GCC warnings:** `-Wmissing-field-initializers` is off on purpose
  (designated initializers), `-Wnon-virtual-dtor` is not used (COM).

## Build and check

Windows, Developer PowerShell for VS 2022:

```
cmake -S . -B build -A x64 -DMONITONE_WARNINGS_AS_ERRORS=ON
cmake --build build --config Release
```

Output: `build\bin\Release\Monitone.exe` and `MonitoneSettings.dll`.
UI check without Windows: `node tools/ui-preview/preview.mjs build/ui-preview`
(fails on script errors and CSP violations). Details: `docs/BUILDING.md`.

## Release

`tools\build-release.ps1 -DryRun`, then `tools\build-release.ps1` (builds
the candidate into `build\release-candidate\dist`, no git changes), the owner tests,
then `tools\publish-release.ps1`. The first release is 0.1.0. Details:
`CONTRIBUTING.md`.

## Status at handover

Everything is on `main`. Verified only on Linux:
- MinGW cross-build with warnings as errors
- UI in headless Chromium (all pages, light/dark, interaction test)
- DLL load / refusal (no WebView2) / unload under Wine: module gone afterwards

**Never run on real Windows yet.** First tasks on the owner's PC:
1. First MSVC build; fix any `/W4` findings.
2. Routing: two displays, move a playing browser window across.
3. Balance (both modes) on one speaker set.
4. Settings window: Mica, dark mode, all pages with real data.
5. Unloading: after closing the settings, `MonitoneSettings.dll` must be gone
   from `Monitone.exe` (Process Explorer) and `msedgewebview2.exe` must exit.
6. Test tone, Identify displays, Start with Windows, tray icon at 125/150 %
   scaling, exit with "Undo changes on exit".
7. `tools\build-release.ps1` and `tools\publish-release.ps1` for the first
   release.

A small unload test harness was used under Wine but is not in the repo: load
`MonitoneSettings.dll`, call `MonitoneSettingsOpen` (with dummy host
callbacks), `MonitoneSettingsShutdown`, `FreeLibrary`, then check
`GetModuleHandleW(L"MonitoneSettings.dll") == nullptr`, three rounds. Worth
adding as `tools/unload-test/` and running on Windows with real WebView2.

## Backlog (ideas discussed, not decided)

- Code signing in `build-release.ps1` (SmartScreen)
- Installer or winget package
- German README
- Remember settings window size and position
- Issue template for bug reports
- Tray icon animation while routing

## Housekeeping for the owner on GitHub

- Delete branch `ccr-862583c2-cb6zxq` (identical to `main`; could not be
  deleted from the cloud session).
- Delete the old repository and the old failed Actions runs.
