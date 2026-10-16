// macOS's window and document look for the SDL program (mac_window.h). Built without ARC (CMake's default for .mm files).

#import <Cocoa/Cocoa.h>

#include <SDL3/SDL.h>

#include <atomic>
#include <cstdio>

#include "mac_window.h"

namespace {

NSString* nsText(const std::string& s) {
    NSString* t = [NSString stringWithUTF8String:s.c_str()];
    return t ? t : @"";
}

NSWindow* cocoaWindow(SDL_Window* w) {
    if (!w) return nil;
    return (NSWindow*)SDL_GetPointerProperty(SDL_GetWindowProperties(w), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
}

// A color as 8-bit sRGB, resolved in `appearance` (a dynamic system color has a value for each appearance).
bool srgbOf(NSColor* color, NSAppearance* appearance, uint8_t& r, uint8_t& g, uint8_t& b) {
    __block NSColor* c = nil;
    void (^resolve)(void) = ^{
        c = [[color colorUsingColorSpace:[NSColorSpace sRGBColorSpace]] retain];
    };
    if (!appearance) resolve();
    else if (@available(macOS 11.0, *)) [appearance performAsCurrentDrawingAppearance:resolve];
    else {
        NSAppearance* was = [NSAppearance currentAppearance];
        [NSAppearance setCurrentAppearance:appearance];
        resolve();
        [NSAppearance setCurrentAppearance:was];
    }
    if (!c) return false;
    auto byte = [](CGFloat v) { return uint8_t(v <= 0 ? 0 : v >= 1 ? 255 : int(v * 255 + 0.5)); };
    r = byte([c redComponent]), g = byte([c greenComponent]), b = byte([c blueComponent]);
    [c release];
    return true;
}

std::atomic<bool> gLookChanged{false};

}  // namespace

// Watches the application's effective appearance (dark or light: KVO) and the system colors (the accent color).
@interface StarDraftLookWatcher : NSObject
@end

@implementation StarDraftLookWatcher
- (void)observeValueForKeyPath:(NSString*)keyPath ofObject:(id)object change:(NSDictionary*)change context:(void*)context {
    (void)keyPath, (void)object, (void)change, (void)context;
    gLookChanged = true;
}
- (void)colorsChanged:(NSNotification*)n {
    (void)n;
    gLookChanged = true;
}
@end

namespace {
StarDraftLookWatcher* gWatcher = nil;  // (kept for the program's life)
}

void macSetupWindow(SDL_Window* w) {
    @autoreleasepool {
        NSWindow* nw = cocoaWindow(w);
        if (!nw) return;
        // The green button: full screen in a space of its own, and Split View (tiling) with another app's window.
        NSWindowCollectionBehavior b = [nw collectionBehavior];
        b &= ~(NSWindowCollectionBehaviorFullScreenAuxiliary | NSWindowCollectionBehaviorFullScreenNone | NSWindowCollectionBehaviorFullScreenDisallowsTiling);
        b |= NSWindowCollectionBehaviorFullScreenPrimary | NSWindowCollectionBehaviorFullScreenAllowsTiling | NSWindowCollectionBehaviorManaged;
        [nw setCollectionBehavior:b];
        [nw setTabbingMode:NSWindowTabbingModeDisallowed];  // (the program has its own tabs: no system tab bar)
    }
}

void macSetWindowDocument(SDL_Window* w, const std::string& path, bool edited) {
    @autoreleasepool {
        NSWindow* nw = cocoaWindow(w);
        if (!nw) return;
        NSURL* url = path.empty() ? nil : [NSURL fileURLWithPath:nsText(path)];
        if (!(url == nil && [nw representedURL] == nil) && ![[nw representedURL] isEqual:url]) [nw setRepresentedURL:url];
        if ([nw isDocumentEdited] != (edited ? YES : NO)) [nw setDocumentEdited:edited ? YES : NO];
    }
}

void macSetWindowDark(SDL_Window* w, bool dark) {
    @autoreleasepool {
        NSWindow* nw = cocoaWindow(w);
        if (!nw) return;
        [nw setAppearance:[NSAppearance appearanceNamed:dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua]];
    }
}

void macNoteRecent(const std::string& path) {
    @autoreleasepool {
        if (path.empty()) return;
        [[NSDocumentController sharedDocumentController] noteNewRecentDocumentURL:[NSURL fileURLWithPath:nsText(path)]];
    }
}

void macClearRecents() {
    @autoreleasepool {
        [[NSDocumentController sharedDocumentController] clearRecentDocuments:nil];
    }
}

bool macSystemDark() {
    @autoreleasepool {
        NSAppearance* a = [NSApp effectiveAppearance];
        if (!a) return true;
        NSAppearanceName n = [a bestMatchFromAppearancesWithNames:@[ NSAppearanceNameAqua, NSAppearanceNameDarkAqua ]];
        return [n isEqualToString:NSAppearanceNameDarkAqua];
    }
}

bool macSystemAccent(uint8_t& r, uint8_t& g, uint8_t& b) {
    @autoreleasepool {
        return srgbOf([NSColor controlAccentColor], [NSApp effectiveAppearance], r, g, b);
    }
}

// The sidebar's color when it is not translucent: the window's ground (windowBackgroundColor) in that appearance (in
// the dark appearance lighter than a window's content, in the light one darker, as Finder's sidebar).
bool macSidebarColor(bool dark, uint8_t& r, uint8_t& g, uint8_t& b) {
    @autoreleasepool {
        return srgbOf([NSColor windowBackgroundColor], [NSAppearance appearanceNamed:dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua], r, g, b);
    }
}

void macWatchLook() {
    if (gWatcher) return;
    gWatcher = [[StarDraftLookWatcher alloc] init];
    [NSApp addObserver:gWatcher forKeyPath:@"effectiveAppearance" options:NSKeyValueObservingOptionNew context:nullptr];
    [[NSNotificationCenter defaultCenter] addObserver:gWatcher selector:@selector(colorsChanged:) name:NSSystemColorsDidChangeNotification object:nil];
}

bool macLookChanged() { return gLookChanged.exchange(false); }

bool macBeginSheet(SDL_Window* w, const MacAlert& spec, std::function<void(int)> done) {
    @autoreleasepool {
        NSWindow* nw = cocoaWindow(w);
        if (!nw || ![nw isVisible] || [nw isMiniaturized] || spec.buttons.empty()) return false;
        NSAlert* alert = [[NSAlert alloc] init];
        [alert setMessageText:nsText(spec.message)];
        if (!spec.info.empty()) [alert setInformativeText:nsText(spec.info)];
        [alert setAlertStyle:spec.style == 2 ? NSAlertStyleCritical : spec.style == 1 ? NSAlertStyleWarning : NSAlertStyleInformational];
        for (size_t i = 0; i < spec.buttons.size(); ++i) {
            NSButton* b = [alert addButtonWithTitle:nsText(spec.buttons[i])];
            const std::string key = i < spec.keys.size() ? spec.keys[i] : std::string();
            if (i == 0 || key.empty()) continue;  // (the first button keeps Return)
            [b setKeyEquivalent:nsText(key)];
            [b setKeyEquivalentModifierMask:key == "\r" || key == "\x1b" ? 0 : NSEventModifierFlagCommand];
        }
        std::function<void(int)>* cb = new std::function<void(int)>(std::move(done));
        [alert beginSheetModalForWindow:nw
                      completionHandler:^(NSModalResponse r) {
                          const int k = r >= NSAlertFirstButtonReturn ? int(r - NSAlertFirstButtonReturn) : -1;
                          if (*cb) (*cb)(k);
                          delete cb;
                          [alert release];
                      }];
        return true;
    }
}

std::string macWindowReport(SDL_Window* w) {
    @autoreleasepool {
        NSWindow* nw = cocoaWindow(w);
        if (!nw) return "no Cocoa window";
        const NSWindowCollectionBehavior b = [nw collectionBehavior];
        NSURL* url = [nw representedURL];
        NSAppearance* a = [nw appearance];
        std::string s = "represented file ";
        s += url ? [[url path] UTF8String] : "none";
        s += std::string("; edited ") + ([nw isDocumentEdited] ? "yes" : "no");
        s += std::string("; full screen ") + ((b & NSWindowCollectionBehaviorFullScreenPrimary) ? "yes" : "no");
        s += std::string("; split view ") + ((b & NSWindowCollectionBehaviorFullScreenAllowsTiling) ? "yes" : "no");
        s += std::string("; appearance ") + (a ? [[a name] UTF8String] : "the system's");
        s += std::string("; resizable ") + (([nw styleMask] & NSWindowStyleMaskResizable) ? "yes" : "no");
        return s;
    }
}
