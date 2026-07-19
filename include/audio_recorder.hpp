#pragma once
#include <filesystem>
#include <string>
#include <cstdint>

struct RecordedAudio {
    bool ok{false};
    bool has_audio{false};
    std::uint64_t captured_frames{0};  // duration = captured_frames / sample rate
    int peak_amplitude{0};             // max |sample| seen, 0..32768
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
