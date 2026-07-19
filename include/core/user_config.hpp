#pragma once

#include <filesystem>
#include <string>

struct TranscriptionOptions {
    std::string language;
    std::string prompt;
};

struct UserConfigLoadResult {
    bool ok{false};
    bool created_stub{false};
    std::filesystem::path config_path;
    TranscriptionOptions transcription;
    std::string error;
};

// Reads/creates config.json and the transcript log inside the app data
// directory. Resolving that directory is the platform layer's job — the
// resolved path is injected here.
class UserConfigStore {
public:
    explicit UserConfigStore(std::filesystem::path data_dir);

    UserConfigLoadResult ensure_exists_and_load() const;
    bool append_transcript_log(const std::string& text, std::string& error) const;

private:
    bool write_stub_file(const std::filesystem::path& path, std::string& error) const;

    std::filesystem::path data_dir_;
};
