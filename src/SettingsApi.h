/* Interface between Monitone.exe and MonitoneSettings.dll.
 *
 * Plain C on purpose: both modules link the C runtime statically and have
 * separate heaps, so no C++ object or allocation may cross the boundary.
 * Strings passed in either direction are only valid during the call.
 *
 * Lifetime:
 *   exe: LoadLibrary, MonitoneSettingsOpen(host)      -> UI thread starts
 *   DLL: host->closed(context) when the window is gone (from the UI thread)
 *   exe: MonitoneSettingsShutdown(), FreeLibrary       -> everything released
 *
 * Host callbacks are invoked on the DLL's UI thread; the host marshals them
 * to its own thread. MonitoneSettingsShutdown keeps dispatching sent
 * messages while it waits, so the host can call it at any time.
 */
#ifndef MONITONE_SETTINGS_API_H
#define MONITONE_SETTINGS_API_H

#include <stdint.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MONITONE_SETTINGS_API_VERSION 1u

enum MonitoneSettingsPage {
  MONITONE_PAGE_DISPLAYS = 0,
  MONITONE_PAGE_APPLICATIONS = 1,
  MONITONE_PAGE_RULES = 2,
  MONITONE_PAGE_GENERAL = 3,
  MONITONE_PAGE_ABOUT = 4
};

typedef struct MonitoneReportEntry {
  uint32_t pid;
  const wchar_t* exe;
  const wchar_t* exePath;
  const wchar_t* windowTitle;
  const wchar_t* monitor;
  const wchar_t* device;
  const wchar_t* note;
  int32_t playing;
  int32_t hasBalance;
  float left;
  float right;
} MonitoneReportEntry;

typedef void (*MonitoneReportSink)(void* sinkContext, const MonitoneReportEntry* entry);

typedef struct MonitoneHost {
  uint32_t apiVersion; /* MONITONE_SETTINGS_API_VERSION */
  void* context;
  const wchar_t* configPath;
  const wchar_t* version;
  /* The configuration file was written; reload it. */
  void (*configChanged)(void* context);
  /* Runs an evaluation pass and calls sink once per application. */
  void (*queryReport)(void* context, MonitoneReportSink sink, void* sinkContext);
  /* Clears all per-application output devices stored by Windows. */
  void (*resetWindowsAppDevices)(void* context);
  /* The settings window is gone; call MonitoneSettingsShutdown and unload. */
  void (*closed)(void* context);
} MonitoneHost;

/* Returns S_OK, or an HRESULT; HRESULT_FROM_WIN32(ERROR_PRODUCT_UNINSTALLED)
 * means the Microsoft Edge WebView2 Runtime is not installed. If the window
 * is already open it is activated instead. */
typedef long(__stdcall* MonitoneSettingsOpenFn)(const MonitoneHost* host, int page);
/* Brings an open window to the front and shows page. */
typedef void(__stdcall* MonitoneSettingsActivateFn)(int page);
/* Closes the window if needed and waits until the UI thread has ended. */
typedef void(__stdcall* MonitoneSettingsShutdownFn)(void);

#ifdef __cplusplus
}
#endif

#endif /* MONITONE_SETTINGS_API_H */
