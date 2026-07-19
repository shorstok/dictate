#pragma once
// Platform abstraction layer.
//
// The core (App state machine and everything under src/core) talks to the OS
// exclusively through these interfaces. Each platform provides implementations
// (src/platform/win, src/platform/mac) plus its own main() that owns the UI
// run loop, constructs the implementations, and feeds hotkey events into App.
//
// Threading contract:
//  - All interface methods are called on the UI thread, except UiDispatcher::post,
//    which is the one thread-safe entry point used to marshal work *onto* it.
//  - All strings are UTF-8; platforms convert at their boundary.

#include <functional>
#include <string>

namespace platform {

// Marshals a closure onto the UI thread. post() must be callable from any
// thread. Closures posted after the UI loop has ended may be silently dropped.
struct UiDispatcher {
    virtual ~UiDispatcher() = default;
    virtual void post(std::function<void()> fn) = 0;
};

// Small always-on-top status window ("Listening…", "Done", errors).
struct Overlay {
    virtual ~Overlay() = default;
    virtual void show_listening(bool latched) = 0;
    virtual void show_transcribing() = 0;
    virtual void show_done() = 0;                              // auto-hides
    virtual void show_notice(const std::string& message) = 0;  // neutral blip, auto-hides
    virtual void show_error(const std::string& message) = 0;   // auto-hides
    virtual void hide() = 0;
};

enum class TrayState {
    Idle,
    Listening,
    Transcribing,
    Error
};

// Status-area presence: tray icon on Windows, menu-bar item on macOS.
// Menu handling stays inside the platform layer (it queries App directly).
struct Tray {
    virtual ~Tray() = default;
    virtual void set_state(TrayState state, const std::string& tooltip_suffix) = 0;
    virtual void show_notification(const std::string& title,
                                   const std::string& text,
                                   bool is_error) = 0;
};

struct Clipboard {
    virtual ~Clipboard() = default;
    virtual bool get_text(std::string& out) = 0;  // false if no text on clipboard
    virtual bool set_text(const std::string& text) = 0;
};

// Focus bookkeeping + synthetic paste (Ctrl+V / Cmd+V).
struct Paster {
    virtual ~Paster() = default;
    // Snapshot the focused window/app before the overlay might interfere.
    virtual void capture_focus() = 0;
    // Re-activate the captured target and send the paste keystroke.
    virtual void restore_and_paste() = 0;
};

enum class SoundCue {
    RecordStart,
    RecordStop,
    Done,
    Error
};

struct Sound {
    virtual ~Sound() = default;
    virtual void play(SoundCue cue) = 0;
};

} // namespace platform
