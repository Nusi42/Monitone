#include "Config.h"

#include <algorithm>
#include <array>
#include <format>
#include <ranges>
#include <utility>

#include "Win32.h"

namespace monitone {
namespace {

constexpr std::wstring_view kGeneral = L"General";
constexpr std::wstring_view kMonitors = L"Monitors";
constexpr std::wstring_view kApps = L"Apps";

// Win32 private profile API bound to one file.
class IniFile {
 public:
  explicit IniFile(const std::wstring& path) noexcept : path_{path} {}

  [[nodiscard]] std::wstring Read(std::wstring_view section, std::wstring_view key,
                                  std::wstring_view fallback = {}) const {
    std::array<wchar_t, 4096> buffer{};
    const DWORD length = ::GetPrivateProfileStringW(std::wstring{section}.c_str(), std::wstring{key}.c_str(),
                                                    std::wstring{fallback}.c_str(), buffer.data(),
                                                    static_cast<DWORD>(buffer.size()), path_.c_str());
    return std::wstring(buffer.data(), length);
  }

  [[nodiscard]] UINT ReadUInt(std::wstring_view section, std::wstring_view key, UINT fallback) const {
    return ::GetPrivateProfileIntW(std::wstring{section}.c_str(), std::wstring{key}.c_str(), static_cast<INT>(fallback),
                                   path_.c_str());
  }

  void Write(std::wstring_view section, std::wstring_view key, std::wstring_view value) const {
    ::WritePrivateProfileStringW(std::wstring{section}.c_str(), std::wstring{key}.c_str(), std::wstring{value}.c_str(),
                                 path_.c_str());
  }

  void DeleteKey(std::wstring_view section, std::wstring_view key) const {
    ::WritePrivateProfileStringW(std::wstring{section}.c_str(), std::wstring{key}.c_str(), nullptr, path_.c_str());
  }

  void DeleteSection(std::wstring_view section) const {
    ::WritePrivateProfileStringW(std::wstring{section}.c_str(), nullptr, nullptr, path_.c_str());
  }

  void Flush() const { ::WritePrivateProfileStringW(nullptr, nullptr, nullptr, path_.c_str()); }

  // The profile API writes UTF-16 only into files that already start with a BOM.
  void EnsureUnicodeFile() const {
    const UniqueHandle file{
        ::CreateFileW(path_.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (!file) return;  // exists already
    constexpr std::array<BYTE, 2> kUtf16LeBom{0xFF, 0xFE};
    DWORD written = 0;
    ::WriteFile(file.Get(), kUtf16LeBom.data(), static_cast<DWORD>(kUtf16LeBom.size()), &written, nullptr);
  }

 private:
  const std::wstring& path_;
};

std::wstring MonitorSection(size_t index) { return std::format(L"Monitor{}", index); }
std::wstring AppSection(size_t index) { return std::format(L"App{}", index); }

std::wstring_view Trim(std::wstring_view text) noexcept {
  constexpr std::wstring_view kBlank = L" \t";
  const size_t first = text.find_first_not_of(kBlank);
  if (first == std::wstring_view::npos) return {};
  return text.substr(first, text.find_last_not_of(kBlank) - first + 1);
}

std::wstring_view BalanceName(BalanceMode mode) noexcept {
  switch (mode) {
    case BalanceMode::Monitor:
      return L"monitor";
    case BalanceMode::Window:
      return L"window";
    case BalanceMode::Off:
      break;
  }
  return L"off";
}

BalanceMode ParseBalance(std::wstring_view name) noexcept {
  if (EqualsNoCase(name, L"monitor")) return BalanceMode::Monitor;
  if (EqualsNoCase(name, L"window")) return BalanceMode::Window;
  return BalanceMode::Off;
}

std::wstring_view ActionName(AppAction action) noexcept {
  switch (action) {
    case AppAction::Exclude:
      return L"exclude";
    case AppAction::Pin:
      return L"pin";
    case AppAction::Follow:
      break;
  }
  return L"follow";
}

AppAction ParseAction(std::wstring_view name) noexcept {
  if (EqualsNoCase(name, L"exclude")) return AppAction::Exclude;
  if (EqualsNoCase(name, L"pin")) return AppAction::Pin;
  return AppAction::Follow;
}

std::wstring ConfigDirectory() {
  std::array<wchar_t, MAX_PATH> buffer{};
  if (const DWORD length = ::GetEnvironmentVariableW(L"APPDATA", buffer.data(), static_cast<DWORD>(buffer.size()));
      length > 0 && length < buffer.size()) {
    std::wstring directory = std::wstring(buffer.data(), length) + L"\\Monitone";
    ::CreateDirectoryW(directory.c_str(), nullptr);
    return directory;
  }
  // Portable fallback: next to the executable.
  const DWORD length = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  std::wstring directory(buffer.data(), length);
  directory.erase(std::min(directory.find_last_of(L'\\'), directory.size()));
  return directory;
}

template <typename Rules>
auto FindRule(Rules& rules, const MonitorInfo& monitor) noexcept {
  if (!monitor.key.empty()) {
    const auto byKey =
        std::ranges::find_if(rules, [&](const MonitorRule& rule) { return EqualsNoCase(rule.key, monitor.key); });
    if (byKey != rules.end()) return byKey;
  }
  return std::ranges::find_if(
      rules, [&](const MonitorRule& rule) { return rule.key.empty() && EqualsNoCase(rule.gdiName, monitor.gdiName); });
}

}  // namespace

Config::Config() : path_{ConfigDirectory() + L"\\Monitone.ini"} {}

Config::Config(std::wstring path) : path_{std::move(path)} {}

void Config::Load() {
  const IniFile ini{path_};
  paused = ini.ReadUInt(kGeneral, L"Paused", 0) != 0;
  restoreOnExit = ini.ReadUInt(kGeneral, L"RestoreOnExit", 1) != 0;
  pollMs = std::clamp(ini.ReadUInt(kGeneral, L"PollMs", kDefaultPollMs), kMinPollMs, kMaxPollMs);
  balance = ParseBalance(ini.Read(kGeneral, L"Balance", L"off"));

  rules.clear();
  savedRuleCount_ = ini.ReadUInt(kMonitors, L"Count", 0);
  for (size_t i = 0; i < savedRuleCount_; ++i) {
    const std::wstring section = MonitorSection(i);
    MonitorRule rule{.key = ini.Read(section, L"Key"),
                     .gdiName = ini.Read(section, L"Gdi"),
                     .name = ini.Read(section, L"Name"),
                     .deviceId = ini.Read(section, L"Device")};
    if (!rule.key.empty() || !rule.gdiName.empty()) rules.push_back(std::move(rule));
  }

  appRules.clear();
  savedAppRuleCount_ = ini.ReadUInt(kApps, L"Count", 0);
  for (size_t i = 0; i < savedAppRuleCount_; ++i) {
    const std::wstring section = AppSection(i);
    SetAppRule(AppRule{.exe = ini.Read(section, L"Exe"),
                       .action = ParseAction(ini.Read(section, L"Action")),
                       .deviceId = ini.Read(section, L"Device")});
  }
}

void Config::Save() {
  const IniFile ini{path_};
  ini.EnsureUnicodeFile();

  ini.Write(kGeneral, L"Paused", paused ? L"1" : L"0");
  ini.Write(kGeneral, L"RestoreOnExit", restoreOnExit ? L"1" : L"0");
  ini.Write(kGeneral, L"Balance", BalanceName(balance));
  ini.Write(kGeneral, L"PollMs", std::to_wstring(pollMs));
  ini.DeleteKey(kGeneral, L"Exclude");

  for (size_t i = 0; i < savedRuleCount_; ++i) ini.DeleteSection(MonitorSection(i));
  ini.Write(kMonitors, L"Count", std::to_wstring(rules.size()));
  for (size_t i = 0; i < rules.size(); ++i) {
    const std::wstring section = MonitorSection(i);
    ini.Write(section, L"Key", rules[i].key);
    ini.Write(section, L"Gdi", rules[i].gdiName);
    ini.Write(section, L"Name", rules[i].name);
    ini.Write(section, L"Device", rules[i].deviceId);
  }
  savedRuleCount_ = rules.size();

  for (size_t i = 0; i < savedAppRuleCount_; ++i) ini.DeleteSection(AppSection(i));
  ini.Write(kApps, L"Count", std::to_wstring(appRules.size()));
  for (size_t i = 0; i < appRules.size(); ++i) {
    const std::wstring section = AppSection(i);
    ini.Write(section, L"Exe", appRules[i].exe);
    ini.Write(section, L"Action", ActionName(appRules[i].action));
    ini.Write(section, L"Device", appRules[i].deviceId);
  }
  savedAppRuleCount_ = appRules.size();
  ini.Flush();
}

const MonitorRule* Config::Find(const MonitorInfo& monitor) const noexcept {
  const auto it = FindRule(rules, monitor);
  return it != rules.end() ? &*it : nullptr;
}

void Config::SetDevice(const MonitorInfo& monitor, std::wstring_view deviceId) {
  auto it = FindRule(rules, monitor);
  if (it == rules.end()) {
    if (deviceId.empty()) return;
    it = rules.insert(rules.end(), MonitorRule{});
  }
  *it = MonitorRule{
      .key = monitor.key, .gdiName = monitor.gdiName, .name = monitor.friendlyName, .deviceId = std::wstring{deviceId}};
}

const AppRule* Config::FindAppRule(std::wstring_view exe) const noexcept {
  const auto it = std::ranges::find_if(appRules, [exe](const AppRule& rule) { return EqualsNoCase(rule.exe, exe); });
  return it != appRules.end() ? &*it : nullptr;
}

void Config::SetAppRule(AppRule rule) {
  rule.exe = ToLower(Trim(rule.exe));
  if (rule.exe.empty()) return;
  if (rule.action != AppAction::Pin) rule.deviceId.clear();
  if (rule.action == AppAction::Pin && rule.deviceId.empty()) rule.action = AppAction::Follow;

  RemoveAppRule(rule.exe);
  if (rule.action != AppAction::Follow) appRules.push_back(std::move(rule));
}

void Config::RemoveAppRule(std::wstring_view exe) {
  std::erase_if(appRules, [exe](const AppRule& rule) { return EqualsNoCase(rule.exe, exe); });
}

}  // namespace monitone
