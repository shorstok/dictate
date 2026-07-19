#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "core/platform.hpp"

class PasteWin final : public platform::Paster {
public:
    // platform::Paster
    void capture_focus() override;       // snapshot the current foreground window
    void restore_and_paste() override;   // re-activate it and send Ctrl+V

private:
    HWND previous_window_{nullptr};
};
