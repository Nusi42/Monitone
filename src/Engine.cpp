#include "Engine.h"

#include <dwmapi.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <span>
#include <unordered_set>

namespace monitone {
namespace {

constexpr float kGainTolerance = 0.005f;

bool IsProcessAlive(DWORD pid) {
  const UniqueHandle process{::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)};
  DWORD exitCode = 0;
  return process && ::GetExitCodeProcess(process.Get(), &exitCode) && exitCode == STILL_ACTIVE;
}

bool NearlyEqual(StereoGain a, StereoGain b) noexcept {
  return std::fabs(a.left - b.left) < kGainTolerance && std::fabs(a.right - b.right) < kGainTolerance;
}

// Standard WAVEFORMATEXTENSIBLE order: FL FR FC LFE BL BR SL SR.
float ChannelGain(size_t channel, StereoGain gain) noexcept {
  switch (channel) {
    case 2:
      return (gain.left + gain.right) * 0.5f;  // centre
    case 3:
      return 1.0f;  // LFE
    default:
      return channel % 2 == 0 ? gain.left : gain.right;
  }
}

UINT32 ChannelCount(IChannelAudioVolume& channels) {
  UINT32 count = 0;
  return SUCCEEDED(channels.GetChannelCount(&count)) ? count : 0;
}

RECT WindowBounds(HWND window) {
  RECT bounds{};
  if (FAILED(::DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, sizeof(bounds))))
    ::GetWindowRect(window, &bounds);
  return bounds;
}

DWORD WindowProcess(HWND window) {
  DWORD pid = 0;
  ::GetWindowThreadProcessId(window, &pid);
  return pid;
}

std::wstring WindowTitle(HWND window) {
  std::array<wchar_t, 256> buffer{};
  const int length = ::GetWindowTextW(window, buffer.data(), static_cast<int>(buffer.size()));
  return std::wstring(buffer.data(), static_cast<size_t>(std::max(length, 0)));
}

}  // namespace

Engine::Engine(const Config& config, const AudioRouter& audio, MonitorList& monitors) noexcept
    : config_{config}, audio_{audio}, monitors_{monitors} {}

void Engine::Tick() {
  const bool apply = !config_.paused;
  const DWORD ownPid = ::GetCurrentProcessId();  // e.g. the settings test tone

  windows_.Build();
  const std::vector<AudioSession> sessions = audio_.Sessions();
  std::vector<RouteReport> report;
  std::unordered_set<DWORD> seenPids;
  std::unordered_set<std::wstring> seenSessions;
  std::unordered_set<DWORD> playingPids;
  for (const AudioSession& session : sessions) {
    if (session.active) playingPids.insert(session.pid);
  }

  for (const AudioSession& session : sessions) {
    if (session.pid == ownPid) continue;
    seenSessions.insert(session.instanceId);
    const auto [pidIt, firstOfProcess] = seenPids.insert(session.pid);
    RouteReport entry = Evaluate(session, firstOfProcess, playingPids.contains(session.pid), apply);
    if (firstOfProcess) {
      report.push_back(std::move(entry));
    } else if (entry.playing) {
      // A process with several sessions counts as playing if any of them plays.
      const auto existing = std::ranges::find(report, session.pid, &RouteReport::pid);
      if (existing != report.end()) existing->playing = true;
    }
  }

  // Forget processes that are gone and sessions that ended.
  std::erase_if(routed_,
                [&](const auto& item) { return !seenPids.contains(item.first) && !IsProcessAlive(item.first); });
  std::erase_if(balanced_, [&](const auto& item) { return !seenSessions.contains(item.first); });
  std::erase_if(locks_, [&](const auto& item) { return !seenPids.contains(item.first); });
  report_ = std::move(report);
}

// Browsers play all tabs of all their windows through one audio process, so
// the window in front is not necessarily the one playing. The window that is
// in front when playback starts is: it is kept until playback starts again or
// the window goes away. Programs with one window are not affected.
HWND Engine::RepresentativeWindow(DWORD pid, bool playing) {
  const HWND candidate = windows_.WindowForProcess(pid);
  const auto it = locks_.find(pid);
  if (candidate == nullptr) {
    if (it != locks_.end()) locks_.erase(it);
    return nullptr;
  }
  if (it != locks_.end()) {
    WindowLock& lock = it->second;
    const bool started = playing && !lock.playing;
    const bool valid = ::IsWindow(lock.window) && ::IsWindowVisible(lock.window) &&
                       WindowProcess(lock.window) == WindowProcess(candidate);
    lock.playing = playing;
    if (valid && !started) return lock.window;
  }
  locks_.insert_or_assign(pid, WindowLock{.window = candidate, .playing = playing});
  return candidate;
}

RouteReport Engine::Evaluate(const AudioSession& session, bool firstOfProcess, bool playing, bool apply) {
  RouteReport report{.pid = session.pid,
                     .exe = windows_.ExeName(session.pid),
                     .exePath = ProcessImagePath(session.pid),
                     .device = audio_.DeviceName(session.deviceId),
                     .playing = session.active};
  if (report.exe.empty()) report.exe = std::format(L"pid {}", session.pid);

  const HWND window = RepresentativeWindow(session.pid, playing);
  const MonitorInfo* monitor = window != nullptr ? monitors_.FromWindow(window) : nullptr;
  if (window != nullptr) report.windowTitle = WindowTitle(window);
  if (monitor != nullptr) report.monitor = monitors_.Label(*monitor);
  if (!apply) return report;

  if (const AppRule* rule = config_.FindAppRule(report.exe); rule != nullptr) {
    ResetBalance(session);
    if (rule->action == AppAction::Exclude) {
      if (firstOfProcess) ApplyRoute(session.pid, {}, report);
      return report;
    }
    if (rule->action == AppAction::Pin && firstOfProcess) {
      if (audio_.HasDevice(rule->deviceId)) {
        ApplyRoute(session.pid, rule->deviceId, report);
      } else {
        report.note = L"pinned device is not connected";
        ApplyRoute(session.pid, {}, report);
      }
    }
    return report;
  }

  if (monitor == nullptr) {
    report.note = L"no window, left alone";
    return report;
  }

  if (firstOfProcess) {
    const MonitorRule* rule = config_.Find(*monitor);
    std::wstring_view target = rule != nullptr ? std::wstring_view{rule->deviceId} : std::wstring_view{};
    if (!target.empty() && !audio_.HasDevice(target)) {
      report.note = L"assigned device is not connected";
      target = {};
    }
    ApplyRoute(session.pid, target, report);
  }

  if (config_.balance == BalanceMode::Off)
    ResetBalance(session);
  else
    ApplyBalance(session, GainForPan(PanPosition(window, *monitor)), report);
  return report;
}

void Engine::ApplyRoute(DWORD pid, std::wstring_view target, RouteReport& report) {
  const auto it = routed_.find(pid);
  if (target.empty()) {
    if (it == routed_.end()) return;  // never touched: keep whatever the user set
    if (SUCCEEDED(audio_.Policy().SetAppEndpoint(pid, {}))) routed_.erase(it);
    return;
  }
  if (it == routed_.end() || !EqualsNoCase(it->second, target)) {
    if (const HRESULT hr = audio_.Policy().SetAppEndpoint(pid, target); FAILED(hr)) {
      report.note = std::format(L"routing failed ({})", FormatHResult(hr));
      return;
    }
    routed_.insert_or_assign(pid, std::wstring{target});
  }
  report.device = audio_.DeviceName(target);
}

void Engine::ApplyBalance(const AudioSession& session, StereoGain gain, RouteReport& report) {
  if (!session.channels || session.instanceId.empty()) return;
  const UINT32 count = ChannelCount(*session.channels.Get());
  if (count < 2) return;
  report.balance = gain;

  const auto it = balanced_.find(session.instanceId);
  if (it != balanced_.end() && NearlyEqual(it->second.gain, gain)) return;

  BalanceState state{.gain = gain};
  if (it != balanced_.end()) {
    state.original = std::move(it->second.original);
  } else {
    state.original.assign(count, 1.0f);
    session.channels->GetAllVolumes(count, state.original.data());
  }

  std::vector<float> gains(count, 1.0f);
  for (size_t i = 0; i < gains.size(); ++i) gains[i] = ChannelGain(i, gain);
  if (SUCCEEDED(session.channels->SetAllVolumes(count, gains.data(), nullptr)))
    balanced_.insert_or_assign(session.instanceId, std::move(state));
}

void Engine::ResetBalance(const AudioSession& session) {
  const auto it = balanced_.find(session.instanceId);
  if (it == balanced_.end()) return;
  std::span<float> original{it->second.original};
  if (session.channels && ChannelCount(*session.channels.Get()) == original.size())
    session.channels->SetAllVolumes(static_cast<UINT32>(original.size()), original.data(), nullptr);
  balanced_.erase(it);
}

float Engine::PanPosition(HWND window, const MonitorInfo& monitor) const {
  if (config_.balance == BalanceMode::Monitor) return MonitorIndexPan(monitors_, monitor);
  const RECT bounds = ::IsIconic(window) ? monitor.rect : WindowBounds(window);
  return HorizontalPan((bounds.left + bounds.right) / 2, monitors_.VirtualRect());
}

void Engine::Restore() {
  for (const AudioSession& session : audio_.Sessions()) ResetBalance(session);
  balanced_.clear();
  for (const auto& [pid, endpoint] : routed_) (void)audio_.Policy().SetAppEndpoint(pid, {});
  routed_.clear();
}

}  // namespace monitone
