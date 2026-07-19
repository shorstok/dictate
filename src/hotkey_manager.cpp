#include "hotkey_manager.hpp"
#include "app.hpp"

HotkeyManager* HotkeyManager::self_ = nullptr;

namespace {
    bool is_ctrl_vk(DWORD vk) {
        return vk == VK_LCONTROL || vk == VK_RCONTROL;
    }

    bool is_win_vk(DWORD vk) {
        return vk == VK_LWIN || vk == VK_RWIN;
    }
}

HotkeyManager::~HotkeyManager() {
    unregister_hotkey();
}

bool HotkeyManager::register_hotkey(HWND hwnd, int id, UINT modifiers, UINT vk) {
    hwnd_        = hwnd;
    id_          = id;
    registered_  = (RegisterHotKey(hwnd, id, modifiers, vk) != FALSE);
    return registered_;
}

// ---------------------------------------------------------------------------
// Dedicated hook thread
// ---------------------------------------------------------------------------

void HotkeyManager::hook_thread_func() {
    hook_thread_id_ = GetCurrentThreadId();
    hook_ = SetWindowsHookExW(WH_KEYBOARD_LL, keyboard_proc, nullptr, 0);
    if (!hook_) {
        hook_status_ = HookStatus::failed;
        return;
    }

    hook_status_ = HookStatus::ok;

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UnhookWindowsHookEx(hook_);
    hook_ = nullptr;
}

bool HotkeyManager::install_hold_ctrl_win(HWND hwnd) {
    hwnd_ = hwnd;
    self_ = this;

    hook_status_ = HookStatus::pending;
    hook_thread_ = std::thread([this]() {
        hook_thread_func();
    });

    while (hook_status_ == HookStatus::pending) {
        Sleep(1);
    }

    if (hook_status_ != HookStatus::ok) {
        hook_thread_.join();
        self_ = nullptr;
        return false;
    }

    return true;
}

void HotkeyManager::unregister_hotkey() {
    if (registered_) {
        UnregisterHotKey(hwnd_, id_);
        registered_ = false;
    }
    if (hook_thread_.joinable()) {
        if (hook_thread_id_ != 0) {
            PostThreadMessageW(hook_thread_id_, WM_QUIT, 0, 0);
        }
        hook_thread_.join();
    }
    hook_ = nullptr;
    if (self_ == this) {
        self_ = nullptr;
    }
    lctrl_down_ = false;
    rctrl_down_ = false;
    win_down_ = false;
    hold_active_ = false;
    combo_session_ = false;
    latched_ = false;
    hook_status_ = HookStatus::pending;
    hook_thread_id_ = 0;
}

// ---------------------------------------------------------------------------
// Low-level keyboard hook callback (runs on hook thread)
//
// State machine:
//   idle  ──(Ctrl+Win both down)──▸  hold_active   ──(new Ctrl press)──▸  latched
//   hold_active  ──(either key up, not latched)──▸  idle  + post STOP
//   latched  ──(all keys released)──▸  latched (waiting for unlatch)
//   latched  ──(Ctrl down after all keys up)──▸  idle  + post STOP
// ---------------------------------------------------------------------------

LRESULT CALLBACK HotkeyManager::keyboard_proc(int code, WPARAM wparam, LPARAM lparam) {
    if (code < 0 || !self_) {
        return CallNextHookEx(nullptr, code, wparam, lparam);
    }

    auto* k = reinterpret_cast<KBDLLHOOKSTRUCT*>(lparam);
    if (!k) {
        return CallNextHookEx(nullptr, code, wparam, lparam);
    }

    if ((k->flags & LLKHF_INJECTED) != 0) {
        return CallNextHookEx(nullptr, code, wparam, lparam);
    }

    const bool is_down = (wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN);
    const bool is_up   = (wparam == WM_KEYUP   || wparam == WM_SYSKEYUP);

    if (!is_down && !is_up) {
        return CallNextHookEx(nullptr, code, wparam, lparam);
    }

    const DWORD vk = k->vkCode;
    if (!is_ctrl_vk(vk) && !is_win_vk(vk)) {
        return CallNextHookEx(nullptr, code, wparam, lparam);
    }

    // --- Latch activation: new Ctrl press while combo is held ---
    // Only triggers for a physically distinct key (not autorepeat) by
    // checking that this specific VK was not already down.
    if (self_->hold_active_ && !self_->latched_ && is_down && is_ctrl_vk(vk)) {
        const bool already_down = (vk == VK_LCONTROL) ? self_->lctrl_down_
                                                       : self_->rctrl_down_;
        if (!already_down) {
            self_->latched_ = true;
            if (vk == VK_LCONTROL) self_->lctrl_down_ = true;
            if (vk == VK_RCONTROL) self_->rctrl_down_ = true;
            PostMessageW(self_->hwnd_, WM_APP_HOLD_LATCHED, 0, 0);
            return 1;
        }
    }

    // --- Unlatch: Ctrl down while latched and all keys were released ---
    if (self_->latched_ && !self_->hold_active_ && is_ctrl_vk(vk) && is_down) {
        self_->latched_ = false;
        // Track this Ctrl as down so its release gets suppressed.
        if (vk == VK_LCONTROL) self_->lctrl_down_ = true;
        if (vk == VK_RCONTROL) self_->rctrl_down_ = true;
        PostMessageW(self_->hwnd_, WM_APP_HOLD_STOP, 0, 0);
        return 1;
    }

    // --- Normal key state tracking ---
    const bool was_active  = self_->hold_active_;
    const bool was_session = self_->combo_session_;

    if (vk == VK_LCONTROL) self_->lctrl_down_ = is_down;
    if (vk == VK_RCONTROL) self_->rctrl_down_ = is_down;
    if (is_win_vk(vk))     self_->win_down_   = is_down;

    const bool ctrl_down  = self_->lctrl_down_ || self_->rctrl_down_;
    const bool now_active = ctrl_down && self_->win_down_;

    bool suppress_this_event = was_session;

    if (now_active && !was_active) {
        self_->hold_active_ = true;
        self_->combo_session_ = true;
        suppress_this_event = true;
        PostMessageW(self_->hwnd_, WM_APP_HOLD_START, 0, 0);
    } else if (!now_active && was_active) {
        self_->hold_active_ = false;
        suppress_this_event = true;
        if (!self_->latched_) {
            PostMessageW(self_->hwnd_, WM_APP_HOLD_STOP, 0, 0);
        }
    }

    // Clear the session when all keys are up — unless latched.
    if (!ctrl_down && !self_->win_down_ && !self_->latched_) {
        if (self_->combo_session_) {
            suppress_this_event = true;
        }
        self_->combo_session_ = false;
    }

    if (suppress_this_event) {
        return 1;
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}
