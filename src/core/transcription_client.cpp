#include "core/config.hpp"
#include "core/transcription_client.hpp"

#include <cstdlib>
#include <string>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

namespace {
    size_t write_callback(void* ptr, size_t size, size_t nmemb, std::string* data) {
        data->append(static_cast<char*>(ptr), size * nmemb);
        return size * nmemb;
    }
}

TranscriptionResult TranscriptionClient::transcribe_file(
    const AudioUploadSpec& audio,
    const TranscriptionOptions& options)
{
    if (audio.path.empty()) {
        return {false, {}, "Audio file path is empty."};
    }
    if (audio.upload_filename.empty()) {
        return {false, {}, "Audio upload filename is empty."};
    }
    if (audio.mime_type.empty()) {
        return {false, {}, "Audio MIME type is empty."};
    }

    const char* api_key_env = std::getenv("OPENAI_API_KEY");
    if (!api_key_env || api_key_env[0] == '\0') {
        return {false, {}, "OPENAI_API_KEY environment variable is not set."};
    }
    const std::string api_key(api_key_env);

    // Convert path to UTF-8 for libcurl (which is ANSI/UTF-8 on the curl side).
    // On Windows the path may be wide; convert to the narrow representation.
    const std::string audio_path_str = audio.path.string();

    CURL* curl = curl_easy_init();
    if (!curl) {
        return {false, {}, "curl_easy_init failed"};
    }

    curl_mime*  mime      = curl_mime_init(curl);
    if (!mime) {
        curl_easy_cleanup(curl);
        return {false, {}, "curl_mime_init failed"};
    }
    std::string response_body;

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

    curl_mimepart* file_part = curl_mime_addpart(mime);
    curl_mime_name(file_part, "file");
    curl_mime_filedata(file_part, audio_path_str.c_str());
    curl_mime_filename(file_part, audio.upload_filename.c_str());
    curl_mime_type(file_part, audio.mime_type.c_str());

    std::string auth_header = "Authorization: Bearer " + api_key;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, auth_header.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, "https://api.openai.com/v1/audio/transcriptions");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "dictate_cpp/1.0");
    // Without timeouts a hung connection leaves the app stuck in
    // "Transcribing" forever (the state machine blocks further hotkeys).
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);

    CURLcode res = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_mime_free(mime);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        return {false, {}, std::string("curl error: ") + curl_easy_strerror(res)};
    }
    if (http_code < 200 || http_code >= 300) {
        return {false, {}, "HTTP " + std::to_string(http_code) + ": " + response_body};
    }

    try {
        auto j = nlohmann::json::parse(response_body);
        if (!j.contains("text")) {
            return {false, {}, "Response missing 'text' field: " + response_body};
        }
        return {true, j["text"].get<std::string>(), {}};
    } catch (const std::exception& ex) {
        return {false, {}, std::string("JSON parse error: ") + ex.what() + " | body: " + response_body};
    }
}
