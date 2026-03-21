#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>

// Copy wide text to the Windows clipboard.
// owner may be nullptr; passing the app's hidden HWND is preferred.
bool set_clipboard_text(HWND owner, const std::wstring& text);
