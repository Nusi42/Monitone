# Building

C++20, CMake 3.20+. Visual Studio 2022 (Desktop C++ workload):

```
cmake -S . -B build -A x64
cmake --build build --config Release
```

The first configure downloads two pinned, hash-checked dependencies: the
WebView2 SDK (NuGet) and nlohmann/json. MSVC links the WebView2 loader
statically; MinGW builds ship `WebView2Loader.dll` next to the DLL.

Cross-compiling from Linux with MinGW-w64 (GCC 13+, posix thread model):

```
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

`-DMONITONE_WARNINGS_AS_ERRORS=ON` turns warnings into errors (`/W4 /WX` or
`-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wold-style-cast -Werror`).
Formatting follows `.clang-format`. The MinGW build is a developer check only;
releases are built with MSVC by `tools\build-release.ps1` (see CONTRIBUTING.md).

The settings UI can be developed in a browser, with sample data instead of the
real application:

```
npm install --no-save playwright && npx playwright install chromium
node tools/ui-preview/preview.mjs build/ui-preview
```

It fails on script errors and Content Security Policy violations, so it also
works as a quick UI check.

Versioning, releases and commit messages: see [CONTRIBUTING.md](../CONTRIBUTING.md).

The README screenshots in `docs/images` are made the same way, with sample
data and a Windows 11 style window frame (`pngquant` shrinks them if installed):

```
node tools/ui-preview/readme-screenshots.mjs
```

## Git hooks

The hooks in `.githooks` need Node.js. The first CMake configure of a clone
enables them (`cmake/Checkout.cmake` sets `core.hooksPath` to `.githooks`; a
different value is kept, with a warning). Without a build, enable them once
per clone:

```
git config core.hooksPath .githooks
```

- **pre-commit:** the staged version of every added or changed C++ file must
  match `.clang-format`. It only checks; run `clang-format -i` yourself. It
  uses `clang-format` from PATH, else the one in Visual Studio
  (`VC\Tools\Llvm`), and skips the check with a notice when there is none.
- **commit-msg:** the subject follows Conventional Commits with a type from
  [CONTRIBUTING.md](../CONTRIBUTING.md) (at most 72 characters); no session
  trailers or claude.ai links.
- **pre-push:** `tools/git/push-plan.mjs` decides from the checkout's role and
  the remote's URL (next section). Every push it allows checks that `main`
  keeps a linear history: branches are combined by rebasing onto the target
  and fast-forwarding, never with a merge commit, so the hook refuses merge
  commits in what a push adds (merge commits already on the remote stay).

The tests commit and push in temporary repositories through the real hooks
and run `cmake/Checkout.cmake` there (those tests are skipped without `cmake`
on PATH, the formatting tests without clang-format):

```
node --test "tools/git/*.test.mjs"
```

The full check runs the MSVC build with warnings as errors, these tests and
the UI preview; it finds `cmake` on PATH or in Visual Studio:

```
node tools/check.mjs
```

## Master and work folders

Optional, for a maintainer: one **master** checkout is the only one that talks
to GitHub, and the work happens in **work folders** cloned from it. A plain
clone (no master, e.g. a fork) needs none of this; its pushes get the
linear-history check only.

- **Master:** marked by the local git config `monitone.role=master`. It
  pushes only `main` and release tags `v<X.Y.Z>`, only to a network remote
  (by convention `github`), and runs only checks that build nothing (linear
  history): `main` only moves to commits that passed the full check in a work
  folder. It builds no development builds (`cmake/Checkout.cmake` refuses);
  releases are built from it by `tools\build-release.ps1`, which exports
  `HEAD` into a fresh folder outside the checkout (default
  `%LOCALAPPDATA%\Monitone-release`) and builds there, so every release starts
  clean and nothing is built inside the master. `tools\publish-release.ps1`
  commits the release there and pushes `main` and the tag to `github`.
- **Work folders:** `origin` is the master (a local path), branch `WIN<n>`.
  They push only to the master, and their pre-push hook runs the full check
  (`tools/check.mjs`) first. A push to a network remote is refused with the
  command to use instead. Both release scripts refuse to run in a work folder.
- **Every other combination is refused** (the master pushing to a local path
  or another ref, a work folder pushing to the network).

Publishing a change from work folder `n`:

```
git push origin WIN<n>
git -C <master> merge --ff-only WIN<n>
git -C <master> push github main
```

To update a work folder afterwards: `git fetch origin && git rebase origin/main`.

Setup, run from an existing work folder (its `origin` is the master):

```
node tools/git/work-folders.mjs guard-master   # mark origin as the master, turn its hooks on
node tools/git/work-folders.mjs create <n>     # clone the master into ../Monitone.win<n> on WIN<n>
```

Why: the GitHub remote and its credentials live in one checkout that is
never reset or rebuilt, what may go public is decided in one tested function,
and the master (often in a synced folder) stays free of build output. Rejected:
publishing from a work folder (any folder could push anywhere) and repeating
the full check in the master (it would build there; `main` only receives
checked commits).
