# Monitone: briefing for AI coding sessions

Monitone is a Windows tray application: every program plays on the speakers of
the screen its window is on. Optional left/right balance for a single speaker
set, per-app rules, and a modern settings window. C++20, Win32, Core Audio,
WebView2. Owner: the repository owner (experienced C++/Win32 developer).

## Decisions already made (do not reopen without being asked)

- **Windows 10/11 only.** No other platforms. MinGW cross-compile from Linux
  and the Chromium UI preview are kept purely as developer tools.
- **No GitHub Actions / CI.** Releases are built on the owner's PC with
  `tools\build-release.ps1`, tested, then published with
  `tools\publish-release.ps1`. Do not add workflows.
- **The repository is public.** `main` is protected by the ruleset "Protect
  main" (pull requests, squash merge only, no force push or deletion); the
  owner can bypass it, which `tools\publish-release.ps1` relies on. GitHub
  Actions is disabled.
- **Master and work folders** (the owner's setup, `docs/BUILDING.md` "Master
  and work folders"; the actual paths are in the untracked `CLAUDE.local.md`).
  The master checkout (local git config `monitone.role=master`) is the only one
  that pushes to GitHub (its remote `github`), and only `main` and release tags
  `v<X.Y.Z>`. It builds nothing except releases (`tools\build-release.ps1`
  exports `HEAD` into a fresh folder outside the checkout); CMake refuses a
  development build there. Work happens in work folders (`origin` = the
  master, branch `WIN<n>`): push the branch to the master (the pre-push hook
  runs the full check `tools/check.mjs`), fast-forward the master's `main`
  (`git -C <master> merge --ff-only WIN<n>`), then publish from the master
  (`git -C <master> push github main`). A work folder's push to a network
  remote is refused. Ask the owner before publishing.
- **Linear history: rebase, never merge.** Combine branches by rebasing onto
  the target and fast-forwarding (`git rebase main`, then
  `git switch main && git merge --ff-only <branch>`), never with a merge
  commit. The pre-push hook `.githooks/pre-push` refuses merge commits in what
  a push adds (old merge commits on the remote stay).
- **Git hooks** in `.githooks`: pre-commit (private-data guard on the staged
  files, staged C++ matches `.clang-format`), commit-msg (Conventional Commits
  subject, no session links, guard and identity check), pre-push
  (`tools/git/push-plan.mjs` decides from the checkout's role and the remote:
  guard, identity check and linear history on the pushed commits everywhere,
  the full check from a work folder, the guard on every tracked file from the
  master, refusals). The first CMake configure of a clone sets
  `core.hooksPath` to `.githooks` (`cmake/Checkout.cmake`); the master gets it
  from `node tools/git/work-folders.mjs guard-master`. Never use `--no-verify`.
- **Never commit personal data:** no real names, e-mail addresses (use the
  GitHub noreply address), locations, local user paths or session links. The
  private-data guard (`tools/git/check-private.mjs`) refuses matches of the
  owner's patterns file, which lives outside the repository (named by the git
  config key `monitone.privatePatterns`; never copy it or its values into the
  repository). Every commit carries the public identity: where the local git
  config sets `monitone.identity`, the hooks refuse any other author or
  committer.
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
| `tools/build-release.ps1` | Release candidate (master or plain clone): next version, notes, clean MSVC build of an export of `HEAD`, zip + NSIS setup + MSI in `%LOCALAPPDATA%\Monitone-release\dist`; no git changes |
| `tools/publish-release.ps1` | Publishes the tested candidate: version.txt, CHANGELOG, commit, tag, push (master: `github`), GitHub release via `gh` |
| `tools/check.mjs` | Full check: MSVC build with warnings as errors, git tooling tests, UI preview; run by the pre-push hook from a work folder |
| `installer/` | `Monitone.nsi` (NSIS 3), `Monitone.wxs` (WiX 5 + Util ext); both per-user, no admin |
| `docs/release-notes/` | Optional `X.Y.Z.md` intro for a release's notes (required for the first release) |
| `tools/ui-preview/` | Renders the UI in Chromium with `mock-bridge.js`; also makes README screenshots |
| `.githooks/`, `tools/git/` | Git hooks: pre-commit clang-format check (`check-format.mjs`, `format.mjs`), commit-msg Conventional Commits check (`check-commit-msg.mjs`, `commit-message.mjs`), private-data guard and identity check in all three (`check-private.mjs`, `private-patterns.mjs`, `identity.mjs`), pre-push (`pre-push.mjs`, decision in `push-plan.mjs`, linear-history step `check-linear.mjs`/`linear.mjs`); `work-folders.mjs` (`guard-master`, `create <n>`); tests in `*.test.mjs` (helpers `test-repos.mjs`) |
| `tools/icon/make_icon.py` | Regenerates all icons from SVG (needs rsvg-convert, icotool) |
| `cmake/Checkout.cmake` | At configure time: refuses the master checkout, sets `core.hooksPath` to `.githooks` (keeps a different value, warns) |
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

Windows, Developer PowerShell for VS 2022, in a work folder or plain clone:

```
cmake -S . -B build -A x64 -DMONITONE_WARNINGS_AS_ERRORS=ON
cmake --build build --config Release
```

Output: `build\bin\Release\Monitone.exe` and `MonitoneSettings.dll`.
UI check without Windows: `node tools/ui-preview/preview.mjs build/ui-preview`
(fails on script errors and CSP violations). Git tooling tests:
`node --test "tools/git/*.test.mjs"`. All of it at once: `node tools/check.mjs`.
Details: `docs/BUILDING.md`.

## Release

In the master: `tools\build-release.ps1 -DryRun`, then
`tools\build-release.ps1` (clean build of an export of `HEAD` into
`%LOCALAPPDATA%\Monitone-release`, no git changes), the owner tests, then
`tools\publish-release.ps1` (pushes to `github`). Work folders fetch the
release commit from the master afterwards. Details: `CONTRIBUTING.md`.

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
- Tray icon animation while routing

## Housekeeping for the owner on GitHub

- Delete the old repository if it still exists.
- Ask GitHub Support to purge unreachable commits and cached views of
  `github.com/Nusi42/Monitone` (history was rewritten before going public).
