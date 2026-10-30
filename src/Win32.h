#pragma once

// Thin RAII layer over the Win32 resources Monitone uses, plus small helpers.

#include <windows.h>

#include <objbase.h>
#include <propidl.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace monitone {

using Microsoft::WRL::ComPtr;

// Private window messages, posted from audio callback threads to the UI thread.
inline constexpr UINT kMsgTray = WM_APP + 1;
inline constexpr UINT kMsgAudioSessionsChanged = WM_APP + 2;
inline constexpr UINT kMsgAudioDevicesChanged = WM_APP + 3;
inline constexpr UINT kMsgSettingsCall = WM_APP + 4;    // sent from the settings UI thread
inline constexpr UINT kMsgSettingsClosed = WM_APP + 5;  // posted from the settings UI thread

// Move-only owner of a handle-like value. Traits supply Type, Invalid(),
// IsValid() and Close().
template <typename Traits>
class UniqueResource {
 public:
  using Type = typename Traits::Type;

  UniqueResource() noexcept = default;
  explicit UniqueResource(Type value) noexcept : value_{value} {}
  ~UniqueResource() { Reset(); }

  UniqueResource(const UniqueResource&) = delete;
  UniqueResource& operator=(const UniqueResource&) = delete;
  UniqueResource(UniqueResource&& other) noexcept : value_{other.Release()} {}
  UniqueResource& operator=(UniqueResource&& other) noexcept {
    if (this != &other) Reset(other.Release());
    return *this;
  }

  [[nodiscard]] Type Get() const noexcept { return value_; }
  [[nodiscard]] bool IsValid() const noexcept { return Traits::IsValid(value_); }
  explicit operator bool() const noexcept { return IsValid(); }

  [[nodiscard]] Type Release() noexcept { return std::exchange(value_, Traits::Invalid()); }
  void Reset(Type value = Traits::Invalid()) noexcept {
    if (IsValid()) Traits::Close(value_);
    value_ = value;
  }
  [[nodiscard]] Type* Put() noexcept {
    Reset();
    return &value_;
  }

 private:
  Type value_ = Traits::Invalid();
};

namespace detail {

template <typename T, auto CloseFn>
struct NullableTraits {
  using Type = T;
  static Type Invalid() noexcept { return nullptr; }
  static bool IsValid(Type value) noexcept { return value != nullptr; }
  static void Close(Type value) noexcept { CloseFn(value); }
};

struct KernelHandleTraits {
  using Type = HANDLE;
  static Type Invalid() noexcept { return nullptr; }
  static bool IsValid(Type value) noexcept { return value != nullptr && value != INVALID_HANDLE_VALUE; }
  static void Close(Type value) noexcept { ::CloseHandle(value); }
};

inline void CloseRegKey(HKEY key) noexcept { ::RegCloseKey(key); }
inline void DestroyMenuHandle(HMENU menu) noexcept { ::DestroyMenu(menu); }
inline void DestroyIconHandle(HICON icon) noexcept { ::DestroyIcon(icon); }
inline void DeleteBitmapHandle(HBITMAP bitmap) noexcept { ::DeleteObject(bitmap); }
inline void UnhookWinEventHandle(HWINEVENTHOOK hook) noexcept { ::UnhookWinEvent(hook); }
inline void DestroyWindowHandle(HWND window) noexcept { ::DestroyWindow(window); }

}  // namespace detail

using UniqueHandle = UniqueResource<detail::KernelHandleTraits>;
using UniqueRegKey = UniqueResource<detail::NullableTraits<HKEY, detail::CloseRegKey>>;
using UniqueMenu = UniqueResource<detail::NullableTraits<HMENU, detail::DestroyMenuHandle>>;
using UniqueIcon = UniqueResource<detail::NullableTraits<HICON, detail::DestroyIconHandle>>;
using UniqueBitmap = UniqueResource<detail::NullableTraits<HBITMAP, detail::DeleteBitmapHandle>>;
using UniqueWinEventHook = UniqueResource<detail::NullableTraits<HWINEVENTHOOK, detail::UnhookWinEventHandle>>;
using UniqueWindow = UniqueResource<detail::NullableTraits<HWND, detail::DestroyWindowHandle>>;

struct CoTaskMemDeleter {
  void operator()(void* memory) const noexcept { ::CoTaskMemFree(memory); }
};
template <typename T>
using UniqueCoTaskMem = std::unique_ptr<T, CoTaskMemDeleter>;

// Takes ownership of a CoTaskMem string returned through an out parameter.
class CoTaskMemString {
 public:
  [[nodiscard]] wchar_t** Put() noexcept {
    owner_.reset();
    raw_ = nullptr;
    return &raw_;
  }
  // Must be called after the API that filled Put() returned.
  [[nodiscard]] std::wstring Take() {
    owner_.reset(std::exchange(raw_, nullptr));
    return owner_ ? std::wstring{owner_.get()} : std::wstring{};
  }

 private:
  wchar_t* raw_ = nullptr;
  UniqueCoTaskMem<wchar_t> owner_;
};

class PropVariant {
 public:
  PropVariant() noexcept { ::PropVariantInit(&value_); }
  ~PropVariant() { ::PropVariantClear(&value_); }
  PropVariant(const PropVariant&) = delete;
  PropVariant& operator=(const PropVariant&) = delete;

  [[nodiscard]] PROPVARIANT* Put() noexcept {
    ::PropVariantClear(&value_);
    return &value_;
  }
  [[nodiscard]] const PROPVARIANT& Get() const noexcept { return value_; }

 private:
  PROPVARIANT value_{};
};

class ComApartment {
 public:
  explicit ComApartment(DWORD model) noexcept : result_{::CoInitializeEx(nullptr, model)} {}
  ~ComApartment() {
    if (SUCCEEDED(result_)) ::CoUninitialize();
  }
  ComApartment(const ComApartment&) = delete;
  ComApartment& operator=(const ComApartment&) = delete;

  [[nodiscard]] HRESULT Result() const noexcept { return result_; }

 private:
  HRESULT result_ = E_FAIL;
};

// Reference counting for COM callback objects that implement one interface.
template <typename Interface>
class ComCallback : public Interface {
 public:
  ComCallback(const ComCallback&) = delete;
  ComCallback& operator=(const ComCallback&) = delete;

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
    if (object == nullptr) return E_POINTER;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(Interface)) {
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

 protected:
  ComCallback() noexcept = default;
  virtual ~ComCallback() = default;

 private:
  std::atomic<ULONG> refs_{1};
};

template <typename T, typename... Args>
[[nodiscard]] ComPtr<T> MakeComCallback(Args&&... args) {
  ComPtr<T> object;
  object.Attach(new T(std::forward<Args>(args)...));
  return object;
}

[[nodiscard]] inline bool EqualsNoCase(std::wstring_view a, std::wstring_view b) noexcept {
  return ::CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), TRUE) ==
         CSTR_EQUAL;
}

[[nodiscard]] inline std::wstring ToLower(std::wstring_view text) {
  std::wstring result{text};
  if (!result.empty()) ::CharLowerBuffW(result.data(), static_cast<DWORD>(result.size()));
  return result;
}

[[nodiscard]] inline std::wstring FormatHResult(HRESULT hr) {
  return std::format(L"0x{:08X}", static_cast<std::uint32_t>(hr));
}

}  // namespace monitone
