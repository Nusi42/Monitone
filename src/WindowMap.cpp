#include "WindowMap.h"

#include <dwmapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <array>
#include <string_view>

#include "Win32.h"

namespace monitone {
namespace {

constexpr int kMaxParentDepth = 10;

// Climbing into these would attach background audio to the taskbar or desktop.
constexpr std::array<std::wstring_view, 12> kStopProcesses{
    L"explorer.exe",      L"svchost.exe", L"services.exe", L"wininit.exe", L"winlogon.exe", L"sihost.exe",
    L"runtimebroker.exe", L"csrss.exe",   L"smss.exe",     L"system",      L"dllhost.exe",  L"taskhostw.exe"};

constexpr std::array<std::wstring_view, 4> kShellWindowClasses{L"Progman", L"WorkerW", L"Shell_TrayWnd",
                                                               L"Shell_SecondaryTrayWnd"};

std::optional<ULONGLONG> CreationTime(DWORD pid) {
  const UniqueHandle process{::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)};
  if (!process) return std::nullopt;
  FILETIME creation{};
  FILETIME exit{};
  FILETIME kernel{};
  FILETIME user{};
  if (!::GetProcessTimes(process.Get(), &creation, &exit, &kernel, &user)) return std::nullopt;
  return (static_cast<ULONGLONG>(creation.dwHighDateTime) << 32) | creation.dwLowDateTime;
}

std::wstring WindowClass(HWND window) {
  std::array<wchar_t, 128> buffer{};
  const int length = ::GetClassNameW(window, buffer.data(), static_cast<int>(buffer.size()));
  return std::wstring(buffer.data(), static_cast<size_t>(std::max(length, 0)));
}

bool IsShellWindow(std::wstring_view windowClass) {
  return std::ranges::find(kShellWindowClasses, windowClass) != kShellWindowClasses.end();
}

DWORD WindowProcess(HWND window) {
  DWORD pid = 0;
  ::GetWindowThreadProcessId(window, &pid);
  return pid;
}

bool IsCloaked(HWND window) {
  DWORD cloaked = 0;
  return SUCCEEDED(::DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked != 0;
}

bool HasArea(HWND window) {
  RECT rect{};
  return ::GetWindowRect(window, &rect) && rect.right - rect.left > 1 && rect.bottom - rect.top > 1;
}

}  // namespace

std::wstring ProcessImagePath(DWORD pid) {
  const UniqueHandle process{::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)};
  if (!process) return {};
  std::wstring path(UNICODE_STRING_MAX_CHARS, L'\0');
  auto length = static_cast<DWORD>(path.size());
  if (!::QueryFullProcessImageNameW(process.Get(), 0, path.data(), &length)) return {};
  path.resize(length);
  return path;
}

void ProcessWindowMap::Build() {
  windows_.clear();
  SnapshotProcesses();

  ::EnumWindows(
      [](HWND window, LPARAM param) -> BOOL {
        reinterpret_cast<ProcessWindowMap*>(param)->Consider(window);
        return TRUE;
      },
      reinterpret_cast<LPARAM>(this));

  // The window the user is working with always represents its process.
  if (const HWND foreground = ::GetForegroundWindow();
      foreground != nullptr && ::IsWindowVisible(foreground) && !IsShellWindow(WindowClass(foreground))) {
    Offer(WindowProcess(foreground), foreground, Score::Foreground);
  }
}

void ProcessWindowMap::SnapshotProcesses() {
  processes_.clear();
  const UniqueHandle snapshot{::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)};
  if (!snapshot) return;

  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  for (BOOL ok = ::Process32FirstW(snapshot.Get(), &entry); ok; ok = ::Process32NextW(snapshot.Get(), &entry)) {
    processes_[entry.th32ProcessID] =
        ProcessEntry{.parentPid = entry.th32ParentProcessID, .exe = ToLower(entry.szExeFile)};
  }
}

void ProcessWindowMap::Consider(HWND window) {
  if (!::IsWindowVisible(window)) return;
  const LONG_PTR exStyle = ::GetWindowLongPtrW(window, GWL_EXSTYLE);
  if ((exStyle & WS_EX_TOOLWINDOW) != 0 && (exStyle & WS_EX_APPWINDOW) == 0) return;

  const std::wstring windowClass = WindowClass(window);
  if (IsShellWindow(windowClass)) return;

  // Cloaked windows live on another virtual desktop or are suspended UWP frames.
  Score score = Score::Visible;
  if (IsCloaked(window)) {
    score = Score::Cloaked;
  } else if (::IsIconic(window)) {
    score = Score::Minimized;
  } else if (!HasArea(window)) {
    return;
  }

  const DWORD pid = WindowProcess(window);
  Offer(pid, window, score);

  // UWP: the frame belongs to ApplicationFrameHost.exe, the content window
  // (and the audio) to the app process.
  if (windowClass == L"ApplicationFrameWindow") {
    struct Context {
      ProcessWindowMap* self = nullptr;
      DWORD framePid = 0;
      HWND frame = nullptr;
      Score score = Score::Cloaked;
    } context{.self = this, .framePid = pid, .frame = window, .score = score};

    ::EnumChildWindows(
        window,
        [](HWND child, LPARAM param) -> BOOL {
          const auto& ctx = *reinterpret_cast<const Context*>(param);
          if (const DWORD childPid = WindowProcess(child); childPid != ctx.framePid)
            ctx.self->Offer(childPid, ctx.frame, ctx.score);
          return TRUE;
        },
        reinterpret_cast<LPARAM>(&context));
  }
}

void ProcessWindowMap::Offer(DWORD pid, HWND window, Score score) {
  const auto [it, inserted] = windows_.try_emplace(pid, Candidate{.window = window, .score = score});
  if (!inserted && score > it->second.score) it->second = Candidate{.window = window, .score = score};
}

bool ProcessWindowMap::IsStopProcess(DWORD pid) const {
  if (pid <= 4) return true;  // idle and System
  return std::ranges::find(kStopProcesses, ExeName(pid)) != kStopProcesses.end();
}

HWND ProcessWindowMap::WindowForProcess(DWORD pid) const {
  for (int depth = 0; depth < kMaxParentDepth && pid != 0; ++depth) {
    if (const auto window = windows_.find(pid); window != windows_.end()) return window->second.window;

    const auto process = processes_.find(pid);
    if (process == processes_.end()) return nullptr;
    const DWORD parentPid = process->second.parentPid;
    if (parentPid == 0 || parentPid == pid || IsStopProcess(parentPid)) return nullptr;

    // Process ids are recycled: a "parent" created after the child is an impostor.
    const auto childTime = CreationTime(pid);
    const auto parentTime = CreationTime(parentPid);
    if (childTime && parentTime && *parentTime > *childTime) return nullptr;
    pid = parentPid;
  }
  return nullptr;
}

std::wstring ProcessWindowMap::ExeName(DWORD pid) const {
  const auto it = processes_.find(pid);
  return it != processes_.end() ? it->second.exe : std::wstring{};
}

}  // namespace monitone
