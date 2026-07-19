# dictate_cpp

Push-to-talk dictation for Windows. Hold **Ctrl+Win**, speak, release — the audio is
transcribed via the OpenAI API (`gpt-4o-transcribe`) and pasted into whatever window
had focus. Lives in the system tray; no main window.

## Usage

| Action | Result |
|---|---|
| Hold **Ctrl+Win** | Records while held ("Listening…" overlay) |
| Release | Stops, transcribes, pastes the text into the previously focused window |
| Tap the *other* Ctrl while holding | **Latch**: recording continues after releasing all keys |
| Tap Ctrl while latched | Stops the latched recording |
| Tray icon double-click | Shows current status overlay |
| Tray icon right-click | Menu: show status, copy last transcript, exit |

Feedback: a small topmost overlay near the top of the screen (Listening / Transcribing /
Done / errors) plus short beeps. Tray icon changes with state.

Guards against accidental use — both discard the recording client-side with a quiet
gray blip, no API call is made:

- **Too short** — the key was held for less than `kMinRecordingMs` (700 ms).
- **No speech detected** — peak amplitude stayed below `kMinPeakAmplitude`
  (a muted/wrong mic would otherwise come back as a hallucinated transcript).

**Latch mode**: while holding Ctrl+Win, tap the second (other) Ctrl key —
e.g. hold LCtrl+Win, tap RCtrl. You can then release everything and recording
continues hands-free. Tap any Ctrl again to stop and transcribe.

Unrelated to the above: the codebase also contains an alternative *input mode*
(`InputMode::ToggleHotkey` in `include/config.hpp`, off by default, compile-time
choice) that replaces the hold-Ctrl+Win gesture entirely with a classic toggle
hotkey — press Ctrl+Alt+Shift+F9 to start, press it again to stop.

## Runtime requirements

- Windows 10/11
- `OPENAI_API_KEY` environment variable (read at transcription time, not stored anywhere)

## Data locations

Everything lives under `%LOCALAPPDATA%\dictate`:

| Path | Purpose |
|---|---|
| `config.json` | User config: `language` (ISO-639-1, e.g. `en`) and `prompt` (transcription hint). A stub is created on first run. |
| `transcribe.log` | Append-only log of all transcripts, one per line |
| `out\transcript_*.txt` | One timestamped file per transcription |
| `out\dictation_error.log` | Error log |
| `mic_input.mp3` | The in-progress recording (16 kHz mono, 48 kbps MP3). Deleted after successful transcription; kept on failure for debugging. |

## How it works

```
LL keyboard hook (own thread)          UI thread (message loop)              worker (std::jthread)
────────────────────────────          ─────────────────────────             ─────────────────────
Ctrl+Win state machine    ──PostMessage──▸  App::handle_hotkey
  hold / latch detection                    │ start: miniaudio capture ──▸ LAME ──▸ mic_input.mp3
  suppresses the keys                       │ stop:  duration/silence guards
  from other apps                           │        └─ ok ──▸ spawn worker  ──▸ curl multipart POST
                                            │                                    api.openai.com
                                            ◂──────────UiDispatcher::post(closure)───────┘
                                            paste: save clipboard ▸ set transcript ▸
                                            refocus target ▸ synthetic Ctrl+V ▸ restore clipboard
```

The code is split into a **platform-neutral core** and **platform layers**
(see the macOS porting guide in [MACOS.md](MACOS.md)):

- `src/core` + `include/core` — the state machine, recording guards, audio →
  MP3 → OpenAI pipeline, config and history. Talks to the OS only through the
  interfaces in `include/core/platform.hpp` (`Overlay`, `Tray`, `Clipboard`,
  `Paster`, `Sound`, `UiDispatcher`). All strings are UTF-8.
- `src/platform/win` — Win32 implementations of those interfaces plus
  `wWinMain` and the message loop. Converts UTF-8 ↔ UTF-16 at its boundary.
- `src/platform/mac` — not implemented; `MACOS.md` documents exactly what to
  build there.

Key design points:

- **Low-level keyboard hook** (`WH_KEYBOARD_LL`) on a dedicated thread, because
  `RegisterHotKey` cannot express "act on release" for a modifier-only chord. The hook
  swallows Ctrl/Win events during a combo session so the Start menu doesn't pop up,
  and ignores injected events (`LLKHF_INJECTED`) so the app's own synthetic Ctrl+V
  doesn't feed back into the state machine. One wrinkle: the *first* modifier's
  key-down necessarily passes through (one key isn't yet a chord), so when its
  release is later swallowed, the hook injects a compensating key-up — otherwise
  the OS would consider that key stuck down. A leaked Win key additionally gets a
  dummy key event first, so the injected release doesn't register as a Win tap
  and open the Start menu.
- **Audio is encoded to MP3 while recording** (miniaudio capture callback → LAME),
  so stopping is instant — there is no post-processing step, and 16 kHz mono @ 48 kbps
  keeps uploads small.
- **All state transitions happen on the UI thread.** The hook thread and the network
  worker communicate exclusively via `PostMessage`; the worker passes results as
  heap-allocated `std::wstring*` owned by the receiver.
- **Pasting** goes through the clipboard: the previous clipboard text is saved,
  replaced with the transcript, a synthetic Ctrl+V is sent to the refocused target
  window, and the old clipboard content is restored after a short delay.

### Source map

| File | Responsibility |
|---|---|
| `include/core/platform.hpp` | The core ↔ platform interface contract |
| `src/core/app.cpp` | Central state machine (idle → listening → transcribing), guards, pipeline |
| `src/core/audio_recorder.cpp` | miniaudio capture device; tracks frames + peak amplitude |
| `src/core/mp3_encoder.cpp` | Streaming LAME encoder |
| `src/core/transcription_client.cpp` | libcurl multipart upload to OpenAI |
| `src/core/user_config.cpp` | `config.json` load/stub, transcript log |
| `src/core/history_store.cpp` | Timestamped transcript files, error log |
| `include/core/config.hpp` | Compile-time constants (input mode, sample rate, guard thresholds, model) |
| `src/platform/win/main_win.cpp` | `wWinMain`, hidden window, message loop, service wiring, data-dir resolution |
| `src/platform/win/hotkey_win.cpp` | LL keyboard hook thread; hold/latch state machine |
| `src/platform/win/overlay_win.cpp` | Borderless topmost status overlay |
| `src/platform/win/tray_win.cpp` | Tray icon, state icons, context menu, balloons |
| `src/platform/win/paste_win.cpp` | Foreground-window capture + synthetic Ctrl+V |
| `src/platform/win/clipboard_win.cpp` | Get/set clipboard text (UTF-8 ↔ UTF-16) |

## Building

Prerequisites:

- Visual Studio 2022 with the C++ workload (MSVC, C++20)
- CMake ≥ 3.21 (the one bundled with VS works)
- [vcpkg](https://github.com/microsoft/vcpkg) with the `VCPKG_ROOT` environment variable set

Then either:

```bat
build.bat                       :: configure + build the x64-release preset
```

or pick a preset yourself:

```bat
cmake --preset x64-release      :: also: x64-debug, x86-release, x86-debug
cmake --build --preset x64-release
```

or just open the folder in Visual Studio (native CMake support picks up the presets).

The result lands in `out\build\<preset>\<Config>\dictate_cpp.exe`. On the first
configure, vcpkg builds the dependencies declared in `vcpkg.json` (curl,
nlohmann-json, mp3lame) into `out\build\<preset>\vcpkg_installed` — that first run
takes a while; later runs are incremental.

## Project layout — why it looks the way it does

```
CMakeLists.txt        the build definition — the single source of truth
CMakePresets.json     configure/build presets (generator, vcpkg toolchain, out/ layout)
vcpkg.json            dependency manifest (curl, nlohmann-json, mp3lame)
include/core/         portable headers + the platform interface contract
src/core/             portable implementation (state machine, audio, network, storage)
src/platform/win/     Win32 layer: entry point, hook, overlay, tray, clipboard, paste
src/platform/mac/     macOS layer — not yet implemented, see MACOS.md
external/miniaudio    vendored single-header audio library (not in vcpkg's model of a
                      linkable lib; pinning the header is simpler)
resources/ assets/    .rc file, icons
out/                  ALL build output — generated, disposable, gitignored
plans/                design notes
```

Points that trip people up:

- **`out/build/<preset>/dictate_cpp.sln` is a build artifact, not the project.**
  The presets use the "Visual Studio 17 2022" CMake generator, and that generator's
  build backend *is* MSBuild — so CMake generates a `.sln`/`.vcxproj` inside the
  binary dir. Never edit or commit it; it is regenerated from `CMakeLists.txt` on
  every configure. Deleting `out/` loses nothing but build time.
- Because the repo caches absolute paths inside `out/`, **moving the repo folder
  invalidates the build trees** — delete `out/build/*` and reconfigure.
- The VS generator is *multi-config*: one build tree serves Debug and Release via
  `--config`/build presets; the `CMAKE_BUILD_TYPE` cache variable is ignored.
- `/utf-8` is set in `CMakeLists.txt` — source files are UTF-8, and without the flag
  MSVC decodes them as the system codepage and silently mangles non-ASCII string
  literals (em-dashes, ellipses) at compile time.
- Secrets never enter the repo: the API key comes from the environment, and
  transcripts/recordings live in LocalAppData, outside the source tree.
