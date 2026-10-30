#pragma once

#include <windows.h>

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "Config.h"
#include "Monitors.h"
#include "SettingsApi.h"
#include "TestTone.h"

namespace monitone::settings {

// The host interface with its strings copied into this module.
struct HostInfo {
  MonitoneHost callbacks{};
  std::wstring configPath;
  std::wstring version;
};

// Executes the requests of the web UI. Every change is written to the
// configuration file at once and the host is told to reload it; there is no
// separate apply step. Runs on the UI thread.
class Bridge {
 public:
  using Post = std::function<void(const nlohmann::json&)>;

  Bridge(HINSTANCE module, const HostInfo& host, bool backdrop);

  void SetWindow(HWND window) noexcept { window_ = window; }
  void Handle(const nlohmann::json& message, const Post& post);
  [[nodiscard]] nlohmann::json State();

 private:
  struct Device {
    std::wstring id;
    std::wstring name;
  };

  void Commit();
  [[nodiscard]] nlohmann::json Report();
  [[nodiscard]] const MonitorInfo* Display(const nlohmann::json& message) const;
  [[nodiscard]] std::string IconDataUrl(const std::wstring& exePath);
  [[nodiscard]] std::wstring BrowseExecutable() const;
  void RefreshDevices();

  HINSTANCE module_ = nullptr;
  HWND window_ = nullptr;
  HostInfo host_;
  bool backdrop_ = false;
  Config config_;
  MonitorList monitors_;
  std::vector<Device> devices_;
  std::wstring defaultDeviceId_;
  std::unordered_map<std::wstring, std::string> icons_;
  TonePlayer tone_;
};

}  // namespace monitone::settings
