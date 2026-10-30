#pragma once

#include <windows.h>

#include "Config.h"
#include "Monitors.h"

namespace monitone {

struct StereoGain {
  float left = 1.0f;
  float right = 1.0f;
};

// Linear balance law for pan in [0, 1]: the centre plays both channels at
// full level, the edges one channel only.
[[nodiscard]] StereoGain GainForPan(float pan) noexcept;

// Pan of a horizontal position on the desktop, 0 = left edge, 1 = right edge.
[[nodiscard]] float HorizontalPan(LONG centerX, const RECT& desktop) noexcept;

// Pan by display order: leftmost display 0, rightmost 1.
[[nodiscard]] float MonitorIndexPan(const MonitorList& monitors, const MonitorInfo& monitor) noexcept;

// Balance a display gets in the given mode; window mode uses the display centre.
[[nodiscard]] StereoGain DisplayGain(BalanceMode mode, const MonitorList& monitors, const MonitorInfo& monitor);

}  // namespace monitone
