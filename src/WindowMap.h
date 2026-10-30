#pragma once

#include <windows.h>

#include <optional>
#include <string>
#include <unordered_map>

namespace monitone {

// Full path of the process image, empty if the process cannot be opened.
[[nodiscard]] std::wstring ProcessImagePath(DWORD pid);

// Snapshot of "which top-level window represents process X".
// Audio is often produced by helper processes without windows (Chrome or Edge
// audio service, Firefox content processes, ...), so lookups walk up the
// parent process chain until a process with a window is found.
class ProcessWindowMap {
 public:
  void Build();

  [[nodiscard]] HWND WindowForProcess(DWORD pid) const;
  [[nodiscard]] std::wstring ExeName(DWORD pid) const;  // lower case, empty if unknown

 private:
  // Higher wins; within equal scores the first window in Z-order wins.
  enum class Score : int { Cloaked = 1, Minimized = 2, Visible = 3, Foreground = 4 };

  struct Candidate {
    HWND window = nullptr;
    Score score = Score::Cloaked;
  };

  struct ProcessEntry {
    DWORD parentPid = 0;
    std::wstring exe;
  };

  void SnapshotProcesses();
  void Consider(HWND window);
  void Offer(DWORD pid, HWND window, Score score);
  [[nodiscard]] bool IsStopProcess(DWORD pid) const;

  std::unordered_map<DWORD, Candidate> windows_;
  std::unordered_map<DWORD, ProcessEntry> processes_;
};

}  // namespace monitone
