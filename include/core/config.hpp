#pragma once

enum class InputMode {
    ToggleHotkey,     // classic press-to-start / press-to-stop hotkey
    HoldPushToTalk    // hold the modifier chord (Ctrl+Win / Ctrl+Cmd) to record
};

namespace config {
    constexpr InputMode kInputMode = InputMode::HoldPushToTalk;

    constexpr int kSampleRate  = 16000;
    constexpr int kChannels    = 1;

    // All names are ASCII; std::filesystem::path conversion is safe everywhere.
    constexpr char kConfigSubdir[]          = "dictate";
    constexpr char kConfigFilename[]        = "config.json";
    constexpr char kTranscribeLogFilename[] = "transcribe.log";
    constexpr char kAudioFilenameMp3[]      = "mic_input.mp3";
    constexpr char kOutputDir[]             = "out";

    constexpr char kModel[]        = "gpt-4o-transcribe";
    constexpr int  kMp3BitrateKbps = 48;

    // Guards against accidental presses / silent recordings: anything shorter
    // than kMinRecordingMs or quieter than kMinPeakAmplitude is discarded
    // client-side instead of being sent for transcription.
    constexpr int kMinRecordingMs   = 700;
    constexpr int kMinPeakAmplitude = 500;  // ~1.5% of int16 full scale
}
