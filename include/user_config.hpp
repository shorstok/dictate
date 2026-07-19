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

class UserConfigStore {
public:
    UserConfigLoadResult ensure_exists_and_load() const;
    std::filesystem::path config_path(std::string& error) const;
    bool append_transcript_log(const std::string& text, std::string& error) const;

    // LocalAppData\<kConfigSubdir> — also used for recordings and history.
    std::filesystem::path appdata_dir(std::string& error) const;

private:
    bool write_stub_file(const std::filesystem::path& path, std::string& error) const;
};
