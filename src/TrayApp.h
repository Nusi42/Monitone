#pragma once

#include <windows.h>

#include <shellapi.h>

#include <memory>
#include <unordered_map>
#include <vector>

#include "AudioRouter.h"
#include "Config.h"
#include "Engine.h"
#include "Monitors.h"
#include "SettingsModule.h"
#include "Win32.h"

namespace monitone {

// Notification area UI, window event tracking and the message loop.
class TrayApp final : private SettingsHost {
 public:
  TrayApp() = default;
  ~TrayApp();
  TrayApp(const TrayApp&) = delete;
  TrayApp& operator=(const TrayApp&) = delete;

  [[nodiscard]] int Run(HINSTANCE instance);

 private:
  static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
  static void CALLBACK WinEventProc(HWINEVENTHOOK hook, DWORD event, HWND window, LONG objectId, LONG childId,
                                    DWORD thread, DWORD time);

  [[nodiscard]] bool CreateMainWindow(HINSTANCE instance);
  void InstallHooks();
  LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
  void OnWindowEvent(DWORD event, HWND window);
  void ScheduleTick();
  void Tick();
  void StartPollTimer();

  [[nodiscard]] NOTIFYICONDATAW NotifyData() const noexcept;
  void AddTrayIcon();
  void RemoveTrayIcon();
  void UpdateTrayIcon();

  void ShowMenu();
  void HandleCommand(UINT id);
  void OpenSettings(MonitoneSettingsPage page);
  void Shutdown();

  // SettingsHost
  [[nodiscard]] const std::wstring& ConfigPath() const override { return config_.Path(); }
  void ReloadConfig() override;
  [[nodiscard]] std::vector<RouteReport> RefreshReport() override;
  void ResetWindowsAppDevices() override;

  // WinEvent callbacks carry no context pointer; a named mutex in main()
  // guarantees a single instance per session.
  static TrayApp* hookTarget_;

  Config config_;
  MonitorList monitors_;
  UniqueWindow window_;
  UniqueIcon iconActive_;
  UniqueIcon iconPaused_;
  std::unique_ptr<AudioRouter> audio_;
  std::unique_ptr<Engine> engine_;
  std::vector<UniqueWinEventHook> hooks_;
  std::unique_ptr<SettingsModule> settings_;

  HINSTANCE instance_ = nullptr;
  UINT taskbarCreatedMessage_ = 0;
  bool trayIconAdded_ = false;
  bool throttlePending_ = false;
  bool shutDown_ = false;
  std::unordered_map<HWND, HMONITOR> lastMonitor_;

  // Snapshot taken when the menu opens, used to decode monitor commands.
  std::vector<MonitorInfo> menuMonitors_;
  std::vector<AudioDevice> menuDevices_;
};

}  // namespace monitone
