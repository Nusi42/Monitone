#pragma once

#include <windows.h>

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "AudioRouter.h"
#include "Balance.h"
#include "Config.h"
#include "Monitors.h"
#include "WindowMap.h"

namespace monitone {

struct RouteReport {
  DWORD pid = 0;
  std::wstring exe;
  std::wstring exePath;
  std::wstring windowTitle;
  std::wstring monitor;
  std::wstring device;
  bool playing = false;
  std::optional<StereoGain> balance;
  std::wstring note;
};

// One evaluation pass: for every audio session find the window that
// represents it, the display that window is on, and apply the matching rule
// (per-app rule first, then the display's output device and balance).
// While paused the pass only reports and changes nothing.
class Engine {
 public:
  Engine(const Config& config, const AudioRouter& audio, MonitorList& monitors) noexcept;

  void Tick();
  void Restore();  // undo every change made so far

  [[nodiscard]] const std::vector<RouteReport>& LastReport() const noexcept { return report_; }
  [[nodiscard]] size_t RoutedCount() const noexcept { return routed_.size(); }

 private:
  struct BalanceState {
    StereoGain gain;
    std::vector<float> original;  // channel volumes before we touched the session
  };

  // The window a process's sound belongs to, kept while that window exists.
  struct WindowLock {
    HWND window = nullptr;
    bool playing = false;
  };

  [[nodiscard]] HWND RepresentativeWindow(DWORD pid, bool playing);
  [[nodiscard]] RouteReport Evaluate(const AudioSession& session, bool firstOfProcess, bool playing, bool apply);
  void ApplyRoute(DWORD pid, std::wstring_view target, RouteReport& report);
  void ApplyBalance(const AudioSession& session, StereoGain gain, RouteReport& report);
  void ResetBalance(const AudioSession& session);
  [[nodiscard]] float PanPosition(HWND window, const MonitorInfo& monitor) const;

  const Config& config_;
  const AudioRouter& audio_;
  MonitorList& monitors_;
  ProcessWindowMap windows_;
  std::unordered_map<DWORD, std::wstring> routed_;           // pid -> endpoint we assigned
  std::unordered_map<std::wstring, BalanceState> balanced_;  // session instance id -> state
  std::unordered_map<DWORD, WindowLock> locks_;              // session pid -> window
  std::vector<RouteReport> report_;
};

}  // namespace monitone
