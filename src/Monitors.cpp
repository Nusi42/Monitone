#include "Monitors.h"

#include <algorithm>
#include <format>
#include <map>
#include <utility>

namespace monitone {
namespace {

struct TargetName {
  std::wstring friendlyName;
  std::wstring devicePath;
};

// Maps "\\.\DISPLAYn" to the friendly name and device path of the monitor it drives.
std::map<std::wstring, TargetName> QueryTargetNames() {
  std::vector<DISPLAYCONFIG_PATH_INFO> paths;
  std::vector<DISPLAYCONFIG_MODE_INFO> modes;
  LONG status = ERROR_INSUFFICIENT_BUFFER;
  while (status == ERROR_INSUFFICIENT_BUFFER) {
    UINT32 pathCount = 0;
    UINT32 modeCount = 0;
    if (::GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) return {};
    paths.resize(pathCount);
    modes.resize(modeCount);
    status = ::QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr);
    paths.resize(pathCount);
  }
  if (status != ERROR_SUCCESS) return {};

  std::map<std::wstring, TargetName> result;
  for (const DISPLAYCONFIG_PATH_INFO& path : paths) {
    DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
    source.header = {.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,
                     .size = sizeof(source),
                     .adapterId = path.sourceInfo.adapterId,
                     .id = path.sourceInfo.id};
    if (::DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS) continue;

    DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
    target.header = {.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME,
                     .size = sizeof(target),
                     .adapterId = path.targetInfo.adapterId,
                     .id = path.targetInfo.id};
    if (::DisplayConfigGetDeviceInfo(&target.header) != ERROR_SUCCESS) continue;

    // In clone mode one source drives several targets; the first one wins.
    result.try_emplace(source.viewGdiDeviceName, TargetName{.friendlyName = target.monitorFriendlyDeviceName,
                                                            .devicePath = target.monitorDevicePath});
  }
  return result;
}

BOOL CALLBACK CollectMonitor(HMONITOR handle, HDC, LPRECT, LPARAM param) {
  auto& monitors = *reinterpret_cast<std::vector<MonitorInfo>*>(param);
  MONITORINFOEXW info{};
  info.cbSize = sizeof(info);
  if (::GetMonitorInfoW(handle, &info)) {
    monitors.push_back({.handle = handle,
                        .rect = info.rcMonitor,
                        .primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0,
                        .gdiName = info.szDevice});
  }
  return TRUE;
}

void ResolveNames(MonitorInfo& monitor, const std::map<std::wstring, TargetName>& names) {
  if (const auto it = names.find(monitor.gdiName); it != names.end()) {
    monitor.friendlyName = it->second.friendlyName;
    monitor.key = it->second.devicePath;
  }
  if (monitor.key.empty()) {
    DISPLAY_DEVICEW device{};
    device.cb = sizeof(device);
    if (::EnumDisplayDevicesW(monitor.gdiName.c_str(), 0, &device, EDD_GET_DEVICE_INTERFACE_NAME)) {
      monitor.key = device.DeviceID;
      if (monitor.friendlyName.empty()) monitor.friendlyName = device.DeviceString;
    }
  }
  if (monitor.friendlyName.empty()) monitor.friendlyName = L"Monitor";
}

std::wstring PositionName(int index, size_t count) {
  switch (count) {
    case 0:
    case 1:
      return {};
    case 2:
      return index == 0 ? L"left" : L"right";
    case 3:
      return index == 0 ? L"left" : (index == 1 ? L"center" : L"right");
    default:
      return std::format(L"{} from left", index + 1);
  }
}

}  // namespace

void MonitorList::Refresh() {
  std::vector<MonitorInfo> monitors;
  ::EnumDisplayMonitors(nullptr, nullptr, CollectMonitor, reinterpret_cast<LPARAM>(&monitors));

  const auto names = QueryTargetNames();
  for (MonitorInfo& monitor : monitors) ResolveNames(monitor, names);

  std::ranges::sort(monitors, {}, [](const MonitorInfo& m) { return std::pair{m.rect.left, m.rect.top}; });
  for (int i = 0; auto& monitor : monitors) monitor.index = i++;
  monitors_ = std::move(monitors);
}

const MonitorInfo* MonitorList::FromHandle(HMONITOR handle) const noexcept {
  const auto it = std::ranges::find(monitors_, handle, &MonitorInfo::handle);
  return it != monitors_.end() ? &*it : nullptr;
}

const MonitorInfo* MonitorList::FromWindow(HWND window) {
  const HMONITOR handle = ::MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
  if (const MonitorInfo* monitor = FromHandle(handle)) return monitor;
  Refresh();
  return FromHandle(handle);
}

RECT MonitorList::VirtualRect() const noexcept {
  if (monitors_.empty()) return RECT{};
  RECT bounds = monitors_.front().rect;
  for (const MonitorInfo& monitor : monitors_) ::UnionRect(&bounds, &bounds, &monitor.rect);
  return bounds;
}

std::wstring MonitorList::Position(const MonitorInfo& monitor) const {
  return PositionName(monitor.index, monitors_.size());
}

std::wstring MonitorList::Label(const MonitorInfo& monitor) const {
  std::wstring position = PositionName(monitor.index, monitors_.size());
  if (monitor.primary) position += position.empty() ? L"primary" : L", primary";

  std::wstring label = std::format(L"Display {}", monitor.index + 1);
  if (!position.empty()) label += std::format(L" ({})", position);
  label += std::format(L": {}, {}x{}", monitor.friendlyName, monitor.rect.right - monitor.rect.left,
                       monitor.rect.bottom - monitor.rect.top);
  return label;
}

}  // namespace monitone
