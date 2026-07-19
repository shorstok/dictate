#include "core/history_store.hpp"
#include <ctime>
#include <filesystem>
#include <fstream>

namespace {
    // Thread-safe local time (called from both the UI thread and the worker).
    tm local_now() {
        time_t now = time(nullptr);
        tm local{};
#if defined(_WIN32)
        localtime_s(&local, &now);
#else
        localtime_r(&now, &local);
#endif
        return local;
    }
}

HistoryStore::HistoryStore(const std::filesystem::path& out_dir)
    : out_dir_(out_dir)
{
    std::filesystem::create_directories(out_dir_);
}

void HistoryStore::save_transcript(const std::string& text) {
    const tm local = local_now();
    char timestamp[32]{};
    strftime(timestamp, sizeof(timestamp), "%Y%m%d-%H%M%S", &local);

    auto path = out_dir_ / ("transcript_" + std::string(timestamp) + ".txt");
    std::ofstream f(path, std::ios::out | std::ios::binary);
    f << text;
}

void HistoryStore::log_error(const std::string& message) {
    const tm local = local_now();
    char timestamp[32]{};
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &local);

    auto path = out_dir_ / "dictation_error.log";
    std::ofstream f(path, std::ios::app | std::ios::binary);
    f << "[" << timestamp << "] " << message << "\n";
}
