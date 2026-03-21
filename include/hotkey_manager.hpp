#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

class HotkeyManager {
public:
    HotkeyManager() = default;
    ~HotkeyManager();
    HotkeyManager(const HotkeyManager&) = delete;
    HotkeyManager& operator=(const HotkeyManager&) = delete;

    bool register_hotkey(HWND hwnd, int id, UINT modifiers, UINT vk);
    void unregister_hotkey();

private:
    HWND hwnd_{nullptr};
    int  id_{0};
    bool registered_{false};
};
