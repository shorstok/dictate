#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include <string>

constexpr UINT kTrayShowStatusCommand = 1001;
constexpr UINT kTrayCopyLastCommand = 1002;
constexpr UINT kTrayExitCommand = 1003;

enum class TrayState {
    Idle,
    Listening,
    Transcribing,
    Error
};

class TrayIcon {
public:
    TrayIcon() = default;
    ~TrayIcon();
    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    bool create(HWND hwnd, HINSTANCE instance, UINT callback_msg);
    void destroy();
    bool recreate();

    void set_state(TrayState state, const std::wstring& tooltip_suffix = L"");
    void show_balloon(const std::wstring& title, const std::wstring& text, DWORD info_flags = NIIF_INFO);
    void show_context_menu(POINT screen_pt, bool has_last_transcript);

private:
    bool add_icon();
    bool modify_icon();
    bool load_icons(HINSTANCE instance);
    void destroy_icons();
    HICON icon_for_state(TrayState state) const noexcept;
    std::wstring tooltip_for_state() const;

    HWND hwnd_{nullptr};
    UINT callback_msg_{0};
    bool added_{false};
    TrayState state_{TrayState::Idle};
    std::wstring tooltip_suffix_;
    NOTIFYICONDATAW nid_{};
    HICON idle_icon_{nullptr};
    HICON listening_icon_{nullptr};
    HICON transcribing_icon_{nullptr};
    HICON error_icon_{nullptr};
};
