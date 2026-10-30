#pragma once

#include <windows.h>

#include <atomic>
#include <string>
#include <vector>

#include "Engine.h"
#include "SettingsApi.h"

namespace monitone {

// What the settings UI needs from the running application. Called on the
// thread that owns the host window.
class SettingsHost {
 public:
  [[nodiscard]] virtual const std::wstring& ConfigPath() const = 0;
  virtual void ReloadConfig() = 0;
  [[nodiscard]] virtual std::vector<RouteReport> RefreshReport() = 0;
  virtual void ResetWindowsAppDevices() = 0;

 protected:
  SettingsHost() = default;
  ~SettingsHost() = default;
  SettingsHost(const SettingsHost&) = default;
  SettingsHost& operator=(const SettingsHost&) = default;
};

// Loads MonitoneSettings.dll only while the settings window is open and
// unloads it afterwards, so an idle Monitone carries no UI code or WebView2.
class SettingsModule {
 public:
  SettingsModule(HWND window, SettingsHost& host) noexcept;
  ~SettingsModule();
  SettingsModule(const SettingsModule&) = delete;
  SettingsModule& operator=(const SettingsModule&) = delete;

  // Opens the window, or activates it if it is open already.
  void Open(MonitoneSettingsPage page);
  [[nodiscard]] bool IsOpen() const noexcept;

  // Window message handlers, called by the host window procedure.
  LRESULT OnCall(WPARAM operation, LPARAM argument);
  void OnClosed();

 private:
  enum class Operation : WPARAM { ConfigChanged = 1, QueryReport, ResetWindowsAppDevices };

  struct ReportCall {
    MonitoneReportSink sink = nullptr;
    void* sinkContext = nullptr;
  };

  // C callbacks handed to the DLL; they run on its UI thread.
  static void ConfigChangedThunk(void* context);
  static void QueryReportThunk(void* context, MonitoneReportSink sink, void* sinkContext);
  static void ResetWindowsAppDevicesThunk(void* context);
  static void ClosedThunk(void* context);

  [[nodiscard]] bool Load();
  void Unload();

  HWND window_ = nullptr;
  SettingsHost& host_;
  HMODULE module_ = nullptr;
  MonitoneSettingsOpenFn open_ = nullptr;
  MonitoneSettingsActivateFn activate_ = nullptr;
  MonitoneSettingsShutdownFn shutdown_ = nullptr;
  std::atomic<bool> closed_{false};
  std::wstring version_;
};

}  // namespace monitone
