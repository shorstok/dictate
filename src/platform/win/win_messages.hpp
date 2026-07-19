#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// Custom window messages used for cross-thread communication within the
// Windows platform layer (hook thread / worker -> UI thread).
constexpr UINT WM_APP_DISPATCH     = WM_APP + 1;   // lparam = std::function<void()>* (owned)
constexpr UINT WM_APP_TRAY         = WM_APP + 10;
constexpr UINT WM_APP_HOLD_START   = WM_APP + 20;
constexpr UINT WM_APP_HOLD_STOP    = WM_APP + 21;
constexpr UINT WM_APP_HOLD_LATCHED = WM_APP + 22;

// ToggleHotkey input mode (see core/config.hpp).
constexpr UINT kHotkeyModifiers = MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT;
constexpr UINT kHotkeyVK        = VK_F9;
constexpr int  kHotkeyId        = 1;
