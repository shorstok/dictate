// macOS entry point: owns the NSApplication run loop and the platform service
// implementations; feeds hotkey events into the core App.
//
// The app runs as an agent (LSUIElement / NSApplicationActivationPolicyAccessory):
// menu-bar presence only, no Dock icon, no main menu.

#import <AppKit/AppKit.h>
#include <curl/curl.h>
#include <dispatch/dispatch.h>

#include <filesystem>
#include <functional>
#include <string>
#include <utility>

#include "core/app.hpp"
#include "core/config.hpp"
#include "clipboard_mac.hpp"
#include "hotkey_mac.hpp"
#include "mac_common.hpp"
#include "overlay_mac.hpp"
#include "paste_mac.hpp"
#include "permissions_mac.hpp"
#include "tray_mac.hpp"

namespace {

// ---------------------------------------------------------------------------
// Platform service implementations that live in the entry point
// ---------------------------------------------------------------------------

class MacDispatcher final : public platform::UiDispatcher {
public:
    void post(std::function<void()> fn) override {
        // The block copy carries the closure; blocks queued after the run loop
        // has ended simply never run, which the interface allows.
        std::function<void()> work = std::move(fn);
        dispatch_async(dispatch_get_main_queue(), ^{
            work();
        });
    }
};

// The Windows layer uses Beep() tones; the macOS equivalents are the system
// alert sounds. Missing sounds degrade to NSBeep rather than silence.
class MacSound final : public platform::Sound {
public:
    MacSound() {
        start_ = [[NSSound soundNamed:@"Tink"] retain];
        stop_  = [[NSSound soundNamed:@"Pop"] retain];
        done_  = [[NSSound soundNamed:@"Glass"] retain];
        error_ = [[NSSound soundNamed:@"Basso"] retain];
    }

    ~MacSound() override {
        [start_ release];
        [stop_ release];
        [done_ release];
        [error_ release];
    }

    MacSound(const MacSound&) = delete;
    MacSound& operator=(const MacSound&) = delete;

    void play(platform::SoundCue cue) override {
        NSSound* sound = nil;
        switch (cue) {
        case platform::SoundCue::RecordStart: sound = start_; break;
        case platform::SoundCue::RecordStop:  sound = stop_;  break;
        case platform::SoundCue::Done:        sound = done_;  break;
        case platform::SoundCue::Error:       sound = error_; break;
        }
        if (!sound) {
            NSBeep();
            return;
        }
        // Cues can arrive faster than they play out (short holds); restarting
        // is better than dropping the newer cue.
        [sound stop];
        [sound play];
    }

private:
    NSSound* start_{nil};
    NSSound* stop_{nil};
    NSSound* done_{nil};
    NSSound* error_{nil};
};

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------

// ~/Library/Application Support/dictate — recordings, config, and history live
// here (the analog of %LOCALAPPDATA%\dictate). Falls back to the temp dir.
std::filesystem::path resolve_data_dir() {
    NSArray<NSString*>* dirs = NSSearchPathForDirectoriesInDomains(
        NSApplicationSupportDirectory, NSUserDomainMask, YES);
    if ([dirs count] == 0) {
        return std::filesystem::temp_directory_path() / config::kConfigSubdir;
    }
    return std::filesystem::path(from_ns([dirs objectAtIndex:0])) / config::kConfigSubdir;
}

// ---------------------------------------------------------------------------
// Alerts / run loop control
// ---------------------------------------------------------------------------

void show_fatal(const std::string& message) {
    NSAlert* alert = [[[NSAlert alloc] init] autorelease];
    alert.alertStyle      = NSAlertStyleCritical;
    alert.messageText     = @"dictate";
    alert.informativeText = to_ns(message);
    [alert addButtonWithTitle:@"OK"];
    [NSApp activateIgnoringOtherApps:YES];
    [alert runModal];
}

// Actionable UI for a denied permission — a dictation app that silently does
// nothing is the default failure mode of getting TCC wrong.
void offer_settings(const std::string& message, void (*open_settings)()) {
    NSAlert* alert = [[[NSAlert alloc] init] autorelease];
    alert.alertStyle      = NSAlertStyleWarning;
    alert.messageText     = @"dictate";
    alert.informativeText = to_ns(message);
    [alert addButtonWithTitle:@"Open Settings"];
    [alert addButtonWithTitle:@"Later"];
    [NSApp activateIgnoringOtherApps:YES];
    if ([alert runModal] == NSAlertFirstButtonReturn) {
        open_settings();
    }
}

// [NSApp stop:] only takes effect once one more event has been processed, so
// the run loop has to be nudged with a dummy event.
void stop_run_loop() {
    [NSApp stop:nil];
    NSEvent* wake = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                       location:NSZeroPoint
                                  modifierFlags:0
                                      timestamp:0
                                   windowNumber:0
                                        context:nil
                                        subtype:0
                                          data1:0
                                          data2:0];
    [NSApp postEvent:wake atStart:YES];
}

// ---------------------------------------------------------------------------
// Input installation
// ---------------------------------------------------------------------------

struct InputSetup {
    bool ok{false};
    bool fell_back_to_toggle{false};
};

InputSetup install_input(HotkeyMac& hotkey, HotkeyMac::Sink sink) {
    InputSetup result;

    switch (config::kInputMode) {
    case InputMode::ToggleHotkey:
        result.ok = hotkey.install_toggle_hotkey(std::move(sink));
        return result;

    case InputMode::HoldPushToTalk:
        // Shows the system's Input Monitoring prompt when undecided. The grant
        // does not take effect in this process even if the user accepts, so
        // the tap below may still fail on a first run.
        mac::permissions::request_input_monitoring();

        if (hotkey.install_hold_chord(sink)) {
            result.ok = true;
            return result;
        }
        // CGEventTapCreate returned NULL — no Input Monitoring grant. The
        // Carbon hotkey needs no such permission, so push-button dictation
        // still works for users who decline it.
        result.ok = hotkey.install_toggle_hotkey(std::move(sink));
        result.fell_back_to_toggle = result.ok;
        return result;
    }

    return result;
}

} // namespace

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int /*argc*/, const char* /*argv*/[]) {
    @autoreleasepool {
        curl_global_init(CURL_GLOBAL_ALL);

        {
            [NSApplication sharedApplication];
            [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];

            // Services are declared before App so they outlive it: App's
            // destructor joins the transcription worker, which may still touch
            // the dispatcher.
            MacDispatcher dispatcher;
            MacSound      sound;
            ClipboardMac  clipboard;
            PasteMac      paster;
            OverlayMac    overlay;
            TrayMac       tray;
            HotkeyMac     hotkey;

            if (!overlay.create()) {
                show_fatal("Failed to create the overlay window.");
                curl_global_cleanup();
                return 1;
            }

            // Set once App exists; the menu closures below run only while the
            // run loop is alive, which is strictly inside App's lifetime.
            App* core = nullptr;

            TrayMac::Callbacks callbacks;
            callbacks.show_status = [&core]() {
                if (core) core->show_status_feedback();
            };
            callbacks.copy_last = [&core]() {
                if (core) core->copy_last_transcript();
            };
            callbacks.has_last_transcript = [&core]() {
                return core && core->has_last_transcript();
            };
            callbacks.quit = []() { stop_run_loop(); };

            if (!tray.create(std::move(callbacks), &overlay)) {
                show_fatal("Failed to create the menu bar item.");
                curl_global_cleanup();
                return 1;
            }
            tray.set_state(platform::TrayState::Idle, {});

            App::Services services;
            services.dispatcher = &dispatcher;
            services.overlay    = &overlay;
            services.tray       = &tray;
            services.clipboard  = &clipboard;
            services.paster     = &paster;
            services.sound      = &sound;

            App app(services, resolve_data_dir());
            core = &app;

            std::string startup_error;
            if (!app.startup(startup_error)) {
                core = nullptr;
                tray.destroy();
                overlay.destroy();
                show_fatal(startup_error);
                curl_global_cleanup();
                return 1;
            }

            const InputSetup input = install_input(hotkey, [&core](HotkeyEvent event) {
                if (core) core->handle_hotkey(event);
            });

            if (!input.ok) {
                core = nullptr;
                tray.destroy();
                overlay.destroy();
                show_fatal("Failed to install the input handler.");
                curl_global_cleanup();
                return 1;
            }

            // --- permissions the app degrades on rather than fails on ---

            // Asks up front so the first recording is not silently empty while
            // the microphone prompt is still on screen.
            mac::permissions::request_microphone_access();
            if (mac::permissions::microphone_denied()) {
                offer_settings(
                    "Microphone access is denied, so recordings will be empty. "
                    "Enable dictate under Privacy & Security > Microphone.",
                    &mac::permissions::open_microphone_settings);
            }

            // Shows Apple's own "grant access" dialog when undecided; without
            // it CGEventPost no-ops and transcripts are copied but never pasted.
            if (!mac::permissions::accessibility_trusted(/*prompt=*/true)) {
                offer_settings(
                    "Accessibility access is not granted, so dictate cannot press "
                    "Cmd+V for you. Transcripts will still be copied to the clipboard. "
                    "Enable dictate under Privacy & Security > Accessibility.",
                    &mac::permissions::open_accessibility_settings);
            }

            if (input.fell_back_to_toggle) {
                offer_settings(
                    "Input Monitoring is not granted, so hold-to-talk (Ctrl+Cmd) is "
                    "unavailable. Falling back to the Ctrl+Option+Shift+F9 toggle. "
                    "Enable dictate under Privacy & Security > Input Monitoring, then "
                    "restart dictate.",
                    &mac::permissions::open_input_monitoring_settings);
            }

            [NSApp run];

            // Stop feeding events before App is destroyed at scope exit.
            hotkey.uninstall();
            core = nullptr;
        }

        curl_global_cleanup();
        return 0;
    }
}
