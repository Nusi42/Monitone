#pragma once

#include <windows.h>

#include <objbase.h>

#include <string>

#include "WebView2.h"

namespace monitone::settings {

// Access to the WebView2 runtime. MSVC builds link the static loader; other
// toolchains load WebView2Loader.dll from the folder of this DLL on first use
// and release it again in UnloadWebView2Loader.

// S_OK if a WebView2 runtime is installed.
[[nodiscard]] HRESULT CheckWebView2Runtime();

[[nodiscard]] HRESULT CreateEnvironment(const std::wstring& userDataFolder,
                                        ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* handler);

void UnloadWebView2Loader();

}  // namespace monitone::settings
