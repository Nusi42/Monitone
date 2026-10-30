#pragma once

#include <stop_token>
#include <string>
#include <thread>

#include "Balance.h"

namespace monitone {

// Plays a short beep pattern on one output device, on a worker thread.
// Starting a new tone stops the previous one.
class TonePlayer {
 public:
  TonePlayer() = default;
  ~TonePlayer() = default;  // std::jthread requests stop and joins
  TonePlayer(const TonePlayer&) = delete;
  TonePlayer& operator=(const TonePlayer&) = delete;

  // An empty deviceId plays on the system default output device.
  void Play(std::wstring deviceId, StereoGain gain);

 private:
  std::jthread worker_;
};

}  // namespace monitone
