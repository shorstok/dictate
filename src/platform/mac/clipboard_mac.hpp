#pragma once
#include <string>

#include "core/platform.hpp"

// NSPasteboard-backed clipboard. Unlike Win32 there is no open/close or
// ownership dance — the general pasteboard is always available.
class ClipboardMac final : public platform::Clipboard {
public:
    // platform::Clipboard (UTF-8 <-> NSPasteboardTypeString)
    bool get_text(std::string& out) override;
    bool set_text(const std::string& text) override;
};
