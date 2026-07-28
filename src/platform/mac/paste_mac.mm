#include "paste_mac.hpp"
#include "mac_common.hpp"

#import <AppKit/AppKit.h>
#include <Carbon/Carbon.h>  // kVK_ANSI_V

#include <unistd.h>

namespace {
    // Matches the Windows layer's 60 ms focus settle before SendInput.
    constexpr useconds_t kFocusSettleUs = 60 * 1000;
}

PasteMac::~PasteMac() {
    [previous_app_ release];
    previous_app_ = nullptr;
}

void PasteMac::capture_focus() {
    NSRunningApplication* front = [[NSWorkspace sharedWorkspace] frontmostApplication];
    // Never re-activate ourselves: we are an agent app, so if we somehow ended
    // up frontmost the transcript belongs to whatever was focused before.
    if (front && [front processIdentifier] == [[NSRunningApplication currentApplication] processIdentifier]) {
        return;
    }
    [previous_app_ release];
    previous_app_ = [front retain];
}

void PasteMac::restore_and_paste() {
    if (previous_app_ && ![previous_app_ isTerminated]) {
        [previous_app_ activateWithOptions:0];
        usleep(kFocusSettleUs);  // give focus a moment to settle
    }

    CGEventSourceRef source = mac::synthetic_event_source();
    if (!source) {
        return;
    }

    CGEventRef down = CGEventCreateKeyboardEvent(source, kVK_ANSI_V, true);
    CGEventRef up   = CGEventCreateKeyboardEvent(source, kVK_ANSI_V, false);
    if (!down || !up) {
        if (down) CFRelease(down);
        if (up)   CFRelease(up);
        return;
    }

    // Set the flags explicitly rather than inheriting whatever the user still
    // holds down — a stray Shift would turn this into paste-and-match-style.
    CGEventSetFlags(down, kCGEventFlagMaskCommand);
    CGEventSetFlags(up, kCGEventFlagMaskCommand);

    CGEventPost(kCGHIDEventTap, down);
    CGEventPost(kCGHIDEventTap, up);

    CFRelease(down);
    CFRelease(up);
}
