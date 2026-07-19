#pragma once
#include <filesystem>
#include <string>

class HistoryStore {
public:
    explicit HistoryStore(const std::filesystem::path& out_dir);

    // Write out/transcript_YYYYMMDD-HHMMSS.txt
    void save_transcript(const std::string& text);

    // Append [YYYY-MM-DD HH:MM:SS] message to out/dictation_error.log
    void log_error(const std::string& message);

private:
    std::filesystem::path out_dir_;
};
