#pragma once

#include <windows.h>

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

namespace monitone::settings {

// Origin under which the embedded UI files are served to WebView2.
inline constexpr std::wstring_view kUiOrigin = L"https://monitone.ui/";

struct WebResource {
  std::span<const std::byte> data;
  std::wstring_view contentType;
};

// Looks up an embedded UI file by URL; the data lives in the module image.
[[nodiscard]] std::optional<WebResource> FindWebResource(HMODULE module, std::wstring_view url);

}  // namespace monitone::settings
