#include "app.hpp"
#include "config.hpp"
#include "clipboard_service.hpp"
#include "history_store.hpp"
#include "utf8.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

namespace {
    constexpr wchar_t kMainWindowClass[] = L"DictateCppMainWindow";

    // Trim leading and trailing ASCII whitespace.
    std::string trim(std::string s) {
        const auto not_space = [](unsigned char c){ return !std::isspace(c); };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
        s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
        return s;
    }

    std::wstring shorten_for_balloon(const std::wstring& text) {
        constexpr std::size_t kMaxChars = 200;
        if (text.size() <= kMaxChars) {
            return text;
        }
        return text.substr(0, kMaxChars - 3) + L"...";
    }

    void post_owned_wstring(HWND hwnd, UINT msg, std::wstring* text) {
        if (!text) {
            return;
        }
        if (!PostMessageW(hwnd, msg, 0, reinterpret_cast<LPARAM>(text))) {
            delete text;
        }
    }
}

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------

LRESULT CALLBACK App::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }

    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_HOTKEY:
        if (app && wparam == static_cast<WPARAM>(config::kHotkeyId)) {
            app->on_hotkey();
            return 0;
        }
        break;

    case WM_APP_TRANSCRIPTION_OK: {
        auto* text = reinterpret_cast<std::wstring*>(lparam);
        if (app && text) app->on_transcription_success(*text);
        delete text;
        return 0;
    }

    case WM_APP_TRANSCRIPTION_ERROR: {
        auto* msg_text = reinterpret_cast<std::wstring*>(lparam);
        if (app && msg_text) app->on_transcription_error(*msg_text);
        delete msg_text;
        return 0;
    }

    case WM_APP_TRAY:
        if (app) {
            switch (LOWORD(lparam)) {
            case WM_CONTEXTMENU:
            case WM_RBUTTONUP: {
                POINT pt{};
                GetCursorPos(&pt);
                app->show_tray_menu(pt);
                return 0;
            }
            case WM_LBUTTONDBLCLK:
                app->show_status_feedback();
                return 0;
            }
        }
        break;

    case WM_COMMAND:
        if (app) {
            switch (LOWORD(wparam)) {
            case kTrayShowStatusCommand:
                app->show_status_feedback();
                return 0;
            case kTrayCopyLastCommand:
                app->copy_last_transcript();
                return 0;
            case kTrayExitCommand:
                app->request_exit();
                return 0;
            }
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    if (app && msg == app->taskbar_created_message()) {
        app->recreate_tray_icon();
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

// ---------------------------------------------------------------------------
// run()
// ---------------------------------------------------------------------------

int App::run(HINSTANCE instance, int /*show_cmd*/) {
    instance_ = instance;

    // Resolve the directory containing the executable.
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    exe_dir_ = std::filesystem::path(buf).parent_path();

    if (!create_window(instance)) {
        MessageBoxW(nullptr, L"Failed to create application window.", L"dictate_cpp", MB_ICONERROR);
        return 1;
    }

    if (!overlay_.create(instance, hwnd_)) {
        MessageBoxW(nullptr, L"Failed to create overlay window.", L"dictate_cpp", MB_ICONERROR);
        return 1;
    }

    if (!hotkey_.register_hotkey(hwnd_, config::kHotkeyId, config::kHotkeyModifiers, config::kHotkeyVK)) {
        MessageBoxW(nullptr,
            L"Failed to register hotkey Ctrl+Win.\n"
            L"Another instance may already be running.",
            L"dictate_cpp", MB_ICONERROR);
        return 1;
    }

    taskbar_created_msg_ = RegisterWindowMessageW(L"TaskbarCreated");

    if (!tray_.create(hwnd_, instance_, WM_APP_TRAY)) {
        MessageBoxW(nullptr, L"Failed to create tray icon.", L"dictate_cpp", MB_ICONERROR);
        hotkey_.unregister_hotkey();
        overlay_.destroy();
        return 1;
    }
    set_tray_state(TrayState::Idle);

    // Ensure output directory exists.
    std::error_code ec;
    std::filesystem::create_directories(exe_dir_ / config::kOutputDir, ec);

    auto cfg = user_config_.ensure_exists_and_load();
    if (!cfg.ok) {
        HistoryStore store(exe_dir_ / config::kOutputDir);
        store.log_error(cfg.error);
        tray_.destroy();
        hotkey_.unregister_hotkey();
        overlay_.destroy();
        MessageBoxW(
            nullptr,
            utf8_to_wide(cfg.error).c_str(),
            L"dictate_cpp - configuration error",
            MB_ICONERROR);
        return 1;
    }
    if (cfg.created_stub) {
        tray_.show_balloon(
            L"dictate_cpp",
            L"Created config file in LocalAppData\\dictate. Edit it to set prompt/language.");
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // Wait for any in-flight worker before the app object is destroyed.
    worker_ = std::jthread{};  // request stop + join

    hotkey_.unregister_hotkey();
    tray_.destroy();
    overlay_.destroy();

    return static_cast<int>(msg.wParam);
}

bool App::create_window(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = App::WndProc;
    wc.hInstance     = instance;
    wc.lpszClassName = kMainWindowClass;

    if (!RegisterClassExW(&wc)) {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    }

    hwnd_ = CreateWindowExW(
        0,
        kMainWindowClass,
        L"dictate_cpp",
        WS_OVERLAPPED,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        nullptr,
        nullptr,
        instance,
        this
    );
    return hwnd_ != nullptr;
}

// ---------------------------------------------------------------------------
// Hotkey & state machine
// ---------------------------------------------------------------------------

void App::on_hotkey() {
    switch (state_) {
    case AppState::idle:
        start_recording();
        break;

    case AppState::listening:
        stop_recording_and_transcribe();
        break;

    case AppState::transcribing:
        // Ignored for v1.
        break;
    }
}

void App::start_recording() {
    // Snapshot the focused window before the overlay might interfere.
    paste_.capture_foreground();

    const auto mp3_path = exe_dir_ / config::kAudioFilenameMp3;
    std::string error;
    if (!recorder_.start(mp3_path, error)) {
        const std::wstring message = utf8_to_wide(error.empty() ? "Microphone error" : error);
        overlay_.show_error(message);
        set_tray_state(TrayState::Error, message);
        return;
    }

    Beep(5000, 25);
    overlay_.show_listening();
    state_ = AppState::listening;
    set_tray_state(TrayState::Listening);
}

void App::stop_recording_and_transcribe() {
    const RecordedAudio recorded = recorder_.stop();
    Beep(2500, 25);

    if (!recorded.ok) {
        const std::wstring message = utf8_to_wide(recorded.error.empty() ? "Recording error" : recorded.error);
        overlay_.show_error(message);
        state_ = AppState::idle;
        set_tray_state(TrayState::Error, message);
        return;
    }

    if (!recorded.has_audio) {
        overlay_.show_error(L"No audio captured");
        state_ = AppState::idle;
        set_tray_state(TrayState::Error, L"No audio captured");
        return;
    }

    overlay_.show_transcribing();
    state_ = AppState::transcribing;
    set_tray_state(TrayState::Transcribing);

    const auto out_dir = exe_dir_ / config::kOutputDir;
    HWND hwnd = hwnd_;

    worker_ = std::jthread([this, recorded, out_dir, hwnd]() {
        std::filesystem::create_directories(out_dir);

        AudioUploadSpec upload;
        upload.path = recorded.path;
        upload.upload_filename = recorded.upload_filename;
        upload.mime_type = recorded.mime_type;

        const auto cfg = user_config_.ensure_exists_and_load();
        if (!cfg.ok) {
            post_owned_wstring(hwnd, WM_APP_TRANSCRIPTION_ERROR, new std::wstring(utf8_to_wide(cfg.error)));
            return;
        }

        auto result = transcription_.transcribe_file(upload, cfg.transcription);

        if (!result.ok) {
            post_owned_wstring(hwnd, WM_APP_TRANSCRIPTION_ERROR, new std::wstring(utf8_to_wide(result.error)));
            return;
        }

        // Normalize: trim, flatten newlines (match Python behavior).
        std::string text = trim(result.text);
        std::replace(text.begin(), text.end(), '\n', ' ');
        std::replace(text.begin(), text.end(), '\r', ' ');

        if (text.empty()) {
            post_owned_wstring(hwnd, WM_APP_TRANSCRIPTION_ERROR, new std::wstring(L"Empty transcript"));
            return;
        }

        // Save timestamped history file.
        HistoryStore store(out_dir);
        std::string transcribe_log_error;
        if (!user_config_.append_transcript_log(text, transcribe_log_error)) {
            store.log_error(transcribe_log_error);
        }
        store.save_transcript(text);

        post_owned_wstring(hwnd, WM_APP_TRANSCRIPTION_OK, new std::wstring(utf8_to_wide(text)));
    });
}

// ---------------------------------------------------------------------------
// Completion callbacks (UI thread)
// ---------------------------------------------------------------------------

void App::on_transcription_success(const std::wstring& text) {
    last_transcript_ = text;
    set_clipboard_text(hwnd_, text);
    paste_.restore_and_paste();
    overlay_.show_done();
    Beep(8000, 25);
    state_ = AppState::idle;
    set_tray_state(TrayState::Idle);
}

void App::on_transcription_error(const std::wstring& message) {
    HistoryStore store(exe_dir_ / config::kOutputDir);
    store.log_error(wide_to_utf8(message));
    overlay_.show_error(message);
    Beep(300, 600);
    state_ = AppState::idle;
    set_tray_state(TrayState::Error, message);
    tray_.show_balloon(L"dictate_cpp", shorten_for_balloon(message), NIIF_ERROR);
}

void App::show_tray_menu(POINT screen_pt) {
    tray_.show_context_menu(screen_pt, !last_transcript_.empty());
}

void App::show_status_feedback() {
    switch (tray_state_) {
    case TrayState::Listening:
        overlay_.show_listening();
        break;
    case TrayState::Transcribing:
        overlay_.show_transcribing();
        break;
    case TrayState::Error:
        overlay_.show_error(tray_error_message_.empty() ? L"Error" : tray_error_message_);
        break;
    case TrayState::Idle:
    default:
        overlay_.show_done();
        break;
    }
}

void App::copy_last_transcript() {
    if (last_transcript_.empty()) {
        return;
    }

    (void)set_clipboard_text(hwnd_, last_transcript_);
}

void App::recreate_tray_icon() {
    (void)tray_.recreate();
    set_tray_state(tray_state_, tray_error_message_);
}

void App::request_exit() {
    tray_.destroy();
    DestroyWindow(hwnd_);
}

void App::set_tray_state(TrayState state, const std::wstring& tooltip_suffix) {
    tray_state_ = state;
    tray_error_message_ = (state == TrayState::Error) ? tooltip_suffix : std::wstring{};
    tray_.set_state(state, tooltip_suffix);
}
