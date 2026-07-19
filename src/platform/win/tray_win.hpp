#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include <string>

#include "core/platform.hpp"

constexpr UINT kTrayShowStatusCommand = 1001;
constexpr UINT kTrayCopyLastCommand = 1002;
constexpr UINT kTrayExitCommand = 1003;

class TrayIcon final : public platform::Tray {
public:
    TrayIcon() = default;
    ~TrayIcon() override;
    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    bool create(HWND hwnd, HINSTANCE instance, UINT callback_msg);
    void destroy();
    bool recreate();
    void show_context_menu(POINT screen_pt, bool has_last_transcript);

    // platform::Tray
    void set_state(platform::TrayState state, const std::string& tooltip_suffix) override;
    void show_notification(const std::string& title,
                           const std::string& text,
                           bool is_error) override;

private:
    bool add_icon();
    bool modify_icon();
    bool load_icons(HINSTANCE instance);
    void destroy_icons();
    HICON icon_for_state(platform::TrayState state) const noexcept;
    std::wstring tooltip_for_state() const;

    HWND hwnd_{nullptr};
    UINT callback_msg_{0};
    bool added_{false};
    platform::TrayState state_{platform::TrayState::Idle};
    std::wstring tooltip_suffix_;
    NOTIFYICONDATAW nid_{};
    HICON idle_icon_{nullptr};
    HICON listening_icon_{nullptr};
    HICON transcribing_icon_{nullptr};
    HICON error_icon_{nullptr};
};
