#pragma once
#include <functional>
#include <string>

#include "core/platform.hpp"

#ifdef __OBJC__
@class StatusItemController;
#else
class StatusItemController;
#endif

// NSStatusItem in the menu bar — the counterpart of the Windows tray icon.
// Menu handling stays here (as on Windows); the entry point supplies the
// closures that route into App.
class TrayMac final : public platform::Tray {
public:
    struct Callbacks {
        std::function<void()> show_status;
        std::function<void()> copy_last;
        std::function<bool()> has_last_transcript;
        std::function<void()> quit;
    };

    TrayMac() = default;
    ~TrayMac() override;
    TrayMac(const TrayMac&) = delete;
    TrayMac& operator=(const TrayMac&) = delete;

    // `notification_fallback` receives notification text when the system
    // notification center is unavailable or unauthorized — which is the norm
    // for ad-hoc-signed builds, and silence there would hide real errors.
    bool create(Callbacks callbacks, platform::Overlay* notification_fallback);
    void destroy();

    // platform::Tray
    void set_state(platform::TrayState state, const std::string& tooltip_suffix) override;
    void show_notification(const std::string& title,
                           const std::string& text,
                           bool is_error) override;

private:
    StatusItemController* controller_{nullptr};  // +1 retained
};
