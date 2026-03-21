#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

class Mp3Encoder {
public:
    Mp3Encoder();
    ~Mp3Encoder();

    Mp3Encoder(const Mp3Encoder&) = delete;
    Mp3Encoder& operator=(const Mp3Encoder&) = delete;

    bool start(const std::filesystem::path& path,
               int sample_rate,
               int channels,
               int bitrate_kbps,
               std::string& error);

    bool encode_interleaved_s16(const int16_t* samples,
                                std::size_t frame_count,
                                std::string& error);

    bool finish(std::string& error);

    bool is_started() const noexcept;
    std::uint64_t total_input_frames() const noexcept;
    std::uint64_t total_output_bytes() const noexcept;
    const std::filesystem::path& output_path() const noexcept;

private:
    struct Impl;
    Impl* impl_{nullptr};
};
