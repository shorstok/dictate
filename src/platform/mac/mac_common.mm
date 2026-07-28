#include "mac_common.hpp"

namespace mac {

CGEventSourceRef synthetic_event_source() {
    static CGEventSourceRef source = []() -> CGEventSourceRef {
        CGEventSourceRef s = CGEventSourceCreate(kCGEventSourceStatePrivate);
        if (s) {
            CGEventSourceSetUserData(s, kSyntheticEventTag);
        }
        return s;
    }();
    return source;
}

bool is_synthetic_event(CGEventRef event) {
    if (!event) return false;
    return CGEventGetIntegerValueField(event, kCGEventSourceUserData) == kSyntheticEventTag;
}

} // namespace mac
