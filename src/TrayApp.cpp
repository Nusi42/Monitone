#include "TrayApp.h"

#include <commctrl.h>
#include <shellapi.h>

#include <array>
#include <format>
#include <optional>
#include <string>

#include "Autostart.h"
#include "resource.h"
#include "version.h"

namespace monitone {
namespace {

constexpr wchar_t kWindowClass[] = L"Monitone.Tray";
constexpr wchar_t kAppName[] = L"Monitone";
constexpr UINT kTrayIconId = 1;

constexpr UINT_PTR kTimerPoll = 1;
constexpr UINT_PTR kTimerThrottle = 2;
constexpr UINT kThrottleMs = 80;
constexpr size_t kMaxTrackedWindows = 1024;

namespace command {
constexpr UINT kSettings = 1;
constexpr UINT kApplications = 2;
constexpr UINT kBalanceOff = 10;
constexpr UINT kBalanceMonitor = 11;
constexpr UINT kBalanceWindow = 12;
constexpr UINT kPause = 20;
constexpr UINT kAutostart = 21;
constexpr UINT kExit = 40;

// Monitor commands: kMonitorBase + monitor * kMonitorStride + slot, where
// slot 0 means "do not route" and slot n selects device n - 1.
constexpr UINT kMonitorBase = 1000;
constexpr UINT kMonitorStride = 100;
constexpr size_t kMaxMonitors = 9;
constexpr size_t kMaxDevices = kMonitorStride - 1;

struct MonitorCommand {
  size_t monitor = 0;
  std::optional<size_t> device;
};

constexpr UINT EncodeMonitor(size_t monitor, std::optional<size_t> device) noexcept {
  return kMonitorBase + static_cast<UINT>(monitor) * kMonitorStride + (device ? static_cast<UINT>(*device) + 1 : 0);
}

constexpr std::optional<MonitorCommand> DecodeMonitor(UINT id) noexcept {
  if (id < kMonitorBase) return std::nullopt;
  const UINT slot = (id - kMonitorBase) % kMonitorStride;
  return MonitorCommand{.monitor = (id - kMonitorBase) / kMonitorStride,
                        .device = slot == 0 ? std::nullopt : std::optional<size_t>{slot - 1}};
}

static_assert(DecodeMonitor(EncodeMonitor(3, 7))->monitor == 3);
static_assert(*DecodeMonitor(EncodeMonitor(3, 7))->device == 7);
static_assert(!DecodeMonitor(EncodeMonitor(2, std::nullopt))->device);
}  // namespace command

// Picks the hand-tuned small rendition from the .ico for the current DPI.
UniqueIcon LoadTrayIcon(HINSTANCE instance, int id) {
  UniqueIcon icon;
  if (FAILED(::LoadIconMetric(instance, MAKEINTRESOURCEW(id), LIM_SMALL, icon.Put()))) return {};
  return icon;
}

UINT Checked(bool checked) noexcept { return checked ? MF_CHECKED : MF_UNCHECKED; }

void AppendItem(HMENU menu, UINT id, const std::wstring& text, bool checked = false) {
  ::AppendMenuW(menu, MF_STRING | Checked(checked), id, text.c_str());
}

void AppendSubmenu(HMENU menu, UniqueMenu submenu, const std::wstring& text) {
  // The parent menu takes ownership of the submenu.
  ::AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(submenu.Release()), text.c_str());
}

void AppendSeparator(HMENU menu) { ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr); }

}  // namespace

TrayApp* TrayApp::hookTarget_ = nullptr;

TrayApp::~TrayApp() { Shutdown(); }

int TrayApp::Run(HINSTANCE instance) {
  instance_ = instance;
  if (!CreateMainWindow(instance)) return 1;

  config_.Load();
  monitors_.Refresh();
  audio_ = std::make_unique<AudioRouter>(window_.Get());
  if (const std::wstring error = audio_->Initialize(); !error.empty()) {
    ::MessageBoxW(nullptr, error.c_str(), kAppName, MB_ICONERROR);
    return 1;
  }
  engine_ = std::make_unique<Engine>(config_, *audio_, monitors_);

  iconActive_ = LoadTrayIcon(instance, IDI_APP);
  iconPaused_ = LoadTrayIcon(instance, IDI_PAUSED);
  taskbarCreatedMessage_ = ::RegisterWindowMessageW(L"TaskbarCreated");
  AddTrayIcon();

  settings_ = std::make_unique<SettingsModule>(window_.Get(), static_cast<SettingsHost&>(*this));
  InstallHooks();
  StartPollTimer();
  Tick();

  // First start: show the settings so the user can choose speakers per display.
  if (!config_.welcomed) {
    config_.welcomed = true;
    config_.Save();
    OpenSettings(MONITONE_PAGE_DISPLAYS);
  }

  MSG message{};
  while (::GetMessageW(&message, nullptr, 0, 0) > 0) {
    ::TranslateMessage(&message);
    ::DispatchMessageW(&message);
  }
  Shutdown();
  return 0;
}

bool TrayApp::CreateMainWindow(HINSTANCE instance) {
  const WNDCLASSEXW windowClass{
      .cbSize = sizeof(WNDCLASSEXW), .lpfnWndProc = WindowProc, .hInstance = instance, .lpszClassName = kWindowClass};
  if (::RegisterClassExW(&windowClass) == 0) return false;

  // Hidden top-level window rather than message-only, so it receives the
  // TaskbarCreated broadcast after an Explorer restart.
  window_.Reset(
      ::CreateWindowExW(0, kWindowClass, kAppName, WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, instance, this));
  return window_.IsValid();
}

void TrayApp::InstallHooks() {
  hookTarget_ = this;
  constexpr std::array kEvents{EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_MOVESIZEEND, EVENT_SYSTEM_MINIMIZEEND,
                               EVENT_OBJECT_SHOW, EVENT_OBJECT_LOCATIONCHANGE};
  for (const DWORD event : kEvents) {
    UniqueWinEventHook hook{
        ::SetWinEventHook(event, event, nullptr, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS)};
    if (hook) hooks_.push_back(std::move(hook));
  }
}

void TrayApp::Shutdown() {
  if (shutDown_) return;
  shutDown_ = true;

  hooks_.clear();
  hookTarget_ = nullptr;
  if (window_) {
    ::KillTimer(window_.Get(), kTimerPoll);
    ::KillTimer(window_.Get(), kTimerThrottle);
  }
  settings_.reset();  // closes the window and unloads the DLL
  RemoveTrayIcon();
  if (engine_ && config_.restoreOnExit) engine_->Restore();
  engine_.reset();
  audio_.reset();
  window_.Reset();
}

LRESULT CALLBACK TrayApp::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  if (message == WM_NCCREATE) {
    const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
    ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
  } else if (message == WM_NCDESTROY) {
    ::SetWindowLongPtrW(window, GWLP_USERDATA, 0);
  }
  auto* self = reinterpret_cast<TrayApp*>(::GetWindowLongPtrW(window, GWLP_USERDATA));
  if (self != nullptr && self->window_.Get() == window) return self->HandleMessage(message, wParam, lParam);
  return ::DefWindowProcW(window, message, wParam, lParam);
}

void CALLBACK TrayApp::WinEventProc(HWINEVENTHOOK, DWORD event, HWND window, LONG objectId, LONG childId, DWORD,
                                    DWORD) {
  if (hookTarget_ == nullptr || window == nullptr || objectId != OBJID_WINDOW || childId != CHILDID_SELF) return;
  if (::GetAncestor(window, GA_ROOT) != window) return;
  hookTarget_->OnWindowEvent(event, window);
}

void TrayApp::OnWindowEvent(DWORD event, HWND window) {
  if (config_.paused) return;
  if (event == EVENT_OBJECT_LOCATIONCHANGE && config_.balance != BalanceMode::Window) {
    // Moves within one display change nothing; only react when the display changes.
    const HMONITOR monitor = ::MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    if (lastMonitor_.size() > kMaxTrackedWindows) lastMonitor_.clear();
    const auto [it, inserted] = lastMonitor_.try_emplace(window, monitor);
    if (!inserted && it->second == monitor) return;
    it->second = monitor;
  }
  ScheduleTick();
}

void TrayApp::ScheduleTick() {
  if (throttlePending_) return;
  throttlePending_ = true;
  ::SetTimer(window_.Get(), kTimerThrottle, kThrottleMs, nullptr);
}

void TrayApp::Tick() {
  if (!engine_) return;
  engine_->Tick();
  UpdateTrayIcon();
}

void TrayApp::StartPollTimer() { ::SetTimer(window_.Get(), kTimerPoll, config_.pollMs, nullptr); }

LRESULT TrayApp::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_TIMER:
      if (wParam == kTimerThrottle) {
        ::KillTimer(window_.Get(), kTimerThrottle);
        throttlePending_ = false;
      }
      Tick();
      return 0;

    case kMsgAudioSessionsChanged:
      ScheduleTick();
      return 0;

    case kMsgAudioDevicesChanged: {
      // One hardware change produces several notifications; handle them once.
      MSG pending{};
      while (::PeekMessageW(&pending, window_.Get(), kMsgAudioDevicesChanged, kMsgAudioDevicesChanged, PM_REMOVE)) {
      }
      if (audio_) audio_->RefreshDevices();
      ScheduleTick();
      return 0;
    }

    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE:
      monitors_.Refresh();
      lastMonitor_.clear();
      ScheduleTick();
      return 0;

    case WM_ENDSESSION:
      if (wParam != FALSE) Shutdown();
      return 0;

    case kMsgSettingsCall:
      return settings_ ? settings_->OnCall(wParam, lParam) : 0;

    case kMsgSettingsClosed:
      if (settings_) settings_->OnClosed();
      return 0;

    case kMsgTray:
      switch (LOWORD(lParam)) {
        case WM_CONTEXTMENU:
        case NIN_SELECT:
        case NIN_KEYSELECT:
          // While the settings dialog is open it is the place to change things.
          if (settings_ && settings_->IsOpen())
            settings_->Open(MONITONE_PAGE_DISPLAYS);
          else
            ShowMenu();
          break;
        case WM_LBUTTONDBLCLK:
          OpenSettings(MONITONE_PAGE_DISPLAYS);
          break;
        default:
          break;
      }
      return 0;

    default:
      break;
  }
  if (taskbarCreatedMessage_ != 0 && message == taskbarCreatedMessage_) {
    trayIconAdded_ = false;  // Explorer restarted and forgot the icon
    AddTrayIcon();
    return 0;
  }
  return ::DefWindowProcW(window_.Get(), message, wParam, lParam);
}

NOTIFYICONDATAW TrayApp::NotifyData() const noexcept {
  NOTIFYICONDATAW data{};
  data.cbSize = sizeof(data);
  data.hWnd = window_.Get();
  data.uID = kTrayIconId;
  return data;
}

void TrayApp::AddTrayIcon() {
  NOTIFYICONDATAW data = NotifyData();
  data.uFlags = NIF_MESSAGE;
  data.uCallbackMessage = kMsgTray;
  trayIconAdded_ = ::Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
  data.uVersion = NOTIFYICON_VERSION_4;
  ::Shell_NotifyIconW(NIM_SETVERSION, &data);
  UpdateTrayIcon();
}

void TrayApp::RemoveTrayIcon() {
  if (!trayIconAdded_) return;
  NOTIFYICONDATAW data = NotifyData();
  ::Shell_NotifyIconW(NIM_DELETE, &data);
  trayIconAdded_ = false;
}

void TrayApp::UpdateTrayIcon() {
  if (!trayIconAdded_) return;
  NOTIFYICONDATAW data = NotifyData();
  data.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP;
  data.hIcon = config_.paused ? iconPaused_.Get() : iconActive_.Get();
  const std::wstring tip =
      config_.paused ? std::format(L"{}: paused", kAppName)
                     : std::format(L"{}: {} app(s) routed", kAppName, engine_ ? engine_->RoutedCount() : size_t{0});
  ::wcsncpy_s(data.szTip, tip.c_str(), _TRUNCATE);
  ::Shell_NotifyIconW(NIM_MODIFY, &data);
}

void TrayApp::ShowMenu() {
  monitors_.Refresh();
  menuMonitors_ = monitors_.All();
  menuDevices_ = audio_->Devices();

  const UniqueMenu menu{::CreatePopupMenu()};
  if (!menu) return;
  AppendItem(menu.Get(), command::kSettings, L"&Settings...");
  AppendItem(menu.Get(), command::kApplications, L"&Applications...");
  ::SetMenuDefaultItem(menu.Get(), command::kSettings, FALSE);
  AppendSeparator(menu.Get());

  for (size_t m = 0; m < menuMonitors_.size() && m < command::kMaxMonitors; ++m) {
    const MonitorInfo& monitor = menuMonitors_[m];
    const MonitorRule* rule = config_.Find(monitor);
    const std::wstring assigned = rule != nullptr ? rule->deviceId : std::wstring{};

    UniqueMenu devices{::CreatePopupMenu()};
    AppendItem(devices.Get(), command::EncodeMonitor(m, std::nullopt), L"Do not route (system default)",
               assigned.empty());
    AppendSeparator(devices.Get());
    for (size_t d = 0; d < menuDevices_.size() && d < command::kMaxDevices; ++d) {
      AppendItem(devices.Get(), command::EncodeMonitor(m, d), menuDevices_[d].name,
                 EqualsNoCase(assigned, menuDevices_[d].id));
    }

    std::wstring label = monitors_.Label(monitor);
    if (!assigned.empty()) {
      const std::wstring name = audio_->DeviceName(assigned);
      label += std::format(L"  ->  {}", name.empty() ? L"(device not connected)" : name);
    }
    AppendSubmenu(menu.Get(), std::move(devices), label);
  }

  AppendSeparator(menu.Get());
  UniqueMenu balance{::CreatePopupMenu()};
  AppendItem(balance.Get(), command::kBalanceOff, L"Off", config_.balance == BalanceMode::Off);
  AppendItem(balance.Get(), command::kBalanceMonitor,
             L"By display (leftmost = left speaker, rightmost = right speaker)",
             config_.balance == BalanceMode::Monitor);
  AppendItem(balance.Get(), command::kBalanceWindow, L"By window position (continuous panning)",
             config_.balance == BalanceMode::Window);
  AppendSubmenu(menu.Get(), std::move(balance), L"Left/right balance on one speaker set");

  AppendSeparator(menu.Get());
  AppendItem(menu.Get(), command::kPause, L"Pause", config_.paused);
  AppendItem(menu.Get(), command::kAutostart, L"Start with Windows", IsAutostartEnabled());
  AppendSeparator(menu.Get());
  AppendItem(menu.Get(), command::kExit, L"Exit");

  POINT cursor{};
  ::GetCursorPos(&cursor);
  ::SetForegroundWindow(window_.Get());  // otherwise the menu does not close on outside clicks
  const auto id = static_cast<UINT>(::TrackPopupMenuEx(menu.Get(), TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                                                       cursor.x, cursor.y, window_.Get(), nullptr));
  ::PostMessageW(window_.Get(), WM_NULL, 0, 0);
  if (id != 0) HandleCommand(id);
}

void TrayApp::HandleCommand(UINT id) {
  if (const auto monitorCommand = command::DecodeMonitor(id)) {
    if (monitorCommand->monitor >= menuMonitors_.size()) return;
    if (monitorCommand->device && *monitorCommand->device >= menuDevices_.size()) return;
    config_.SetDevice(menuMonitors_[monitorCommand->monitor],
                      monitorCommand->device ? menuDevices_[*monitorCommand->device].id : std::wstring{});
    config_.Save();
    Tick();
    return;
  }

  switch (id) {
    case command::kSettings:
      OpenSettings(MONITONE_PAGE_DISPLAYS);
      break;
    case command::kApplications:
      OpenSettings(MONITONE_PAGE_APPLICATIONS);
      break;
    case command::kBalanceOff:
    case command::kBalanceMonitor:
    case command::kBalanceWindow:
      config_.balance = id == command::kBalanceMonitor  ? BalanceMode::Monitor
                        : id == command::kBalanceWindow ? BalanceMode::Window
                                                        : BalanceMode::Off;
      config_.Save();
      Tick();
      break;
    case command::kPause:
      config_.paused = !config_.paused;
      config_.Save();
      Tick();
      UpdateTrayIcon();
      break;
    case command::kAutostart:
      SetAutostart(!IsAutostartEnabled());
      break;
    case command::kExit:
      ::PostQuitMessage(0);
      break;
    default:
      break;
  }
}

void TrayApp::OpenSettings(MonitoneSettingsPage page) {
  if (settings_) settings_->Open(page);
}

void TrayApp::ReloadConfig() {
  config_.Load();
  StartPollTimer();
  lastMonitor_.clear();
  Tick();
}

std::vector<RouteReport> TrayApp::RefreshReport() {
  Tick();
  return engine_ ? engine_->LastReport() : std::vector<RouteReport>{};
}

void TrayApp::ResetWindowsAppDevices() {
  if (!engine_ || !audio_) return;
  engine_->Restore();
  (void)audio_->Policy().ClearAll();
  Tick();
}

}  // namespace monitone
