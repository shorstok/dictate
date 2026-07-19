// Windows entry point: owns the hidden message window, the UI message loop,
// and the platform service implementations; feeds hotkey events into the core App.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#include <curl/curl.h>

#include <filesystem>
#include <functional>
#include <string>

#include "core/app.hpp"
#include "core/config.hpp"
#include "clipboard_win.hpp"
#include "hotkey_win.hpp"
#include "overlay_win.hpp"
#include "paste_win.hpp"
#include "tray_win.hpp"
#include "utf8_win.hpp"
#include "win_messages.hpp"

namespace {

constexpr wchar_t kMainWindowClass[] = L"DictateCppMainWindow";

// ---------------------------------------------------------------------------
// Platform service implementations that live in the entry point
// ---------------------------------------------------------------------------

class WinDispatcher final : public platform::UiDispatcher {
public:
    void attach(HWND hwnd) { hwnd_ = hwnd; }

    void post(std::function<void()> fn) override {
        auto* heap_fn = new std::function<void()>(std::move(fn));
        if (!PostMessageW(hwnd_, WM_APP_DISPATCH, 0, reinterpret_cast<LPARAM>(heap_fn))) {
            delete heap_fn;  // window gone (shutdown) — drop the closure
        }
    }

private:
    HWND hwnd_{nullptr};
};

class BeepSound final : public platform::Sound {
public:
    void play(platform::SoundCue cue) override {
        switch (cue) {
        case platform::SoundCue::RecordStart: Beep(1400, 30); break;
        case platform::SoundCue::RecordStop:  Beep(900, 30);  break;
        case platform::SoundCue::Done:        Beep(1400, 30); break;
        case platform::SoundCue::Error:       Beep(300, 600); break;
        }
    }
};

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------

std::filesystem::path exe_dir() {
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(buf).parent_path();
}

// LocalAppData\dictate — recordings, config, and history live here (the exe
// dir may not be writable, e.g. Program Files). Falls back to the exe dir.
std::filesystem::path resolve_data_dir() {
    PWSTR raw = nullptr;
    const HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &raw);
    if (FAILED(hr) || !raw) {
        return exe_dir();
    }
    std::filesystem::path base(raw);
    CoTaskMemFree(raw);
    return base / config::kConfigSubdir;
}

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------

struct WindowContext {
    App*      app{nullptr};
    TrayIcon* tray{nullptr};
    UINT      taskbar_created_msg{0};
};

LRESULT CALLBACK main_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }

    auto* ctx = reinterpret_cast<WindowContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_APP_DISPATCH: {
        auto* fn = reinterpret_cast<std::function<void()>*>(lparam);
        if (fn) {
            (*fn)();
            delete fn;
        }
        return 0;
    }

    case WM_HOTKEY:
        if (ctx && ctx->app && wparam == static_cast<WPARAM>(kHotkeyId)) {
            ctx->app->handle_hotkey(HotkeyEvent::Toggle);
            return 0;
        }
        break;

    case WM_APP_HOLD_START:
        if (ctx && ctx->app) ctx->app->handle_hotkey(HotkeyEvent::HoldStart);
        return 0;

    case WM_APP_HOLD_STOP:
        if (ctx && ctx->app) ctx->app->handle_hotkey(HotkeyEvent::HoldStop);
        return 0;

    case WM_APP_HOLD_LATCHED:
        if (ctx && ctx->app) ctx->app->handle_hotkey(HotkeyEvent::HoldLatched);
        return 0;

    case WM_APP_TRAY:
        if (ctx && ctx->app && ctx->tray) {
            switch (LOWORD(lparam)) {
            case WM_CONTEXTMENU:
            case WM_RBUTTONUP: {
                POINT pt{};
                GetCursorPos(&pt);
                ctx->tray->show_context_menu(pt, ctx->app->has_last_transcript());
                return 0;
            }
            case WM_LBUTTONDBLCLK:
                ctx->app->show_status_feedback();
                return 0;
            }
        }
        break;

    case WM_COMMAND:
        if (ctx && ctx->app) {
            switch (LOWORD(wparam)) {
            case kTrayShowStatusCommand:
                ctx->app->show_status_feedback();
                return 0;
            case kTrayCopyLastCommand:
                ctx->app->copy_last_transcript();
                return 0;
            case kTrayExitCommand:
                if (ctx->tray) ctx->tray->destroy();
                DestroyWindow(hwnd);
                return 0;
            }
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    // Explorer restarted — re-add the tray icon.
    if (ctx && ctx->tray && ctx->taskbar_created_msg != 0 && msg == ctx->taskbar_created_msg) {
        (void)ctx->tray->recreate();
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

HWND create_main_window(HINSTANCE instance, WindowContext* ctx) {
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = main_wnd_proc;
    wc.hInstance     = instance;
    wc.lpszClassName = kMainWindowClass;

    if (!RegisterClassExW(&wc)) {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return nullptr;
    }

    return CreateWindowExW(
        0,
        kMainWindowClass,
        L"dictate_cpp",
        WS_OVERLAPPED,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        nullptr,
        nullptr,
        instance,
        ctx);
}

void show_fatal(const std::wstring& message) {
    MessageBoxW(nullptr, message.c_str(), L"dictate_cpp", MB_ICONERROR);
}

} // namespace

// ---------------------------------------------------------------------------
// wWinMain
// ---------------------------------------------------------------------------

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int /*show_cmd*/) {
    curl_global_init(CURL_GLOBAL_ALL);

    int exit_code = 0;
    {
        WindowContext ctx;

        HWND hwnd = create_main_window(instance, &ctx);
        if (!hwnd) {
            show_fatal(L"Failed to create application window.");
            curl_global_cleanup();
            return 1;
        }

        // Platform services. Declared before App so they outlive it (App's
        // destructor joins the worker, which may still touch the dispatcher).
        WinDispatcher dispatcher;
        BeepSound     sound;
        ClipboardWin  clipboard(hwnd);
        PasteWin      paster;
        OverlayWindow overlay;
        TrayIcon      tray;
        HotkeyManager hotkey;

        dispatcher.attach(hwnd);

        if (!overlay.create(instance, hwnd)) {
            show_fatal(L"Failed to create overlay window.");
            curl_global_cleanup();
            return 1;
        }

        bool input_ok = false;
        switch (config::kInputMode) {
        case InputMode::ToggleHotkey:
            input_ok = hotkey.register_hotkey(hwnd, kHotkeyId, kHotkeyModifiers, kHotkeyVK);
            break;
        case InputMode::HoldPushToTalk:
            input_ok = hotkey.install_hold_ctrl_win(hwnd);
            break;
        }
        if (!input_ok) {
            const DWORD err = GetLastError();
            show_fatal(L"Failed to install input handler. GetLastError=" + std::to_wstring(err));
            overlay.destroy();
            curl_global_cleanup();
            return 1;
        }

        ctx.taskbar_created_msg = RegisterWindowMessageW(L"TaskbarCreated");

        if (!tray.create(hwnd, instance, WM_APP_TRAY)) {
            show_fatal(L"Failed to create tray icon.");
            hotkey.unregister_hotkey();
            overlay.destroy();
            curl_global_cleanup();
            return 1;
        }
        ctx.tray = &tray;
        tray.set_state(platform::TrayState::Idle, {});

        App::Services services;
        services.dispatcher = &dispatcher;
        services.overlay    = &overlay;
        services.tray       = &tray;
        services.clipboard  = &clipboard;
        services.paster     = &paster;
        services.sound      = &sound;

        App app(services, resolve_data_dir());
        ctx.app = &app;

        std::string startup_error;
        if (!app.startup(startup_error)) {
            ctx.app = nullptr;
            tray.destroy();
            hotkey.unregister_hotkey();
            overlay.destroy();
            show_fatal(utf8_to_wide(startup_error));
            curl_global_cleanup();
            return 1;
        }

        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        exit_code = static_cast<int>(msg.wParam);

        // App's destructor (end of scope) joins any in-flight worker before
        // the services above are torn down.
        ctx.app = nullptr;
    }

    curl_global_cleanup();
    return exit_code;
}
