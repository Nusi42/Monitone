#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace monitone {

struct MonitorInfo {
  HMONITOR handle = nullptr;
  RECT rect{};
  bool primary = false;
  int index = 0;              // position in left-to-right order
  std::wstring gdiName;       // "\\.\DISPLAY1", may change when hardware changes
  std::wstring key;           // monitor device interface path, stable per monitor and port
  std::wstring friendlyName;  // "DELL U2720Q"
};

class MonitorList {
 public:
  void Refresh();

  [[nodiscard]] const std::vector<MonitorInfo>& All() const noexcept { return monitors_; }
  [[nodiscard]] const MonitorInfo* FromHandle(HMONITOR handle) const noexcept;
  // Refreshes once if the window is on a display that appeared since the last refresh.
  [[nodiscard]] const MonitorInfo* FromWindow(HWND window);
  [[nodiscard]] RECT VirtualRect() const noexcept;
  [[nodiscard]] std::wstring Label(const MonitorInfo& monitor) const;
  // "left", "center", "right", "3 from left" or empty for a single display.
  [[nodiscard]] std::wstring Position(const MonitorInfo& monitor) const;

 private:
  std::vector<MonitorInfo> monitors_;
};

}  // namespace monitone
