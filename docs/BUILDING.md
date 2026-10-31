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
enables them (`cmake/GitHooks.cmake` sets `core.hooksPath` to `.githooks`; a
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
- **pre-push:** `main` keeps a linear history. Branches are combined by
  rebasing onto the target and fast-forwarding, never with a merge commit;
  the hook refuses merge commits in what a push adds (merge commits already on
  the remote stay).

The tests commit and push in temporary repositories through the real hooks
and run `cmake/GitHooks.cmake` there (those tests are skipped without `cmake`
on PATH, the formatting tests without clang-format):

```
node --test "tools/git/*.test.mjs"
```
