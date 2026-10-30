#include "TestTone.h"

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <span>

#include "Win32.h"

namespace monitone {
namespace {

constexpr DWORD kSampleRate = 48000;
constexpr WORD kChannels = 2;
constexpr float kFrequencyHz = 660.0f;
constexpr float kAmplitude = 0.3f;
constexpr int kBeeps = 3;
constexpr float kBeepSeconds = 0.15f;
constexpr float kGapSeconds = 0.10f;
constexpr float kFadeSeconds = 0.01f;
constexpr REFERENCE_TIME kBufferDuration = 1'000'000;  // 100 ms in 100 ns units
constexpr DWORD kFillIntervalMs = 10;

constexpr float kPatternSeconds = kBeeps * kBeepSeconds + (kBeeps - 1) * kGapSeconds;

// Beep pattern amplitude at time t, with short fades against clicks.
float Envelope(float t) noexcept {
  const float period = kBeepSeconds + kGapSeconds;
  const float inBeep = std::fmod(t, period);
  if (t >= kPatternSeconds || inBeep >= kBeepSeconds) return 0.0f;
  return std::min({1.0f, inBeep / kFadeSeconds, (kBeepSeconds - inBeep) / kFadeSeconds});
}

ComPtr<IMMDevice> OpenDevice(const std::wstring& deviceId) {
  ComPtr<IMMDeviceEnumerator> enumerator;
  if (FAILED(::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))))
    return {};
  ComPtr<IMMDevice> device;
  const HRESULT hr = deviceId.empty() ? enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device)
                                      : enumerator->GetDevice(deviceId.c_str(), &device);
  return SUCCEEDED(hr) ? device : ComPtr<IMMDevice>{};
}

void RenderTone(std::stop_token stop, const std::wstring& deviceId, StereoGain gain) {
  const ComApartment apartment{COINIT_MULTITHREADED};
  if (FAILED(apartment.Result())) return;

  const ComPtr<IMMDevice> device = OpenDevice(deviceId);
  if (!device) return;
  ComPtr<IAudioClient> client;
  if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                              reinterpret_cast<void**>(client.ReleaseAndGetAddressOf()))))
    return;

  // The audio engine converts our fixed float format to the device mix format.
  WAVEFORMATEX format{};
  format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
  format.nChannels = kChannels;
  format.nSamplesPerSec = kSampleRate;
  format.wBitsPerSample = 32;
  format.nBlockAlign = static_cast<WORD>(format.nChannels * format.wBitsPerSample / 8);
  format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
  constexpr DWORD kFlags = AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
  if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, kFlags, kBufferDuration, 0, &format, nullptr))) return;

  UINT32 bufferFrames = 0;
  ComPtr<IAudioRenderClient> render;
  if (FAILED(client->GetBufferSize(&bufferFrames)) || FAILED(client->GetService(IID_PPV_ARGS(&render)))) return;
  if (FAILED(client->Start())) return;

  const auto totalFrames = static_cast<UINT64>(kPatternSeconds * static_cast<float>(kSampleRate));
  UINT64 written = 0;
  while (written < totalFrames && !stop.stop_requested()) {
    UINT32 padding = 0;
    if (FAILED(client->GetCurrentPadding(&padding))) break;
    const auto frames = static_cast<UINT32>(std::min<UINT64>(bufferFrames - padding, totalFrames - written));
    BYTE* data = nullptr;
    if (frames > 0 && SUCCEEDED(render->GetBuffer(frames, &data))) {
      const std::span<float> samples{reinterpret_cast<float*>(data), static_cast<size_t>(frames) * kChannels};
      for (UINT32 i = 0; i < frames; ++i) {
        const float t = static_cast<float>(written + i) / static_cast<float>(kSampleRate);
        const float value = kAmplitude * Envelope(t) * std::sin(2.0f * std::numbers::pi_v<float> * kFrequencyHz * t);
        samples[static_cast<size_t>(i) * kChannels] = value * gain.left;
        samples[static_cast<size_t>(i) * kChannels + 1] = value * gain.right;
      }
      render->ReleaseBuffer(frames, 0);
      written += frames;
    }
    ::Sleep(kFillIntervalMs);
  }

  // Let the queued audio play out before stopping the stream.
  UINT32 padding = 0;
  while (!stop.stop_requested() && SUCCEEDED(client->GetCurrentPadding(&padding)) && padding > 0)
    ::Sleep(kFillIntervalMs);
  client->Stop();
}

}  // namespace

void TonePlayer::Play(std::wstring deviceId, StereoGain gain) {
  worker_ = std::jthread{[id = std::move(deviceId), gain](std::stop_token stop) { RenderTone(stop, id, gain); }};
}

}  // namespace monitone
