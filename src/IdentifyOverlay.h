#pragma once

#include <windows.h>

#include "Monitors.h"

namespace monitone {

// Shows the display number and name in the centre of every display for a few
// seconds, like the Windows "Identify" button.
void ShowDisplayNumbers(HINSTANCE instance, const MonitorList& monitors);

// Destroys the overlays of the calling thread that are still visible and
// unregisters the window class, so the module that owns it can be unloaded.
void CloseDisplayNumbers(HINSTANCE instance);

}  // namespace monitone
