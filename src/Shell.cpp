#include "Shell.h"

#include <windows.h>

#include <shellapi.h>

#include <thread>

#include "Win32.h"

namespace monitone {

void OpenInShell(std::wstring target) {
  std::thread{[path = std::move(target)] {
    const ComApartment apartment{COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE};
    ::ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  }}.detach();
}

}  // namespace monitone
