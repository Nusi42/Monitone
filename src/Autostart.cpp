#include "Autostart.h"

#include <format>
#include <string>
#include <string_view>

#include "Win32.h"

namespace monitone {
namespace {

constexpr std::wstring_view kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr std::wstring_view kValueName = L"Monitone";

std::wstring ExecutablePath() {
  std::wstring path(MAX_PATH, L'\0');
  for (;;) {
    const DWORD length = ::GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length < path.size()) {
      path.resize(length);
      return path;
    }
    path.resize(path.size() * 2);
  }
}

}  // namespace

bool IsAutostartEnabled() {
  return ::RegGetValueW(HKEY_CURRENT_USER, kRunKey.data(), kValueName.data(), RRF_RT_REG_SZ, nullptr, nullptr,
                        nullptr) == ERROR_SUCCESS;
}

void SetAutostart(bool enable) {
  UniqueRegKey key;
  if (::RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey.data(), 0, KEY_SET_VALUE, key.Put()) != ERROR_SUCCESS) return;

  if (enable) {
    const std::wstring command = std::format(L"\"{}\"", ExecutablePath());
    ::RegSetValueExW(key.Get(), kValueName.data(), 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                     static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
  } else {
    ::RegDeleteValueW(key.Get(), kValueName.data());
  }
}

}  // namespace monitone
