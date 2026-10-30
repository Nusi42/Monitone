#include "SettingsModule.h"

#include <format>
#include <string_view>
#include <utility>

#include "Shell.h"
#include "Win32.h"
#include "version.h"

namespace monitone {
namespace {

constexpr std::wstring_view kModuleName = L"MonitoneSettings.dll";
constexpr std::wstring_view kRuntimeDownload = L"https://go.microsoft.com/fwlink/p/?LinkId=2124703";

std::wstring ModulePath() {
  std::wstring path(MAX_PATH, L'\0');
  for (;;) {
    const DWORD length = ::GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length < path.size()) {
      path.resize(length);
      break;
    }
    path.resize(path.size() * 2);
  }
  path.erase(path.find_last_of(L'\\') + 1);
  return path.append(kModuleName);
}

template <typename Fn>
Fn Export(HMODULE module, const char* name) {
  return reinterpret_cast<Fn>(reinterpret_cast<void*>(::GetProcAddress(module, name)));
}

SettingsModule& Self(void* context) { return *static_cast<SettingsModule*>(context); }

}  // namespace

SettingsModule::SettingsModule(HWND window, SettingsHost& host) noexcept
    : window_{window}, host_{host}, version_{L"" MONITONE_VERSION_FULL} {}

SettingsModule::~SettingsModule() { Unload(); }

bool SettingsModule::IsOpen() const noexcept { return module_ != nullptr && !closed_; }

void SettingsModule::Open(MonitoneSettingsPage page) {
  if (IsOpen()) {
    activate_(page);
    return;
  }
  if (module_ != nullptr) Unload();  // closed, but the notification is still queued
  if (!Load()) {
    ::MessageBoxW(window_, std::format(L"{} could not be loaded. Reinstall Monitone.", kModuleName).c_str(),
                  L"Monitone", MB_ICONERROR);
    return;
  }

  closed_ = false;
  const MonitoneHost host{.apiVersion = MONITONE_SETTINGS_API_VERSION,
                          .context = this,
                          .configPath = host_.ConfigPath().c_str(),
                          .version = version_.c_str(),
                          .configChanged = ConfigChangedThunk,
                          .queryReport = QueryReportThunk,
                          .resetWindowsAppDevices = ResetWindowsAppDevicesThunk,
                          .closed = ClosedThunk};
  const HRESULT hr = open_(&host, page);
  if (SUCCEEDED(hr)) return;

  Unload();
  if (hr == HRESULT_FROM_WIN32(ERROR_PRODUCT_UNINSTALLED)) {
    if (::MessageBoxW(window_,
                      L"The Monitone settings need the Microsoft Edge WebView2 Runtime, which is not installed.\n\n"
                      L"Open the download page?",
                      L"Monitone", MB_YESNO | MB_ICONINFORMATION) == IDYES)
      OpenInShell(std::wstring{kRuntimeDownload});
    return;
  }
  ::MessageBoxW(window_, std::format(L"The settings window could not be opened ({}).", FormatHResult(hr)).c_str(),
                L"Monitone", MB_ICONERROR);
}

bool SettingsModule::Load() {
  const std::wstring path = ModulePath();
  // DLL_LOAD_DIR lets a MinGW build find WebView2Loader.dll next to the module.
  module_ = ::LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (module_ == nullptr) return false;
  open_ = Export<MonitoneSettingsOpenFn>(module_, "MonitoneSettingsOpen");
  activate_ = Export<MonitoneSettingsActivateFn>(module_, "MonitoneSettingsActivate");
  shutdown_ = Export<MonitoneSettingsShutdownFn>(module_, "MonitoneSettingsShutdown");
  if (open_ != nullptr && activate_ != nullptr && shutdown_ != nullptr) return true;
  Unload();
  return false;
}

void SettingsModule::Unload() {
  if (module_ == nullptr) return;
  if (shutdown_ != nullptr) shutdown_();  // joins the UI thread
  ::FreeLibrary(std::exchange(module_, nullptr));
  open_ = nullptr;
  activate_ = nullptr;
  shutdown_ = nullptr;
  closed_ = false;
  if (::GetModuleHandleW(kModuleName.data()) != nullptr)
    ::OutputDebugStringW(L"Monitone: MonitoneSettings.dll is still loaded after FreeLibrary\n");
}

void SettingsModule::OnClosed() {
  if (closed_) Unload();
}

LRESULT SettingsModule::OnCall(WPARAM operation, LPARAM argument) {
  switch (static_cast<Operation>(operation)) {
    case Operation::ConfigChanged:
      host_.ReloadConfig();
      return 0;
    case Operation::QueryReport: {
      const auto& call = *reinterpret_cast<const ReportCall*>(argument);
      for (const RouteReport& report : host_.RefreshReport()) {
        const MonitoneReportEntry entry{.pid = report.pid,
                                        .exe = report.exe.c_str(),
                                        .exePath = report.exePath.c_str(),
                                        .windowTitle = report.windowTitle.c_str(),
                                        .monitor = report.monitor.c_str(),
                                        .device = report.device.c_str(),
                                        .note = report.note.c_str(),
                                        .playing = report.playing ? 1 : 0,
                                        .hasBalance = report.balance ? 1 : 0,
                                        .left = report.balance ? report.balance->left : 1.0f,
                                        .right = report.balance ? report.balance->right : 1.0f};
        call.sink(call.sinkContext, &entry);
      }
      return 0;
    }
    case Operation::ResetWindowsAppDevices:
      host_.ResetWindowsAppDevices();
      return 0;
  }
  return 0;
}

// SendMessage runs the handler on the host thread and blocks the UI thread
// until it returns, so the call arguments may live on the UI thread's stack.

void SettingsModule::ConfigChangedThunk(void* context) {
  ::SendMessageW(Self(context).window_, kMsgSettingsCall, static_cast<WPARAM>(Operation::ConfigChanged), 0);
}

void SettingsModule::QueryReportThunk(void* context, MonitoneReportSink sink, void* sinkContext) {
  ReportCall call{.sink = sink, .sinkContext = sinkContext};
  ::SendMessageW(Self(context).window_, kMsgSettingsCall, static_cast<WPARAM>(Operation::QueryReport),
                 reinterpret_cast<LPARAM>(&call));
}

void SettingsModule::ResetWindowsAppDevicesThunk(void* context) {
  ::SendMessageW(Self(context).window_, kMsgSettingsCall, static_cast<WPARAM>(Operation::ResetWindowsAppDevices), 0);
}

void SettingsModule::ClosedThunk(void* context) {
  SettingsModule& self = Self(context);
  self.closed_ = true;
  ::PostMessageW(self.window_, kMsgSettingsClosed, 0, 0);
}

}  // namespace monitone
