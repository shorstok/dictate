#include "wav_writer.hpp"
#include <cstdint>
#include <cstring>
#include <fstream>

namespace {
    void write_u16(std::ofstream& f, uint16_t v) {
        f.write(reinterpret_cast<const char*>(&v), 2);
    }
    void write_u32(std::ofstream& f, uint32_t v) {
        f.write(reinterpret_cast<const char*>(&v), 4);
    }
    void write_tag(std::ofstream& f, const char tag[4]) {
        f.write(tag, 4);
    }
}

bool write_wav_file(
    const std::filesystem::path& path,
    const std::vector<int16_t>&  samples,
    int sample_rate,
    int channels)
{
    if (samples.empty()) return false;

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;

    const uint32_t data_size  = static_cast<uint32_t>(samples.size() * sizeof(int16_t));
    const uint32_t chunk_size = 36 + data_size;          // file size - 8
    const uint16_t block_align   = static_cast<uint16_t>(channels * 2);
    const uint32_t byte_rate     = sample_rate * block_align;

    // RIFF header
    write_tag(f, "RIFF");
    write_u32(f, chunk_size);
    write_tag(f, "WAVE");

    // fmt chunk
    write_tag(f, "fmt ");
    write_u32(f, 16);
    write_u16(f, 1);                                // PCM
    write_u16(f, static_cast<uint16_t>(channels));
    write_u32(f, static_cast<uint32_t>(sample_rate));
    write_u32(f, byte_rate);
    write_u16(f, block_align);
    write_u16(f, 16);                               // bits per sample

    // data chunk
    write_tag(f, "data");
    write_u32(f, data_size);
    f.write(reinterpret_cast<const char*>(samples.data()), data_size);

    return f.good();
}
