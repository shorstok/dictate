#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>

#include "core/platform.hpp"

enum class OverlayState {
    Hidden,
    Listening,
    Transcribing,
    Done,
    Notice,
    Error
};

class OverlayWindow final : public platform::Overlay {
public:
    OverlayWindow() = default;
    ~OverlayWindow() override;
    OverlayWindow(const OverlayWindow&) = delete;
    OverlayWindow& operator=(const OverlayWindow&) = delete;

    bool create(HINSTANCE instance, HWND owner = nullptr);
    void destroy();

    // platform::Overlay
    void show_listening(bool latched) override;
    void show_transcribing() override;
    void show_done() override;
    void show_notice(const std::string& message) override;
    void show_error(const std::string& message) override;
    void hide() override;

    HWND hwnd() const noexcept { return hwnd_; }

private:
    static constexpr UINT_PTR kHideTimerId = 1;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    LRESULT handle_message(UINT msg, WPARAM wparam, LPARAM lparam);

    void show_text(OverlayState state, const std::wstring& text, DWORD auto_hide_ms);
    void reposition();
    void update_region();
    void paint(HDC hdc);

    HWND      hwnd_{nullptr};
    HINSTANCE instance_{nullptr};
    HWND      owner_{nullptr};
    OverlayState state_{OverlayState::Hidden};
    std::wstring text_;
};
