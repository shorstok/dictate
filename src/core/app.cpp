#include "core/app.hpp"

#include "core/config.hpp"
#include "core/history_store.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <utility>

namespace {
    // Trim leading and trailing ASCII whitespace.
    std::string trim(std::string s) {
        const auto not_space = [](unsigned char c){ return !std::isspace(c); };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
        s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
        return s;
    }
}

App::App(Services services, std::filesystem::path data_dir)
    : services_(services)
    , data_dir_(std::move(data_dir))
    , user_config_(data_dir_)
{}

App::~App() {
    worker_ = std::jthread{};  // request stop + join any in-flight worker
}

bool App::startup(std::string& error) {
    error.clear();

    std::error_code ec;
    std::filesystem::create_directories(data_dir_ / config::kOutputDir, ec);

    const auto cfg = user_config_.ensure_exists_and_load();
    if (!cfg.ok) {
        HistoryStore store(data_dir_ / config::kOutputDir);
        store.log_error(cfg.error);
        error = cfg.error;
        return false;
    }
    if (cfg.created_stub) {
        services_.tray->show_notification(
            "dictate_cpp",
            "Created config file in the app data folder. Edit it to set prompt/language.",
            /*is_error=*/false);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Hotkey & state machine
// ---------------------------------------------------------------------------

void App::handle_hotkey(HotkeyEvent event) {
    switch (event) {
    case HotkeyEvent::Toggle:
        if (state_ == AppState::idle) {
            start_recording();
        } else if (state_ == AppState::listening) {
            stop_recording_and_transcribe();
        }
        // Ignored while transcribing.
        break;

    case HotkeyEvent::HoldStart:
        if (state_ == AppState::idle) {
            start_recording();
        }
        break;

    case HotkeyEvent::HoldStop:
        if (state_ == AppState::listening) {
            stop_recording_and_transcribe();
        }
        break;

    case HotkeyEvent::HoldLatched:
        if (state_ == AppState::listening) {
            services_.overlay->show_listening(/*latched=*/true);
        }
        break;
    }
}

void App::start_recording() {
    // Snapshot the focused window before the overlay might interfere.
    services_.paster->capture_focus();

    const auto mp3_path = data_dir_ / config::kAudioFilenameMp3;
    std::string error;
    if (!recorder_.start(mp3_path, error)) {
        const std::string message = error.empty() ? "Microphone error" : error;
        services_.overlay->show_error(message);
        set_tray_state(platform::TrayState::Error, message);
        return;
    }

    recording_started_ = std::chrono::steady_clock::now();
    services_.sound->play(platform::SoundCue::RecordStart);
    services_.overlay->show_listening(/*latched=*/false);
    state_ = AppState::listening;
    set_tray_state(platform::TrayState::Listening);
}

void App::stop_recording_and_transcribe() {
    const RecordedAudio recorded = recorder_.stop();
    const auto held_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - recording_started_).count();
    services_.sound->play(platform::SoundCue::RecordStop);

    // Discard accidental presses before anything else — a quick tap is a
    // user-intent signal, not an error: quiet blip, no error sound, no API call.
    if (held_ms < config::kMinRecordingMs) {
        services_.overlay->show_notice("Too short — discarded");
        state_ = AppState::idle;
        set_tray_state(platform::TrayState::Idle);
        return;
    }

    if (!recorded.ok) {
        const std::string message = recorded.error.empty() ? "Recording error" : recorded.error;
        services_.overlay->show_error(message);
        state_ = AppState::idle;
        set_tray_state(platform::TrayState::Error, message);
        return;
    }

    if (!recorded.has_audio) {
        services_.overlay->show_error("No audio captured");
        state_ = AppState::idle;
        set_tray_state(platform::TrayState::Error, "No audio captured");
        return;
    }

    // Silence gate: a long hold with no speech (muted mic, wrong device)
    // would otherwise be sent to OpenAI and hallucinate a transcript.
    if (recorded.peak_amplitude < config::kMinPeakAmplitude) {
        services_.overlay->show_notice("No speech detected — discarded");
        state_ = AppState::idle;
        set_tray_state(platform::TrayState::Idle);
        return;
    }

    services_.overlay->show_transcribing();
    state_ = AppState::transcribing;
    set_tray_state(platform::TrayState::Transcribing);

    const auto out_dir = data_dir_ / config::kOutputDir;

    worker_ = std::jthread([this, recorded, out_dir]() {
        std::filesystem::create_directories(out_dir);

        AudioUploadSpec upload;
        upload.path = recorded.path;
        upload.upload_filename = recorded.upload_filename;
        upload.mime_type = recorded.mime_type;

        const auto cfg = user_config_.ensure_exists_and_load();
        if (!cfg.ok) {
            services_.dispatcher->post([this, error = cfg.error]() {
                on_transcription_error(error);
            });
            return;
        }

        auto result = transcription_.transcribe_file(upload, cfg.transcription);

        // Voice recording is no longer needed once transcription succeeded;
        // keep it around on failure to allow debugging.
        if (result.ok) {
            std::error_code remove_ec;
            std::filesystem::remove(recorded.path, remove_ec);
        }

        if (!result.ok) {
            services_.dispatcher->post([this, error = result.error]() {
                on_transcription_error(error);
            });
            return;
        }

        // Normalize: trim, flatten newlines.
        std::string text = trim(result.text);
        std::replace(text.begin(), text.end(), '\n', ' ');
        std::replace(text.begin(), text.end(), '\r', ' ');

        if (text.empty()) {
            services_.dispatcher->post([this]() {
                on_transcription_error("Empty transcript");
            });
            return;
        }

        // Save timestamped history file.
        HistoryStore store(out_dir);
        std::string transcribe_log_error;
        if (!user_config_.append_transcript_log(text, transcribe_log_error)) {
            store.log_error(transcribe_log_error);
        }
        store.save_transcript(text);

        services_.dispatcher->post([this, text = std::move(text)]() {
            on_transcription_success(text);
        });
    });
}

// ---------------------------------------------------------------------------
// Completion callbacks (UI thread)
// ---------------------------------------------------------------------------

void App::on_transcription_success(const std::string& text) {
    last_transcript_ = text;

    std::string prev_clipboard;
    const bool had_clipboard = services_.clipboard->get_text(prev_clipboard);

    services_.clipboard->set_text(text);
    services_.paster->restore_and_paste();

    if (had_clipboard) {
        // Give the target app time to read the clipboard before restoring the
        // previous content; slow targets (RDP, busy Electron apps) otherwise
        // paste the restored text instead of the transcript.
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        services_.clipboard->set_text(prev_clipboard);
    }

    services_.overlay->show_done();
    services_.sound->play(platform::SoundCue::Done);
    state_ = AppState::idle;
    set_tray_state(platform::TrayState::Idle);
}

void App::on_transcription_error(const std::string& message) {
    HistoryStore store(data_dir_ / config::kOutputDir);
    store.log_error(message);
    services_.overlay->show_error(message);
    services_.sound->play(platform::SoundCue::Error);
    state_ = AppState::idle;
    set_tray_state(platform::TrayState::Error, message);
    services_.tray->show_notification("dictate_cpp", message, /*is_error=*/true);
}

// ---------------------------------------------------------------------------
// Tray/menu actions
// ---------------------------------------------------------------------------

void App::show_status_feedback() {
    switch (tray_state_) {
    case platform::TrayState::Listening:
        services_.overlay->show_listening(/*latched=*/false);
        break;
    case platform::TrayState::Transcribing:
        services_.overlay->show_transcribing();
        break;
    case platform::TrayState::Error:
        services_.overlay->show_error(
            tray_error_message_.empty() ? "Error" : tray_error_message_);
        break;
    case platform::TrayState::Idle:
    default:
        services_.overlay->show_done();
        break;
    }
}

void App::copy_last_transcript() {
    if (last_transcript_.empty()) {
        return;
    }
    (void)services_.clipboard->set_text(last_transcript_);
}

void App::set_tray_state(platform::TrayState state, const std::string& tooltip_suffix) {
    tray_state_ = state;
    tray_error_message_ = (state == platform::TrayState::Error) ? tooltip_suffix : std::string{};
    services_.tray->set_state(state, tooltip_suffix);
}
