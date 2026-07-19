#include "clipboard_service.hpp"
#include <cstring>

std::optional<std::wstring> get_clipboard_text(HWND owner) {
    if (!OpenClipboard(owner)) return std::nullopt;
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        CloseClipboard();
        return std::nullopt;
    }
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (!hData) {
        CloseClipboard();
        return std::nullopt;
    }
    const wchar_t* ptr = static_cast<const wchar_t*>(GlobalLock(hData));
    if (!ptr) {
        CloseClipboard();
        return std::nullopt;
    }
    std::wstring result(ptr);
    GlobalUnlock(hData);
    CloseClipboard();
    return result;
}

bool set_clipboard_text(HWND owner, const std::wstring& text) {
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
