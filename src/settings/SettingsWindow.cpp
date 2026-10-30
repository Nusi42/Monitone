#include "settings/SettingsWindow.h"

#include <dwmapi.h>
#include <shlwapi.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>

#include "settings/Handlers.h"
#include "settings/Text.h"
#include "settings/WebResources.h"

#include "settings/Loader.h"

namespace monitone::settings {
namespace {

constexpr wchar_t kWindowClass[] = L"Monitone.Settings";
constexpr wchar_t kTitle[] = L"Monitone Settings";
constexpr int kDefaultWidthDip = 1040;
constexpr int kDefaultHeightDip = 700;
constexpr int kMinWidthDip = 760;
constexpr int kMinHeightDip = 520;
constexpr int kAppIconId = 1;  // IDI_APP in Monitone.exe

// DWM attributes newer than some SDK headers.
constexpr DWORD kDwmUseImmersiveDarkMode = 20;
constexpr DWORD kDwmSystemBackdropType = 38;
constexpr int kDwmBackdropMainWindow = 2;  // Mica

using EnvironmentCompleted =
    Handler<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,
            IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT, ICoreWebView2Environment*>;
using ControllerCompleted =
    Handler<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,
            IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller*>;
using WebMessageReceived =
    Handler<ICoreWebView2WebMessageReceivedEventHandler, IID_ICoreWebView2WebMessageReceivedEventHandler,
            ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs*>;
using WebResourceRequested =
    Handler<ICoreWebView2WebResourceRequestedEventHandler, IID_ICoreWebView2WebResourceRequestedEventHandler,
            ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs*>;

bool SystemUsesDarkTheme() {
  DWORD light = 1;
  DWORD size = sizeof(light);
  ::RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
  return light == 0;
}

int Scale(int dip, UINT dpi) { return ::MulDiv(dip, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI); }

std::wstring UserDataFolder() {
  // Next to the configuration would roam; WebView2 caches belong to LocalAppData.
  std::array<wchar_t, MAX_PATH> buffer{};
  const DWORD length = ::GetEnvironmentVariableW(L"LOCALAPPDATA", buffer.data(), static_cast<DWORD>(buffer.size()));
  if (length == 0 || length >= buffer.size()) return {};
  return (std::filesystem::path{std::wstring_view{buffer.data(), length}} / L"Monitone" / L"WebView2").wstring();
}

UniqueIcon LoadAppIcon(int metricX, int metricY) {
  return UniqueIcon{
      static_cast<HICON>(::LoadImageW(::GetModuleHandleW(nullptr), MAKEINTRESOURCEW(kAppIconId), IMAGE_ICON,
                                      ::GetSystemMetrics(metricX), ::GetSystemMetrics(metricY), LR_DEFAULTCOLOR))};
}

constexpr std::array<std::string_view, 5> kPages{"displays", "applications", "rules", "general", "about"};

std::string_view PageName(int page) {
  return page >= 0 && static_cast<size_t>(page) < kPages.size() ? kPages[static_cast<size_t>(page)] : kPages[0];
}

}  // namespace

SettingsWindow::SettingsWindow(HINSTANCE module, const HostInfo& host, int page)
    : module_{module},
      startPage_{page},
      // Mica exists on Windows 11 22H2 and later (build 22621).
      backdrop_{[] {
        OSVERSIONINFOEXW version{};
        version.dwOSVersionInfoSize = sizeof(version);
        version.dwBuildNumber = 22621;
        DWORDLONG mask = 0;
        VER_SET_CONDITION(mask, VER_BUILDNUMBER, VER_GREATER_EQUAL);
        return ::VerifyVersionInfoW(&version, VER_BUILDNUMBER, mask) != FALSE;
      }()},
      bridge_{module, host, backdrop_} {}

SettingsWindow::~SettingsWindow() {
  ReleaseWebView();
  if (window_ != nullptr) ::DestroyWindow(window_);
}

bool SettingsWindow::Create() {
  iconLarge_ = LoadAppIcon(SM_CXICON, SM_CYICON);
  iconSmall_ = LoadAppIcon(SM_CXSMICON, SM_CYSMICON);

  WNDCLASSEXW existing{};
  existing.cbSize = sizeof(existing);
  if (!::GetClassInfoExW(module_, kWindowClass, &existing)) {
    const WNDCLASSEXW windowClass{.cbSize = sizeof(WNDCLASSEXW),
                                  .lpfnWndProc = WindowProc,
                                  .hInstance = module_,
                                  .hCursor = ::LoadCursorW(nullptr, IDC_ARROW),
                                  // Black is transparent for the DWM backdrop.
                                  .hbrBackground = static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH)),
                                  .lpszClassName = kWindowClass};
    if (::RegisterClassExW(&windowClass) == 0) return false;
  }

  window_ = ::CreateWindowExW(WS_EX_APPWINDOW, kWindowClass, kTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                              CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, module_, this);
  if (window_ == nullptr) return false;
  bridge_.SetWindow(window_);
  ::SendMessageW(window_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(iconLarge_.Get()));
  ::SendMessageW(window_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(iconSmall_.Get()));
  ApplyFrameStyle();

  // Centre on the display under the cursor, sized for its DPI.
  POINT cursor{};
  ::GetCursorPos(&cursor);
  MONITORINFO monitor{};
  monitor.cbSize = sizeof(monitor);
  ::GetMonitorInfoW(::MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &monitor);
  const UINT dpi = ::GetDpiForWindow(window_);
  const RECT& work = monitor.rcWork;
  const int width = std::min(Scale(kDefaultWidthDip, dpi), static_cast<int>(work.right - work.left));
  const int height = std::min(Scale(kDefaultHeightDip, dpi), static_cast<int>(work.bottom - work.top));
  ::SetWindowPos(window_, nullptr, work.left + (work.right - work.left - width) / 2,
                 work.top + (work.bottom - work.top - height) / 2, width, height, SWP_NOZORDER | SWP_NOACTIVATE);

  ::ShowWindow(window_, SW_SHOWNORMAL);
  ::SetForegroundWindow(window_);
  CreateWebView();
  return true;
}

void SettingsWindow::UnregisterWindowClass(HINSTANCE module) { ::UnregisterClassW(kWindowClass, module); }

void SettingsWindow::ApplyFrameStyle() {
  const BOOL dark = SystemUsesDarkTheme() ? TRUE : FALSE;
  ::DwmSetWindowAttribute(window_, kDwmUseImmersiveDarkMode, &dark, sizeof(dark));
  if (backdrop_) {
    const MARGINS margins{-1, -1, -1, -1};
    ::DwmExtendFrameIntoClientArea(window_, &margins);
    const int backdrop = kDwmBackdropMainWindow;
    ::DwmSetWindowAttribute(window_, kDwmSystemBackdropType, &backdrop, sizeof(backdrop));
  }
}

void SettingsWindow::CreateWebView() {
  const std::wstring dataFolder = UserDataFolder();
  auto handler = Make<EnvironmentCompleted>([this](HRESULT result, ICoreWebView2Environment* environment) {
    return OnEnvironmentCreated(result, environment);
  });
  const HRESULT hr = CreateEnvironment(dataFolder, handler.Get());
  if (FAILED(hr)) Fail(hr);
}

HRESULT SettingsWindow::OnEnvironmentCreated(HRESULT result, ICoreWebView2Environment* environment) {
  if (window_ == nullptr) return S_OK;  // closed while the environment was being created
  if (FAILED(result) || environment == nullptr) {
    Fail(FAILED(result) ? result : E_FAIL);
    return S_OK;
  }
  environment_ = environment;
  const HRESULT hr = environment_->CreateCoreWebView2Controller(
      window_, Make<ControllerCompleted>([this](HRESULT created, ICoreWebView2Controller* controller) {
                 return OnControllerCreated(created, controller);
               }).Get());
  if (FAILED(hr)) Fail(hr);
  return S_OK;
}

HRESULT SettingsWindow::OnControllerCreated(HRESULT result, ICoreWebView2Controller* controller) {
  if (window_ == nullptr) {
    if (controller != nullptr) controller->Close();
    return S_OK;
  }
  if (FAILED(result) || controller == nullptr) {
    Fail(FAILED(result) ? result : E_FAIL);
    return S_OK;
  }
  controller_ = controller;
  if (FAILED(controller_->get_CoreWebView2(&webview_))) {
    Fail(E_FAIL);
    return S_OK;
  }

  // Transparent page background lets the Mica backdrop shine through.
  ComPtr<ICoreWebView2Controller2> controller2;
  if (SUCCEEDED(controller_->QueryInterface(IID_ICoreWebView2Controller2,
                                            reinterpret_cast<void**>(controller2.GetAddressOf())))) {
    const COREWEBVIEW2_COLOR transparent{0, 0, 0, 0};
    controller2->put_DefaultBackgroundColor(transparent);
  }

  ComPtr<ICoreWebView2Settings> settings;
  if (SUCCEEDED(webview_->get_Settings(&settings))) {
    settings->put_AreDefaultContextMenusEnabled(FALSE);
    settings->put_IsStatusBarEnabled(FALSE);
    settings->put_IsZoomControlEnabled(FALSE);
    settings->put_AreHostObjectsAllowed(FALSE);
#ifdef NDEBUG
    settings->put_AreDevToolsEnabled(FALSE);
#endif
    ComPtr<ICoreWebView2Settings3> settings3;
    if (SUCCEEDED(
            settings->QueryInterface(IID_ICoreWebView2Settings3, reinterpret_cast<void**>(settings3.GetAddressOf()))))
      settings3->put_AreBrowserAcceleratorKeysEnabled(FALSE);
  }

  const std::wstring filter = std::wstring{kUiOrigin} + L"*";
  webview_->AddWebResourceRequestedFilter(filter.c_str(), COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
  webview_->add_WebResourceRequested(
      Make<WebResourceRequested>([this](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args) {
        return OnWebResourceRequested(args);
      }).Get(),
      &resourceToken_);
  webview_->add_WebMessageReceived(
      Make<WebMessageReceived>([this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) {
        return OnWebMessage(args);
      }).Get(),
      &messageToken_);

  ResizeWebView();
  controller_->put_IsVisible(TRUE);
  const std::wstring url = std::format(L"{}index.html#{}", kUiOrigin, FromUtf8(PageName(startPage_)));
  webview_->Navigate(url.c_str());
  return S_OK;
}

HRESULT SettingsWindow::OnWebResourceRequested(ICoreWebView2WebResourceRequestedEventArgs* args) {
  ComPtr<ICoreWebView2WebResourceRequest> request;
  CoTaskMemString uri;
  if (FAILED(args->get_Request(&request)) || FAILED(request->get_Uri(uri.Put()))) return S_OK;

  const std::optional<WebResource> resource = FindWebResource(module_, uri.Take());
  ComPtr<IStream> stream;
  if (resource)
    stream.Attach(::SHCreateMemStream(reinterpret_cast<const BYTE*>(resource->data.data()),
                                      static_cast<UINT>(resource->data.size())));
  const std::wstring headers =
      resource ? std::format(L"Content-Type: {}\r\nCache-Control: no-store", resource->contentType) : std::wstring{};

  ComPtr<ICoreWebView2WebResourceResponse> response;
  if (SUCCEEDED(environment_->CreateWebResourceResponse(stream.Get(), resource ? 200 : 404,
                                                        resource ? L"OK" : L"Not Found", headers.c_str(), &response)))
    args->put_Response(response.Get());
  return S_OK;
}

HRESULT SettingsWindow::OnWebMessage(ICoreWebView2WebMessageReceivedEventArgs* args) {
  // Only the embedded UI may talk to the bridge.
  CoTaskMemString source;
  if (FAILED(args->get_Source(source.Put())) || !source.Take().starts_with(kUiOrigin)) return S_OK;

  CoTaskMemString text;
  if (FAILED(args->get_WebMessageAsJson(text.Put()))) return S_OK;
  const nlohmann::json message = nlohmann::json::parse(ToUtf8(text.Take()), nullptr, false);
  if (message.is_discarded()) return S_OK;

  if (message.value("type", std::string{}) == "ready") uiReady_ = true;
  if (message.value("type", std::string{}) == "close") {
    ::PostMessageW(window_, WM_CLOSE, 0, 0);
    return S_OK;
  }
  bridge_.Handle(message, [this](const nlohmann::json& reply) { Post(reply); });
  return S_OK;
}

void SettingsWindow::Post(const nlohmann::json& message) {
  if (!webview_) return;
  const std::wstring text = FromUtf8(message.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
  webview_->PostWebMessageAsJson(text.c_str());
}

void SettingsWindow::Navigate(int page) {
  if (uiReady_) Post({{"type", "navigate"}, {"page", PageName(page)}});
}

void SettingsWindow::ResizeWebView() {
  if (!controller_) return;
  RECT bounds{};
  ::GetClientRect(window_, &bounds);
  controller_->put_Bounds(bounds);
}

void SettingsWindow::ReleaseWebView() {
  if (webview_) {
    webview_->remove_WebMessageReceived(messageToken_);
    webview_->remove_WebResourceRequested(resourceToken_);
  }
  if (controller_) controller_->Close();
  webview_.Reset();
  controller_.Reset();
  environment_.Reset();
}

void SettingsWindow::Fail(HRESULT result) {
  ::MessageBoxW(window_,
                std::format(L"The settings user interface could not be started ({}).", FormatHResult(result)).c_str(),
                kTitle, MB_ICONERROR);
  ::PostMessageW(window_, WM_CLOSE, 0, 0);
}

LRESULT CALLBACK SettingsWindow::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  if (message == WM_NCCREATE) {
    const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
    ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
  }
  if (message == WM_NCDESTROY) ::SetWindowLongPtrW(window, GWLP_USERDATA, 0);
  auto* self = reinterpret_cast<SettingsWindow*>(::GetWindowLongPtrW(window, GWLP_USERDATA));
  return self != nullptr ? self->HandleMessage(window, message, wParam, lParam)
                         : ::DefWindowProcW(window, message, wParam, lParam);
}

LRESULT SettingsWindow::HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_SIZE:
      ResizeWebView();
      return 0;
    case WM_GETMINMAXINFO: {
      auto& info = *reinterpret_cast<MINMAXINFO*>(lParam);
      const UINT dpi = ::GetDpiForWindow(window_);
      info.ptMinTrackSize = {Scale(kMinWidthDip, dpi), Scale(kMinHeightDip, dpi)};
      return 0;
    }
    case WM_DPICHANGED: {
      const auto& suggested = *reinterpret_cast<const RECT*>(lParam);
      ::SetWindowPos(window_, nullptr, suggested.left, suggested.top, suggested.right - suggested.left,
                     suggested.bottom - suggested.top, SWP_NOZORDER | SWP_NOACTIVATE);
      return 0;
    }
    case WM_SETTINGCHANGE:
      if (lParam != 0 && std::wstring_view{reinterpret_cast<const wchar_t*>(lParam)} == L"ImmersiveColorSet")
        ApplyFrameStyle();
      return 0;
    case WM_DISPLAYCHANGE:
    case WM_DEVICECHANGE:
      if (uiReady_) Post(bridge_.State());
      return 0;
    case kMsgActivate:
      if (::IsIconic(window_)) ::ShowWindow(window_, SW_RESTORE);
      ::SetForegroundWindow(window_);
      Navigate(static_cast<int>(wParam));
      return 0;
    case WM_DESTROY:
      ReleaseWebView();
      window_ = nullptr;
      ::PostQuitMessage(0);
      return 0;
    default:
      return ::DefWindowProcW(window, message, wParam, lParam);
  }
}

}  // namespace monitone::settings
