#include "tray_win.hpp"
#include "utf8_win.hpp"

#include "app_resources.h"

namespace {
    HICON load_small_icon(HINSTANCE instance, int resource_id) {
        const int width = GetSystemMetrics(SM_CXSMICON);
        const int height = GetSystemMetrics(SM_CYSMICON);
        return static_cast<HICON>(LoadImageW(
            instance,
            MAKEINTRESOURCEW(resource_id),
            IMAGE_ICON,
            width,
            height,
            LR_DEFAULTCOLOR));
    }
}

TrayIcon::~TrayIcon() {
    destroy();
}

bool TrayIcon::create(HWND hwnd, HINSTANCE instance, UINT callback_msg) {
    destroy();

    hwnd_ = hwnd;
    callback_msg_ = callback_msg;

    if (!load_icons(instance)) {
        destroy();
        return false;
    }

    nid_ = {};
    nid_.cbSize = sizeof(nid_);
    nid_.hWnd = hwnd_;
    nid_.uID = 1;
    nid_.uCallbackMessage = callback_msg_;

    if (!add_icon()) {
        destroy();
        return false;
    }

    return true;
}

void TrayIcon::destroy() {
    if (added_) {
        Shell_NotifyIconW(NIM_DELETE, &nid_);
        added_ = false;
    }

    destroy_icons();
    nid_ = {};
    hwnd_ = nullptr;
    callback_msg_ = 0;
    state_ = platform::TrayState::Idle;
    tooltip_suffix_.clear();
}

bool TrayIcon::recreate() {
    if (!hwnd_) {
        return false;
    }

    added_ = false;
    return add_icon();
}

void TrayIcon::set_state(platform::TrayState state, const std::string& tooltip_suffix) {
    state_ = state;
    tooltip_suffix_ = utf8_to_wide(tooltip_suffix);

    if (added_) {
        (void)modify_icon();
    }
}

void TrayIcon::show_notification(const std::string& title,
                                 const std::string& text,
                                 bool is_error) {
    if (!added_) {
        return;
    }

    // Balloon text is capped (szInfo is 256 wchars); truncate with an ellipsis.
    std::wstring wide_text = utf8_to_wide(text);
    constexpr std::size_t kMaxChars = 200;
    if (wide_text.size() > kMaxChars) {
        wide_text = wide_text.substr(0, kMaxChars - 3) + L"...";
    }

    nid_.uFlags = NIF_INFO;
    nid_.dwInfoFlags = is_error ? NIIF_ERROR : NIIF_INFO;
    wcsncpy_s(nid_.szInfoTitle, utf8_to_wide(title).c_str(), _TRUNCATE);
    wcsncpy_s(nid_.szInfo, wide_text.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid_);
}

void TrayIcon::show_context_menu(POINT screen_pt, bool has_last_transcript) {
    if (!hwnd_) {
        return;
    }

    HMENU menu = CreatePopupMenu();
    if (!menu) {
        return;
    }

    AppendMenuW(menu, MF_STRING, kTrayShowStatusCommand, L"Show status");
    AppendMenuW(
        menu,
        MF_STRING | (has_last_transcript ? MF_ENABLED : MF_GRAYED),
        kTrayCopyLastCommand,
        L"Copy last");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kTrayExitCommand, L"Exit");

    SetForegroundWindow(hwnd_);
    TrackPopupMenu(
        menu,
        TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN,
        screen_pt.x,
        screen_pt.y,
        0,
        hwnd_,
        nullptr);

    PostMessageW(hwnd_, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

bool TrayIcon::add_icon() {
    nid_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid_.hIcon = icon_for_state(state_);
    wcsncpy_s(nid_.szTip, tooltip_for_state().c_str(), _TRUNCATE);

    if (!Shell_NotifyIconW(NIM_ADD, &nid_)) {
        return false;
    }

    nid_.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid_);
    added_ = true;
    return true;
}

bool TrayIcon::modify_icon() {
    nid_.uFlags = NIF_ICON | NIF_TIP;
    nid_.hIcon = icon_for_state(state_);
    wcsncpy_s(nid_.szTip, tooltip_for_state().c_str(), _TRUNCATE);
    return Shell_NotifyIconW(NIM_MODIFY, &nid_) == TRUE;
}

bool TrayIcon::load_icons(HINSTANCE instance) {
    idle_icon_ = load_small_icon(instance, IDI_APP_ICON);
    listening_icon_ = load_small_icon(instance, IDI_APP_LISTENING);
    transcribing_icon_ = load_small_icon(instance, IDI_APP_TRANSCRIBING);
    error_icon_ = load_small_icon(instance, IDI_APP_ERROR);

    return idle_icon_ && listening_icon_ && transcribing_icon_ && error_icon_;
}

void TrayIcon::destroy_icons() {
    if (idle_icon_) {
        DestroyIcon(idle_icon_);
        idle_icon_ = nullptr;
    }
    if (listening_icon_) {
        DestroyIcon(listening_icon_);
        listening_icon_ = nullptr;
    }
    if (transcribing_icon_) {
        DestroyIcon(transcribing_icon_);
        transcribing_icon_ = nullptr;
    }
    if (error_icon_) {
        DestroyIcon(error_icon_);
        error_icon_ = nullptr;
    }
}

HICON TrayIcon::icon_for_state(platform::TrayState state) const noexcept {
    switch (state) {
    case platform::TrayState::Listening:
        return listening_icon_;
    case platform::TrayState::Transcribing:
        return transcribing_icon_;
    case platform::TrayState::Error:
        return error_icon_;
    case platform::TrayState::Idle:
    default:
        return idle_icon_;
    }
}

std::wstring TrayIcon::tooltip_for_state() const {
    std::wstring tip = L"dictate_cpp - ";

    switch (state_) {
    case platform::TrayState::Listening:
        tip += L"Listening";
        break;
    case platform::TrayState::Transcribing:
        tip += L"Transcribing";
        break;
    case platform::TrayState::Error:
        tip += L"Error";
        break;
    case platform::TrayState::Idle:
    default:
        tip += L"Idle";
        break;
    }

    if (!tooltip_suffix_.empty()) {
        tip += L" - ";
        tip += tooltip_suffix_;
    }

    return tip;
}
