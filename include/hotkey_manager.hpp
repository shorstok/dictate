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
    bool install_hold_ctrl_win(HWND hwnd);
    void unregister_hotkey();

private:
    static LRESULT CALLBACK keyboard_proc(int code, WPARAM wparam, LPARAM lparam);

    HWND  hwnd_{nullptr};
    int   id_{0};
    bool  registered_{false};

    HHOOK hook_{nullptr};
    bool  ctrl_down_{false};
    bool  win_down_{false};
    bool  hold_active_{false};

    static HotkeyManager* self_;
};
