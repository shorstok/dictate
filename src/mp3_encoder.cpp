#include "mp3_encoder.hpp"

#include <cstdio>
#include <vector>

#include <lame/lame.h>

struct Mp3Encoder::Impl {
    lame_t gfp{nullptr};
    FILE* file{nullptr};
    std::filesystem::path path;
    std::vector<unsigned char> buffer;
    std::uint64_t input_frames{0};
    std::uint64_t output_bytes{0};
    int channels{0};
    bool started{false};

    void close_handles() {
        if (gfp) {
            lame_close(gfp);
            gfp = nullptr;
        }
        if (file) {
            std::fclose(file);
            file = nullptr;
        }
        channels = 0;
        started = false;
    }

    void reset_state() {
        close_handles();
        path.clear();
        buffer.clear();
        input_frames = 0;
        output_bytes = 0;
    }
};

namespace {
    int required_mp3_buffer_size(std::size_t frame_count) {
        return static_cast<int>(1.25 * static_cast<double>(frame_count) + 7200.0);
    }
}

Mp3Encoder::Mp3Encoder()
    : impl_(new Impl{})
{}

Mp3Encoder::~Mp3Encoder() {
    std::string ignored;
    finish(ignored);
    delete impl_;
}

bool Mp3Encoder::start(const std::filesystem::path& path,
                       int sample_rate,
                       int channels,
                       int bitrate_kbps,
                       std::string& error)
{
    error.clear();
    impl_->reset_state();

    if (channels <= 0 || sample_rate <= 0 || bitrate_kbps <= 0) {
        error = "Invalid MP3 encoder configuration.";
        return false;
    }

    impl_->file = _wfopen(path.c_str(), L"wb");
    if (!impl_->file) {
        error = "Failed to open MP3 output file.";
        impl_->reset_state();
        return false;
    }

    impl_->gfp = lame_init();
    if (!impl_->gfp) {
        error = "lame_init failed.";
        impl_->reset_state();
        return false;
    }

    lame_set_num_channels(impl_->gfp, channels);
    lame_set_in_samplerate(impl_->gfp, sample_rate);
    lame_set_brate(impl_->gfp, bitrate_kbps);
    lame_set_quality(impl_->gfp, 5);

    if (channels == 1) {
        lame_set_mode(impl_->gfp, MONO);
    }

    if (lame_init_params(impl_->gfp) < 0) {
        error = "lame_init_params failed.";
        impl_->reset_state();
        return false;
    }

    impl_->path = path;
    impl_->channels = channels;
    impl_->buffer.resize(required_mp3_buffer_size(4096));
    impl_->started = true;
    return true;
}

bool Mp3Encoder::encode_interleaved_s16(const int16_t* samples,
                                        std::size_t frame_count,
                                        std::string& error)
{
    error.clear();

    if (!impl_->started || !samples || frame_count == 0) {
        return true;
    }

    const int required_size = required_mp3_buffer_size(frame_count);
    if (static_cast<int>(impl_->buffer.size()) < required_size) {
        impl_->buffer.resize(required_size);
    }

    int written = 0;
    if (impl_->channels == 1) {
        written = lame_encode_buffer(
            impl_->gfp,
            const_cast<short int*>(reinterpret_cast<const short int*>(samples)),
            nullptr,
            static_cast<int>(frame_count),
            impl_->buffer.data(),
            static_cast<int>(impl_->buffer.size()));
    } else {
        written = lame_encode_buffer_interleaved(
            impl_->gfp,
            const_cast<short int*>(reinterpret_cast<const short int*>(samples)),
            static_cast<int>(frame_count),
            impl_->buffer.data(),
            static_cast<int>(impl_->buffer.size()));
    }

    if (written < 0) {
        error = "lame_encode_buffer failed.";
        return false;
    }

    if (written > 0) {
        const std::size_t bytes_to_write = static_cast<std::size_t>(written);
        if (std::fwrite(impl_->buffer.data(), 1, bytes_to_write, impl_->file) != bytes_to_write) {
            error = "Failed to write MP3 data.";
            return false;
        }
        impl_->output_bytes += bytes_to_write;
    }

    impl_->input_frames += frame_count;
    return true;
}

bool Mp3Encoder::finish(std::string& error) {
    error.clear();

    if (!impl_->started) {
        return true;
    }

    std::vector<unsigned char> flush_buf(7200);
    const int written = lame_encode_flush(
        impl_->gfp,
        flush_buf.data(),
        static_cast<int>(flush_buf.size()));

    if (written < 0) {
        error = "lame_encode_flush failed.";
        impl_->close_handles();
        return false;
    }

    if (written > 0) {
        const std::size_t bytes_to_write = static_cast<std::size_t>(written);
        if (std::fwrite(flush_buf.data(), 1, bytes_to_write, impl_->file) != bytes_to_write) {
            error = "Failed to write final MP3 data.";
            impl_->close_handles();
            return false;
        }
        impl_->output_bytes += bytes_to_write;
    }

    impl_->close_handles();
    return true;
}

bool Mp3Encoder::is_started() const noexcept {
    return impl_->started;
}

std::uint64_t Mp3Encoder::total_input_frames() const noexcept {
    return impl_->input_frames;
}

std::uint64_t Mp3Encoder::total_output_bytes() const noexcept {
    return impl_->output_bytes;
}

const std::filesystem::path& Mp3Encoder::output_path() const noexcept {
    return impl_->path;
}
