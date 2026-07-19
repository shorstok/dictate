#include "paste_win.hpp"

void PasteWin::capture_focus() {
    previous_window_ = GetForegroundWindow();
}

void PasteWin::restore_and_paste() {
    if (previous_window_ && IsWindow(previous_window_)) {
        SetForegroundWindow(previous_window_);
        Sleep(60); // give focus a moment to settle
    }

    INPUT inputs[4]{};

    inputs[0].type    = INPUT_KEYBOARD;
    inputs[0].ki.wVk  = VK_CONTROL;

    inputs[1].type    = INPUT_KEYBOARD;
    inputs[1].ki.wVk  = 'V';

    inputs[2].type         = INPUT_KEYBOARD;
    inputs[2].ki.wVk       = 'V';
    inputs[2].ki.dwFlags   = KEYEVENTF_KEYUP;

    inputs[3].type         = INPUT_KEYBOARD;
    inputs[3].ki.wVk       = VK_CONTROL;
    inputs[3].ki.dwFlags   = KEYEVENTF_KEYUP;

    SendInput(4, inputs, sizeof(INPUT));
}
