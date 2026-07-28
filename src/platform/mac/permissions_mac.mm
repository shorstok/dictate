#include "permissions_mac.hpp"
#include "mac_common.hpp"

#import <AppKit/AppKit.h>
#import <AVFoundation/AVFoundation.h>
#include <ApplicationServices/ApplicationServices.h>

namespace {

void open_settings(NSString* url) {
    [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:url]];
}

} // namespace

namespace mac::permissions {

bool accessibility_trusted(bool prompt) {
    NSDictionary* options = @{ (__bridge id)kAXTrustedCheckOptionPrompt: @(prompt) };
    return AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options) == TRUE;
}

bool input_monitoring_granted() {
    return CGPreflightListenEventAccess() == true;
}

bool request_input_monitoring() {
    if (CGPreflightListenEventAccess()) {
        return true;
    }
    // Shows the system dialog. It returns false in practice — the grant only
    // takes effect after the user flips the switch (and usually a restart).
    return CGRequestListenEventAccess() == true;
}

void request_microphone_access() {
    if ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio]
            != AVAuthorizationStatusNotDetermined) {
        return;
    }
    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                             completionHandler:^(BOOL granted) {
        if (!granted) {
            NSLog(@"dictate: microphone access denied");
        }
    }];
}

bool microphone_denied() {
    const AVAuthorizationStatus status =
        [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
    return status == AVAuthorizationStatusDenied || status == AVAuthorizationStatusRestricted;
}

void open_accessibility_settings() {
    open_settings(@"x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility");
}

void open_input_monitoring_settings() {
    open_settings(@"x-apple.systempreferences:com.apple.preference.security?Privacy_ListenEvent");
}

void open_microphone_settings() {
    open_settings(@"x-apple.systempreferences:com.apple.preference.security?Privacy_Microphone");
}

} // namespace mac::permissions
