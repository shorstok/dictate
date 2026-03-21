# Gist

Make app configurable, like there are prompt and language parameters in the OpenAI transcription endpoint, and I would like them to be configurable - currently, just only via config file. so the logic I want is that upon starting and prior to each transcription call, the application should read some simple configuration file from AppData local folder / dictate, and if it's not present for that user, to create a stub file for later editing.

# Plan

here’s arefactor plan for adding a per-user config file in `%LocalAppData%\dictate`, with `prompt` and `language` read at startup and re-read before every transcription call.

Right now the app still treats transcription settings as compile-time constants: `config.hpp` defines `kModel` and `kLanguage`, `TranscriptionClient::transcribe_file(...)` only accepts `language`, and `App::stop_recording_and_transcribe()` passes `config::kLanguage` into the worker-side transcription call. That is exactly the seam to refactor, and it is narrow enough that this can be done without disturbing recording, tray, overlay, or paste logic.   

For the API side, the current transcription endpoint supports both `language` and `prompt`. `language` is an optional string, and supplying it in ISO-639-1 form such as `en` improves accuracy and latency. `prompt` is also optional and is used to guide style or continue a previous audio segment; it should match the audio language. `gpt-4o-transcribe` supports only `json` as its response format. ([OpenAI Platform][1])

For the Windows path, use `SHGetKnownFolderPath(FOLDERID_LocalAppData, ...)` to resolve the per-user Local AppData directory. Microsoft’s Win32 guidance is to use `SHGetKnownFolderPath` plus the relevant `KNOWNFOLDERID` for special folders. ([Microsoft Learn][2])

## Refactor goal

Add a new runtime config subsystem so that:

* on startup, the app ensures `%LocalAppData%\dictate\config.json` exists
* if it does not exist, it creates a stub file with editable defaults
* before **each** transcription call, the app re-reads that file
* the current transcription request uses the file’s `language` and `prompt`
* config file errors are surfaced clearly and logged like other operational failures
* static app constants such as hotkey, sample rate, output filenames, and MP3 bitrate stay in `config.hpp`

## Design choice to keep the refactor clean

Do **not** turn the existing `config.hpp` namespace into a file-backed settings system.

That file currently holds true compile-time constants: sample rate, hotkey, audio filenames, output dir name, default model, MP3 bitrate, and debug flags. It is used broadly by the app and recorder, and mixing file I/O into it would blur the boundary between “build/runtime invariant” and “user-editable settings.” Keep it as the static constants module, and add a new runtime config module beside it. 

## Step 1: add a dedicated runtime config module

Create two new files:

```text
include/user_config.hpp
src/user_config.cpp
```

These should own:

* locating `%LocalAppData%\dictate`
* locating `config.json`
* creating the directory if needed
* writing a stub file if missing
* loading/parsing the file into a small struct
* validating/coercing fields into a usable form

Do **not** put this logic into `App`, `HistoryStore`, or `TranscriptionClient`.

### Public API to implement

Use something like this:

```cpp
#pragma once
#include <filesystem>
#include <string>

struct TranscriptionOptions {
    std::string language;  // empty = let API auto-detect
    std::string prompt;    // empty = omit from request
};

struct UserConfigLoadResult {
    bool ok{false};
    bool created_stub{false};
    std::filesystem::path config_path;
    TranscriptionOptions transcription;
    std::string error;
};

class UserConfigStore {
public:
    UserConfigLoadResult ensure_exists_and_load() const;
    std::filesystem::path config_path(std::string& error) const;

private:
    bool write_stub_file(const std::filesystem::path& path, std::string& error) const;
};
```

## Why this shape

The app needs only two behaviors:

* startup: “make sure the file exists so the user can edit it”
* transcription time: “load the latest settings now”

So the store should expose a small surface and return a single result object with:

* path
* parsed options
* whether a stub was created
* error if something failed

That keeps `App` simple.

---

## Step 2: use JSON for the file format

Use:

```text
%LocalAppData%\dictate\config.json
```

Use JSON rather than INI because:

* `nlohmann/json` is already in the project and linked in CMake
* the data shape is tiny
* you avoid inventing a custom parser
* future extensibility stays easy

The project already depends on `nlohmann_json`, so adding a small JSON config reader introduces no new dependency or style mismatch. 

### Stub file content

Write this exact content on first run if the file is missing:

```json
{
  "language": "",
  "prompt": "",
  "_note": "language is optional; use ISO-639-1 like en or pl. Empty values mean defaults."
}
```

### Why this stub works

* it is valid JSON
* it is editable with Notepad
* it avoids JSON comments, which are invalid
* `_note` is harmless if your parser ignores unknown keys

Do **not** auto-overwrite the file if it already exists.

---

## Step 3: implement the Local AppData path helper correctly

This is one of the two places most likely to derail if done loosely.

Use `SHGetKnownFolderPath(FOLDERID_LocalAppData, ...)`, append `dictate`, create the directory if needed, then append `config.json`. That is the Windows-native per-user location for the file. ([Microsoft Learn][2])

### Good implementation shape

```cpp
#include "user_config.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>

#include <filesystem>
#include <system_error>

std::filesystem::path UserConfigStore::config_path(std::string& error) const {
    error.clear();

    PWSTR raw = nullptr;
    const HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &raw);
    if (FAILED(hr) || !raw) {
        error = "Failed to resolve LocalAppData path.";
        return {};
    }

    std::filesystem::path base(raw);
    CoTaskMemFree(raw);

    std::error_code ec;
    std::filesystem::path dir = base / L"dictate";
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        error = "Failed to create config directory: " + dir.string();
        return {};
    }

    return dir / L"config.json";
}
```

### Notes for the coding agent

* free the returned buffer with `CoTaskMemFree`
* use `KF_FLAG_CREATE` so the folder is materialized if needed
* return a filesystem path, not a raw `PWSTR`
* keep this path separate from `exe_dir_ / out`; the user only asked to move the config file, not the transcript history

---

## Step 4: make parsing permissive and default-friendly

The config file is for operational convenience, not strict schema enforcement.

That means:

* missing `language` => use empty string
* missing `prompt` => use empty string
* unknown keys => ignore
* malformed JSON => fail with a clear error
* wrong types for known keys => fail with a clear error

Do **not** hard-reject non-two-letter language values. The docs recommend ISO-639-1 for latency and accuracy, but the app should not become brittle over that. Accept any string, trim whitespace, and pass it through if non-empty. ([OpenAI Platform][1])

### Suggested parser

```cpp
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace {
    std::string trim_copy(std::string s) {
        auto not_space = [](unsigned char c) { return !std::isspace(c); };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
        s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
        return s;
    }
}

UserConfigLoadResult UserConfigStore::ensure_exists_and_load() const {
    UserConfigLoadResult result;

    std::string path_error;
    result.config_path = config_path(path_error);
    if (result.config_path.empty()) {
        result.error = path_error.empty() ? "Unable to locate config path." : path_error;
        return result;
    }

    if (!std::filesystem::exists(result.config_path)) {
        std::string stub_error;
        if (!write_stub_file(result.config_path, stub_error)) {
            result.error = stub_error;
            return result;
        }
        result.created_stub = true;
    }

    std::ifstream f(result.config_path, std::ios::binary);
    if (!f) {
        result.error = "Failed to open config file for reading.";
        return result;
    }

    nlohmann::json j;
    try {
        f >> j;
    } catch (const std::exception& e) {
        result.error = std::string("Failed to parse config.json: ") + e.what();
        return result;
    }

    try {
        if (j.contains("language") && !j["language"].is_string()) {
            result.error = "\"language\" must be a string.";
            return result;
        }
        if (j.contains("prompt") && !j["prompt"].is_string()) {
            result.error = "\"prompt\" must be a string.";
            return result;
        }

        result.transcription.language = trim_copy(j.value("language", ""));
        result.transcription.prompt   = trim_copy(j.value("prompt", ""));
    } catch (const std::exception& e) {
        result.error = std::string("Invalid config values: ") + e.what();
        return result;
    }

    result.ok = true;
    return result;
}
```

### Why this is the right level of strictness

It catches the mistakes that matter:

* malformed JSON
* wrong field types

But it does not burden the user with validation rules that are not necessary for the app to function.

---

## Step 5: implement stub creation as a single, explicit write

This should not be hidden inside parsing logic.

Add a helper:

```cpp
bool UserConfigStore::write_stub_file(const std::filesystem::path& path, std::string& error) const {
    error.clear();

    std::ofstream f(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!f) {
        error = "Failed to create stub config file: " + path.string();
        return false;
    }

    f <<
R"({
  "language": "",
  "prompt": "",
  "_note": "language is optional; use ISO-639-1 like en or pl. Empty values mean defaults."
}
)";
    return true;
}
```

Do **not** create a more elaborate generator. The point is discoverability and editability.

---

## Step 6: keep `config.hpp` for invariants, remove runtime `kLanguage`

This is one of the key cleanup steps.

Right now `config.hpp` contains:

* hotkey config
* sample rate
* output/audio names
* model
* language
* MP3 bitrate
* debug flags 

After this refactor:

* keep `kModel`
* remove `kLanguage`
* keep the rest unchanged

### Why

`language` is now a runtime user setting. Leaving a stale `kLanguage` constant in place invites future bugs where the app silently reads the wrong source.

You can optionally add:

```cpp
constexpr wchar_t kConfigSubdir[] = L"dictate";
constexpr wchar_t kConfigFilename[] = L"config.json";
```

to `config.hpp`, because those are now path naming invariants, not user-editable data. That is fine.

---

## Step 7: expand the transcription client API to take both prompt and language

Current state:

* `TranscriptionClient::transcribe_file(...)` accepts only `language`
* `App` passes `config::kLanguage` into it  

That API needs to become cohesive.

### Update `include/transcription_client.hpp`

Use:

```cpp
#pragma once
#include <filesystem>
#include <string>
#include "user_config.hpp"

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
        const TranscriptionOptions& options);
};
```

### Why this is better

`prompt` and `language` are one conceptual group: request options for transcription. Passing them separately will get messy as soon as another request field becomes configurable.

Do **not** make `TranscriptionClient` load the file itself. Keep it pure: it should receive already-loaded options.

---

## Step 8: make the app ensure the config file exists on startup

This is your first required behavior: create the stub early so the user knows where to edit it.

In `App::run()`, after the app has resolved its runtime environment and before the message loop begins, call `UserConfigStore{}.ensure_exists_and_load()` once.

Current `App::run()` already does startup setup for the window, overlay, hotkey, tray, and output directory, so this is the correct place for the startup-side existence check. 

### Add `UserConfigStore` to the app

In `include/app.hpp`:

```cpp
#include "user_config.hpp"
```

And add:

```cpp
UserConfigStore user_config_;
```

### Startup behavior

In `App::run()`:

```cpp
auto cfg = user_config_.ensure_exists_and_load();
if (!cfg.ok) {
    MessageBoxW(
        nullptr,
        utf8_to_wide(cfg.error).c_str(),
        L"dictate_cpp - configuration error",
        MB_ICONERROR);
    return 1;
}
```

### Optional nicety

If `cfg.created_stub` is true, show a one-time tray balloon such as:

* title: `dictate_cpp`
* text: `Created config file in LocalAppData\dictate. Edit it to set prompt/language.`

That is helpful, but not required.

### Why fail fast here

If the app cannot even create or read its config directory/file at startup, that is a basic environment problem and should not silently degrade.

---

## Step 9: re-read the config immediately before every transcription

This is your second required behavior, and it is more important than the startup read.

Do **not** cache the config in `App` and reuse it across transcriptions. Your stated requirement is explicit: read the file before each transcription call so edits take effect without restarting the app.

The right place is inside the worker thread in `App::stop_recording_and_transcribe()`, just before calling `transcription_.transcribe_file(...)`. That is where the current upload is already built and the request is about to happen. 

### Replace this current pattern

The code currently does:

```cpp
auto result = transcription_.transcribe_file(upload, config::kLanguage);
```

with a runtime load immediately before it. 

### Good replacement shape

```cpp
worker_ = std::jthread([this, recorded, out_dir, hwnd]() {
    std::filesystem::create_directories(out_dir);

    AudioUploadSpec upload;
    upload.path = recorded.path;
    upload.upload_filename = recorded.upload_filename;
    upload.mime_type = recorded.mime_type;

    auto cfg = user_config_.ensure_exists_and_load();
    if (!cfg.ok) {
        post_owned_wstring(hwnd, WM_APP_TRANSCRIPTION_ERROR,
            new std::wstring(utf8_to_wide(cfg.error)));
        return;
    }

    auto result = transcription_.transcribe_file(upload, cfg.transcription);

    if (!result.ok) {
        post_owned_wstring(hwnd, WM_APP_TRANSCRIPTION_ERROR,
            new std::wstring(utf8_to_wide(result.error)));
        return;
    }

    // existing normalization + save + success post
});
```

### Why load in the worker

* the transcription request is already off the UI thread
* JSON file I/O is small but still better kept away from UI
* it keeps the “latest config wins” semantics exact

---

## Step 10: add `prompt` and `language` to the multipart request only when non-empty

This is the other crucial implementation step.

The transcription request should still always send:

* `file`
* `model`
* `response_format=json`

And then conditionally send:

* `language` only if non-empty
* `prompt` only if non-empty

The OpenAI API documents both fields as optional, with `json` as the supported response format for `gpt-4o-transcribe`. ([OpenAI Platform][1])

### In `src/transcription_client.cpp`

Keep the current `libcurl` structure and add these fields conditionally.

```cpp
TranscriptionResult TranscriptionClient::transcribe_file(
    const AudioUploadSpec& audio,
    const TranscriptionOptions& options) {

    // existing API key lookup, curl init, response buffer...

    auto add_field = [&](const char* name, const char* value) {
        auto* part = curl_mime_addpart(mime);
        curl_mime_name(part, name);
        curl_mime_data(part, value, CURL_ZERO_TERMINATED);
    };

    add_field("model", config::kModel);
    add_field("response_format", "json");

    if (!options.language.empty()) {
        add_field("language", options.language.c_str());
    }

    if (!options.prompt.empty()) {
        add_field("prompt", options.prompt.c_str());
    }

    // existing file part + perform + parse json
}
```

### Why conditional fields matter

Do not send empty strings unnecessarily. Omitting absent values is cleaner and matches the API semantics better.

---

## Step 11: keep model compile-time for now

You only asked for `prompt` and `language` to be configurable.

Do not widen scope to configurable model selection in this refactor. The current project already has `config::kModel = "gpt-4o-transcribe"` and that is a good invariant to keep until there is a real user need to expose model choice. 

This keeps the refactor narrow:

* new runtime config only for requested fields
* no accidental product-surface explosion

---

## Step 12: surface config errors like operational errors, not like silent fallbacks

Config failures should be visible, because otherwise the user edits the file and nothing happens.

Use the existing error path:

* overlay error
* tray error state/balloon
* `dictation_error.log`

The current app already has a clean UI-thread error path through `on_transcription_error(...)` and `HistoryStore::log_error(...)`. Reuse that instead of inventing new UI or diagnostics.  

Examples of messages to emit:

* `Failed to resolve LocalAppData path.`
* `Failed to create stub config file: ...`
* `Failed to parse config.json: ...`
* `"language" must be a string.`
* `"prompt" must be a string.`

Do **not** silently fall back to empty settings on malformed JSON. Silent recovery would make editing frustrating.

---

## Step 13: keep the file location logic separate from output/history logic

Current output and error history still live under `exe_dir_ / out`, and that is already wired through `HistoryStore` and `App`. Leave that alone in this refactor. The config location is the only thing moving to Local AppData.  

That means:

* config: `%LocalAppData%\dictate\config.json`
* transcripts: existing `out/transcript_*.txt`
* error log: existing `out/dictation_error.log`

This keeps the change narrow and avoids introducing two location migrations at once.

---

## Step 14: “peek here first” guidance for the coding agent

Before changing anything, inspect these existing files so the refactor reuses the current architecture and style:

* `include/config.hpp`: this is the current home of static constants; remove runtime `kLanguage` from here, but keep the rest as compile-time invariants. 
* `include/transcription_client.hpp` and `src/transcription_client.cpp`: this is the correct seam for request-option expansion. Do not invent a second HTTP client or move request assembly into `App`.  
* `src/app.cpp`: this is where startup happens and where the worker thread currently calls `transcribe_file(...)`; add startup ensure-exists here and pre-request reload here. 
* `include/history_store.hpp` / `src/history_store.cpp`: reuse the existing logging pattern for config failures rather than creating a new diagnostics sink.  

---

## Step 15: manual verification checklist

The coding agent should consider the refactor done only when all of these are true:

1. On first launch with no config file, `%LocalAppData%\dictate\config.json` is created.
2. The file contains a valid stub with `language` and `prompt`.
3. Startup succeeds after creating the stub.
4. Editing `config.json` and changing `language` or `prompt` takes effect on the **next** transcription without restarting the app.
5. Invalid JSON produces a clear runtime error via the existing error path.
6. Wrong types such as `"language": 123` also produce a clear runtime error.
7. Empty strings are accepted and simply omit those request fields.
8. Existing output/history behavior remains unchanged.

## Definition of done

This refactor is complete when:

* the app creates and uses `%LocalAppData%\dictate\config.json`
* `prompt` and `language` are no longer compile-time settings
* the file is ensured at startup
* the file is re-read before each transcription request
* the request uses those runtime values
* failure modes are visible and logged through the current operational error path

[1]: https://platform.openai.com/docs/api-reference/audio/createTranscription "Create transcription | OpenAI API Reference"
[2]: https://learn.microsoft.com/en-us/windows/win32/shell/knownfolderid?utm_source=chatgpt.com "KNOWNFOLDERID (Knownfolders.h) - Win32 apps"
