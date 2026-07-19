#include "clipboard_win.hpp"
#include "utf8_win.hpp"

#include <cstring>

namespace {
    bool get_clipboard_wide(HWND owner, std::wstring& out) {
        if (!OpenClipboard(owner)) return false;
        if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) {
            CloseClipboard();
            return false;
        }
        HANDLE hData = GetClipboardData(CF_UNICODETEXT);
        if (!hData) {
            CloseClipboard();
            return false;
        }
        const wchar_t* ptr = static_cast<const wchar_t*>(GlobalLock(hData));
        if (!ptr) {
            CloseClipboard();
            return false;
        }
        out.assign(ptr);
        GlobalUnlock(hData);
        CloseClipboard();
        return true;
    }

    bool set_clipboard_wide(HWND owner, const std::wstring& text) {
        if (!OpenClipboard(owner)) return false;

        EmptyClipboard();

        const size_t byte_count = (text.size() + 1) * sizeof(wchar_t);
        HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, byte_count);
        if (!hg) {
            CloseClipboard();
            return false;
        }

        void* ptr = GlobalLock(hg);
        if (!ptr) {
            GlobalFree(hg);
            CloseClipboard();
            return false;
        }
        std::memcpy(ptr, text.c_str(), byte_count);
        GlobalUnlock(hg);

        if (!SetClipboardData(CF_UNICODETEXT, hg)) {
            // Ownership transfers to the system only on success.
            GlobalFree(hg);
            CloseClipboard();
            return false;
        }
        CloseClipboard();
        return true;
    }
}

bool ClipboardWin::get_text(std::string& out) {
    std::wstring wide;
    if (!get_clipboard_wide(owner_, wide)) {
        return false;
    }
    out = wide_to_utf8(wide);
    return true;
}

bool ClipboardWin::set_text(const std::string& text) {
    return set_clipboard_wide(owner_, utf8_to_wide(text));
}
