#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <optional>
#include <string>

// Read the current clipboard text (CF_UNICODETEXT).
// Returns empty optional if clipboard doesn't contain text.
std::optional<std::wstring> get_clipboard_text(HWND owner);

// Copy wide text to the Windows clipboard.
// owner may be nullptr; passing the app's hidden HWND is preferred.
bool set_clipboard_text(HWND owner, const std::wstring& text);
