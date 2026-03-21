# Gist

Ok, we've added mp3 encoding and it seemingly works, and now what is missing is that we need some tray icon, something moderately fancy that displays that the app is running and gives option to shut it down in case it is needed.

Yes — this is the right next feature, and your current codebase is already close to supporting it cleanly.

The one structural change I would make first is this: **stop using a message-only main window for the app shell**. Right now `App::create_window()` creates `hwnd_` with `HWND_MESSAGE`, which is great for a pure hidden hotkey receiver, but for a tray icon you want a **real hidden top-level window** that can reliably receive tray callback messages and own a popup menu. Your app is currently using that message-only window as the central app HWND.  

For the tray icon itself, the Windows-supported path is to fill `NOTIFYICONDATA` and call `Shell_NotifyIcon` with `NIM_ADD`, then later update it with `NIM_MODIFY` and remove it with `NIM_DELETE`. Microsoft also recommends setting the icon version and re-adding the icon after Explorer/taskbar restarts. ([Microsoft Learn][1])

## What I would add

Add one small component:

```text
include/tray_icon.hpp
src/tray_icon.cpp
assets/app.ico
resources/app.rc
```

And make the tray icon responsible for exactly three things:

* showing a persistent notification-area icon
* updating tooltip/status text like `dictate_cpp — Idle` / `Listening` / `Transcribing`
* showing a right-click menu with at least:

  * `Show status`
  * `Exit`

That gives you the “moderately fancy” part without dragging in any framework.

## Recommended design

Keep `App` as the orchestrator and add:

* `TrayIcon tray_;`
* one custom callback message, for example:

  * `WM_APP_TRAY = WM_APP + 10`
* one registered message for taskbar recreation:

  * `taskbar_created_msg_ = RegisterWindowMessageW(L"TaskbarCreated");`

### Public tray API

I would keep it this small:

```cpp
enum class TrayState {
    Idle,
    Listening,
    Transcribing,
    Error
};

class TrayIcon {
public:
    bool create(HWND hwnd, HINSTANCE instance, UINT callback_msg);
    void destroy();

    void set_state(TrayState state, const std::wstring& tooltip_suffix = L"");
    void show_balloon(const std::wstring& title, const std::wstring& text, DWORD info_flags = NIIF_INFO);
    void show_context_menu(POINT screen_pt);
    void handle_command(UINT command_id);

private:
    bool add_icon();
    bool modify_icon();
    void load_icons(HINSTANCE instance);

    HWND hwnd_{nullptr};
    UINT callback_msg_{0};
    NOTIFYICONDATAW nid_{};
    HICON idle_icon_{nullptr};
    HICON listening_icon_{nullptr};
    HICON transcribing_icon_{nullptr};
    HICON error_icon_{nullptr};
};
```

## Why this fits your current app

Your `App` already owns the long-lived runtime pieces — hotkey manager, overlay, recorder, transcription client, worker thread — so tray ownership belongs there too. `App` is already where the state transitions happen (`idle`, `listening`, `transcribing`), so it is the right place to call `tray_.set_state(...)`. 

## The key change: replace `HWND_MESSAGE`

Change this:

```cpp
hwnd_ = CreateWindowExW(
    0, kMainWindowClass, L"dictate_cpp",
    0,
    0, 0, 0, 0,
    HWND_MESSAGE,
    nullptr, instance, this
);
```

to a **hidden top-level window**:

```cpp
hwnd_ = CreateWindowExW(
    0,
    kMainWindowClass,
    L"dictate_cpp",
    WS_OVERLAPPED,
    CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
    nullptr,
    nullptr,
    instance,
    this
);

// Keep it hidden; do not call ShowWindow.
```

You still get:

* no visible main window
* no taskbar button, because you never show it

But now it is a normal window that can own menus and tray callbacks more naturally.

## Add a tray callback message

In `app.hpp`:

```cpp
constexpr UINT WM_APP_TRANSCRIPTION_OK    = WM_APP + 1;
constexpr UINT WM_APP_TRANSCRIPTION_ERROR = WM_APP + 2;
constexpr UINT WM_APP_TRAY                = WM_APP + 10;

constexpr UINT ID_TRAY_SHOW_STATUS = 1001;
constexpr UINT ID_TRAY_EXIT        = 1002;
```

Then in `App` add:

```cpp
UINT taskbar_created_msg_{0};
TrayIcon tray_;
```

## Create the tray icon during startup

In `run()`:

```cpp
taskbar_created_msg_ = RegisterWindowMessageW(L"TaskbarCreated");

if (!tray_.create(hwnd_, instance_, WM_APP_TRAY)) {
    MessageBoxW(nullptr, L"Failed to create tray icon.", L"dictate_cpp", MB_ICONERROR);
    return 1;
}
tray_.set_state(TrayState::Idle);
```

And on shutdown:

```cpp
tray_.destroy();
```

Do that before destroying the window.

## Handle tray callbacks in `WndProc`

Add these cases:

```cpp
case WM_APP_TRAY:
    if (app) {
        switch (LOWORD(lparam)) {
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU: {
            POINT pt{};
            GetCursorPos(&pt);
            app->show_tray_menu(pt);
            return 0;
        }
        case WM_LBUTTONDBLCLK:
            app->show_status_feedback();
            return 0;
        }
    }
    break;

case WM_COMMAND:
    if (app) {
        switch (LOWORD(wparam)) {
        case ID_TRAY_SHOW_STATUS:
            app->show_status_feedback();
            return 0;
        case ID_TRAY_EXIT:
            DestroyWindow(hwnd);
            return 0;
        }
    }
    break;
```

And also handle taskbar recreation:

```cpp
default:
    if (msg == app->taskbar_created_message()) {
        app->recreate_tray_icon();
        return 0;
    }
    break;
```

That last part matters because Explorer can restart and wipe the notification area; Microsoft explicitly documents re-adding the icon when needed. ([Microsoft Learn][1])

## `TrayIcon::create()`

This is the core setup:

```cpp
bool TrayIcon::create(HWND hwnd, HINSTANCE instance, UINT callback_msg) {
    hwnd_ = hwnd;
    callback_msg_ = callback_msg;

    load_icons(instance);

    nid_ = {};
    nid_.cbSize = sizeof(nid_);
    nid_.hWnd = hwnd_;
    nid_.uID = 1;
    nid_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid_.uCallbackMessage = callback_msg_;
    nid_.hIcon = idle_icon_;

    wcscpy_s(nid_.szTip, L"dictate_cpp — Idle");

    if (!Shell_NotifyIconW(NIM_ADD, &nid_)) {
        return false;
    }

    nid_.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid_);

    return true;
}
```

That matches the documented Shell API flow: add, then set version. ([Microsoft Learn][2])

## Context menu handling

This is the easy-to-miss detail: before `TrackPopupMenu`, call `SetForegroundWindow(hwnd_)`. Microsoft’s docs are explicit that otherwise the menu may not dismiss correctly when the user clicks away. ([Microsoft Learn][3])

Use:

```cpp
void TrayIcon::show_context_menu(POINT pt) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    AppendMenuW(menu, MF_STRING, ID_TRAY_SHOW_STATUS, L"Show status");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"Exit");

    SetForegroundWindow(hwnd_);

    TrackPopupMenu(
        menu,
        TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN,
        pt.x, pt.y,
        0,
        hwnd_,
        nullptr
    );

    PostMessageW(hwnd_, WM_NULL, 0, 0);
    DestroyMenu(menu);
}
```

That `PostMessage(WM_NULL)` pattern is commonly used with tray popup menus to help menu dismissal behavior after `TrackPopupMenu`.

## “Moderately fancy” state presentation

You do not need custom-drawn menus yet. The easiest nice-looking version is:

* **different icon** per state
* **tooltip text** changes with state
* optional **balloon notification** only for errors

So:

* Idle: grey or neutral icon
* Listening: red dot / mic icon
* Transcribing: blue spinner-ish icon or alternate colored mic
* Error: warning icon

Then in `set_state()`:

```cpp
void TrayIcon::set_state(TrayState state, const std::wstring& tooltip_suffix) {
    HICON icon = idle_icon_;
    std::wstring tip = L"dictate_cpp — Idle";

    switch (state) {
    case TrayState::Idle:
        icon = idle_icon_;
        tip = L"dictate_cpp — Idle";
        break;
    case TrayState::Listening:
        icon = listening_icon_;
        tip = L"dictate_cpp — Listening";
        break;
    case TrayState::Transcribing:
        icon = transcribing_icon_;
        tip = L"dictate_cpp — Transcribing";
        break;
    case TrayState::Error:
        icon = error_icon_;
        tip = L"dictate_cpp — Error";
        break;
    }

    if (!tooltip_suffix.empty()) {
        tip += L" — " + tooltip_suffix;
    }

    nid_.uFlags = NIF_ICON | NIF_TIP;
    nid_.hIcon = icon;
    wcsncpy_s(nid_.szTip, tip.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid_);
}
```

## Where to update the tray state in your app

These should line up with your current transitions:

In `start_recording()`:

```cpp
tray_.set_state(TrayState::Listening);
```

When stop begins transcription:

```cpp
tray_.set_state(TrayState::Transcribing);
```

On success:

```cpp
tray_.set_state(TrayState::Idle);
```

On error:

```cpp
tray_.set_state(TrayState::Error);
tray_.show_balloon(L"dictate_cpp", short_message, NIIF_ERROR);
```

That matches your existing `AppState` and current flow through `start_recording()`, `stop_recording_and_transcribe()`, and the worker completion messages. 

## Nice left-click / double-click behavior

Keep it simple:

* **right click** → context menu
* **double left click** → show quick feedback

Since you already have the overlay window, double-click can reuse it:

```cpp
void App::show_status_feedback() {
    switch (state_) {
    case AppState::idle:
        overlay_.show_done(); // or add overlay_.show_idle()
        break;
    case AppState::listening:
        overlay_.show_listening();
        break;
    case AppState::transcribing:
        overlay_.show_transcribing();
        break;
    }
}
```

That makes the tray feel integrated instead of separate.

## Add a real app icon resource

Right now I do not see resource integration in the current `CMakeLists.txt`; it links the executable and libraries but does not yet mention a `.rc` file. 

Add:

```rc
IDI_APP_ICON ICON "assets\\app.ico"
IDI_APP_LISTENING ICON "assets\\app_listening.ico"
IDI_APP_TRANSCRIBING ICON "assets\\app_transcribing.ico"
IDI_APP_ERROR ICON "assets\\app_error.ico"
```

Then in `CMakeLists.txt`, include the resource file in `add_executable(...)`.

Load them with:

```cpp
idle_icon_ = static_cast<HICON>(
    LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
```

## What I would not do yet

I would skip these for now:

* owner-drawn menu
* custom hover popups
* animated tray icon
* separate settings window
* “start with Windows”
* “pause hotkey” mode

Those are all nice future additions, but the smallest good tray integration is:

* icon
* tooltip
* right-click menu
* exit
* state updates

## The only architectural caution

Because your app currently posts worker results back to the UI thread via `WM_APP_TRANSCRIPTION_OK` and `WM_APP_TRANSCRIPTION_ERROR`, keep tray updates on the **UI thread only**, just like the overlay. That is already the pattern your app uses for UI-facing state completion, so keep following it. 

## My recommendation in one sentence

Add a `TrayIcon` class, switch the main app window from `HWND_MESSAGE` to a hidden top-level window, create a persistent notification-area icon with `Shell_NotifyIcon`, handle right-click with `TrackPopupMenu`, and update the tray icon/tooltip off your existing `AppState`.  ([Microsoft Learn][1])

[1]: https://learn.microsoft.com/en-us/windows/win32/shell/notification-area?utm_source=chatgpt.com "Notifications and the Notification Area - Win32 apps"
[2]: https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shell_notifyiconw?utm_source=chatgpt.com "Shell_NotifyIconW function (shellapi.h) - Win32 apps"
[3]: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-trackpopupmenu?utm_source=chatgpt.com "TrackPopupMenu function (winuser.h) - Win32 apps"
