#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>

#include "core/platform.hpp"

class ClipboardWin final : public platform::Clipboard {
public:
    explicit ClipboardWin(HWND owner = nullptr) : owner_(owner) {}
    void set_owner(HWND owner) { owner_ = owner; }

    // platform::Clipboard (UTF-8 <-> CF_UNICODETEXT)
    bool get_text(std::string& out) override;
    bool set_text(const std::string& text) override;

private:
    HWND owner_{nullptr};
};
