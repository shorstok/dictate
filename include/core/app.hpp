#pragma once
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

#include "core/app_state.hpp"
#include "core/audio_recorder.hpp"
#include "core/platform.hpp"
#include "core/transcription_client.hpp"
#include "core/user_config.hpp"

// Hotkey/input events delivered by the platform layer (always on the UI thread).
enum class HotkeyEvent {
    Toggle,       // ToggleHotkey mode: start if idle, stop if listening
    HoldStart,    // push-to-talk chord pressed
    HoldStop,     // push-to-talk chord released (or latched session ended)
    HoldLatched   // latch activated while recording
};

// Platform-neutral application core: the idle → listening → transcribing state
// machine, recording guards, transcription pipeline, and paste/history logic.
// All methods must be called on the UI thread; internal worker completion is
// marshaled back through the UiDispatcher.
class App {
public:
    struct Services {
        platform::UiDispatcher* dispatcher{nullptr};
        platform::Overlay*      overlay{nullptr};
        platform::Tray*         tray{nullptr};
        platform::Clipboard*    clipboard{nullptr};
        platform::Paster*       paster{nullptr};
        platform::Sound*        sound{nullptr};
    };

    App(Services services, std::filesystem::path data_dir);
    ~App();  // joins any in-flight transcription worker

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // Load (or stub-create) the user config; shows a notification when the
    // stub was just created. Returns false with a UTF-8 error message (also
    // logged to the error log) — the platform layer decides how to surface it.
    bool startup(std::string& error);

    void handle_hotkey(HotkeyEvent event);

    // Tray/menu-driven actions (platform layer routes menu clicks here).
    void show_status_feedback();
    void copy_last_transcript();
    bool has_last_transcript() const { return !last_transcript_.empty(); }

private:
    void start_recording();
    void stop_recording_and_transcribe();
    void on_transcription_success(const std::string& text);
    void on_transcription_error(const std::string& message);
    void set_tray_state(platform::TrayState state, const std::string& tooltip_suffix = {});

    Services  services_;
    AppState  state_{AppState::idle};
    platform::TrayState tray_state_{platform::TrayState::Idle};
    std::string tray_error_message_;
    std::string last_transcript_;
    std::chrono::steady_clock::time_point recording_started_{};

    std::filesystem::path data_dir_;

    AudioRecorder        recorder_;
    TranscriptionClient  transcription_;
    UserConfigStore      user_config_;

    std::jthread worker_;  // keep last: joined before other members are destroyed
};
