#include "tray_mac.hpp"
#include "mac_common.hpp"

#import <AppKit/AppKit.h>
#import <UserNotifications/UserNotifications.h>

namespace {
    constexpr CGFloat kMenuBarIconSize = 18;

    NSString* image_name_for(platform::TrayState state) {
        switch (state) {
        case platform::TrayState::Listening:    return @"app_listening";
        case platform::TrayState::Transcribing: return @"app_transcribing";
        case platform::TrayState::Error:        return @"app_error";
        case platform::TrayState::Idle:
        default:                                return @"app";
        }
    }

    // Shown when the bundled images are missing (e.g. running the raw binary
    // out of the build tree) — a menu-bar item with no image is invisible.
    NSString* fallback_title_for(platform::TrayState state) {
        switch (state) {
        case platform::TrayState::Listening:    return @"●";
        case platform::TrayState::Transcribing: return @"◐";
        case platform::TrayState::Error:        return @"✖";
        case platform::TrayState::Idle:
        default:                                return @"○";
        }
    }

    NSString* state_label_for(platform::TrayState state) {
        switch (state) {
        case platform::TrayState::Listening:    return @"Listening";
        case platform::TrayState::Transcribing: return @"Transcribing";
        case platform::TrayState::Error:        return @"Error";
        case platform::TrayState::Idle:
        default:                                return @"Idle";
        }
    }
}

// ---------------------------------------------------------------------------
// Status item controller
// ---------------------------------------------------------------------------

@interface StatusItemController : NSObject <NSMenuDelegate>
- (instancetype)initWithCallbacks:(TrayMac::Callbacks)callbacks
             notificationFallback:(platform::Overlay*)fallback;
- (BOOL)install;
- (void)teardown;
- (void)setState:(platform::TrayState)state suffix:(NSString*)suffix;
- (void)notifyTitle:(NSString*)title text:(NSString*)text isError:(BOOL)isError;
@end

@implementation StatusItemController {
    NSStatusItem*        _item;
    NSMenu*              _menu;
    NSMenuItem*          _copyLastItem;
    TrayMac::Callbacks   _callbacks;
    platform::Overlay*   _fallback;
    platform::TrayState  _state;
    NSString*            _suffix;
    BOOL                 _notificationsAuthorized;
}

- (instancetype)initWithCallbacks:(TrayMac::Callbacks)callbacks
             notificationFallback:(platform::Overlay*)fallback {
    self = [super init];
    if (!self) return nil;
    _callbacks = std::move(callbacks);
    _fallback  = fallback;
    _state     = platform::TrayState::Idle;
    _suffix    = [@"" retain];
    _notificationsAuthorized = NO;
    return self;
}

- (void)dealloc {
    [self teardown];
    [_suffix release];
    [super dealloc];
}

- (BOOL)install {
    _item = [[[NSStatusBar systemStatusBar]
        statusItemWithLength:NSVariableStatusItemLength] retain];
    if (!_item) {
        return NO;
    }

    _menu = [[NSMenu alloc] initWithTitle:@"dictate"];
    _menu.delegate = self;
    _menu.autoenablesItems = NO;

    NSMenuItem* showStatus = [[[NSMenuItem alloc] initWithTitle:@"Show status"
                                                         action:@selector(onShowStatus:)
                                                  keyEquivalent:@""] autorelease];
    showStatus.target = self;
    [_menu addItem:showStatus];

    _copyLastItem = [[[NSMenuItem alloc] initWithTitle:@"Copy last"
                                                action:@selector(onCopyLast:)
                                         keyEquivalent:@""] autorelease];
    _copyLastItem.target = self;
    [_menu addItem:_copyLastItem];

    [_menu addItem:[NSMenuItem separatorItem]];

    NSMenuItem* quit = [[[NSMenuItem alloc] initWithTitle:@"Quit dictate"
                                                   action:@selector(onQuit:)
                                            keyEquivalent:@"q"] autorelease];
    quit.target = self;
    [_menu addItem:quit];

    _item.menu = _menu;

    [self applyAppearance];
    [self requestNotificationAuthorization];
    return YES;
}

- (void)teardown {
    if (_item) {
        [[NSStatusBar systemStatusBar] removeStatusItem:_item];
        [_item release];
        _item = nil;
    }
    [_menu release];
    _menu = nil;
    _copyLastItem = nil;
}

// --- appearance ---

- (void)applyAppearance {
    if (!_item) return;

    NSStatusBarButton* button = _item.button;
    NSImage* image = [NSImage imageNamed:image_name_for(_state)];
    if (image) {
        [image setSize:NSMakeSize(kMenuBarIconSize, kMenuBarIconSize)];
        // The state colors carry meaning, so these are not template images.
        // (Spelled as a message send: `template` is a C++ keyword.)
        [image setTemplate:NO];
        button.image = image;
        button.title = @"";
    } else {
        button.image = nil;
        button.title = fallback_title_for(_state);
    }

    NSString* tip = [NSString stringWithFormat:@"dictate — %@", state_label_for(_state)];
    if ([_suffix length] > 0) {
        tip = [NSString stringWithFormat:@"%@ — %@", tip, _suffix];
    }
    button.toolTip = tip;
}

- (void)setState:(platform::TrayState)state suffix:(NSString*)suffix {
    _state = state;
    NSString* copy = [(suffix ? suffix : @"") copy];
    [_suffix release];
    _suffix = copy;
    [self applyAppearance];
}

// --- menu ---

- (void)menuNeedsUpdate:(NSMenu*)menu {
    (void)menu;
    const bool has_last = _callbacks.has_last_transcript && _callbacks.has_last_transcript();
    _copyLastItem.enabled = has_last ? YES : NO;
}

- (void)onShowStatus:(id)sender {
    (void)sender;
    if (_callbacks.show_status) _callbacks.show_status();
}

- (void)onCopyLast:(id)sender {
    (void)sender;
    if (_callbacks.copy_last) _callbacks.copy_last();
}

- (void)onQuit:(id)sender {
    (void)sender;
    if (_callbacks.quit) _callbacks.quit();
}

// --- notifications ---

// UNUserNotificationCenter requires a real bundle and raises when there is
// none (running the bare executable). Authorization is asynchronous, so until
// it lands the overlay carries the message instead of nothing at all.
- (void)requestNotificationAuthorization {
    if (![[NSBundle mainBundle] bundleIdentifier]) {
        return;
    }
    @try {
        UNUserNotificationCenter* center = [UNUserNotificationCenter currentNotificationCenter];
        [center requestAuthorizationWithOptions:(UNAuthorizationOptionAlert | UNAuthorizationOptionSound)
                              completionHandler:^(BOOL granted, NSError* error) {
            if (error) {
                NSLog(@"dictate: notification authorization failed: %@", error);
            }
            dispatch_async(dispatch_get_main_queue(), ^{
                self->_notificationsAuthorized = granted;
            });
        }];
    } @catch (NSException* exception) {
        NSLog(@"dictate: notification center unavailable: %@", exception);
    }
}

- (void)notifyTitle:(NSString*)title text:(NSString*)text isError:(BOOL)isError {
    if (!_notificationsAuthorized) {
        if (_fallback) {
            if (isError) {
                _fallback->show_error(from_ns(text));
            } else {
                _fallback->show_notice(from_ns(text));
            }
        }
        return;
    }

    @try {
        UNMutableNotificationContent* content = [[[UNMutableNotificationContent alloc] init] autorelease];
        content.title = title;
        content.body  = text;
        if (isError) {
            content.sound = [UNNotificationSound defaultSound];
        }

        NSString* identifier = [[NSUUID UUID] UUIDString];
        UNNotificationRequest* request =
            [UNNotificationRequest requestWithIdentifier:identifier
                                                 content:content
                                                 trigger:nil];
        [[UNUserNotificationCenter currentNotificationCenter]
            addNotificationRequest:request
             withCompletionHandler:^(NSError* error) {
            if (error) {
                NSLog(@"dictate: notification delivery failed: %@", error);
            }
        }];
    } @catch (NSException* exception) {
        NSLog(@"dictate: notification delivery raised: %@", exception);
    }
}

@end

// ---------------------------------------------------------------------------
// platform::Tray
// ---------------------------------------------------------------------------

TrayMac::~TrayMac() {
    destroy();
}

bool TrayMac::create(Callbacks callbacks, platform::Overlay* notification_fallback) {
    destroy();
    controller_ = [[StatusItemController alloc] initWithCallbacks:std::move(callbacks)
                                            notificationFallback:notification_fallback];
    if (!controller_) {
        return false;
    }
    if (![controller_ install]) {
        destroy();
        return false;
    }
    return true;
}

void TrayMac::destroy() {
    if (!controller_) return;
    [controller_ teardown];
    [controller_ release];
    controller_ = nullptr;
}

void TrayMac::set_state(platform::TrayState state, const std::string& tooltip_suffix) {
    if (!controller_) return;
    [controller_ setState:state suffix:to_ns(tooltip_suffix)];
}

void TrayMac::show_notification(const std::string& title,
                                const std::string& text,
                                bool is_error) {
    if (!controller_) return;
    [controller_ notifyTitle:to_ns(title) text:to_ns(text) isError:(is_error ? YES : NO)];
}
