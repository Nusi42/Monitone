#include "settings/Loader.h"

#include <utility>

#include "Win32.h"

namespace monitone::settings {

#ifdef MONITONE_WEBVIEW2_STATIC_LOADER

HRESULT CheckWebView2Runtime() {
  CoTaskMemString version;
  const HRESULT hr = ::GetAvailableCoreWebView2BrowserVersionString(nullptr, version.Put());
  return SUCCEEDED(hr) && !version.Take().empty() ? S_OK : HRESULT_FROM_WIN32(ERROR_PRODUCT_UNINSTALLED);
}

HRESULT CreateEnvironment(const std::wstring& userDataFolder,
                          ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* handler) {
  return ::CreateCoreWebView2EnvironmentWithOptions(nullptr, userDataFolder.empty() ? nullptr : userDataFolder.c_str(),
                                                    nullptr, handler);
}

void UnloadWebView2Loader() {}

#else

namespace {

using GetVersionFn = HRESULT(STDAPICALLTYPE*)(PCWSTR, LPWSTR*);
using CreateEnvironmentFn = HRESULT(STDAPICALLTYPE*)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*,
                                                     ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);

HMODULE g_loader = nullptr;

HMODULE Loader() {
  if (g_loader != nullptr) return g_loader;
  HMODULE self = nullptr;
  ::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&Loader), &self);
  std::wstring path(MAX_PATH, L'\0');
  path.resize(::GetModuleFileNameW(self, path.data(), static_cast<DWORD>(path.size())));
  path.erase(path.find_last_of(L'\\') + 1);
  path += L"WebView2Loader.dll";
  g_loader = ::LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
  return g_loader;
}

template <typename Fn>
Fn Function(const char* name) {
  const HMODULE loader = Loader();
  return loader != nullptr ? reinterpret_cast<Fn>(reinterpret_cast<void*>(::GetProcAddress(loader, name))) : nullptr;
}

}  // namespace

HRESULT CheckWebView2Runtime() {
  const auto getVersion = Function<GetVersionFn>("GetAvailableCoreWebView2BrowserVersionString");
  if (getVersion == nullptr) return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
  CoTaskMemString version;
  const HRESULT hr = getVersion(nullptr, version.Put());
  return SUCCEEDED(hr) && !version.Take().empty() ? S_OK : HRESULT_FROM_WIN32(ERROR_PRODUCT_UNINSTALLED);
}

HRESULT CreateEnvironment(const std::wstring& userDataFolder,
                          ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* handler) {
  const auto create = Function<CreateEnvironmentFn>("CreateCoreWebView2EnvironmentWithOptions");
  if (create == nullptr) return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
  return create(nullptr, userDataFolder.empty() ? nullptr : userDataFolder.c_str(), nullptr, handler);
}

void UnloadWebView2Loader() {
  if (g_loader != nullptr) ::FreeLibrary(std::exchange(g_loader, nullptr));
}

#endif

}  // namespace monitone::settings
