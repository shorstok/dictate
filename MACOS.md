# macOS port — developer guide

Status: **not implemented yet — this is the guide for doing it.** The codebase
has been refactored so that everything under `src/core` + `include/core` is
platform-neutral (built and tested on Windows); what's missing is the macOS
platform layer in `src/platform/mac` and its packaging. `CMakeLists.txt`
contains a commented scaffold of the expected target under `elseif (APPLE)`.

## Architecture recap

The core never touches the OS directly. It talks through the interfaces in
`include/core/platform.hpp`:

| Interface | Purpose | macOS implementation |
|---|---|---|
| `UiDispatcher` | marshal closures onto the UI thread (thread-safe) | `dispatch_async(dispatch_get_main_queue(), ...)` |
| `Overlay` | topmost status blip window | borderless non-activating `NSPanel` |
| `Tray` | status icon + notifications | `NSStatusItem` + `UNUserNotificationCenter` (or just skip notifications initially) |
| `Clipboard` | get/set text | `NSPasteboard` |
| `Paster` | remember focused app, synthetic paste | `NSWorkspace.frontmostApplication` + `CGEventPost` **Cmd+V** |
| `Sound` | 4 audio cues | `NSSound`/`AudioServicesPlaySystemSound`, or `NSBeep` to start |

Input events flow the other way: the platform layer detects the chord and calls
`App::handle_hotkey(HotkeyEvent)` **on the UI thread** (`HoldStart` / `HoldStop`
/ `HoldLatched`, or `Toggle` for the alternative input mode).

Everything else ports for free: `audio_recorder.cpp` uses miniaudio (CoreAudio
backend — no changes), `mp3_encoder.cpp` uses LAME (vcpkg builds it for
`arm64-osx`), `transcription_client.cpp` uses libcurl, and all strings crossing
the interfaces are UTF-8 (native for macOS APIs).

## The hard part: global key capture

Windows uses a `WH_KEYBOARD_LL` hook (`src/platform/win/hotkey_win.cpp`).
The macOS equivalent is a **`CGEventTap`**:

```objc
CFMachPortRef tap = CGEventTapCreate(
    kCGSessionEventTap, kCGHeadInsertEventTap,
    kCGEventTapOptionDefault,              // NOT listenOnly — we must swallow events
    CGEventMaskBit(kCGEventFlagsChanged),  // modifiers arrive as flagsChanged!
    tap_callback, /*userInfo=*/self);
```

Things to know before porting the state machine:

- **Modifier keys do not produce keyDown/keyUp on macOS.** They arrive as
  `kCGEventFlagsChanged`; you derive down/up by diffing `CGEventGetFlags`
  against the previous flags, and distinguish left/right via the keycode
  (`kVK_Control`=0x3B, `kVK_RightControl`=0x3E, `kVK_Command`=0x37,
  `kVK_RightCommand`=0x36). The Windows hold/latch state machine
  (hold = Ctrl+Cmd, latch = tap the other Ctrl) maps 1:1 once you have
  per-key down/up events reconstructed.
- **Suggested chord: Ctrl+Cmd** (Cmd ≈ Win key). Avoid Fn/Globe — it's not
  reliably visible to event taps.
- **Swallowing**: return `NULL` from the tap callback to suppress an event.
  The Windows layer's "leaked modifier" problem exists here in a milder form:
  suppressing a `flagsChanged` release while the press went through leaves
  apps seeing a stale modifier state. Mirror the compensation logic in
  `hotkey_win.cpp` (`compensate_suppressed_up`): post a synthetic
  flags-clearing event when you swallow the release of a key whose press
  leaked. There is no Start-menu equivalent, so no dummy-key trick is needed
  (Cmd alone doesn't trigger anything system-wide by default).
- **Feedback loop**: your own `CGEventPost`ed Cmd+V passes through the tap.
  Tag synthetic events and skip them — create a private `CGEventSource` and
  compare `kCGEventSourceStateID`, or set `kCGEventSourceUserData` to a magic
  value and check it in the callback (this is the Windows `LLKHF_INJECTED`
  check's analog).
- **The tap can be disabled by the OS** (timeout or user revoking permission):
  listen for `kCGEventTapDisabledByTimeout` / `...ByUserInput` in the callback
  and re-enable with `CGEventTapEnable`.
- Run the tap on a dedicated thread with its own `CFRunLoop` (the analog of the
  Windows hook thread), or on the main run loop — either works; keep delivering
  `App::handle_hotkey` via `dispatch_async` onto the main queue.

## Permissions (TCC) — budget real time for this

Three separate permissions, three different prompts:

| Permission | Needed for | Triggered by |
|---|---|---|
| **Microphone** | recording | first `ma_device_start`; requires `NSMicrophoneUsageDescription` in Info.plist or the process is killed |
| **Input Monitoring** | the `CGEventTap` | `CGEventTapCreate` returns NULL until granted (System Settings → Privacy & Security) |
| **Accessibility** | `CGEventPost` (synthetic Cmd+V) | silently no-ops until granted; check with `AXIsProcessTrustedWithOptions` |

Practical notes:

- Permissions are keyed to the **code signature**. Unsigned/ad-hoc-signed dev
  builds lose their grants every rebuild; sign with a stable identity
  (`codesign --force --sign - --identifier com.yourname.dictate ...` keeps at
  least the identifier stable) or expect to re-grant constantly.
- Detect the missing-permission states and show actionable UI (open
  `x-apple.systempreferences:com.apple.preference.security?Privacy_ListenEvent`
  etc.) — a dictation app that silently does nothing is the default failure
  mode of getting this wrong.
- `CGEventTapCreate` returning NULL is *also* how a sandboxed build fails —
  **do not sandbox** this app (no App Store distribution for this mechanism).

## Overlay / Tray specifics

- Overlay: `NSPanel` with `styleMask = .borderless | .nonactivatingPanel`,
  `level = .statusBar`, `ignoresMouseEvents = true`,
  `collectionBehavior = [.canJoinAllSpaces, .stationary]`, rounded corners via
  the content view's `layer.cornerRadius`. Auto-hide with an `NSTimer` —
  mirrors `overlay_win.cpp`'s `show_text(state, text, auto_hide_ms)`.
- Tray: `NSStatusItem` in the menu bar; per-state template images (reuse
  `assets/*.png`, provide @2x). The context menu (Show status / Copy last /
  Exit) queries `App::has_last_transcript()` and calls the same three App
  methods `main_win.cpp` routes to.
- App identity: this must be an **agent app** — `LSUIElement = YES` in
  Info.plist (menu-bar presence only, no Dock icon, no main menu).

## Paste

```objc
// Reactivate the remembered app first (capture_focus stored it):
[previousApp activateWithOptions:0];
usleep(60 * 1000);  // parallel to the Windows 60 ms focus settle
// Then Cmd+V from a private event source:
CGEventRef vDown = CGEventCreateKeyboardEvent(source, kVK_ANSI_V, true);
CGEventSetFlags(vDown, kCGEventFlagMaskCommand);
CGEventPost(kCGHIDEventTap, vDown);  // + matching key-up
```

The clipboard save/restore dance lives in the core (`App::on_transcription_success`)
and needs nothing platform-specific beyond `NSPasteboard` get/set. Note
`NSPasteboard` has no notion of ownership like Win32 — `changeCount` is your
friend if you want to skip restoring when someone else wrote meanwhile
(optional; Windows doesn't do this either).

## Storage

Use `~/Library/Application Support/dictate` as the data dir (the analog of
`%LOCALAPPDATA%\dictate`) — resolve via
`NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, ...)`,
append `config::kConfigSubdir`, pass it to the `App` constructor exactly like
`main_win.cpp`'s `resolve_data_dir()`. Everything downstream (config.json stub,
transcript history, mp3 temp file) is core code and just works.

## Building

```sh
# prerequisites: Xcode (or CLT), CMake ≥ 3.21, vcpkg with VCPKG_ROOT set
cmake --preset mac-release        # uses VCPKG_TARGET_TRIPLET=arm64-osx
cmake --build --preset mac-release
```

The presets exist already (`CMakePresets.json`); the CMake `APPLE` branch
currently stops with a pointer to this file — replace the `message(FATAL_ERROR)`
with the commented scaffold above it once the `.mm` files exist. Use
Objective-C++ (`.mm`) so the files can implement the C++ interfaces directly;
if you'd rather write the shell in Swift, wrap the core behind a small C API
instead — both are viable, `.mm` keeps it one language.

## Suggested implementation order

1. `main_mac.mm` with dispatcher + sound + a stub overlay/tray — get the core
   compiling and the app running as an agent.
2. Clipboard + paster + Accessibility permission — verify end-to-end with the
   `Toggle` input mode wired to a temporary `RegisterEventHotKey` (Carbon —
   ~20 lines, no Input Monitoring permission needed) before tackling the tap.
3. The `CGEventTap` chord/latch machine — the only genuinely tricky part.
4. Overlay polish, notifications, packaging/signing.

Step 2's Carbon hotkey is also a permanent low-permission fallback worth
keeping: users who refuse Input Monitoring still get push-button dictation.
