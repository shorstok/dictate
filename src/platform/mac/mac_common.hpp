#pragma once
// Small helpers shared by the macOS platform layer.
//
// The synthetic event source is the macOS analog of the Windows layer's
// LLKHF_INJECTED check: everything this app posts with CGEventPost carries a
// magic user-data tag so the CGEventTap in hotkey_mac can recognize its own
// events and let them through instead of re-processing them.

#include <ApplicationServices/ApplicationServices.h>

#include <string>

#ifdef __OBJC__
#import <Foundation/Foundation.h>

inline NSString* to_ns(const std::string& utf8) {
    NSString* s = [NSString stringWithUTF8String:utf8.c_str()];
    return s ? s : @"";  // stringWithUTF8String: returns nil on invalid UTF-8
}

inline std::string from_ns(NSString* s) {
    if (!s) return {};
    const char* utf8 = [s UTF8String];
    return utf8 ? std::string(utf8) : std::string{};
}
#endif

namespace mac {

// Tag carried by every event this app posts (see kCGEventSourceUserData).
constexpr int64_t kSyntheticEventTag = 0x44494354;  // 'DICT'

// Process-wide event source used for all synthetic key events. Created lazily
// on first use and never released; returns nullptr if creation fails.
CGEventSourceRef synthetic_event_source();

// True if the event was posted by this app through synthetic_event_source().
bool is_synthetic_event(CGEventRef event);

} // namespace mac
