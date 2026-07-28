#pragma once
#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>

#include <atomic>
#include <functional>
#include <future>
#include <thread>

#include "core/app.hpp"  // HotkeyEvent

// Carbon opaque handles, forward-declared so <Carbon/Carbon.h> (which drags in
// Point/Rect/... into every translation unit) stays inside hotkey_mac.mm.
// These match Carbon's own typedefs exactly, so no casts are needed there.
typedef struct OpaqueEventHotKeyRef*  DictateEventHotKeyRef;
typedef struct OpaqueEventHandlerRef* DictateEventHandlerRef;

// Global input capture: either a Carbon RegisterEventHotKey toggle, or the
// hold-Ctrl+Cmd push-to-talk chord via a CGEventTap.
//
// The chord machine is a direct port of hotkey_win.cpp; the difference is that
// macOS reports modifiers as kCGEventFlagsChanged rather than key-down/key-up,
// so per-key down/up is reconstructed by diffing the device-dependent flag
// bits against the keycode that changed.
//
// Events are delivered to the sink on the main thread (the tap itself runs on
// its own thread with its own CFRunLoop — the analog of the Windows hook
// thread — so a slow handler cannot make the OS time the tap out).
class HotkeyMac {
public:
    using Sink = std::function<void(HotkeyEvent)>;

    HotkeyMac() = default;
    ~HotkeyMac();
    HotkeyMac(const HotkeyMac&) = delete;
    HotkeyMac& operator=(const HotkeyMac&) = delete;

    // Ctrl+Option+Shift+F9, matching the Windows toggle chord. Needs no
    // Input Monitoring permission, which makes it the fallback when the tap
    // cannot be installed.
    bool install_toggle_hotkey(Sink sink);

    // Hold Ctrl+Cmd to record; tap the other Ctrl while holding to latch.
    // Returns false when CGEventTapCreate fails — almost always a missing
    // Input Monitoring grant.
    bool install_hold_chord(Sink sink);

    void uninstall();

private:
    // --- event tap ---
    static CGEventRef tap_callback(CGEventTapProxy proxy,
                                   CGEventType type,
                                   CGEventRef event,
                                   void* refcon);
    void tap_thread_func(std::promise<bool> ready);
    CGEventRef handle_flags_changed(CGEventRef event);

    bool* leaked_flag_for(CGKeyCode key) noexcept;
    bool* down_flag_for(CGKeyCode key) noexcept;
    // Modifier state as the rest of the system currently believes it to be:
    // the physical flags minus every tracked key whose press we swallowed.
    CGEventFlags system_visible_flags(CGEventFlags physical) const noexcept;
    void compensate_suppressed_up(CGKeyCode key, CGEventFlags physical);

    void post_event(HotkeyEvent event) const;

    Sink sink_;

    CFMachPortRef            tap_{nullptr};
    std::thread              tap_thread_;
    std::atomic<CFRunLoopRef> tap_run_loop_{nullptr};

    // Chord state (touched only on the tap thread).
    bool lctrl_down_{false};
    bool rctrl_down_{false};
    bool lcmd_down_{false};
    bool rcmd_down_{false};
    bool hold_active_{false};
    bool combo_session_{false};
    bool latched_{false};

    // Presses that reached the rest of the system (the first modifier of a
    // chord must — the tap cannot yet know a chord is coming). When the
    // matching release is swallowed, a compensating flags-changed event is
    // posted so apps do not see the modifier as stuck down.
    bool lctrl_leaked_{false};
    bool rctrl_leaked_{false};
    bool lcmd_leaked_{false};
    bool rcmd_leaked_{false};

    // --- Carbon toggle hotkey ---
    DictateEventHotKeyRef  hotkey_ref_{nullptr};
    DictateEventHandlerRef hotkey_handler_{nullptr};
};
