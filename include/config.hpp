#pragma once
#include <windows.h>

namespace config {
    constexpr int kSampleRate  = 16000;
    constexpr int kChannels    = 1;
    constexpr UINT kHotkeyModifiers = MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT;
    constexpr UINT kHotkeyVK        = VK_F9;
    constexpr int  kHotkeyId        = 1;
    constexpr wchar_t kConfigSubdir[]      = L"dictate";
    constexpr wchar_t kConfigFilename[]    = L"config.json";
    constexpr wchar_t kTranscribeLogFilename[] = L"transcribe.log";
    constexpr wchar_t kAudioFilenameWav[] = L"mic_input.wav";
    constexpr wchar_t kAudioFilenameMp3[] = L"mic_input.mp3";
    constexpr wchar_t kOutputDir[]        = L"out";
    constexpr char kModel[]               = "gpt-4o-transcribe";
    constexpr int  kMp3BitrateKbps        = 48;
    constexpr bool kKeepDebugWav          = false;
}
