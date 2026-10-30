#include "AudioRouter.h"

#include <algorithm>
#include <format>
#include <optional>

namespace monitone {
namespace {

constexpr PROPERTYKEY kDeviceFriendlyName{
    {0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

// Callbacks arrive on MTA worker threads; they only nudge the UI thread.
class SessionNotifier final : public ComCallback<IAudioSessionNotification> {
 public:
  explicit SessionNotifier(HWND window) noexcept : window_{window} {}

  HRESULT STDMETHODCALLTYPE OnSessionCreated(IAudioSessionControl*) override {
    ::PostMessageW(window_, kMsgAudioSessionsChanged, 0, 0);
    return S_OK;
  }

 private:
  HWND window_ = nullptr;
};

class DeviceNotifier final : public ComCallback<IMMNotificationClient> {
 public:
  explicit DeviceNotifier(HWND window) noexcept : window_{window} {}

  HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { return Notify(); }
  HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return Notify(); }
  HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return Notify(); }
  HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow, ERole, LPCWSTR) override { return Notify(); }
  HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

 private:
  HRESULT Notify() const noexcept {
    ::PostMessageW(window_, kMsgAudioDevicesChanged, 0, 0);
    return S_OK;
  }

  HWND window_ = nullptr;
};

std::wstring FriendlyName(IMMDevice& device) {
  ComPtr<IPropertyStore> properties;
  if (FAILED(device.OpenPropertyStore(STGM_READ, &properties))) return {};
  PropVariant value;
  if (FAILED(properties->GetValue(kDeviceFriendlyName, value.Put())) || value.Get().vt != VT_LPWSTR) return {};
  return value.Get().pwszVal;
}

std::optional<AudioSession> ReadSession(IAudioSessionControl& control, const std::wstring& deviceId) {
  AudioSessionState state = AudioSessionStateExpired;
  if (FAILED(control.GetState(&state)) || state == AudioSessionStateExpired) return std::nullopt;

  ComPtr<IAudioSessionControl2> control2;
  if (FAILED(control.QueryInterface(IID_PPV_ARGS(&control2)))) return std::nullopt;
  if (control2->IsSystemSoundsSession() == S_OK) return std::nullopt;

  AudioSession session{.deviceId = deviceId, .active = state == AudioSessionStateActive};
  // AUDCLNT_S_NO_SINGLE_PROCESS is a success code and still yields a pid.
  if (FAILED(control2->GetProcessId(&session.pid)) || session.pid == 0) return std::nullopt;

  CoTaskMemString instanceId;
  if (SUCCEEDED(control2->GetSessionInstanceIdentifier(instanceId.Put()))) session.instanceId = instanceId.Take();
  control.QueryInterface(IID_PPV_ARGS(&session.channels));
  return session;
}

}  // namespace

AudioRouter::AudioRouter(HWND notifyWindow) noexcept : notifyWindow_{notifyWindow} {}

AudioRouter::~AudioRouter() {
  ReleaseEndpoints();
  if (enumerator_ && deviceNotifier_) enumerator_->UnregisterEndpointNotificationCallback(deviceNotifier_.Get());
}

std::wstring AudioRouter::Initialize() {
  if (const HRESULT hr =
          ::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator_));
      FAILED(hr))
    return std::format(L"Cannot create the audio device enumerator ({}).", FormatHResult(hr));

  if (const HRESULT hr = policy_.Initialize(); FAILED(hr))
    return std::format(
        L"The per-application audio routing interface is not available ({}).\n"
        L"Windows 10 version 1803 or newer is required.",
        FormatHResult(hr));

  sessionNotifier_ = MakeComCallback<SessionNotifier>(notifyWindow_);
  deviceNotifier_ = MakeComCallback<DeviceNotifier>(notifyWindow_);
  if (const HRESULT hr = enumerator_->RegisterEndpointNotificationCallback(deviceNotifier_.Get()); FAILED(hr))
    deviceNotifier_.Reset();  // device hot-plug is then only noticed on restart

  RefreshDevices();
  return {};
}

void AudioRouter::ReleaseEndpoints() noexcept {
  for (const Endpoint& endpoint : endpoints_) {
    if (endpoint.manager) endpoint.manager->UnregisterSessionNotification(sessionNotifier_.Get());
  }
  endpoints_.clear();
  devices_.clear();
}

void AudioRouter::RefreshDevices() {
  ReleaseEndpoints();
  if (!enumerator_) return;

  ComPtr<IMMDeviceCollection> collection;
  if (FAILED(enumerator_->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection))) return;
  UINT count = 0;
  if (FAILED(collection->GetCount(&count))) return;

  for (UINT i = 0; i < count; ++i) {
    ComPtr<IMMDevice> device;
    if (FAILED(collection->Item(i, &device))) continue;

    CoTaskMemString id;
    if (FAILED(device->GetId(id.Put()))) continue;

    Endpoint endpoint{.info = {.id = id.Take(), .name = FriendlyName(*device.Get())}};
    if (endpoint.info.name.empty()) endpoint.info.name = endpoint.info.id;

    // Without a registered session notification, the session enumerator
    // never learns about sessions created after it was first obtained.
    if (SUCCEEDED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                                   reinterpret_cast<void**>(endpoint.manager.ReleaseAndGetAddressOf())))) {
      endpoint.manager->RegisterSessionNotification(sessionNotifier_.Get());
    }
    devices_.push_back(endpoint.info);
    endpoints_.push_back(std::move(endpoint));
  }
}

std::wstring AudioRouter::DefaultDeviceId() const {
  if (!enumerator_) return {};
  ComPtr<IMMDevice> device;
  if (FAILED(enumerator_->GetDefaultAudioEndpoint(eRender, eMultimedia, &device))) return {};
  CoTaskMemString id;
  if (FAILED(device->GetId(id.Put()))) return {};
  return id.Take();
}

bool AudioRouter::HasDevice(std::wstring_view id) const noexcept {
  return std::ranges::any_of(devices_, [id](const AudioDevice& device) { return EqualsNoCase(device.id, id); });
}

std::wstring AudioRouter::DeviceName(std::wstring_view id) const {
  const auto it =
      std::ranges::find_if(devices_, [id](const AudioDevice& device) { return EqualsNoCase(device.id, id); });
  return it != devices_.end() ? it->name : std::wstring{};
}

std::vector<AudioSession> AudioRouter::Sessions() const {
  std::vector<AudioSession> result;
  for (const Endpoint& endpoint : endpoints_) {
    if (!endpoint.manager) continue;
    ComPtr<IAudioSessionEnumerator> sessions;
    if (FAILED(endpoint.manager->GetSessionEnumerator(&sessions))) continue;
    int count = 0;
    if (FAILED(sessions->GetCount(&count))) continue;

    for (int i = 0; i < count; ++i) {
      ComPtr<IAudioSessionControl> control;
      if (FAILED(sessions->GetSession(i, &control))) continue;
      if (auto session = ReadSession(*control.Get(), endpoint.info.id)) result.push_back(std::move(*session));
    }
  }
  return result;
}

}  // namespace monitone
