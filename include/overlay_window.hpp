#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>

enum class OverlayState {
    Hidden,
    Listening,
    Transcribing,
    Done,
    Error
};

class OverlayWindow {
public:
    OverlayWindow() = default;
    ~OverlayWindow();
    OverlayWindow(const OverlayWindow&) = delete;
    OverlayWindow& operator=(const OverlayWindow&) = delete;

    bool create(HINSTANCE instance, HWND owner = nullptr);
    void destroy();

    void show_listening();
    void show_transcribing();
    void show_done();
    void show_error(const std::wstring& message = L"Error");
    void hide();

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
