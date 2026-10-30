#pragma once

// COM callback objects for WebView2 events and completions.
//
// WebView2 releases these from its own threads, possibly after the window is
// gone. Every live object is counted so MonitoneSettingsShutdown can wait
// until no reference into this DLL's code remains before it may be unloaded.

#include <windows.h>

#include <atomic>
#include <functional>
#include <utility>

#include "Win32.h"

namespace monitone::settings {

inline std::atomic<long> g_liveCallbacks{0};

template <typename Interface, const IID& InterfaceId, typename... Args>
class Handler final : public Interface {
 public:
  using InterfaceType = Interface;
  using Function = std::function<HRESULT(Args...)>;

  explicit Handler(Function function) : function_{std::move(function)} { ++g_liveCallbacks; }
  ~Handler() { --g_liveCallbacks; }
  Handler(const Handler&) = delete;
  Handler& operator=(const Handler&) = delete;

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
    if (object == nullptr) return E_POINTER;
    if (riid == IID_IUnknown || riid == InterfaceId) {
      *object = static_cast<Interface*>(this);
      AddRef();
      return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG refs = --refs_;
    if (refs == 0) delete this;
    return refs;
  }
  HRESULT STDMETHODCALLTYPE Invoke(Args... args) override { return function_ ? function_(args...) : S_OK; }

 private:
  std::atomic<ULONG> refs_{1};
  Function function_;
};

// Usage: auto handler = Make<SomeHandler>([this](...) { ...; return S_OK; });
template <typename HandlerType>
[[nodiscard]] ComPtr<typename HandlerType::InterfaceType> Make(typename HandlerType::Function function) {
  ComPtr<typename HandlerType::InterfaceType> handler;
  handler.Attach(new HandlerType(std::move(function)));
  return handler;
}

}  // namespace monitone::settings
