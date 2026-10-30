// Exported entry points of MonitoneSettings.dll, see SettingsApi.h.

#include <windows.h>

#include <process.h>

#include <atomic>
#include <memory>

#include "IdentifyOverlay.h"
#include "SettingsApi.h"
#include "Win32.h"
#include "settings/Handlers.h"
#include "settings/Loader.h"
#include "settings/SettingsWindow.h"

namespace monitone::settings {
namespace {

// How long Shutdown waits for WebView2 to release its last callback object.
constexpr DWORD kCallbackDrainMs = 5000;
constexpr DWORD kPollMs = 50;

struct ThreadStart {
  HostInfo host;
  int page = 0;
};

// Only the host thread touches these, except g_window which the UI thread sets.
UniqueHandle g_thread;
std::atomic<HWND> g_window{nullptr};

HINSTANCE ThisModule() {
  HMODULE module = nullptr;
  ::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&ThisModule), &module);
  return module;
}

unsigned __stdcall UiThread(void* parameter) {
  const std::unique_ptr<ThreadStart> start{static_cast<ThreadStart*>(parameter)};
  const HINSTANCE module = ThisModule();
  {
    // WebView2 requires a single-threaded apartment.
    const ComApartment apartment{COINIT_APARTMENTTHREADED};
    if (SUCCEEDED(apartment.Result())) {
      SettingsWindow window{module, start->host, start->page};
      if (window.Create()) {
        g_window = window.Handle();
        MSG message{};
        while (::GetMessageW(&message, nullptr, 0, 0) > 0) {
          ::TranslateMessage(&message);
          ::DispatchMessageW(&message);
        }
        g_window = nullptr;
      }
    }
    CloseDisplayNumbers(module);
    SettingsWindow::UnregisterWindowClass(module);
  }  // window, WebView2 objects and the COM apartment are gone here
  start->host.callbacks.closed(start->host.callbacks.context);
  return 0;
}

bool IsRunning() { return g_thread && ::WaitForSingleObject(g_thread.Get(), 0) == WAIT_TIMEOUT; }

// Waits for the handle while still dispatching messages sent to this thread,
// because the UI thread may be inside a SendMessage to the host right now.
void WaitDispatchingSentMessages(HANDLE handle, const auto& keepGoing) {
  for (;;) {
    const DWORD result =
        ::MsgWaitForMultipleObjects(handle != nullptr ? 1 : 0, &handle, FALSE, kPollMs, QS_SENDMESSAGE);
    if (handle != nullptr && result == WAIT_OBJECT_0) return;
    MSG message{};
    ::PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
    if (!keepGoing()) return;
  }
}

}  // namespace
}  // namespace monitone::settings

using namespace monitone::settings;

extern "C" long __stdcall MonitoneSettingsOpen(const MonitoneHost* host, int page) {
  if (host == nullptr || host->apiVersion != MONITONE_SETTINGS_API_VERSION || host->configChanged == nullptr ||
      host->queryReport == nullptr || host->resetWindowsAppDevices == nullptr || host->closed == nullptr)
    return E_INVALIDARG;
  if (IsRunning()) {
    if (const HWND window = g_window.load())
      ::PostMessageW(window, SettingsWindow::kMsgActivate, static_cast<WPARAM>(page), 0);
    return S_OK;
  }
  g_thread.Reset();

  if (const HRESULT runtime = CheckWebView2Runtime(); FAILED(runtime)) return runtime;

  auto start = std::make_unique<ThreadStart>(
      ThreadStart{.host = HostInfo{.callbacks = *host,
                                   .configPath = host->configPath != nullptr ? host->configPath : L"",
                                   .version = host->version != nullptr ? host->version : L""},
                  .page = page});
  // The copies own the strings now; the host's pointers are only valid during this call.
  start->host.callbacks.configPath = nullptr;
  start->host.callbacks.version = nullptr;

  const uintptr_t thread = ::_beginthreadex(nullptr, 0, UiThread, start.get(), 0, nullptr);
  if (thread == 0) return HRESULT_FROM_WIN32(::GetLastError());
  (void)start.release();  // owned by the thread now
  g_thread.Reset(reinterpret_cast<HANDLE>(thread));
  return S_OK;
}

extern "C" void __stdcall MonitoneSettingsActivate(int page) {
  if (const HWND window = g_window.load())
    ::PostMessageW(window, SettingsWindow::kMsgActivate, static_cast<WPARAM>(page), 0);
}

extern "C" void __stdcall MonitoneSettingsShutdown() {
  if (g_thread) {
    WaitDispatchingSentMessages(g_thread.Get(), [] {
      if (const HWND window = g_window.load()) ::PostMessageW(window, WM_CLOSE, 0, 0);
      return true;
    });
    g_thread.Reset();
  }

  // WebView2 may release callback objects on its own threads a little later.
  const ULONGLONG deadline = ::GetTickCount64() + kCallbackDrainMs;
  WaitDispatchingSentMessages(nullptr, [&] { return g_liveCallbacks > 0 && ::GetTickCount64() < deadline; });
  if (g_liveCallbacks > 0) {
    // Unloading now could leave WebView2 calling into freed code; stay loaded.
    HMODULE pinned = nullptr;
    ::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                         reinterpret_cast<LPCWSTR>(&MonitoneSettingsShutdown), &pinned);
    ::OutputDebugStringW(L"MonitoneSettings: WebView2 still holds callbacks, module pinned\n");
    return;
  }
  UnloadWebView2Loader();
}
