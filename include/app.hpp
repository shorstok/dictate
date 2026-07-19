#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <filesystem>
#include <string>
#include <thread>

#include "app_state.hpp"
#include "hotkey_manager.hpp"
#include "overlay_window.hpp"
#include "audio_recorder.hpp"
#include "transcription_client.hpp"
#include "paste_service.hpp"
#include "tray_icon.hpp"
#include "user_config.hpp"

// Custom window messages for worker->UI thread communication.
constexpr UINT WM_APP_TRANSCRIPTION_OK    = WM_APP + 1;
constexpr UINT WM_APP_TRANSCRIPTION_ERROR = WM_APP + 2;
constexpr UINT WM_APP_TRAY                = WM_APP + 10;
constexpr UINT WM_APP_HOLD_START          = WM_APP + 20;
constexpr UINT WM_APP_HOLD_STOP           = WM_APP + 21;
constexpr UINT WM_APP_HOLD_LATCHED        = WM_APP + 22;

class App {
public:
    int run(HINSTANCE instance, int show_cmd);

    // Called on the UI thread – either from WndProc or from message handler.
    void on_hotkey();
    void on_transcription_success(const std::wstring& text);
    void on_transcription_error(const std::wstring& message);
    UINT taskbar_created_message() const noexcept { return taskbar_created_msg_; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    bool create_window(HINSTANCE instance);

    void start_recording();
    void stop_recording_and_transcribe();
    void show_tray_menu(POINT screen_pt);
    void show_status_feedback();
    void copy_last_transcript();
    void recreate_tray_icon();
    void request_exit();
    void set_tray_state(TrayState state, const std::wstring& tooltip_suffix = L"");

    HWND      hwnd_{nullptr};
    HINSTANCE instance_{nullptr};
    AppState  state_{AppState::idle};
    UINT      taskbar_created_msg_{0};
    TrayState tray_state_{TrayState::Idle};
    std::wstring tray_error_message_;
    std::wstring last_transcript_;
    ULONGLONG recording_started_tick_{0};

    std::filesystem::path exe_dir_;
    std::filesystem::path data_dir_;  // LocalAppData\dictate (recordings, history)

    HotkeyManager        hotkey_;
    OverlayWindow        overlay_;
    TrayIcon             tray_;
    AudioRecorder        recorder_;
    TranscriptionClient  transcription_;
    PasteService         paste_;
    UserConfigStore      user_config_;

    std::jthread worker_;
};
