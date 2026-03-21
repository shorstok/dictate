# Gist

Build a Win C++ openai-based dictate->sound-transcribe app.

The behavioral source of truth is the attached Python script (dictate.py). It already defines the important parity anchors: `SAMPLE_RATE = 16000`, `CHANNELS = 1`, `STOP_KEY = ctrl+shift+f9`, `mic_input.wav` in the script directory, an `out/` folder, OpenAI transcription with `gpt-4o-transcribe`, clipboard copy, flattened newlines before paste, timestamped history files, and `dictation_error.log`. The C++ app should preserve those semantics unless a step below explicitly says otherwise.    

For the OpenAI side, the current API shape to target is `POST /audio/transcriptions` with multipart form data. `wav` is a supported input format, `gpt-4o-transcribe` and `gpt-4o-mini-transcribe` are supported transcription models, `language` is optional but improves accuracy and latency, and for `gpt-4o-transcribe` the response format should be `json` with the transcribed text in the `text` field. ([OpenAI Platform][1])

## Build intent

Do **not** invent a big framework, settings system, plugin model, or cross-platform abstraction layer. This is a focused Windows utility. Keep the codebase small, with explicit Win32 code at the edges and plain C++ classes in the middle. Reuse the existing project name `dictate_cpp`, keep the existing preset names, and preserve the Python script’s behavior where possible.   

## Target end state

A Windows desktop utility that:

* launches with no console window
* stays resident with a hidden Win32 window and message loop
* registers a global hotkey matching the Python script (`Ctrl+Shift+F9`)
* on first press: shows a small overlay and starts recording microphone audio
* on second press: stops recording, writes `mic_input.wav`, uploads it to OpenAI transcription, copies transcript to clipboard, pastes it into the active window, saves timestamped history, and shows success or error feedback
* logs failures to `out/dictation_error.log`
* keeps startup overhead low by remaining resident

## Step-by-step plan

### 1) Convert the project from “Hello CMake” into a Windows app skeleton without changing the repo identity

**Goal:** keep the existing repo and target, but turn it into a real Windows application shell.

**Why:** the current project is just `dictate_cpp.cpp` + `dictate_cpp.h` with a console `main()`; everything else should layer onto a hidden-window/message-loop app instead of creating a separate new project.  

**What to do:**

* Keep target name `dictate_cpp`.
* Replace the console entry point with `wWinMain`.
* Change `add_executable(...)` to a `WIN32` executable so no console opens.
* Keep C++20.
* Keep current preset names and output folder pattern from `CMakePresets.json`.

**Peek first:**

* `context.txt` CMakeLists and presets before editing, so you preserve the current naming/build shape.  

**Expected code shape:**

```cmake
add_executable(dictate_cpp WIN32
    src/main.cpp
    src/app.cpp
    src/win32_window.cpp
    src/hotkey_manager.cpp
    src/overlay_window.cpp
    src/audio_recorder.cpp
    src/wav_writer.cpp
    src/transcription_client.cpp
    src/clipboard_service.cpp
    src/paste_service.cpp
    src/history_store.cpp
)
```

Do not keep `using namespace std;` from the stub.

---

### 2) Restructure the source tree, but keep it modest

**Goal:** move from one source file to a small, obvious set of translation units.

**Why:** this is large enough to benefit from separation, but still small enough that over-abstraction would slow the refactor.

**Create this layout:**

```text
include/
  app.hpp
  app_state.hpp
  config.hpp
  hotkey_manager.hpp
  overlay_window.hpp
  audio_recorder.hpp
  wav_writer.hpp
  transcription_client.hpp
  clipboard_service.hpp
  paste_service.hpp
  history_store.hpp
  utf8.hpp

src/
  main.cpp
  app.cpp
  hotkey_manager.cpp
  overlay_window.cpp
  audio_recorder.cpp
  wav_writer.cpp
  transcription_client.cpp
  clipboard_service.cpp
  paste_service.cpp
  history_store.cpp
```

**Rules while building:**

* Prefer concrete classes over interface hierarchies.
* No service locator.
* No template-heavy framework code.
* No custom event bus.

---

### 3) Add dependencies in the least disruptive way

**Goal:** introduce Route A dependencies: `miniaudio`, `libcurl`, `nlohmann/json`, optionally `wil`, optionally `spdlog`.

**Why:** these solve the real problems directly without dragging the project into platform-agnostic architecture.

**What to do:**

* Add `vcpkg.json` with:

  * `curl`
  * `nlohmann-json`
  * `wil`
  * optionally `spdlog`
* Vendor `miniaudio.h` into `external/miniaudio/`.
* Update `CMakeLists.txt` to:

  * `find_package(CURL REQUIRED)`
  * `find_package(nlohmann_json REQUIRED)`
  * `find_package(wil CONFIG REQUIRED)` if used
  * add include path for vendored miniaudio
  * link Win32 libs explicitly if needed: `user32`, `gdi32`, `shell32`, `ole32`, `advapi32`

**Important:** do not vendor libcurl or nlohmann/json manually; use vcpkg manifest mode. Keep miniaudio vendored because it is single-header and simpler.

**CMake additions to prefer:**

* `target_compile_features(dictate_cpp PRIVATE cxx_std_20)`
* `target_compile_definitions(dictate_cpp PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)`
* stricter warnings, but do not turn warnings into errors during the first pass

---

### 4) Introduce a central config/constants file that mirrors the Python behavior

**Goal:** encode the product behavior in one place before wiring functionality.

**Why:** the Python script already tells us the intended defaults; copying them centrally avoids drift. 

**Create `config.hpp` with constants roughly like:**

* sample rate: `16000`
* channels: `1`
* hotkey modifiers: `MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT`
* hotkey virtual key: `VK_F9`
* audio filename: `mic_input.wav`
* output directory: `out`
* default model: `gpt-4o-transcribe`
* optional default language: configurable, start with empty string or `"en"` only if explicitly chosen

**Peek first:**

* `dictate.py` lines 14–21 for exact parity defaults. 

**Do not:**

* invent JSON config loading yet
* invent runtime settings UI yet

---

### 5) Build the app shell and explicit state machine first

**Goal:** define the app coordination logic before implementing device/network code.

**Why:** this prevents UI, recording, and HTTP code from coupling directly to Win32 message handling.

**Create:**

* `enum class AppState { idle, listening, transcribing };`
* `class App` owning:

  * current state
  * hidden message-only or hidden overlapped window handle
  * `HotkeyManager`
  * `OverlayWindow`
  * `AudioRecorder`
  * `TranscriptionClient`
  * `HistoryStore`

**Core transition rules:**

* `idle -> listening` on hotkey
* `listening -> transcribing` on hotkey
* `transcribing -> idle` on completion or failure
* ignore hotkey while transcribing for v1, unless cancel support is implemented cleanly

**Do not** let lower-level classes mutate app state.

**Suggested `App` API:**

```cpp
class App {
public:
    int run(HINSTANCE instance, int show_cmd);

private:
    void on_hotkey();
    void start_recording();
    void stop_recording_and_transcribe();
    void on_transcription_success(std::string text);
    void on_transcription_error(const std::string& message);

    AppState state_{AppState::idle};
};
```

---

### 6) Implement the hidden Win32 window and global hotkey path

**Goal:** get a resident app with deterministic hotkey handling before touching audio.

**Why:** the app’s lifetime and responsiveness depend on the message loop, not on a console main or polling loop.

**Use:**

* a hidden top-level window or message-only window for message dispatch
* `RegisterHotKey`
* `WM_HOTKEY`

**Keep the Python hotkey semantics:** `Ctrl+Shift+F9`. 

**Exact snippet worth using:**

```cpp
constexpr int kHotkeyId = 1;

if (!RegisterHotKey(hwnd_, kHotkeyId, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, VK_F9)) {
    throw std::runtime_error("RegisterHotKey failed");
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }

    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_HOTKEY:
        if (app && wparam == kHotkeyId) {
            app->on_hotkey();
            return 0;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}
```

**Do not:**

* poll keyboard state like the Python version does
* use hooks unless absolutely necessary

---

## Step 7: implement a tiny overlay window
## Purpose
This step is deliberately **early** in the refactor because it gives you immediate confirmation that the app has the right runtime shape:
* resident process
* hidden message-loop-driven app
* hotkey handling
* visible feedback on user action
Before audio capture or HTTP exist, pressing the hotkey should already prove that the app “feels” like the final utility.
## What the overlay must do
For v1, the overlay must support exactly these states:
* `Listening…`
* `Transcribing…`
* `Done`
* `Error`
And this behavior:
* be a **small topmost popup**
* not steal focus from the active application
* show near the top-center of the primary monitor
* stay visible while recording
* auto-hide after a short timeout for `Done` and `Error`
* optionally auto-hide after a short timeout for `Transcribing…` too, but I recommend leaving `Transcribing…` visible until completion
## Keep the implementation intentionally simple
Do **not** bring in any UI framework.
Use a plain Win32 popup window with:
* `WS_POPUP`
* `WS_EX_TOPMOST`
* `WS_EX_TOOLWINDOW`
* `WS_EX_NOACTIVATE`
That combination gives you a lightweight floating status window that does not appear in Alt+Tab and does not grab focus.
Do **not** use:
* WinUI
* Qt
* custom Direct2D pipeline
* tray icon integration
* rich animation system
For the first pass, a manually painted GDI window is exactly right.
## Files to add
Add these two files first:
```text id="3bzt4e"
include/overlay_window.hpp
src/overlay_window.cpp
```
Then wire them into the existing `dictate_cpp` target rather than creating a new target, since the current project is still just the default single-target skeleton. 
## Public API you should implement
Use a small class with an explicit API. This avoids the app poking random Win32 details.
### `include/overlay_window.hpp`
```cpp
#pragma once
#include <string>
#include <windows.h>
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
    HWND hwnd_{nullptr};
    HINSTANCE instance_{nullptr};
    HWND owner_{nullptr};
    OverlayState state_{OverlayState::Hidden};
    std::wstring text_;
};
```
## Why this shape
This is small, explicit, and enough for the app to say:
* `overlay.show_listening();`
* `overlay.show_transcribing();`
* `overlay.show_done();`
* `overlay.show_error(L"API key missing");`
The overlay owns its own window class registration and message handling. That keeps the app code clean.
## Window styles and creation details
This is one of the places where getting the details right matters.
### Design choices
Use:
* `WS_POPUP` because this is not a normal app window
* `WS_EX_TOPMOST` so it floats above the active app
* `WS_EX_TOOLWINDOW` so it stays out of Alt+Tab
* `WS_EX_NOACTIVATE` so it doesn’t steal focus while appearing
* `WS_EX_LAYERED` only if you want opacity; it is optional in the first pass
For the first pass, you can **skip opacity entirely** and paint an opaque dark background. That is simpler and harder to break. Add layered transparency only if it stays clean.
### Creation code
### `src/overlay_window.cpp`
```cpp
#include "overlay_window.hpp"
#include <windowsx.h>
#include <string_view>
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
    owner_ = owner;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &OverlayWindow::WndProc;
    wc.hInstance = instance_;
    wc.lpszClassName = kOverlayClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr; // we paint manually
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
```
### Why this is right
* passing `this` as `lpParam` lets the static `WndProc` bind to the instance
* no parent/child relationship is required
* `owner_` is okay as owner, but it is not required
* we explicitly paint, so `hbrBackground = nullptr`
## WndProc and instance dispatch
This is the other place that is easy to get subtly wrong.
Use the standard Win32 pattern: capture `this` during `WM_NCCREATE`, store it with `GWLP_USERDATA`, then forward later messages to an instance method.
```cpp
LRESULT CALLBACK OverlayWindow::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
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
```
And then:
```cpp
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
        return 1; // we paint everything ourselves
    case WM_DESTROY:
        if (hwnd_) {
            KillTimer(hwnd_, kHideTimerId);
            hwnd_ = nullptr;
        }
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, wparam, lparam);
}
```
### Why this is right
* `WM_TIMER` is enough for the hide timeout
* returning `1` for `WM_ERASEBKGND` reduces flicker
* overlay lifecycle remains fully inside the class
## Positioning the overlay
The overlay should appear at the **top center of the primary monitor**, not near the mouse cursor and not relative to the app’s hidden main window.
This keeps it predictable and keeps focus concerns simple.
```cpp
void OverlayWindow::reposition() {
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int width  = kOverlayWidth;
    const int height = kOverlayHeight;
    const int x = work.left + ((work.right - work.left) - width) / 2;
    const int y = work.top + kTopMargin;
    SetWindowPos(
        hwnd_,
        HWND_TOPMOST,
        x, y, width, height,
        SWP_NOACTIVATE | SWP_SHOWWINDOW
    );
}
```
### Why use `SPI_GETWORKAREA`
That keeps the overlay out of the taskbar area and is “good enough” for v1. Multi-monitor awareness can come later.
## Rounded shape
You said “rounded-looking rectangle if easy.” It is easy enough, and worth doing.
Use a rounded window region rather than implementing a full custom composition effect.
```cpp
void OverlayWindow::update_region() {
    if (!hwnd_) return;
    HRGN region = CreateRoundRectRgn(
        0, 0,
        kOverlayWidth + 1,
        kOverlayHeight + 1,
        20, 20
    );
    SetWindowRgn(hwnd_, region, TRUE);
    // Do not delete region after SetWindowRgn on success; ownership transfers.
}
```
If this causes trouble in practice, fall back to a square window and move on.
## Painting the overlay
This should be plain GDI. No need for Direct2D here.
### Paint goals
* dark filled background
* light border
* centered text
* slightly different text color based on state
* no icons yet
### Paint implementation
```cpp
void OverlayWindow::paint(HDC hdc) {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    COLORREF bg = RGB(32, 32, 32);
    COLORREF border = RGB(80, 80, 80);
    COLORREF text_color = RGB(245, 245, 245);
    switch (state_) {
    case OverlayState::Listening:
        text_color = RGB(255, 230, 140);
        break;
    case OverlayState::Transcribing:
        text_color = RGB(180, 220, 255);
        break;
    case OverlayState::Done:
        text_color = RGB(180, 255, 180);
        break;
    case OverlayState::Error:
        text_color = RGB(255, 170, 170);
        break;
    default:
        break;
    }
    HBRUSH bg_brush = CreateSolidBrush(bg);
    FillRect(hdc, &rc, bg_brush);
    DeleteObject(bg_brush);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ old_pen = SelectObject(hdc, pen);
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
    DrawTextW(
        hdc,
        text_.c_str(),
        -1,
        &text_rc,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS
    );
    SelectObject(hdc, old_font);
    DeleteObject(font);
}
```
### Notes
* Keep the font creation local for now. This is not performance-critical.
* `Segoe UI` is the right default for a Windows utility.
* `DT_END_ELLIPSIS` is useful if an error message is slightly longer.
## Show/hide methods and timer behavior
This is the core behavior layer the app will call.
Use one internal helper:
```cpp
void OverlayWindow::show_text(OverlayState state, const std::wstring& text, DWORD auto_hide_ms) {
    if (!hwnd_) return;
    state_ = state;
    text_ = text;
    KillTimer(hwnd_, kHideTimerId);
    reposition();
    InvalidateRect(hwnd_, nullptr, TRUE);
    UpdateWindow(hwnd_);
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    if (auto_hide_ms > 0) {
        SetTimer(hwnd_, kHideTimerId, auto_hide_ms, nullptr);
    }
}
```
Then map the public methods onto it:
```cpp
void OverlayWindow::show_listening() {
    show_text(OverlayState::Listening, L"Listening...", 0);
}
void OverlayWindow::show_transcribing() {
    show_text(OverlayState::Transcribing, L"Transcribing...", 0);
}
void OverlayWindow::show_done() {
    show_text(OverlayState::Done, L"Done", 1200);
}
void OverlayWindow::show_error(const std::wstring& message) {
    show_text(OverlayState::Error, message.empty() ? L"Error" : message, 1800);
}
void OverlayWindow::hide() {
    if (!hwnd_) return;
    KillTimer(hwnd_, kHideTimerId);
    ShowWindow(hwnd_, SW_HIDE);
    state_ = OverlayState::Hidden;
    text_.clear();
}
```
### Why these timeouts
* `Listening…`: visible until recording stops
* `Transcribing…`: visible until background work completes
* `Done`: short confirmation
* `Error`: slightly longer so the user can actually see it
## Destroy method
Keep destruction explicit and safe:
```cpp
void OverlayWindow::destroy() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}
```
No extra unregister step is strictly necessary unless you want to unregister the class too. It is fine to leave the class registration for process lifetime.
## Integration with the app
Do not overcomplicate. The overlay is just an output surface.
### Minimum integration contract
The `App` class should do roughly this:
* on startup:
  * `overlay_.create(instance, main_hwnd_)`
* on first hotkey:
  * `overlay_.show_listening();`
* on second hotkey:
  * `overlay_.show_transcribing();`
* on transcription success:
  * `overlay_.show_done();`
* on transcription failure:
  * `overlay_.show_error(L"Error");` or a short message like `L"API key missing"`
Do **not** let the overlay inspect the recorder or network client directly.
### State-flow example
```cpp
void App::on_hotkey() {
    switch (state_) {
    case AppState::idle:
        start_recording();
        overlay_.show_listening();
        state_ = AppState::listening;
        break;
    case AppState::listening:
        stop_recording_and_begin_transcription();
        overlay_.show_transcribing();
        state_ = AppState::transcribing;
        break;
    case AppState::transcribing:
        // Ignore for v1
        break;
    }
}
```
And later, when the worker thread posts completion back to the UI thread:
```cpp
void App::on_transcription_success() {
    overlay_.show_done();
    state_ = AppState::idle;
}
void App::on_transcription_error(const std::wstring& short_message) {
    overlay_.show_error(short_message);
    state_ = AppState::idle;
}
```
## Threading rule
This is important enough to be explicit:
**Only the UI thread should touch the overlay window.**
Later, the transcription worker thread should **not** call `overlay_.show_done()` directly. Instead, it should `PostMessage` to the hidden main window, and the main window should then call overlay methods on the UI thread.
That rule will save you from subtle cross-thread Win32 bugs later.
## Build milestone for this step
This step should be considered complete before any audio or HTTP is added.
### Expected behavior after step 7 alone
* app launches as a resident Windows process
* no console window
* global hotkey is registered
* pressing `Ctrl+Shift+F9` alternates between:
  * `Listening...`
  * `Transcribing...`
  * `Done`
* or a simple test cycle like:
  * first press: `Listening...`
  * second press: `Done`
* overlay is topmost, centered, and does not steal focus
* `Done` auto-hides
* `Error` can be manually tested via a temporary debug path
Since the current project still has no real app shell beyond a trivial `main()`, getting this milestone working proves the Windows message loop refactor is actually on track. 
## What not to do in this step
You should explicitly avoid these temptations:
* no animation system
* no alpha-fade timers
* no Direct2D
* no blurred acrylic/mica experiments
* no system tray icon
* no settings panel
* no dynamic sizing based on text measurement yet
* no error-details popup
* no multi-monitor placement logic beyond primary work area
* no reusable generic “toast framework”
This is a tiny app-specific status window, nothing more.
## One simplification I recommend
Do **not** make the overlay class responsible for choosing text from `AppState`.
Keep the app in charge of meaning, and keep the overlay in charge of presentation only.
That is:
* good: `overlay.show_transcribing();`
* bad: `overlay.set_state(AppState::Transcribing);`
The first keeps the overlay independent from the broader app state machine.

---

### 8) Implement microphone capture with miniaudio and match Python recording behavior

**Goal:** get working mono microphone recording to an in-memory PCM buffer.

**Why:** this is the cleanest replacement for the Python `sounddevice.InputStream` callback loop. The Python script records until the hotkey is detected, concatenates frames, and writes a WAV; the C++ version should do the same conceptually. 

**What to implement:**

* `AudioRecorder::start()`
* `AudioRecorder::stop()`
* `AudioRecorder::take_samples()` or `stop_and_take_samples()`

**Format for v1:**

* 16 kHz
* mono
* signed 16-bit PCM
* append captured frames to `std::vector<std::int16_t>`

**Implementation guidance:**

* use miniaudio device callback
* guard the sample buffer with a mutex, or append from callback into a lock-free-friendly structure if simple enough
* prefer `ma_format_s16` to avoid float conversion later

**Peek first:**

* Python `record_to_wav()` for the intended semantics, including “return false if no frames”. 

**Hard-to-get-right detail:** the callback must stay lightweight. Just append data and return; do not do file I/O or HTTP inside it.

**Useful shape:**

```cpp
void data_callback(ma_device* device, void* output, const void* input, ma_uint32 frame_count) {
    auto* self = reinterpret_cast<AudioRecorder*>(device->pUserData);
    if (!input || !self) return;

    auto* samples = static_cast<const std::int16_t*>(input);
    self->append_samples(samples, frame_count);
}
```

---

### 9) Write a tiny WAV writer and keep `mic_input.wav` for parity

**Goal:** serialize the captured PCM to a WAV file that mirrors the Python output.

**Why:** using a real file keeps the HTTP multipart upload straightforward, matches the Python behavior, and makes debugging easy. The Python script writes `mic_input.wav` before transcription.  

**What to implement:**

* `write_wav_file(path, samples, sample_rate, channels)`

**Behavior:**

* write to `<exe dir>/mic_input.wav`
* overwrite existing file
* create `out/` if missing
* return false/error if there are zero samples

**Do not:**

* add a generic audio serialization abstraction
* switch to memory-only uploads in v1

---

### 10) Implement the OpenAI transcription client with libcurl multipart upload

**Goal:** replace the Python SDK call with a direct HTTP client.

**Why:** this is the main part of the latency reduction and the core C++ learning objective.

**Peek first:**

* Python `run_openai_transcription()` for environment variable handling and result expectations. 

**Implement `TranscriptionClient::transcribe_file(...)` to:**

* read `OPENAI_API_KEY` from the environment
* fail with a clear error if missing, matching the Python guidance
* send multipart `POST` to `https://api.openai.com/v1/audio/transcriptions`
* include:

  * `file=@.../mic_input.wav`
  * `model=gpt-4o-transcribe`
  * optionally `language=<code>` when configured
  * `response_format=json`
* parse the JSON response and return `text`

The endpoint, supported WAV upload, supported models, optional `language`, and JSON/text response shape are all documented in the current OpenAI reference. ([OpenAI Platform][1])

**Good libcurl skeleton:**

```cpp
CURL* curl = curl_easy_init();
curl_mime* mime = curl_mime_init(curl);

auto add_field = [&](const char* name, const char* value) {
    auto* part = curl_mime_addpart(mime);
    curl_mime_name(part, name);
    curl_mime_data(part, value, CURL_ZERO_TERMINATED);
};

add_field("model", "gpt-4o-transcribe");
add_field("response_format", "json");
if (!language.empty()) add_field("language", language.c_str());

auto* file_part = curl_mime_addpart(mime);
curl_mime_name(file_part, "file");
curl_mime_filedata(file_part, wav_path.string().c_str());

struct curl_slist* headers = nullptr;
std::string auth = "Authorization: Bearer " + api_key;
headers = curl_slist_append(headers, auth.c_str());

curl_easy_setopt(curl, CURLOPT_URL, "https://api.openai.com/v1/audio/transcriptions");
curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &WriteCallback);
curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
```

**Parse with `nlohmann::json`:**

* require a `text` field
* surface response body in error logs on non-2xx

**Do not:**

* add streaming transcription in v1
* add retries beyond maybe one network retry if extremely easy
* add diarization or timestamps yet

---

### 11) Move transcription off the UI thread and keep state updates explicit

**Goal:** prevent the hotkey/message loop and overlay from freezing during upload.

**Why:** the resident app must remain responsive during network calls.

**What to do:**

* when stopping recording, immediately switch state to `transcribing`
* show overlay `Transcribing…`
* launch a worker using `std::jthread` or `std::thread`
* have the worker:

  * write WAV
  * call OpenAI
  * normalize transcript
  * post a completion message back to the main window

**Strong recommendation:** do **not** let the worker call Win32 UI APIs directly except thread-safe logging. Post back to the main window using a custom `WM_APP + N` message.

**Suggested flow:**

* `WM_APP_TRANSCRIPTION_OK`
* `WM_APP_TRANSCRIPTION_ERROR`

---

### 12) Replace `clip.exe` with native clipboard handling, then auto-paste

**Goal:** preserve Python behavior while making it faster and more robust.

**Why:** the Python script shells out to `clip`; the C++ version should use the real Win32 clipboard. The Python script also flattens newlines before using the transcript. Preserve that.  

**Behavior to preserve:**

* read transcript text
* trim
* replace `\r` and `\n` with spaces before paste
* copy to clipboard
* paste into the previously focused application

**Implementation plan:**

* capture foreground window handle before showing overlay or before transition to transcribing
* after success:

  * normalize text
  * set clipboard `CF_UNICODETEXT`
  * restore/activate prior foreground window if practical
  * synthesize `Ctrl+V` with `SendInput`

**Useful helper shape:**

```cpp
bool set_clipboard_text(HWND owner, const std::wstring& text);
void send_ctrl_v();
```

**Do not:**

* use `system("clip")`
* attempt character-by-character typing in v1

---

### 13) Preserve Python history and error logging behavior

**Goal:** keep the utility useful and debuggable from day one.

**Why:** the Python script already has the correct operational logging behavior; this should come across nearly unchanged. 

**Implement `HistoryStore` to:**

* ensure `<exe dir>/out/` exists
* on success:

  * write `out/transcript_YYYYMMDD-HHMMSS.txt`
* on failure:

  * append `[YYYY-MM-DD HH:MM:SS] message` to `out/dictation_error.log`

**Also keep:**

* a working `mic_input.wav` file for debugging
* optional overwrite of `out/mic_input.txt` only if you explicitly want parity with the Python intermediate txt behavior

**Do not:**

* invent SQLite history
* invent rolling log retention yet

---

### 14) Add audible cues only after overlay + core flow work

**Goal:** preserve the feel of the Python script without letting sound drive architecture.

**Why:** the visual overlay is the primary UI, but the Python script’s beeps are still useful operationally. It beeps on start, stop, success, and error.    

**Match roughly:**

* start recording: short high beep
* stop recording: short lower beep
* success: double beep
* error: longer low beep

Use `Beep()` or `MessageBeep()`; keep it simple.

---

### 15) Finish with tight cleanup and shutdown behavior

**Goal:** avoid leaked hotkeys, devices, temp resources, or hanging threads.

**Why:** resident utilities are judged heavily on how cleanly they live and die.

**What to verify:**

* `UnregisterHotKey` on shutdown
* stop/uninit miniaudio device on shutdown
* join worker thread or use `std::jthread`
* free `curl_mime`, `curl_slist`, `CURL*`
* close overlay window cleanly
* do not leave clipboard locked on failure paths

---

## Suggested file responsibilities

Keep these boundaries:

* `main.cpp`: `wWinMain` only
* `app.cpp`: orchestration and Win32 message handling
* `hotkey_manager.cpp`: register/unregister only
* `overlay_window.cpp`: tiny status UI only
* `audio_recorder.cpp`: miniaudio capture only
* `wav_writer.cpp`: file serialization only
* `transcription_client.cpp`: OpenAI HTTP + JSON only
* `clipboard_service.cpp`: Win32 clipboard only
* `paste_service.cpp`: focus restore + `SendInput`
* `history_store.cpp`: filesystem writes only

Avoid merging transcription, clipboard, and app state into one file.

## Definition of done

The refactor is complete when all of these are true:

* opening the solution in Visual Studio still works with the existing preset names
* `dictate_cpp` builds as a Windows app with no console
* pressing `Ctrl+Shift+F9` starts recording and shows an overlay
* pressing `Ctrl+Shift+F9` again stops recording and shows `Transcribing…`
* a WAV file is written locally
* the file is uploaded to `/audio/transcriptions`
* the returned transcript is flattened, copied to clipboard, pasted, and logged to `out/transcript_*.txt`
* failures append to `out/dictation_error.log`
* the app remains resident after one transcription and can do another without restart
