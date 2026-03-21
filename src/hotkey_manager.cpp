#include "hotkey_manager.hpp"

HotkeyManager::~HotkeyManager() {
    if (registered_) {
        UnregisterHotKey(hwnd_, id_);
    }
}

bool HotkeyManager::register_hotkey(HWND hwnd, int id, UINT modifiers, UINT vk) {
    hwnd_        = hwnd;
    id_          = id;
    registered_  = (RegisterHotKey(hwnd, id, modifiers, vk) != FALSE);
    return registered_;
}

void HotkeyManager::unregister_hotkey() {
    if (registered_) {
        UnregisterHotKey(hwnd_, id_);
        registered_ = false;
    }
}
