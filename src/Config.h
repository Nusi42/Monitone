#pragma once

#include <windows.h>

#include <string>
#include <string_view>
#include <vector>

#include "Monitors.h"

namespace monitone {

// How the left/right channel gains of a session follow its window.
enum class BalanceMode {
  Off,      // untouched
  Monitor,  // leftmost display = left speaker only, rightmost = right only
  Window,   // continuous, by the horizontal window center on the whole desktop
};

// What happens to an application regardless of where its window is.
enum class AppAction {
  Follow,   // default: follow the display of its window (never stored)
  Exclude,  // never touched
  Pin,      // always plays on deviceId
};

struct AppRule {
  std::wstring exe;  // lower-case file name, e.g. "discord.exe"
  AppAction action = AppAction::Follow;
  std::wstring deviceId;  // AppAction::Pin only
};

struct MonitorRule {
  std::wstring key;       // MonitorInfo::key
  std::wstring gdiName;   // fallback match
  std::wstring name;      // informational
  std::wstring deviceId;  // render endpoint; empty = do not route
};

class Config {
 public:
  static constexpr UINT kDefaultPollMs = 2000;
  static constexpr UINT kMinPollMs = 250;
  static constexpr UINT kMaxPollMs = 60000;

  Config();
  explicit Config(std::wstring path);

  void Load();
  void Save();
  [[nodiscard]] const std::wstring& Path() const noexcept { return path_; }

  [[nodiscard]] const MonitorRule* Find(const MonitorInfo& monitor) const noexcept;
  void SetDevice(const MonitorInfo& monitor, std::wstring_view deviceId);

  [[nodiscard]] const AppRule* FindAppRule(std::wstring_view exe) const noexcept;
  // Replaces the rule for rule.exe; AppAction::Follow removes it.
  void SetAppRule(AppRule rule);
  void RemoveAppRule(std::wstring_view exe);

  bool paused = false;
  bool restoreOnExit = true;
  bool welcomed = false;  // settings were shown once after the first start
  BalanceMode balance = BalanceMode::Off;
  UINT pollMs = kDefaultPollMs;
  std::vector<MonitorRule> rules;
  std::vector<AppRule> appRules;

 private:
  std::wstring path_;
  size_t savedRuleCount_ = 0;
  size_t savedAppRuleCount_ = 0;
};

}  // namespace monitone
