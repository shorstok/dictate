// Miniaudio single-header implementation – must appear in exactly one TU.
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio/miniaudio.h"

#include "audio_recorder.hpp"
#include "config.hpp"
#include "mp3_encoder.hpp"

#include <mutex>
#include <string>

struct AudioRecorder::Impl {
    ma_device device{};
    bool      initialized{false};
    Mp3Encoder encoder;
    std::mutex encoder_mutex;
    std::string last_error;
    std::uint64_t captured_frames{0};
};

static void data_callback(ma_device* device, void* /*output*/, const void* input, ma_uint32 frame_count) {
    auto* self = reinterpret_cast<AudioRecorder*>(device->pUserData);
    if (!input || !self) return;
    self->append_samples(static_cast<const int16_t*>(input), frame_count);
}

AudioRecorder::AudioRecorder()
    : impl_(new Impl{})
{}

AudioRecorder::~AudioRecorder() {
    (void)stop();
    delete impl_;
}

bool AudioRecorder::start(const std::filesystem::path& mp3_path, std::string& error) {
    error.clear();

    if (impl_->initialized || impl_->encoder.is_started()) {
        (void)stop();
    }

    {
        std::lock_guard lock(impl_->encoder_mutex);
        impl_->last_error.clear();
        impl_->captured_frames = 0;

        if (!impl_->encoder.start(
                mp3_path,
                config::kSampleRate,
                config::kChannels,
                config::kMp3BitrateKbps,
                error)) {
            return false;
        }
    }

    ma_device_config cfg = ma_device_config_init(ma_device_type_capture);
    cfg.capture.format   = ma_format_s16;
    cfg.capture.channels = config::kChannels;
    cfg.sampleRate       = config::kSampleRate;
    cfg.dataCallback     = data_callback;
    cfg.pUserData        = this;

    if (ma_device_init(nullptr, &cfg, &impl_->device) != MA_SUCCESS) {
        std::lock_guard lock(impl_->encoder_mutex);
        std::string finish_error;
        impl_->encoder.finish(finish_error);
        error = "Failed to initialize audio capture device.";
        return false;
    }
    impl_->initialized = true;

    if (ma_device_start(&impl_->device) != MA_SUCCESS) {
        ma_device_uninit(&impl_->device);
        impl_->initialized = false;
        std::lock_guard lock(impl_->encoder_mutex);
        std::string finish_error;
        impl_->encoder.finish(finish_error);
        error = "Failed to start audio capture device.";
        return false;
    }
    return true;
}

RecordedAudio AudioRecorder::stop() {
    if (impl_->initialized) {
        ma_device_stop(&impl_->device);
        ma_device_uninit(&impl_->device);
        impl_->initialized = false;
    }

    std::lock_guard lock(impl_->encoder_mutex);

    std::string finish_error;
    if (impl_->encoder.is_started() &&
        !impl_->encoder.finish(finish_error) &&
        impl_->last_error.empty()) {
        impl_->last_error = finish_error;
    }

    if (!impl_->last_error.empty()) {
        return {false, false, {}, {}, {}, impl_->last_error};
    }

    const std::string upload_filename = impl_->encoder.output_path().filename().string();

    return {
        true,
        impl_->captured_frames > 0,
        impl_->encoder.output_path(),
        upload_filename,
        "audio/mpeg",
        {}
    };
}

void AudioRecorder::append_samples(const int16_t* data, uint32_t frame_count) {
    if (!data || frame_count == 0) {
        return;
    }

    std::lock_guard lock(impl_->encoder_mutex);

    if (!impl_->last_error.empty()) {
        return;
    }

    std::string error;
    if (!impl_->encoder.encode_interleaved_s16(data, frame_count, error)) {
        impl_->last_error = error;
        return;
    }

    impl_->captured_frames += frame_count;
}
