#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

class PasteService {
public:
    // Snapshot the current foreground window so we can paste into it later.
    void capture_foreground();

    // Re-activate the captured window (if still valid) and send Ctrl+V.
    void restore_and_paste();

private:
    HWND previous_window_{nullptr};
};
