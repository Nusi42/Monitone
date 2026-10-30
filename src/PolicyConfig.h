#pragma once

#include <windows.h>

#include <string>
#include <string_view>

#include "Win32.h"

struct IAudioPolicyConfigFactory;

namespace monitone {

// Per-application default output device. This is the undocumented mechanism
// behind "Settings > System > Sound > Volume mixer > Output device"
// (Windows 10 1803 and later).
class PolicyConfig {
 public:
  PolicyConfig();
  ~PolicyConfig();
  PolicyConfig(const PolicyConfig&) = delete;
  PolicyConfig& operator=(const PolicyConfig&) = delete;

  [[nodiscard]] HRESULT Initialize();
  [[nodiscard]] bool IsAvailable() const noexcept;

  // endpointId is an IMMDevice id ("{0.0.0.00000000}.{guid}"). An empty id
  // removes the override so the process follows the system default again.
  [[nodiscard]] HRESULT SetAppEndpoint(DWORD pid, std::wstring_view endpointId) const;
  [[nodiscard]] HRESULT GetAppEndpoint(DWORD pid, std::wstring& endpointId) const;
  [[nodiscard]] HRESULT ClearAll() const;

 private:
  ComPtr<IAudioPolicyConfigFactory> factory_;
};

}  // namespace monitone
