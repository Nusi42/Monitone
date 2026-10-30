#include "PolicyConfig.h"

#include <mmdeviceapi.h>
#include <roapi.h>
#include <winstring.h>

#include <array>

// Activation factory of Windows.Media.Internal.AudioPolicyConfig. Only the
// last three methods are used, the others are vtable placeholders. The layout
// is the same for both known IIDs; only the IID changed with Windows 10 21H2.
struct IAudioPolicyConfigFactory : public IInspectable {
  virtual HRESULT STDMETHODCALLTYPE add_CtxVolumeChange() = 0;
  virtual HRESULT STDMETHODCALLTYPE remove_CtxVolumeChanged() = 0;
  virtual HRESULT STDMETHODCALLTYPE add_RingerVibrateStateChanged() = 0;
  virtual HRESULT STDMETHODCALLTYPE remove_RingerVibrateStateChange() = 0;
  virtual HRESULT STDMETHODCALLTYPE SetVolumeGroupGainForId() = 0;
  virtual HRESULT STDMETHODCALLTYPE GetVolumeGroupGainForId() = 0;
  virtual HRESULT STDMETHODCALLTYPE GetActiveVolumeGroupForEndpointId() = 0;
  virtual HRESULT STDMETHODCALLTYPE GetVolumeGroupsForEndpoint() = 0;
  virtual HRESULT STDMETHODCALLTYPE GetCurrentVolumeContext() = 0;
  virtual HRESULT STDMETHODCALLTYPE SetVolumeGroupMuteForId() = 0;
  virtual HRESULT STDMETHODCALLTYPE GetVolumeGroupMuteForId() = 0;
  virtual HRESULT STDMETHODCALLTYPE SetRingerVibrateState() = 0;
  virtual HRESULT STDMETHODCALLTYPE GetRingerVibrateState() = 0;
  virtual HRESULT STDMETHODCALLTYPE SetPreferredChatApplication() = 0;
  virtual HRESULT STDMETHODCALLTYPE ResetPreferredChatApplication() = 0;
  virtual HRESULT STDMETHODCALLTYPE GetPreferredChatApplication() = 0;
  virtual HRESULT STDMETHODCALLTYPE GetCurrentChatApplications() = 0;
  virtual HRESULT STDMETHODCALLTYPE add_ChatContextChanged() = 0;
  virtual HRESULT STDMETHODCALLTYPE remove_ChatContextChanged() = 0;

  virtual HRESULT STDMETHODCALLTYPE SetPersistedDefaultAudioEndpoint(UINT32 processId, EDataFlow flow, ERole role,
                                                                     HSTRING deviceId) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetPersistedDefaultAudioEndpoint(UINT32 processId, EDataFlow flow, ERole role,
                                                                     HSTRING* deviceId) = 0;
  virtual HRESULT STDMETHODCALLTYPE ClearAllPersistedApplicationDefaultEndpoints() = 0;

 protected:
  ~IAudioPolicyConfigFactory() = default;
};

namespace monitone {
namespace {

constexpr IID kIidFactory21H2{0xab3d4648, 0xe242, 0x459f, {0xb0, 0x2f, 0x54, 0x1c, 0x70, 0x30, 0x63, 0x24}};
constexpr IID kIidFactoryDownlevel{0x2a59116d, 0x6c4f, 0x45e0, {0xa7, 0x4f, 0x70, 0x7e, 0x3f, 0xef, 0x92, 0x58}};

constexpr std::wstring_view kClassName = L"Windows.Media.Internal.AudioPolicyConfig";
constexpr std::wstring_view kMmdevapiToken = L"\\\\?\\SWD#MMDEVAPI#";
constexpr std::wstring_view kRenderInterface = L"#{e6327cad-dcec-4949-ae8a-991e976a79d2}";
constexpr std::array kRoles{eMultimedia, eConsole};

class HString {
 public:
  HString() noexcept = default;
  ~HString() {
    if (value_ != nullptr) ::WindowsDeleteString(value_);
  }
  HString(const HString&) = delete;
  HString& operator=(const HString&) = delete;

  [[nodiscard]] HRESULT Assign(std::wstring_view text) {
    Reset();
    return ::WindowsCreateString(text.data(), static_cast<UINT32>(text.size()), &value_);
  }
  [[nodiscard]] HSTRING Get() const noexcept { return value_; }
  [[nodiscard]] HSTRING* Put() noexcept {
    Reset();
    return &value_;
  }
  [[nodiscard]] std::wstring_view View() const noexcept {
    UINT32 length = 0;
    const wchar_t* buffer = ::WindowsGetStringRawBuffer(value_, &length);
    return {buffer, length};
  }

 private:
  void Reset() noexcept {
    if (value_ != nullptr) ::WindowsDeleteString(std::exchange(value_, nullptr));
  }

  HSTRING value_ = nullptr;
};

}  // namespace

PolicyConfig::PolicyConfig() = default;
PolicyConfig::~PolicyConfig() = default;

HRESULT PolicyConfig::Initialize() {
  HString className;
  if (const HRESULT hr = className.Assign(kClassName); FAILED(hr)) return hr;

  for (const IID& iid : {kIidFactory21H2, kIidFactoryDownlevel}) {
    const HRESULT hr =
        ::RoGetActivationFactory(className.Get(), iid, reinterpret_cast<void**>(factory_.ReleaseAndGetAddressOf()));
    if (SUCCEEDED(hr)) return hr;
  }
  factory_.Reset();
  return E_NOINTERFACE;
}

bool PolicyConfig::IsAvailable() const noexcept { return factory_ != nullptr; }

HRESULT PolicyConfig::SetAppEndpoint(DWORD pid, std::wstring_view endpointId) const {
  if (!factory_) return E_NOINTERFACE;

  HString deviceId;  // stays null to clear the override
  if (!endpointId.empty()) {
    std::wstring interfacePath{kMmdevapiToken};
    interfacePath.append(endpointId).append(kRenderInterface);
    if (const HRESULT hr = deviceId.Assign(interfacePath); FAILED(hr)) return hr;
  }
  for (const ERole role : kRoles) {
    if (const HRESULT hr = factory_->SetPersistedDefaultAudioEndpoint(pid, eRender, role, deviceId.Get()); FAILED(hr))
      return hr;
  }
  return S_OK;
}

HRESULT PolicyConfig::GetAppEndpoint(DWORD pid, std::wstring& endpointId) const {
  endpointId.clear();
  if (!factory_) return E_NOINTERFACE;

  HString deviceId;
  if (const HRESULT hr = factory_->GetPersistedDefaultAudioEndpoint(pid, eRender, eMultimedia, deviceId.Put());
      FAILED(hr))
    return hr;

  std::wstring_view path = deviceId.View();
  if (path.starts_with(kMmdevapiToken)) path.remove_prefix(kMmdevapiToken.size());
  if (path.ends_with(kRenderInterface)) path.remove_suffix(kRenderInterface.size());
  endpointId = path;
  return S_OK;
}

HRESULT PolicyConfig::ClearAll() const {
  return factory_ ? factory_->ClearAllPersistedApplicationDefaultEndpoints() : E_NOINTERFACE;
}

}  // namespace monitone
