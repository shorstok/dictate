# Gist

Add MP3 progressive encoding to a Win C++ openai-based dictate->sound-transcribe app.

Given the code we have now, the cleanest path is to add **progressive MP3 encoding during recording**, keep the rest of the app flow almost unchanged, and make the upload client accept a generic audio file instead of hard-coding WAV. Right now the recorder captures PCM into an in-memory `samples_` vector, `App::stop_recording_and_transcribe()` moves those samples out, writes `mic_input.wav`, and `TranscriptionClient` uploads that file with `audio/wav` and the fixed filename `mic_input.wav`.    

The OpenAI file transcription path already accepts `mp3`, so this change can stay within the existing “record → upload finished file → paste text” model. It will reduce upload size, but it still remains a **finished-file upload workflow**, not live partial transcription. ([OpenAI Developers][1])

I would implement it in this exact order.

## 1) Keep the app architecture, only replace the audio artifact

Do **not** redesign hotkeys, overlay, worker messages, clipboard, or history. Those parts are already in the right shape:

* `App` handles the hotkey and UI state
* `AudioRecorder` owns capture
* `TranscriptionClient` owns upload
* `HistoryStore` and paste flow stay independent  

The goal is only to change:

* what `AudioRecorder` produces
* what `TranscriptionClient` uploads

Everything else should stay behaviorally identical.

## 2) Add MP3 support to the build first

Your current build only brings in `curl`, `nlohmann-json`, and `wil`; there is no MP3 encoder library yet. Add `mp3lame` to `vcpkg.json`. vcpkg has a current `mp3lame` package, so this is the most direct dependency path for your project.  ([vcpkg][2])

### Change `vcpkg.json`

```json
{
  "name": "dictate-cpp",
  "version": "0.1.0",
  "dependencies": [
    "curl",
    "nlohmann-json",
    "wil",
    "mp3lame"
  ]
}
```

### Change `CMakeLists.txt`

Do not guess an imported CMake target name for LAME. Use `find_path` / `find_library` so the coding agent does not derail on target naming differences.

Your current CMake target is a single `dictate_cpp` executable with the Route A sources already wired in. Keep that structure and just add one encoder source file plus the LAME include/library lookup. 

Add:

```cmake
find_path(MP3LAME_INCLUDE_DIR NAMES lame/lame.h lame.h)
find_library(MP3LAME_LIBRARY NAMES mp3lame lame libmp3lame)

if (NOT MP3LAME_INCLUDE_DIR OR NOT MP3LAME_LIBRARY)
  message(FATAL_ERROR "mp3lame not found. Make sure vcpkg installed it for the active triplet.")
endif()

target_sources(dictate_cpp PRIVATE
    src/mp3_encoder.cpp
)

target_include_directories(dictate_cpp PRIVATE
    include
    external
    ${MP3LAME_INCLUDE_DIR}
)

target_link_libraries(dictate_cpp PRIVATE
    CURL::libcurl
    nlohmann_json::nlohmann_json
    ${MP3LAME_LIBRARY}
    user32 gdi32 shell32 ole32 advapi32
)
```

## 3) Add MP3-specific config constants, but keep WAV around as fallback

Your current config hard-codes `mic_input.wav`. Add MP3 equivalents, but do not delete the WAV constants yet. This lets the agent keep a fallback path until MP3 is verified stable. 

### Update `include/config.hpp`

```cpp
namespace config {
    constexpr int kSampleRate  = 16000;
    constexpr int kChannels    = 1;

    constexpr UINT kHotkeyModifiers = MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT;
    constexpr UINT kHotkeyVK        = VK_F9;
    constexpr int  kHotkeyId        = 1;

    constexpr wchar_t kAudioFilenameWav[] = L"mic_input.wav";
    constexpr wchar_t kAudioFilenameMp3[] = L"mic_input.mp3";
    constexpr wchar_t kOutputDir[]        = L"out";

    constexpr char kModel[]    = "gpt-4o-transcribe";
    constexpr char kLanguage[] = "";

    constexpr int  kMp3BitrateKbps = 48;   // speech-focused starting point
    constexpr bool kKeepDebugWav    = false;
}
```

Why `48 kbps`:

* good enough for mono speech
* much smaller than WAV
* avoids a second tuning problem on day one

## 4) Generalize the upload client before touching recording

This is the most important “don’t derail” step.

Right now `TranscriptionClient::transcribe_file()` takes a WAV path and hard-codes:

* upload filename = `mic_input.wav`
* MIME type = `audio/wav`  

Change it first so the rest of the app can upload **either** WAV or MP3 cleanly.

### Replace the header with a generic upload spec

#### `include/transcription_client.hpp`

```cpp
#pragma once
#include <filesystem>
#include <string>

struct AudioUploadSpec {
    std::filesystem::path path;
    std::string upload_filename;
    std::string mime_type;
};

struct TranscriptionResult {
    bool        ok{false};
    std::string text;
    std::string error;
};

class TranscriptionClient {
public:
    TranscriptionResult transcribe_file(
        const AudioUploadSpec& audio,
        const std::string& language = "");
};
```

### Update the implementation

Only change the file-part details. Keep the model, response format, response parsing, and error handling as-is.

#### Crucial replacement in `src/transcription_client.cpp`

```cpp
TranscriptionResult TranscriptionClient::transcribe_file(
    const AudioUploadSpec& audio,
    const std::string& language)
{
    const char* api_key_env = std::getenv("OPENAI_API_KEY");
    if (!api_key_env || api_key_env[0] == '\0') {
        return {false, {}, "OPENAI_API_KEY environment variable is not set."};
    }

    const std::string api_key(api_key_env);
    const std::string audio_path_str = audio.path.string();

    CURL* curl = curl_easy_init();
    if (!curl) {
        return {false, {}, "curl_easy_init failed"};
    }

    curl_mime* mime = curl_mime_init(curl);
    std::string response_body;

    auto add_field = [&](const char* name, const char* value) {
        auto* part = curl_mime_addpart(mime);
        curl_mime_name(part, name);
        curl_mime_data(part, value, CURL_ZERO_TERMINATED);
    };

    add_field("model", "gpt-4o-transcribe");
    add_field("response_format", "json");
    if (!language.empty()) {
        add_field("language", language.c_str());
    }

    curl_mimepart* file_part = curl_mime_addpart(mime);
    curl_mime_name(file_part, "file");
    curl_mime_filedata(file_part, audio_path_str.c_str());
    curl_mime_filename(file_part, audio.upload_filename.c_str());
    curl_mime_type(file_part, audio.mime_type.c_str());

    // rest unchanged...
}
```

This is the seam that lets the whole app switch file formats with almost no further HTTP changes. OpenAI’s file transcription guide explicitly supports `mp3` uploads. ([OpenAI Developers][1])

## 5) Add a tiny `Mp3Encoder` class instead of stuffing LAME into `AudioRecorder`

Do not mix LAME setup/flush/details directly into `audio_recorder.cpp`. Keep a narrow encoder wrapper.

Add:

```text
include/mp3_encoder.hpp
src/mp3_encoder.cpp
```

### `include/mp3_encoder.hpp`

```cpp
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

class Mp3Encoder {
public:
    Mp3Encoder();
    ~Mp3Encoder();

    Mp3Encoder(const Mp3Encoder&) = delete;
    Mp3Encoder& operator=(const Mp3Encoder&) = delete;

    bool start(const std::filesystem::path& path,
               int sample_rate,
               int channels,
               int bitrate_kbps,
               std::string& error);

    bool encode_interleaved_s16(const int16_t* samples,
                                std::size_t frame_count,
                                std::string& error);

    bool finish(std::string& error);

    bool is_started() const noexcept;
    std::uint64_t total_input_frames() const noexcept;
    std::uint64_t total_output_bytes() const noexcept;
    const std::filesystem::path& output_path() const noexcept;

private:
    struct Impl;
    Impl* impl_{nullptr};
};
```

### Why this wrapper exists

It isolates:

* LAME initialization
* frame encoding
* flush/finalization
* output file ownership
* counters for “did we actually capture anything?”

That keeps `AudioRecorder` focused on device capture.

## 6) Implement the simple version first: encode directly from the miniaudio callback

Because you asked for a **simple** way, I would not introduce a separate PCM queue and encoder thread yet.

Today the callback just locks a mutex and appends PCM into `samples_`. Replacing that with “lock + feed encoder + maybe count frames” is still small enough. At 16 kHz mono speech, this is a reasonable first pass. If callback pressure appears later, then move to a queue/worker architecture. 

### `src/mp3_encoder.cpp` core shape

Use the LAME API pattern:

* `lame_init()`
* set channels/sample rate/bitrate
* `lame_init_params()`
* `lame_encode_buffer_interleaved()` or mono variant
* `lame_encode_flush()`

The LAME API exposes exactly this incremental model. ([GitHub][3])

### Crucial implementation sketch

```cpp
#include "mp3_encoder.hpp"
#include <lame/lame.h>
#include <cstdio>
#include <vector>

struct Mp3Encoder::Impl {
    lame_t gfp{nullptr};
    FILE* file{nullptr};
    std::filesystem::path path;
    std::vector<unsigned char> buffer;
    std::uint64_t input_frames{0};
    std::uint64_t output_bytes{0};
    bool started{false};
};

bool Mp3Encoder::start(const std::filesystem::path& path,
                       int sample_rate,
                       int channels,
                       int bitrate_kbps,
                       std::string& error)
{
    // close old state first if needed ...

    impl_->file = _wfopen(path.c_str(), L"wb");
    if (!impl_->file) {
        error = "Failed to open MP3 output file";
        return false;
    }

    impl_->gfp = lame_init();
    if (!impl_->gfp) {
        error = "lame_init failed";
        std::fclose(impl_->file);
        impl_->file = nullptr;
        return false;
    }

    lame_set_num_channels(impl_->gfp, channels);
    lame_set_in_samplerate(impl_->gfp, sample_rate);
    lame_set_brate(impl_->gfp, bitrate_kbps);
    lame_set_quality(impl_->gfp, 5);

    if (lame_init_params(impl_->gfp) < 0) {
        error = "lame_init_params failed";
        lame_close(impl_->gfp);
        impl_->gfp = nullptr;
        std::fclose(impl_->file);
        impl_->file = nullptr;
        return false;
    }

    impl_->buffer.resize(8192);
    impl_->path = path;
    impl_->started = true;
    impl_->input_frames = 0;
    impl_->output_bytes = 0;
    return true;
}
```

And the encode method for your **mono s16** capture:

```cpp
bool Mp3Encoder::encode_interleaved_s16(const int16_t* samples,
                                        std::size_t frame_count,
                                        std::string& error)
{
    if (!impl_->started || !samples || frame_count == 0) {
        return true;
    }

    int written = lame_encode_buffer(
        impl_->gfp,
        const_cast<short int*>(samples),
        nullptr,
        static_cast<int>(frame_count),
        impl_->buffer.data(),
        static_cast<int>(impl_->buffer.size())
    );

    if (written < 0) {
        error = "lame_encode_buffer failed";
        return false;
    }

    if (written > 0) {
        if (std::fwrite(impl_->buffer.data(), 1, written, impl_->file) != static_cast<size_t>(written)) {
            error = "Failed to write MP3 data";
            return false;
        }
        impl_->output_bytes += written;
    }

    impl_->input_frames += frame_count;
    return true;
}
```

And `finish()` flushes:

```cpp
bool Mp3Encoder::finish(std::string& error)
{
    if (!impl_->started) return true;

    std::vector<unsigned char> flush_buf(7200);
    int written = lame_encode_flush(impl_->gfp, flush_buf.data(), static_cast<int>(flush_buf.size()));
    if (written < 0) {
        error = "lame_encode_flush failed";
        return false;
    }

    if (written > 0) {
        if (std::fwrite(flush_buf.data(), 1, written, impl_->file) != static_cast<size_t>(written)) {
            error = "Failed to write final MP3 data";
            return false;
        }
        impl_->output_bytes += written;
    }

    lame_close(impl_->gfp);
    impl_->gfp = nullptr;

    std::fclose(impl_->file);
    impl_->file = nullptr;

    impl_->started = false;
    return true;
}
```

## 7) Change `AudioRecorder` so it produces a finished MP3 file instead of a PCM vector

This is the biggest functional change, but it can still stay localized.

Right now `AudioRecorder` owns:

* miniaudio device state
* `samples_`
* `take_samples()`
* callback appending PCM into memory  

Replace that with:

* miniaudio device state
* `Mp3Encoder`
* a mutex guarding encoder calls
* simple capture stats
* a result object returned from `stop()`

### Replace the header with this shape

#### `include/audio_recorder.hpp`

```cpp
#pragma once
#include <filesystem>
#include <string>

struct RecordedAudio {
    bool ok{false};
    bool has_audio{false};
    std::filesystem::path path;
    std::string upload_filename;
    std::string mime_type;
    std::string error;
};

class AudioRecorder {
public:
    AudioRecorder();
    ~AudioRecorder();
    AudioRecorder(const AudioRecorder&) = delete;
    AudioRecorder& operator=(const AudioRecorder&) = delete;

    bool start(const std::filesystem::path& mp3_path, std::string& error);
    RecordedAudio stop();

    void append_samples(const int16_t* data, uint32_t frame_count);

private:
    struct Impl;
    Impl* impl_{nullptr};
};
```

### Why this is better than keeping `take_samples()`

It matches the new product boundary:

* recorder no longer produces raw PCM for the app
* recorder produces a **ready-to-upload artifact**

That lets `App` stay simple.

## 8) Implement `AudioRecorder` around the encoder

Inside `Impl`, add:

* `ma_device device`
* `bool initialized`
* `Mp3Encoder encoder`
* `std::mutex encoder_mutex`
* `std::string last_error`
* `std::filesystem::path current_path`
* `std::uint64_t captured_frames`

### The callback change is the crucial part

Replace:

```cpp
self->append_samples(static_cast<const int16_t*>(input), frame_count);
```

with the same call name, but now `append_samples()` encodes directly instead of appending to `samples_`.

### `append_samples()` should do this

```cpp
void AudioRecorder::append_samples(const int16_t* data, uint32_t frame_count) {
    if (!data || frame_count == 0) return;

    std::lock_guard lock(impl_->encoder_mutex);
    std::string error;
    if (!impl_->encoder.encode_interleaved_s16(data, frame_count, error)) {
        if (impl_->last_error.empty()) {
            impl_->last_error = error;
        }
        return;
    }

    impl_->captured_frames += frame_count;
}
```

### `start()` should

* clear old error/state
* initialize/start the encoder first
* then start miniaudio device

That ordering matters. If the device starts before the output file is ready, the callback can race into an uninitialized encoder.

### `stop()` should

* stop/uninit miniaudio
* lock encoder mutex
* flush encoder
* return a `RecordedAudio`

Example return:

```cpp
RecordedAudio AudioRecorder::stop() {
    if (impl_->initialized) {
        ma_device_stop(&impl_->device);
        ma_device_uninit(&impl_->device);
        impl_->initialized = false;
    }

    std::lock_guard lock(impl_->encoder_mutex);

    if (!impl_->last_error.empty()) {
        return {false, false, {}, {}, {}, impl_->last_error};
    }

    std::string finish_error;
    if (!impl_->encoder.finish(finish_error)) {
        return {false, false, {}, {}, {}, finish_error};
    }

    return {
        true,
        impl_->captured_frames > 0,
        impl_->encoder.output_path(),
        "mic_input.mp3",
        "audio/mpeg",
        {}
    };
}
```

## 9) Update `App` to stop asking for PCM samples or writing WAV

This is where the current implementation is very specific to WAV: the worker thread currently does `take_samples()`, `write_wav_file(...)`, and then uploads the WAV. Remove only those parts. Keep the worker thread, overlay transitions, transcript normalization, history save, clipboard, paste, and UI callbacks unchanged. 

### `start_recording()`

Change it to choose the MP3 output path and pass it into the recorder:

```cpp
void App::start_recording() {
    paste_.capture_foreground();

    const auto mp3_path = exe_dir_ / config::kAudioFilenameMp3;
    std::string error;
    if (!recorder_.start(mp3_path, error)) {
        overlay_.show_error(utf8_to_wide(error.empty() ? "Microphone error" : error));
        return;
    }

    Beep(5000, 25);
    overlay_.show_listening();
    state_ = AppState::listening;
}
```

### `stop_recording_and_transcribe()`

This becomes much simpler:

```cpp
void App::stop_recording_and_transcribe() {
    auto recorded = recorder_.stop();
    Beep(2500, 25);

    if (!recorded.ok) {
        overlay_.show_error(utf8_to_wide(recorded.error.empty() ? "Recording error" : recorded.error));
        state_ = AppState::idle;
        return;
    }

    if (!recorded.has_audio) {
        overlay_.show_error(L"No audio captured");
        state_ = AppState::idle;
        return;
    }

    overlay_.show_transcribing();
    state_ = AppState::transcribing;

    const auto out_dir = exe_dir_ / config::kOutputDir;
    HWND hwnd = hwnd_;

    worker_ = std::jthread([this, recorded, out_dir, hwnd]() {
        std::filesystem::create_directories(out_dir);

        AudioUploadSpec upload{
            .path = recorded.path,
            .upload_filename = recorded.upload_filename,
            .mime_type = recorded.mime_type
        };

        auto result = transcription_.transcribe_file(upload, config::kLanguage);

        if (!result.ok) {
            auto* err = new std::wstring(utf8_to_wide(result.error));
            PostMessageW(hwnd, WM_APP_TRANSCRIPTION_ERROR, 0, reinterpret_cast<LPARAM>(err));
            return;
        }

        std::string text = trim(result.text);
        std::replace(text.begin(), text.end(), '\n', ' ');
        std::replace(text.begin(), text.end(), '\r', ' ');

        if (text.empty()) {
            auto* err = new std::wstring(L"Empty transcript");
            PostMessageW(hwnd, WM_APP_TRANSCRIPTION_ERROR, 0, reinterpret_cast<LPARAM>(err));
            return;
        }

        HistoryStore store(out_dir);
        store.save_transcript(text);

        auto* wide_text = new std::wstring(utf8_to_wide(text));
        PostMessageW(hwnd, WM_APP_TRANSCRIPTION_OK, 0, reinterpret_cast<LPARAM>(wide_text));
    });
}
```

### Important deletion

Remove the now-unused WAV-specific code path:

* `take_samples()`
* `write_wav_file(...)`
* local `wav_path`
* `"Failed to write WAV file"` branch

## 10) Leave `wav_writer.cpp` in the tree for now, but stop using it

Do **not** rip it out immediately. It is still useful as a fallback path if the MP3 integration misbehaves. Your current WAV writer is clean and isolated, so leaving it in the repo costs almost nothing. 

I would:

* leave the files present
* optionally leave them in CMake for one iteration
* just stop calling them from `App`

Once MP3 is stable, remove them in a later cleanup commit.

## 11) Keep the history, paste, and overlay behavior exactly as-is

Do not touch:

* overlay state transitions
* `HistoryStore`
* clipboard copy
* paste restore/send
* transcript normalization

Those are already aligned with the Python behavior and are independent from the audio file format.     

## 12) Validate in this exact order

First test only the recording artifact:

1. Launch app.
2. Press hotkey.
3. Speak for 5–10 seconds.
4. Press hotkey again.
5. Confirm `mic_input.mp3` exists beside the executable.
6. Confirm its size is much smaller than the previous WAV would have been.
7. Confirm it plays in a normal media player.

Then test upload:

8. Verify the multipart request sends:

   * filename `mic_input.mp3`
   * MIME type `audio/mpeg`
9. Confirm transcription succeeds unchanged.
10. Confirm paste/history/error behavior is unchanged.

OpenAI’s speech-to-text guide supports MP3 file uploads on the same finished-file endpoint you are already using. ([OpenAI Developers][1])

## 13) Only add a queue/encoder worker if callback pressure shows up

This is the only “later if needed” step.

The simple implementation above encodes directly from the miniaudio callback. That is the smallest change and should be acceptable for 16 kHz mono dictation. If you see:

* capture glitches
* dropped audio
* high callback latency
* lock contention

then phase 2 is:

* callback pushes PCM chunks into a queue
* a dedicated encoder thread drains the queue and calls LAME
* `stop()` signals end-of-stream and joins the encoder thread

Do **not** start there unless you need it.

## Definition of done

This MP3 refactor is complete when all of these are true:

* `vcpkg.json` includes `mp3lame`
* project builds with LAME linked
* `AudioRecorder` no longer requires `take_samples()` for the main path
* stopping a recording produces `mic_input.mp3`
* `TranscriptionClient` uploads generic audio metadata rather than hard-coded WAV
* OpenAI transcription succeeds with the MP3 artifact
* overlay, clipboard, paste, and history behavior are unchanged from the current app   ([OpenAI Developers][1])

If you want, I can turn this into a file-by-file patch plan next, with exact edits for `audio_recorder.hpp/.cpp`, `transcription_client.hpp/.cpp`, `config.hpp`, `CMakeLists.txt`, and the new `mp3_encoder.hpp/.cpp`.

[1]: https://developers.openai.com/api/docs/guides/speech-to-text/?utm_source=chatgpt.com "Speech to text | OpenAI API"
[2]: https://vcpkg.io/en/package/mp3lame.html?utm_source=chatgpt.com "mp3lame - vcpkg package"
[3]: https://github.com/gypified/libmp3lame/blob/master/API?utm_source=chatgpt.com "libmp3lame/API at master"
