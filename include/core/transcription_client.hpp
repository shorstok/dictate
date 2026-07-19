#pragma once
#include <filesystem>
#include <string>

#include "core/user_config.hpp"

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
