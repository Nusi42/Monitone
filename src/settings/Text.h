#pragma once

#include <windows.h>

#include <string>
#include <string_view>

namespace monitone::settings {

[[nodiscard]] inline std::string ToUtf8(std::wstring_view text) {
  if (text.empty()) return {};
  const int length =
      ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
  std::string result(static_cast<size_t>(length), '\0');
  ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length, nullptr,
                        nullptr);
  return result;
}

[[nodiscard]] inline std::wstring FromUtf8(std::string_view text) {
  if (text.empty()) return {};
  const int length = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring result(static_cast<size_t>(length), L'\0');
  ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length);
  return result;
}

}  // namespace monitone::settings
