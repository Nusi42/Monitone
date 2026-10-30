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
releases are built with MSVC by `tools\release.ps1` (see CONTRIBUTING.md).

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
