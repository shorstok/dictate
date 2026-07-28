#pragma once
#include <string>

#include "core/platform.hpp"

#ifdef __OBJC__
@class OverlayPanelController;
#else
class OverlayPanelController;
#endif

enum class OverlayState {
    Hidden,
    Listening,
    Transcribing,
    Done,
    Notice,
    Error
};

// Borderless non-activating NSPanel pinned near the top of the active screen —
// the counterpart of overlay_win.cpp's layered popup window. All methods must
// be called on the main thread.
class OverlayMac final : public platform::Overlay {
public:
    OverlayMac() = default;
    ~OverlayMac() override;
    OverlayMac(const OverlayMac&) = delete;
    OverlayMac& operator=(const OverlayMac&) = delete;

    bool create();
    void destroy();

    // platform::Overlay
    void show_listening(bool latched) override;
    void show_transcribing() override;
    void show_done() override;
    void show_notice(const std::string& message) override;
    void show_error(const std::string& message) override;
    void hide() override;

private:
    void show_text(OverlayState state, const std::string& text, double auto_hide_s);

    OverlayPanelController* controller_{nullptr};  // +1 retained
};
