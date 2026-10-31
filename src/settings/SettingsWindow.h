#pragma once

#include <windows.h>

#include <objbase.h>

#include <atomic>
#include <memory>

#include "WebView2.h"
#include "Win32.h"
#include "settings/Bridge.h"

namespace monitone::settings {

// Top-level settings window hosting the WebView2 UI. Lives on the UI thread.
class SettingsWindow {
 public:
  SettingsWindow(HINSTANCE module, const HostInfo& host, int page);
  ~SettingsWindow();
  SettingsWindow(const SettingsWindow&) = delete;
  SettingsWindow& operator=(const SettingsWindow&) = delete;

  // Creates and shows the window and starts creating the WebView.
  [[nodiscard]] bool Create();
  [[nodiscard]] HWND Handle() const noexcept { return window_; }

  static void UnregisterWindowClass(HINSTANCE module);

  // Messages for the window, posted by the exported functions.
  static constexpr UINT kMsgActivate = WM_APP + 1;  // wParam: page

 private:
  static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
  LRESULT HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

  void ApplyFrameStyle();
  void CreateWebView();
  HRESULT OnEnvironmentCreated(HRESULT result, ICoreWebView2Environment* environment);
  HRESULT OnControllerCreated(HRESULT result, ICoreWebView2Controller* controller);
  HRESULT OnWebMessage(ICoreWebView2WebMessageReceivedEventArgs* args);
  HRESULT OnWebResourceRequested(ICoreWebView2WebResourceRequestedEventArgs* args);
  void Post(const nlohmann::json& message);
  void Navigate(int page);
  void ResizeWebView();
  void ReleaseWebView();
  void Fail(HRESULT result);

  HINSTANCE module_ = nullptr;
  HWND window_ = nullptr;
  int startPage_ = 0;
  bool backdrop_ = false;
  Bridge bridge_;
  UniqueIcon iconLarge_;
  UniqueIcon iconSmall_;

  ComPtr<ICoreWebView2Environment> environment_;
  ComPtr<ICoreWebView2Controller> controller_;
  ComPtr<ICoreWebView2> webview_;
  EventRegistrationToken messageToken_{};
  EventRegistrationToken resourceToken_{};
  bool uiReady_ = false;
};

}  // namespace monitone::settings
