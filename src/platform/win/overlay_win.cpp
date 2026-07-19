#include "overlay_win.hpp"
#include "utf8_win.hpp"
#include <windowsx.h>

namespace {
    constexpr wchar_t kOverlayClassName[] = L"DictateCppOverlayWindow";
    constexpr int kOverlayWidth  = 320;
    constexpr int kOverlayHeight = 72;
    constexpr int kTopMargin     = 48;
}

OverlayWindow::~OverlayWindow() {
    destroy();
}

bool OverlayWindow::create(HINSTANCE instance, HWND owner) {
    instance_ = instance;
    owner_    = owner;

    WNDCLASSEXW wc{};
    wc.cbSize       = sizeof(wc);
    wc.lpfnWndProc  = &OverlayWindow::WndProc;
    wc.hInstance    = instance_;
    wc.lpszClassName = kOverlayClassName;
    wc.hCursor      = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;

    if (!RegisterClassExW(&wc)) {
        const DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }
    }

    hwnd_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kOverlayClassName,
        L"",
        WS_POPUP,
        CW_USEDEFAULT, CW_USEDEFAULT,
        kOverlayWidth, kOverlayHeight,
        owner_,
        nullptr,
        instance_,
        this
    );

    if (!hwnd_) {
        return false;
    }

    update_region();
    return true;
}

void OverlayWindow::destroy() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

LRESULT CALLBACK OverlayWindow::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        auto* cs   = reinterpret_cast<CREATESTRUCTW*>(lparam);
        auto* self = static_cast<OverlayWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    }

    auto* self = reinterpret_cast<OverlayWindow*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self) {
        return self->handle_message(msg, wparam, lparam);
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

LRESULT OverlayWindow::handle_message(UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_TIMER:
        if (wparam == kHideTimerId) {
            KillTimer(hwnd_, kHideTimerId);
            hide();
            return 0;
        }
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd_, &ps);
        paint(hdc);
        EndPaint(hwnd_, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_DESTROY:
        if (hwnd_) {
            KillTimer(hwnd_, kHideTimerId);
            hwnd_ = nullptr;
        }
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, wparam, lparam);
}

void OverlayWindow::reposition() {
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int x = work.left + ((work.right - work.left) - kOverlayWidth) / 2;
    const int y = work.top + kTopMargin;
    SetWindowPos(
        hwnd_, HWND_TOPMOST,
        x, y, kOverlayWidth, kOverlayHeight,
        SWP_NOACTIVATE | SWP_SHOWWINDOW
    );
}

void OverlayWindow::update_region() {
    if (!hwnd_) return;
    HRGN region = CreateRoundRectRgn(
        0, 0,
        kOverlayWidth + 1, kOverlayHeight + 1,
        20, 20
    );
    SetWindowRgn(hwnd_, region, TRUE);
    // SetWindowRgn takes ownership of region on success – do not delete.
}

void OverlayWindow::paint(HDC hdc) {
    RECT rc{};
    GetClientRect(hwnd_, &rc);

    COLORREF bg         = RGB(32, 32, 32);
    COLORREF border     = RGB(80, 80, 80);
    COLORREF text_color = RGB(245, 245, 245);

    switch (state_) {
    case OverlayState::Listening:    text_color = RGB(255, 230, 140); break;
    case OverlayState::Transcribing: text_color = RGB(180, 220, 255); break;
    case OverlayState::Done:         text_color = RGB(180, 255, 180); break;
    case OverlayState::Notice:       text_color = RGB(190, 190, 190); break;
    case OverlayState::Error:        text_color = RGB(255, 170, 170); break;
    default: break;
    }

    HBRUSH bg_brush = CreateSolidBrush(bg);
    FillRect(hdc, &rc, bg_brush);
    DeleteObject(bg_brush);

    HPEN    pen       = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ old_pen   = SelectObject(hdc, pen);
    HGDIOBJ old_brush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 20, 20);
    SelectObject(hdc, old_brush);
    SelectObject(hdc, old_pen);
    DeleteObject(pen);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, text_color);

    HFONT font = CreateFontW(
        24, 0, 0, 0,
        FW_SEMIBOLD,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS,
        L"Segoe UI"
    );
    HGDIOBJ old_font = SelectObject(hdc, font);
    RECT text_rc = rc;
    DrawTextW(hdc, text_.c_str(), -1, &text_rc,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(hdc, old_font);
    DeleteObject(font);
}

void OverlayWindow::show_text(OverlayState state, const std::wstring& text, DWORD auto_hide_ms) {
    if (!hwnd_) return;
    state_ = state;
    text_  = text;
    KillTimer(hwnd_, kHideTimerId);
    reposition();
    InvalidateRect(hwnd_, nullptr, TRUE);
    UpdateWindow(hwnd_);
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    if (auto_hide_ms > 0) {
        SetTimer(hwnd_, kHideTimerId, auto_hide_ms, nullptr);
    }
}

void OverlayWindow::show_listening(bool latched) {
    show_text(OverlayState::Listening,
              latched ? L"Listening (latched)\u2026" : L"Listening\u2026", 0);
}

void OverlayWindow::show_transcribing() {
    show_text(OverlayState::Transcribing, L"Transcribing\u2026", 0);
}

void OverlayWindow::show_done() {
    show_text(OverlayState::Done, L"Done", 1200);
}

void OverlayWindow::show_notice(const std::string& message) {
    show_text(OverlayState::Notice, utf8_to_wide(message), 1400);
}

void OverlayWindow::show_error(const std::string& message) {
    show_text(OverlayState::Error, message.empty() ? L"Error" : utf8_to_wide(message), 1800);
}

void OverlayWindow::hide() {
    if (!hwnd_) return;
    KillTimer(hwnd_, kHideTimerId);
    ShowWindow(hwnd_, SW_HIDE);
    state_ = OverlayState::Hidden;
    text_.clear();
}
