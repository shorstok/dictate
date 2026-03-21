#include "history_store.hpp"
#include <ctime>
#include <filesystem>
#include <fstream>

HistoryStore::HistoryStore(const std::filesystem::path& out_dir)
    : out_dir_(out_dir)
{
    std::filesystem::create_directories(out_dir_);
}

void HistoryStore::save_transcript(const std::string& text) {
    time_t now = time(nullptr);
    char timestamp[32]{};
    strftime(timestamp, sizeof(timestamp), "%Y%m%d-%H%M%S", localtime(&now));

    auto path = out_dir_ / ("transcript_" + std::string(timestamp) + ".txt");
    std::ofstream f(path, std::ios::out | std::ios::binary);
    f << text;
}

void HistoryStore::log_error(const std::string& message) {
    time_t now = time(nullptr);
    char timestamp[32]{};
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));

    auto path = out_dir_ / "dictation_error.log";
    std::ofstream f(path, std::ios::app | std::ios::binary);
    f << "[" << timestamp << "] " << message << "\n";
}
