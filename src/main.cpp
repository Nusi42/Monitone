// Monitone: plays each application's audio on the speakers that belong to the
// display its window is on.

#include <windows.h>

#include <commctrl.h>

#include "TrayApp.h"
#include "Win32.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
  using namespace monitone;

  const UniqueHandle mutex{::CreateMutexW(nullptr, TRUE, L"Local\\Monitone.SingleInstance")};
  if (::GetLastError() == ERROR_ALREADY_EXISTS) {
    ::MessageBoxW(nullptr, L"Monitone is already running. Look for its icon in the notification area.", L"Monitone",
                  MB_ICONINFORMATION);
    return 0;
  }

  const INITCOMMONCONTROLSEX controls{
      .dwSize = sizeof(INITCOMMONCONTROLSEX),
      .dwICC = ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES | ICC_UPDOWN_CLASS | ICC_LINK_CLASS};
  ::InitCommonControlsEx(&controls);

  // MTA: audio notifications are free-threaded and only post messages to the UI thread.
  const ComApartment apartment{COINIT_MULTITHREADED};
  if (FAILED(apartment.Result())) return 1;

  TrayApp app;
  return app.Run(instance);
}
