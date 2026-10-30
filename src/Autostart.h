#pragma once

namespace monitone {

// "Start with Windows" via HKCU\Software\Microsoft\Windows\CurrentVersion\Run.
[[nodiscard]] bool IsAutostartEnabled();
void SetAutostart(bool enable);

}  // namespace monitone
