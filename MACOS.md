# macOS port — developer guide

Status: **implemented.** `src/platform/mac` provides the macOS implementations
of the `include/core/platform.hpp` interfaces plus the `NSApplication` entry
point; `CMakeLists.txt`'s `APPLE` branch builds them into a signed
`dictate_cpp.app` bundle. This file documents how that layer works and, more
importantly, *why* — most of it is not obvious from the API names.

## Architecture recap

The core never touches the OS directly. It talks through the interfaces in
`include/core/platform.hpp`:

| Interface | Purpose | macOS implementation |
|---|---|---|
| `UiDispatcher` | marshal closures onto the UI thread (thread-safe) | `dispatch_async(dispatch_get_main_queue(), …)` — `main_mac.mm` |
| `Overlay` | topmost status blip window | borderless non-activating `NSPanel` — `overlay_mac.mm` |
| `Tray` | status icon + notifications | `NSStatusItem` + `UNUserNotificationCenter` — `tray_mac.mm` |
| `Clipboard` | get/set text | `NSPasteboard` — `clipboard_mac.mm` |
| `Paster` | remember focused app, synthetic paste | `NSWorkspace.frontmostApplication` + `CGEventPost` **Cmd+V** — `paste_mac.mm` |
| `Sound` | 4 audio cues | `NSSound` system alert sounds — `main_mac.mm` |

Input events flow the other way: `hotkey_mac.mm` detects the chord and calls
`App::handle_hotkey(HotkeyEvent)` **on the main thread** (`HoldStart` /
`HoldStop` / `HoldLatched`, or `Toggle` for the alternative input mode).

Everything else ports for free: `audio_recorder.cpp` uses miniaudio (CoreAudio
backend), `mp3_encoder.cpp` uses LAME, `transcription_client.cpp` uses libcurl,
and all strings crossing the interfaces are UTF-8 (native for macOS APIs).

## Global key capture

Windows uses a `WH_KEYBOARD_LL` hook (`src/platform/win/hotkey_win.cpp`); the
macOS equivalent in `hotkey_mac.mm` is a **`CGEventTap`** on
`kCGEventFlagsChanged`, created with `kCGEventTapOptionDefault` (*not*
listen-only — the chord must be swallowed). The hold/latch state machine is a
line-by-line port of the Windows one; what differs is everything around it:

- **Modifier keys do not produce keyDown/keyUp on macOS.** They arrive as
  `kCGEventFlagsChanged`, so per-key down/up is reconstructed by testing the
  device-dependent flag bit that belongs to the keycode that changed
  (`NX_DEVICELCTLKEYMASK` and friends, spelled out as `kDeviceLCtrl` etc. in
  `hotkey_mac.mm` to keep IOKit headers out of the build). `key_is_down()`
  falls back to the side-agnostic mask when neither device bit is reported,
  and `handle_flags_changed()` additionally clears both sides whenever
  `kCGEventFlagMaskControl`/`Command` goes clear — a missed release would
  otherwise leave the chord stuck on forever.
- **Chord: Ctrl+Cmd** (Cmd ≈ Win key). Fn/Globe is deliberately unused — it is
  not reliably visible to event taps.
- **Swallowing**: the callback returns `nullptr` to suppress an event. The
  Windows "leaked modifier" problem exists here in a milder form: the first
  modifier's press necessarily passes through (one key is not yet a chord), so
  when its release is later swallowed, apps would keep seeing it as held.
  `compensate_suppressed_up()` posts a synthetic `kCGEventFlagsChanged`
  carrying `system_visible_flags()` — the physical flags with every tracked
  modifier the tap swallowed masked out, which is exactly the state the rest of
  the system should believe in. There is no Start-menu equivalent, so the
  Windows dummy-key trick is not needed.
- **Feedback loop**: the app's own `CGEventPost`ed Cmd+V and compensation
  events pass back through the tap. All of them are created from one private
  `CGEventSource` tagged via `CGEventSourceSetUserData`
  (`mac_common.hpp`: `kSyntheticEventTag`), and the callback skips anything
  carrying that tag — the analog of the Windows `LLKHF_INJECTED` check.
- **The tap can be disabled by the OS** (callback too slow, or the user
  revoking the grant): `kCGEventTapDisabledByTimeout` / `…ByUserInput` are
  handled by re-arming with `CGEventTapEnable`.
- The tap runs on a **dedicated thread with its own `CFRunLoop`** (the analog
  of the Windows hook thread), so `App::handle_hotkey` — which starts a
  CoreAudio device and can take tens of milliseconds — cannot stall the
  callback into a timeout. Events reach the core via `dispatch_async` onto the
  main queue.

### Carbon fallback

`HotkeyMac::install_toggle_hotkey()` registers **Ctrl+Option+Shift+F9** with
Carbon's `RegisterEventHotKey`. It needs no Input Monitoring permission, and
serves two purposes: it implements `InputMode::ToggleHotkey`, and it is the
automatic fallback when `CGEventTapCreate` returns NULL — users who decline
Input Monitoring still get push-button dictation instead of an app that
silently does nothing.

## Permissions (TCC)

Three separate permissions, three different prompts:

| Permission | Needed for | Probe |
|---|---|---|
| **Microphone** | recording | `AVCaptureDevice authorizationStatusForMediaType:` |
| **Input Monitoring** | the `CGEventTap` | `CGPreflightListenEventAccess()` / `CGRequestListenEventAccess()` |
| **Accessibility** | `CGEventPost` (synthetic Cmd+V) | `AXIsProcessTrustedWithOptions` |

`permissions_mac.mm` wraps all three plus the corresponding
`x-apple.systempreferences:` deep links. `main_mac.mm` leans on Apple's own
prompts where they exist (they already carry an "Open System Settings" button)
and raises its own `NSAlert` only when a grant is *already denied* — the case
where the system will never prompt again — or when the tap fell back to the
Carbon hotkey. All three failures degrade rather than abort: no microphone
means empty recordings, no Accessibility means the transcript is copied but not
pasted, no Input Monitoring means toggle instead of hold.

Practical notes:

- Permissions are keyed to the **code signature**. The CMake `POST_BUILD` step
  ad-hoc signs the bundle with an explicit
  `--identifier ${DICTATE_BUNDLE_ID}` (default `com.shorstok.dictate`), which
  is what keeps grants alive across rebuilds. Set `DICTATE_CODESIGN_IDENTITY`
  to a real Developer ID for distribution.
- **Do not sandbox this app.** `CGEventTapCreate` returning NULL is also how a
  sandboxed build fails, and there is no App Store path for this mechanism.

## Overlay / Tray specifics

- Overlay (`overlay_mac.mm`): `NSPanel` with
  `styleMask = .borderless | .nonactivatingPanel`, `level = .statusBar`,
  `ignoresMouseEvents = true`,
  `collectionBehavior = [.canJoinAllSpaces, .stationary, .fullScreenAuxiliary, .ignoresCycle]`,
  rounded corners via the content view's `layer.cornerRadius`, shown with
  `orderFrontRegardless` so it never steals focus. Auto-hide uses an `NSTimer`,
  mirroring `overlay_win.cpp`'s `show_text(state, text, auto_hide_ms)`. It
  follows the screen the pointer is on rather than the primary display.
- Tray (`tray_mac.mm`): `NSStatusItem` with per-state images loaded from the
  bundle (`assets/*.png`, 128 px originals drawn at 18 pt — sharp on Retina
  without separate @2x files) and a fallback glyph title if the resources are
  missing. The menu (Show status / Copy last / Quit) refreshes *Copy last*'s
  enabled state in `menuNeedsUpdate:` from `App::has_last_transcript()`.
- Notifications: `UNUserNotificationCenter` refuses to authorize an
  ad-hoc-signed app ("Notifications are not allowed for this application"), and
  raises outright when there is no bundle at all. Both cases fall back to the
  overlay, so startup and error messages are never silently dropped.
- App identity: this is an **agent app** — `LSUIElement = YES` in Info.plist
  and `NSApplicationActivationPolicyAccessory` at runtime (menu-bar presence
  only, no Dock icon, no main menu).

## Paste

`paste_mac.mm` reactivates the remembered app, waits 60 ms (the same focus
settle as the Windows layer), then posts Cmd+V from the tagged private event
source with the flags set explicitly — inheriting whatever the user still holds
down would turn it into paste-and-match-style. `capture_focus()` refuses to
record *this* app as the paste target.

The clipboard save/restore dance lives in the core
(`App::on_transcription_success`) and needs nothing platform-specific beyond
`NSPasteboard` get/set.

## Storage

`~/Library/Application Support/dictate` (the analog of `%LOCALAPPDATA%\dictate`),
resolved via `NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, …)`
plus `config::kConfigSubdir`, and passed to the `App` constructor exactly like
`main_win.cpp`'s `resolve_data_dir()`. Everything downstream (config.json stub,
transcript history, mp3 temp file) is core code and works unchanged.

## Building

```sh
# prerequisites: Xcode Command Line Tools, CMake >= 3.21, Ninja, pkg-config,
# and vcpkg with VCPKG_ROOT set
brew install cmake ninja pkg-config   # pkg-config is required by vcpkg's openssl port

cmake --preset mac-release            # uses VCPKG_TARGET_TRIPLET=arm64-osx
cmake --build --preset mac-release
open out/build/mac-release/dictate_cpp.app
```

Useful cache variables: `DICTATE_BUNDLE_ID`, `DICTATE_CODESIGN_IDENTITY`,
`DICTATE_MIN_MACOS`.

The `.mm` (Objective-C++) files implement the C++ interfaces directly, so the
whole layer is one language; `CMakeLists.txt` sets `OBJCXX_STANDARD 20` because
the `cxx_std_20` compile feature only covers the `CXX` language.

## Known gaps

- Notifications only work with a properly signed build; ad-hoc builds always
  take the overlay fallback (see above).
- Intel/universal binaries are untested — the presets target `arm64-osx` only.
- Suppressing `flagsChanged` does not rewrite the modifier flags the
  WindowServer stamps on *other* events, so an app could still observe a stray
  Cmd bit on an unrelated keystroke pressed during a chord. The Windows layer
  has the same limitation.
