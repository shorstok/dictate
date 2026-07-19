#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <atomic>
#include <thread>

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
    void hook_thread_func();
    bool* leaked_flag_for(DWORD vk);
    void compensate_suppressed_up(DWORD vk);

    HWND  hwnd_{nullptr};
    int   id_{0};
    bool  registered_{false};

    // Hold-mode state (accessed only on the hook thread).
    HHOOK hook_{nullptr};
    bool  lctrl_down_{false};
    bool  rctrl_down_{false};
    bool  win_down_{false};
    bool  hold_active_{false};
    bool  combo_session_{false};
    bool  latched_{false};

    // Downs that passed through to the rest of the system (the first modifier
    // of a chord must — the hook can't yet know a chord is coming). When the
    // matching up gets suppressed, a compensating release is injected so the
    // OS doesn't consider the key stuck down.
    bool  lctrl_leaked_{false};
    bool  rctrl_leaked_{false};
    bool  lwin_leaked_{false};
    bool  rwin_leaked_{false};

    // Dedicated thread for the low-level hook. Status/id are written by the
    // hook thread and read by the main thread, hence atomics.
    enum class HookStatus : int { pending, ok, failed };
    std::thread hook_thread_;
    std::atomic<DWORD>      hook_thread_id_{0};
    std::atomic<HookStatus> hook_status_{HookStatus::pending};

    static HotkeyManager* self_;
};
