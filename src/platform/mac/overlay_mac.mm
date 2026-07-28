#include "overlay_mac.hpp"
#include "mac_common.hpp"

#import <AppKit/AppKit.h>

#include <cmath>

namespace {
    constexpr CGFloat kOverlayWidth  = 320;
    constexpr CGFloat kOverlayHeight = 72;
    constexpr CGFloat kTopMargin     = 48;
    constexpr CGFloat kCornerRadius  = 20;

    NSColor* text_color_for(OverlayState state) {
        switch (state) {
        case OverlayState::Listening:
            return [NSColor colorWithSRGBRed:1.00 green:0.90 blue:0.55 alpha:1.0];
        case OverlayState::Transcribing:
            return [NSColor colorWithSRGBRed:0.71 green:0.86 blue:1.00 alpha:1.0];
        case OverlayState::Done:
            return [NSColor colorWithSRGBRed:0.71 green:1.00 blue:0.71 alpha:1.0];
        case OverlayState::Notice:
            return [NSColor colorWithSRGBRed:0.75 green:0.75 blue:0.75 alpha:1.0];
        case OverlayState::Error:
            return [NSColor colorWithSRGBRed:1.00 green:0.67 blue:0.67 alpha:1.0];
        case OverlayState::Hidden:
        default:
            return [NSColor colorWithSRGBRed:0.96 green:0.96 blue:0.96 alpha:1.0];
        }
    }

    // The overlay follows the pointer's screen rather than the primary one:
    // on a multi-display desk that is where the user is typing, and the blip
    // is useless on the display they are not looking at.
    NSScreen* active_screen() {
        const NSPoint mouse = [NSEvent mouseLocation];
        for (NSScreen* screen in [NSScreen screens]) {
            if (NSPointInRect(mouse, [screen frame])) {
                return screen;
            }
        }
        return [NSScreen mainScreen];
    }
}

// ---------------------------------------------------------------------------
// Panel controller
// ---------------------------------------------------------------------------

@interface OverlayPanelController : NSObject
- (instancetype)init;
- (void)showText:(NSString*)text color:(NSColor*)color autoHide:(NSTimeInterval)seconds;
- (void)hide;
- (void)teardown;
@end

@implementation OverlayPanelController {
    NSPanel*      _panel;
    NSTextField*  _label;
    NSTimer*      _hideTimer;
}

- (instancetype)init {
    self = [super init];
    if (!self) return nil;

    _panel = [[NSPanel alloc]
        initWithContentRect:NSMakeRect(0, 0, kOverlayWidth, kOverlayHeight)
                  styleMask:(NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel)
                    backing:NSBackingStoreBuffered
                      defer:NO];

    _panel.level              = NSStatusWindowLevel;
    _panel.opaque             = NO;
    _panel.backgroundColor    = [NSColor clearColor];
    _panel.hasShadow          = YES;
    _panel.ignoresMouseEvents = YES;
    _panel.hidesOnDeactivate  = NO;
    _panel.releasedWhenClosed = NO;
    // Stay put across space switches and over full-screen apps — the blip must
    // be visible wherever the user happens to be dictating.
    _panel.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces
                              | NSWindowCollectionBehaviorStationary
                              | NSWindowCollectionBehaviorFullScreenAuxiliary
                              | NSWindowCollectionBehaviorIgnoresCycle;

    NSView* content = [[[NSView alloc]
        initWithFrame:NSMakeRect(0, 0, kOverlayWidth, kOverlayHeight)] autorelease];
    content.wantsLayer = YES;
    content.layer.backgroundColor = [NSColor colorWithSRGBRed:0.125
                                                        green:0.125
                                                         blue:0.125
                                                        alpha:0.95].CGColor;
    content.layer.cornerRadius = kCornerRadius;
    content.layer.borderWidth  = 1.0;
    content.layer.borderColor  = [NSColor colorWithSRGBRed:0.31
                                                     green:0.31
                                                      blue:0.31
                                                     alpha:1.0].CGColor;
    _panel.contentView = content;

    _label = [[NSTextField alloc] initWithFrame:NSMakeRect(12, 0, kOverlayWidth - 24, kOverlayHeight)];
    _label.editable        = NO;
    _label.selectable      = NO;
    _label.bezeled         = NO;
    _label.drawsBackground = NO;
    _label.alignment       = NSTextAlignmentCenter;
    _label.font            = [NSFont systemFontOfSize:18 weight:NSFontWeightSemibold];
    _label.lineBreakMode   = NSLineBreakByTruncatingTail;
    _label.cell.usesSingleLineMode = YES;
    // Vertically centered by giving the cell the full height and one line.
    [content addSubview:_label];

    return self;
}

- (void)dealloc {
    [self teardown];
    [_label release];
    [_panel release];
    [super dealloc];
}

- (void)reposition {
    NSScreen* screen = active_screen();
    if (!screen) return;
    const NSRect visible = [screen visibleFrame];
    const CGFloat x = NSMinX(visible) + (NSWidth(visible) - kOverlayWidth) / 2;
    const CGFloat y = NSMaxY(visible) - kTopMargin - kOverlayHeight;
    [_panel setFrameOrigin:NSMakePoint(std::round(x), std::round(y))];
}

- (void)showText:(NSString*)text color:(NSColor*)color autoHide:(NSTimeInterval)seconds {
    [_hideTimer invalidate];
    _hideTimer = nil;

    _label.stringValue = text;
    _label.textColor   = color;
    // Keep the label's single line centered on the panel's vertical axis.
    const CGFloat line_height = [_label.cell cellSizeForBounds:_label.bounds].height;
    _label.frame = NSMakeRect(12,
                              std::round((kOverlayHeight - line_height) / 2),
                              kOverlayWidth - 24,
                              line_height);

    [self reposition];
    [_panel orderFrontRegardless];  // visible without stealing focus

    if (seconds > 0) {
        _hideTimer = [NSTimer scheduledTimerWithTimeInterval:seconds
                                                      target:self
                                                    selector:@selector(hideTimerFired:)
                                                    userInfo:nil
                                                     repeats:NO];
    }
}

- (void)hideTimerFired:(NSTimer*)timer {
    (void)timer;
    _hideTimer = nil;
    [self hide];
}

- (void)hide {
    [_hideTimer invalidate];
    _hideTimer = nil;
    [_panel orderOut:nil];
    _label.stringValue = @"";
}

- (void)teardown {
    [_hideTimer invalidate];
    _hideTimer = nil;
    [_panel orderOut:nil];
}

@end

// ---------------------------------------------------------------------------
// platform::Overlay
// ---------------------------------------------------------------------------

OverlayMac::~OverlayMac() {
    destroy();
}

bool OverlayMac::create() {
    if (controller_) return true;
    controller_ = [[OverlayPanelController alloc] init];
    return controller_ != nullptr;
}

void OverlayMac::destroy() {
    if (!controller_) return;
    [controller_ teardown];
    [controller_ release];
    controller_ = nullptr;
}

void OverlayMac::show_text(OverlayState state, const std::string& text, double auto_hide_s) {
    if (!controller_) return;
    [controller_ showText:to_ns(text) color:text_color_for(state) autoHide:auto_hide_s];
}

void OverlayMac::show_listening(bool latched) {
    show_text(OverlayState::Listening,
              latched ? "Listening (latched)…" : "Listening…", 0);
}

void OverlayMac::show_transcribing() {
    show_text(OverlayState::Transcribing, "Transcribing…", 0);
}

void OverlayMac::show_done() {
    show_text(OverlayState::Done, "Done", 1.2);
}

void OverlayMac::show_notice(const std::string& message) {
    show_text(OverlayState::Notice, message, 1.4);
}

void OverlayMac::show_error(const std::string& message) {
    show_text(OverlayState::Error, message.empty() ? "Error" : message, 1.8);
}

void OverlayMac::hide() {
    if (!controller_) return;
    [controller_ hide];
}
