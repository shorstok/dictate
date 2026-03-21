#include "user_config.hpp"

#include "config.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cctype>
#include <exception>
#include <fstream>
#include <system_error>

#include <nlohmann/json.hpp>

namespace {
    std::string trim_copy(std::string s) {
        const auto not_space = [](unsigned char c) { return !std::isspace(c); };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
        s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
        return s;
    }

    std::string single_line_copy(std::string s) {
        std::replace(s.begin(), s.end(), '\r', ' ');
        std::replace(s.begin(), s.end(), '\n', ' ');
        return s;
    }
}

std::filesystem::path UserConfigStore::appdata_dir(std::string& error) const {
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
    const std::filesystem::path dir = base / config::kConfigSubdir;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        error = "Failed to create config directory: " + dir.string();
        return {};
    }

    return dir;
}

std::filesystem::path UserConfigStore::config_path(std::string& error) const {
    const std::filesystem::path dir = appdata_dir(error);
    if (dir.empty()) {
        return {};
    }
    return dir / config::kConfigFilename;
}

bool UserConfigStore::append_transcript_log(const std::string& text, std::string& error) const {
    error.clear();

    const std::filesystem::path dir = appdata_dir(error);
    if (dir.empty()) {
        return false;
    }

    std::ofstream f(dir / config::kTranscribeLogFilename, std::ios::out | std::ios::binary | std::ios::app);
    if (!f) {
        error = "Failed to open transcribe.log for appending.";
        return false;
    }

    f << single_line_copy(text) << '\n';
    if (!f.good()) {
        error = "Failed to append to transcribe.log.";
        return false;
    }

    return true;
}

bool UserConfigStore::write_stub_file(const std::filesystem::path& path, std::string& error) const {
    error.clear();

    std::ofstream f(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!f) {
        error = "Failed to create stub config file: " + path.string();
        return false;
    }

    static constexpr char kStubConfig[] =
        "{\n"
        "  \"language\": \"\",\n"
        "  \"prompt\": \"\",\n"
        "  \"_note\": \"language is optional; use ISO-639-1 like en or pl. Empty values mean defaults.\"\n"
        "}\n";

    f << kStubConfig;
    if (!f.good()) {
        error = "Failed to write stub config file: " + path.string();
        return false;
    }

    return true;
}

UserConfigLoadResult UserConfigStore::ensure_exists_and_load() const {
    UserConfigLoadResult result;

    std::string path_error;
    result.config_path = config_path(path_error);
    if (result.config_path.empty()) {
        result.error = path_error.empty() ? "Unable to locate config path." : path_error;
        return result;
    }

    std::error_code ec;
    const bool exists = std::filesystem::exists(result.config_path, ec);
    if (ec) {
        result.error = "Failed to check config file existence: " + result.config_path.string();
        return result;
    }

    if (!exists) {
        std::string stub_error;
        if (!write_stub_file(result.config_path, stub_error)) {
            result.error = stub_error;
            return result;
        }
        result.created_stub = true;
    }

    std::ifstream f(result.config_path, std::ios::in | std::ios::binary);
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

    if (!j.is_object()) {
        result.error = "Config file must contain a JSON object.";
        return result;
    }

    if (j.contains("language") && !j["language"].is_string()) {
        result.error = "\"language\" must be a string.";
        return result;
    }
    if (j.contains("prompt") && !j["prompt"].is_string()) {
        result.error = "\"prompt\" must be a string.";
        return result;
    }

    result.transcription.language = trim_copy(j.value("language", std::string{}));
    result.transcription.prompt = trim_copy(j.value("prompt", std::string{}));
    result.ok = true;
    return result;
}
