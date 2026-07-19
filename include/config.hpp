#pragma once
#include <windows.h>

enum class InputMode {
    ToggleHotkey,
    HoldCtrlWin
};

namespace config {
    constexpr InputMode kInputMode = InputMode::HoldCtrlWin;

    constexpr int kSampleRate  = 16000;
    constexpr int kChannels    = 1;
    constexpr UINT kHotkeyModifiers = MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT;
    constexpr UINT kHotkeyVK        = VK_F9;
    constexpr int  kHotkeyId        = 1;
    constexpr wchar_t kConfigSubdir[]      = L"dictate";
    constexpr wchar_t kConfigFilename[]    = L"config.json";
    constexpr wchar_t kTranscribeLogFilename[] = L"transcribe.log";
    constexpr wchar_t kAudioFilenameMp3[] = L"mic_input.mp3";
    constexpr wchar_t kOutputDir[]        = L"out";
    constexpr char kModel[]               = "gpt-4o-transcribe";
    constexpr int  kMp3BitrateKbps        = 48;

    // Guards against accidental presses / silent recordings: anything shorter
    // than kMinRecordingMs or quieter than kMinPeakAmplitude is discarded
    // client-side instead of being sent for transcription.
    constexpr int kMinRecordingMs   = 700;
    constexpr int kMinPeakAmplitude = 500;  // ~1.5% of int16 full scale
}
