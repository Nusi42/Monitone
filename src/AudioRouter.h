#pragma once

#include <windows.h>

#include <audioclient.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>

#include <string>
#include <string_view>
#include <vector>

#include "PolicyConfig.h"
#include "Win32.h"

namespace monitone {

struct AudioDevice {
  std::wstring id;    // IMMDevice id
  std::wstring name;  // "Speakers (Realtek(R) Audio)"
};

struct AudioSession {
  std::wstring instanceId;
  std::wstring deviceId;
  DWORD pid = 0;
  bool active = false;  // currently playing, as opposed to open but idle
  ComPtr<IChannelAudioVolume> channels;
};

// Owns the MMDevice side: active render endpoints, their sessions, change
// notifications (posted to a window) and the per-app endpoint policy.
class AudioRouter {
 public:
  explicit AudioRouter(HWND notifyWindow) noexcept;
  ~AudioRouter();
  AudioRouter(const AudioRouter&) = delete;
  AudioRouter& operator=(const AudioRouter&) = delete;

  // Returns a user-readable error message, empty on success.
  [[nodiscard]] std::wstring Initialize();
  void RefreshDevices();

  [[nodiscard]] const std::vector<AudioDevice>& Devices() const noexcept { return devices_; }
  [[nodiscard]] std::wstring DefaultDeviceId() const;
  [[nodiscard]] bool HasDevice(std::wstring_view id) const noexcept;
  [[nodiscard]] std::wstring DeviceName(std::wstring_view id) const;

  // Live (active or inactive, not expired) application sessions on all active
  // render endpoints. The system sounds session is excluded.
  [[nodiscard]] std::vector<AudioSession> Sessions() const;

  [[nodiscard]] const PolicyConfig& Policy() const noexcept { return policy_; }

 private:
  struct Endpoint {
    AudioDevice info;
    ComPtr<IAudioSessionManager2> manager;
  };

  void ReleaseEndpoints() noexcept;

  HWND notifyWindow_ = nullptr;
  ComPtr<IMMDeviceEnumerator> enumerator_;
  ComPtr<IAudioSessionNotification> sessionNotifier_;
  ComPtr<IMMNotificationClient> deviceNotifier_;
  std::vector<Endpoint> endpoints_;
  std::vector<AudioDevice> devices_;
  PolicyConfig policy_;
};

}  // namespace monitone
