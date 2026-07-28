#pragma once
#include "core/platform.hpp"

#ifdef __OBJC__
@class NSRunningApplication;
#else
class NSRunningApplication;
#endif

// Focus bookkeeping + synthetic Cmd+V.
//
// The Windows layer remembers an HWND; here the unit of focus is an
// application (NSRunningApplication), which is what NSWorkspace exposes and
// what can be reactivated without Accessibility permission. The keystroke
// itself does need Accessibility — CGEventPost silently no-ops without it.
class PasteMac final : public platform::Paster {
public:
    PasteMac() = default;
    ~PasteMac() override;
    PasteMac(const PasteMac&) = delete;
    PasteMac& operator=(const PasteMac&) = delete;

    // platform::Paster
    void capture_focus() override;      // snapshot the frontmost application
    void restore_and_paste() override;  // re-activate it and send Cmd+V

private:
    NSRunningApplication* previous_app_{nullptr};  // +1 retained (manual retain/release)
};
