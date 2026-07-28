#include "hotkey_mac.hpp"
#include "mac_common.hpp"

#include <Carbon/Carbon.h>
#include <dispatch/dispatch.h>

namespace {

// Device-dependent modifier bits (IOKit's NX_DEVICE*KEYMASK). CGEventFlags
// only tells you "some Control is down"; these tell you which one.
constexpr CGEventFlags kDeviceLCtrl = 0x00000001;
constexpr CGEventFlags kDeviceRCtrl = 0x00002000;
constexpr CGEventFlags kDeviceLCmd  = 0x00000008;
constexpr CGEventFlags kDeviceRCmd  = 0x00000010;

constexpr CGEventFlags kTrackedBits =
    kDeviceLCtrl | kDeviceRCtrl | kDeviceLCmd | kDeviceRCmd |
    kCGEventFlagMaskControl | kCGEventFlagMaskCommand;

bool is_ctrl_key(CGKeyCode key) {
    return key == kVK_Control || key == kVK_RightControl;
}

bool is_cmd_key(CGKeyCode key) {
    return key == kVK_Command || key == kVK_RightCommand;
}

bool is_tracked_key(CGKeyCode key) {
    return is_ctrl_key(key) || is_cmd_key(key);
}

// Reconstruct "is this specific key now held?" from the post-change flags.
bool key_is_down(CGEventFlags flags, CGKeyCode key) {
    CGEventFlags own = 0, sibling = 0, general = 0;
    switch (key) {
    case kVK_Control:      own = kDeviceLCtrl; sibling = kDeviceRCtrl; general = kCGEventFlagMaskControl; break;
    case kVK_RightControl: own = kDeviceRCtrl; sibling = kDeviceLCtrl; general = kCGEventFlagMaskControl; break;
    case kVK_Command:      own = kDeviceLCmd;  sibling = kDeviceRCmd;  general = kCGEventFlagMaskCommand; break;
    case kVK_RightCommand: own = kDeviceRCmd;  sibling = kDeviceLCmd;  general = kCGEventFlagMaskCommand; break;
    default: return false;
    }
    if ((flags & own) != 0) {
        return true;
    }
    // Some keyboards and remapping drivers never set the device-dependent
    // bits. When neither side is reported, fall back to the side-agnostic
    // mask — the keycode already told us which key changed.
    if ((flags & (own | sibling)) == 0) {
        return (flags & general) != 0;
    }
    return false;
}

// Ctrl+Option+Shift+F9 — the macOS spelling of the Windows toggle chord.
constexpr UInt32 kToggleKeyCode   = kVK_F9;
constexpr UInt32 kToggleModifiers = controlKey | optionKey | shiftKey;
constexpr OSType kToggleSignature = 'dcta';
constexpr UInt32 kToggleHotkeyId  = 1;

} // namespace

HotkeyMac::~HotkeyMac() {
    uninstall();
}

// ---------------------------------------------------------------------------
// Carbon toggle hotkey (main thread; no Input Monitoring permission needed)
// ---------------------------------------------------------------------------

namespace {

OSStatus carbon_hotkey_handler(EventHandlerCallRef /*next*/, EventRef event, void* user_data) {
    auto* sink = static_cast<HotkeyMac::Sink*>(user_data);
    if (!sink || !*sink) {
        return noErr;
    }

    EventHotKeyID id{};
    if (GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID,
                          nullptr, sizeof(id), nullptr, &id) != noErr) {
        return noErr;
    }
    if (id.signature != kToggleSignature || id.id != kToggleHotkeyId) {
        return noErr;
    }

    // Carbon hot-key events are dispatched on the main run loop, so this is
    // already the UI thread.
    (*sink)(HotkeyEvent::Toggle);
    return noErr;
}

} // namespace

bool HotkeyMac::install_toggle_hotkey(Sink sink) {
    if (hotkey_ref_) {
        return true;
    }
    sink_ = std::move(sink);

    EventTypeSpec spec{};
    spec.eventClass = kEventClassKeyboard;
    spec.eventKind  = kEventHotKeyPressed;

    EventHandlerRef handler = nullptr;
    if (InstallApplicationEventHandler(&carbon_hotkey_handler, 1, &spec,
                                       &sink_, &handler) != noErr) {
        sink_ = nullptr;
        return false;
    }
    hotkey_handler_ = handler;

    EventHotKeyID id{};
    id.signature = kToggleSignature;
    id.id        = kToggleHotkeyId;

    EventHotKeyRef ref = nullptr;
    if (RegisterEventHotKey(kToggleKeyCode, kToggleModifiers, id,
                            GetApplicationEventTarget(), 0, &ref) != noErr) {
        RemoveEventHandler(hotkey_handler_);
        hotkey_handler_ = nullptr;
        sink_ = nullptr;
        return false;
    }
    hotkey_ref_ = ref;
    return true;
}

// ---------------------------------------------------------------------------
// Event tap thread
// ---------------------------------------------------------------------------

bool HotkeyMac::install_hold_chord(Sink sink) {
    if (tap_thread_.joinable()) {
        return true;
    }
    sink_ = std::move(sink);

    std::promise<bool> ready;
    std::future<bool>  ready_future = ready.get_future();

    tap_thread_ = std::thread([this, p = std::move(ready)]() mutable {
        tap_thread_func(std::move(p));
    });

    if (!ready_future.get()) {
        tap_thread_.join();
        tap_thread_ = std::thread{};
        sink_ = nullptr;
        return false;
    }
    return true;
}

void HotkeyMac::tap_thread_func(std::promise<bool> ready) {
    // kCGEventTapOptionDefault (not listenOnly): the chord must be swallowed
    // so Ctrl+Cmd does not reach the focused app.
    tap_ = CGEventTapCreate(kCGSessionEventTap,
                            kCGHeadInsertEventTap,
                            kCGEventTapOptionDefault,
                            CGEventMaskBit(kCGEventFlagsChanged),
                            &HotkeyMac::tap_callback,
                            this);
    if (!tap_) {
        ready.set_value(false);  // no Input Monitoring grant, or sandboxed
        return;
    }

    CFRunLoopSourceRef source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, tap_, 0);
    if (!source) {
        CFRelease(tap_);
        tap_ = nullptr;
        ready.set_value(false);
        return;
    }

    CFRunLoopRef run_loop = CFRunLoopGetCurrent();
    CFRetain(run_loop);
    tap_run_loop_.store(run_loop, std::memory_order_release);

    CFRunLoopAddSource(run_loop, source, kCFRunLoopCommonModes);
    CGEventTapEnable(tap_, true);

    ready.set_value(true);

    CFRunLoopRun();

    CFRunLoopRemoveSource(run_loop, source, kCFRunLoopCommonModes);
    CFRelease(source);
    CGEventTapEnable(tap_, false);
    CFRelease(tap_);
    tap_ = nullptr;
}

void HotkeyMac::uninstall() {
    if (hotkey_ref_) {
        UnregisterEventHotKey(hotkey_ref_);
        hotkey_ref_ = nullptr;
    }
    if (hotkey_handler_) {
        RemoveEventHandler(hotkey_handler_);
        hotkey_handler_ = nullptr;
    }

    if (tap_thread_.joinable()) {
        if (CFRunLoopRef run_loop = tap_run_loop_.load(std::memory_order_acquire)) {
            CFRunLoopStop(run_loop);
        }
        tap_thread_.join();
        if (CFRunLoopRef run_loop = tap_run_loop_.exchange(nullptr, std::memory_order_acq_rel)) {
            CFRelease(run_loop);
        }
    }
    tap_thread_ = std::thread{};

    sink_ = nullptr;

    lctrl_down_ = rctrl_down_ = lcmd_down_ = rcmd_down_ = false;
    hold_active_ = combo_session_ = latched_ = false;
    lctrl_leaked_ = rctrl_leaked_ = lcmd_leaked_ = rcmd_leaked_ = false;
}

void HotkeyMac::post_event(HotkeyEvent event) const {
    if (!sink_) {
        return;
    }
    Sink sink = sink_;  // copied into the block; the tap thread must not block
    dispatch_async(dispatch_get_main_queue(), ^{
        sink(event);
    });
}

// ---------------------------------------------------------------------------
// Stuck-modifier compensation
//
// The first modifier's press passes through the tap (a chord cannot be
// recognized from one key), so the system registers it as held. Once the
// chord activates, every further Ctrl/Cmd event is swallowed — including that
// key's release — which would leave apps believing it is still down. Whenever
// such a release is swallowed, a synthetic kCGEventFlagsChanged carrying the
// modifier state the system *should* now see is posted in its place.
// ---------------------------------------------------------------------------

bool* HotkeyMac::leaked_flag_for(CGKeyCode key) noexcept {
    switch (key) {
    case kVK_Control:      return &lctrl_leaked_;
    case kVK_RightControl: return &rctrl_leaked_;
    case kVK_Command:      return &lcmd_leaked_;
    case kVK_RightCommand: return &rcmd_leaked_;
    }
    return nullptr;
}

bool* HotkeyMac::down_flag_for(CGKeyCode key) noexcept {
    switch (key) {
    case kVK_Control:      return &lctrl_down_;
    case kVK_RightControl: return &rctrl_down_;
    case kVK_Command:      return &lcmd_down_;
    case kVK_RightCommand: return &rcmd_down_;
    }
    return nullptr;
}

CGEventFlags HotkeyMac::system_visible_flags(CGEventFlags physical) const noexcept {
    // Untracked modifiers (Shift, Option, Fn) are passed through untouched;
    // Ctrl/Cmd are rebuilt from the leak flags, which are exactly "the system
    // saw this press and has not seen its release".
    CGEventFlags out = physical & ~kTrackedBits;
    if (lctrl_leaked_) out |= kDeviceLCtrl | kCGEventFlagMaskControl;
    if (rctrl_leaked_) out |= kDeviceRCtrl | kCGEventFlagMaskControl;
    if (lcmd_leaked_)  out |= kDeviceLCmd  | kCGEventFlagMaskCommand;
    if (rcmd_leaked_)  out |= kDeviceRCmd  | kCGEventFlagMaskCommand;
    return out;
}

void HotkeyMac::compensate_suppressed_up(CGKeyCode key, CGEventFlags physical) {
    bool* leaked = leaked_flag_for(key);
    if (!leaked || !*leaked) {
        return;
    }
    *leaked = false;

    CGEventSourceRef source = mac::synthetic_event_source();
    if (!source) {
        return;
    }
    CGEventRef ev = CGEventCreate(source);
    if (!ev) {
        return;
    }
    CGEventSetType(ev, kCGEventFlagsChanged);
    CGEventSetIntegerValueField(ev, kCGKeyboardEventKeycode, key);
    CGEventSetFlags(ev, system_visible_flags(physical));
    CGEventPost(kCGHIDEventTap, ev);
    CFRelease(ev);
}

// ---------------------------------------------------------------------------
// Tap callback (runs on the tap thread)
//
// State machine (identical to hotkey_win.cpp):
//   idle         ──(Ctrl+Cmd both down)────────▸ hold_active + post HoldStart
//   hold_active  ──(other Ctrl pressed)────────▸ latched     + post HoldLatched
//   hold_active  ──(either key up, unlatched)──▸ idle        + post HoldStop
//   latched      ──(all keys released)────────▸ latched (waiting to unlatch)
//   latched      ──(Ctrl down after all up)───▸ idle        + post HoldStop
// ---------------------------------------------------------------------------

CGEventRef HotkeyMac::tap_callback(CGEventTapProxy /*proxy*/,
                                   CGEventType type,
                                   CGEventRef event,
                                   void* refcon) {
    auto* self = static_cast<HotkeyMac*>(refcon);
    if (!self) {
        return event;
    }

    // The OS disables the tap if the callback is too slow, or when the user
    // revokes the grant. Re-enabling is the documented recovery.
    if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
        if (self->tap_) {
            CGEventTapEnable(self->tap_, true);
        }
        return event;
    }

    if (type != kCGEventFlagsChanged) {
        return event;
    }

    // Our own posted events (the compensating flags changes and the Cmd+V)
    // come back through the tap — the analog of the Windows LLKHF_INJECTED
    // check. Without this the compensation would feed itself.
    if (mac::is_synthetic_event(event)) {
        return event;
    }

    return self->handle_flags_changed(event);
}

CGEventRef HotkeyMac::handle_flags_changed(CGEventRef event) {
    const auto key = static_cast<CGKeyCode>(
        CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode));
    if (!is_tracked_key(key)) {
        return event;
    }

    const CGEventFlags flags = CGEventGetFlags(event);
    const bool is_down = key_is_down(flags, key);
    const bool is_up   = !is_down;

    // The side-agnostic masks are always reported; the device-dependent ones
    // are not on every keyboard. Use them to drop per-key state that a missed
    // release would otherwise leave stuck down (and with it the chord).
    if ((flags & kCGEventFlagMaskControl) == 0) {
        lctrl_down_ = rctrl_down_ = false;
    }
    if ((flags & kCGEventFlagMaskCommand) == 0) {
        lcmd_down_ = rcmd_down_ = false;
    }

    // --- Latch activation: a new Ctrl press while the chord is held ---
    // Guarded on that specific key not already being down, so key repeat and
    // duplicate flagsChanged events cannot latch.
    if (hold_active_ && !latched_ && is_down && is_ctrl_key(key)) {
        bool* down = down_flag_for(key);
        if (down && !*down) {
            latched_ = true;
            *down = true;
            post_event(HotkeyEvent::HoldLatched);
            return nullptr;
        }
    }

    // --- Unlatch: Ctrl pressed while latched and all chord keys were released ---
    if (latched_ && !hold_active_ && is_down && is_ctrl_key(key)) {
        latched_ = false;
        if (bool* down = down_flag_for(key)) {
            *down = true;  // track it so its release is swallowed too
        }
        post_event(HotkeyEvent::HoldStop);
        return nullptr;
    }

    // --- Normal key state tracking ---
    const bool was_active  = hold_active_;
    const bool was_session = combo_session_;

    if (bool* down = down_flag_for(key)) {
        *down = is_down;
    }

    const bool ctrl_down = lctrl_down_ || rctrl_down_;
    const bool cmd_down  = lcmd_down_ || rcmd_down_;
    const bool now_active = ctrl_down && cmd_down;

    bool suppress = was_session;

    if (now_active && !was_active) {
        hold_active_   = true;
        combo_session_ = true;
        suppress       = true;
        post_event(HotkeyEvent::HoldStart);
    } else if (!now_active && was_active) {
        hold_active_ = false;
        suppress     = true;
        if (!latched_) {
            post_event(HotkeyEvent::HoldStop);
        }
    }

    // Close the session once everything is up — unless we are latched.
    if (!ctrl_down && !cmd_down && !latched_) {
        if (combo_session_) {
            suppress = true;
        }
        combo_session_ = false;
    }

    if (suppress) {
        if (is_up) {
            compensate_suppressed_up(key, flags);
        }
        return nullptr;
    }

    // The event reaches the rest of the system — remember presses so a later
    // swallowed release can be compensated for.
    if (bool* leaked = leaked_flag_for(key)) {
        *leaked = is_down;
    }
    return event;
}
