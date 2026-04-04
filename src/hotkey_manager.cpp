#include "hotkey_manager.hpp"
#include "app.hpp"

HotkeyManager* HotkeyManager::self_ = nullptr;

namespace {
    bool is_ctrl_vk(DWORD vk) {
        return vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL;
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

bool HotkeyManager::install_hold_ctrl_win(HWND hwnd) {
    hwnd_ = hwnd;
    self_ = this;
    hook_ = SetWindowsHookExW(WH_KEYBOARD_LL, keyboard_proc, nullptr, 0);
    return hook_ != nullptr;
}

void HotkeyManager::unregister_hotkey() {
    if (registered_) {
        UnregisterHotKey(hwnd_, id_);
        registered_ = false;
    }
    if (hook_) {
        UnhookWindowsHookEx(hook_);
        hook_ = nullptr;
    }
    if (self_ == this) {
        self_ = nullptr;
    }
    ctrl_down_ = false;
    win_down_ = false;
    hold_active_ = false;
}

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

    const bool was_active = self_->hold_active_;

    if (is_ctrl_vk(vk)) {
        self_->ctrl_down_ = is_down;
    }
    if (is_win_vk(vk)) {
        self_->win_down_ = is_down;
    }

    const bool now_active = self_->ctrl_down_ && self_->win_down_;

    if (now_active && !was_active) {
        self_->hold_active_ = true;
        PostMessageW(self_->hwnd_, WM_APP_HOLD_START, 0, 0);
    } else if (!now_active && was_active) {
        self_->hold_active_ = false;
        PostMessageW(self_->hwnd_, WM_APP_HOLD_STOP, 0, 0);
    }

    if (was_active || now_active) {
        return 1;
    }

    return CallNextHookEx(nullptr, code, wparam, lparam);
}
