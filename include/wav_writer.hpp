#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>

// Write a 16-bit mono PCM WAV file.
// Returns false if samples is empty or the file cannot be written.
bool write_wav_file(
    const std::filesystem::path& path,
    const std::vector<int16_t>&  samples,
    int sample_rate,
    int channels);
