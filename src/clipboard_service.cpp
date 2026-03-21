#include "clipboard_service.hpp"
#include <cstring>

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
    std::memcpy(ptr, text.c_str(), byte_count);
    GlobalUnlock(hg);

    SetClipboardData(CF_UNICODETEXT, hg);
    CloseClipboard();
    return true;
}
