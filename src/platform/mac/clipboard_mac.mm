#include "clipboard_mac.hpp"
#include "mac_common.hpp"

#import <AppKit/AppKit.h>

bool ClipboardMac::get_text(std::string& out) {
    NSString* s = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
    if (!s) {
        return false;
    }
    out = from_ns(s);
    return true;
}

bool ClipboardMac::set_text(const std::string& text) {
    NSPasteboard* pb = [NSPasteboard generalPasteboard];
    [pb clearContents];
    return [pb setString:to_ns(text) forType:NSPasteboardTypeString] == YES;
}
