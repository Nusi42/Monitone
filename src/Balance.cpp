#include "Balance.h"

#include <algorithm>

namespace monitone {

StereoGain GainForPan(float pan) noexcept {
  return {.left = std::min(1.0f, 2.0f * (1.0f - pan)), .right = std::min(1.0f, 2.0f * pan)};
}

float HorizontalPan(LONG centerX, const RECT& desktop) noexcept {
  const auto width = static_cast<float>(desktop.right - desktop.left);
  if (width <= 0.0f) return 0.5f;
  return std::clamp(static_cast<float>(centerX - desktop.left) / width, 0.0f, 1.0f);
}

float MonitorIndexPan(const MonitorList& monitors, const MonitorInfo& monitor) noexcept {
  const size_t count = monitors.All().size();
  return count > 1 ? static_cast<float>(monitor.index) / static_cast<float>(count - 1) : 0.5f;
}

StereoGain DisplayGain(BalanceMode mode, const MonitorList& monitors, const MonitorInfo& monitor) {
  switch (mode) {
    case BalanceMode::Monitor:
      return GainForPan(MonitorIndexPan(monitors, monitor));
    case BalanceMode::Window:
      return GainForPan(HorizontalPan((monitor.rect.left + monitor.rect.right) / 2, monitors.VirtualRect()));
    case BalanceMode::Off:
      break;
  }
  return {};
}

}  // namespace monitone
