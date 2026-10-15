// macOS's application menu for the SDL program (mac_app_menu.h). Built without ARC (CMake's default for .mm files).

#import <Cocoa/Cocoa.h>

#include "mac_app_menu.h"

namespace {
void (*gAbout)() = nullptr;
void (*gSettings)() = nullptr;

NSString* nsText(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()]; }
}  // namespace

@interface StarDraftAppMenuTarget : NSObject
- (void)showAbout:(id)sender;
- (void)showSettings:(id)sender;
@end

@implementation StarDraftAppMenuTarget
- (void)showAbout:(id)sender {
    (void)sender;
    if (gAbout) gAbout();
}
- (void)showSettings:(id)sender {
    (void)sender;
    if (gSettings) gSettings();
}
@end

namespace {
StarDraftAppMenuTarget* gTarget = nil;  // (kept for the program's life: a menu item does not keep its target)
}

void macSetupAppMenu(const std::string& aboutText, const std::string& settingsText, void (*about)(), void (*settings)()) {
    @autoreleasepool {
        gAbout = about, gSettings = settings;
        if (!gTarget) gTarget = [[StarDraftAppMenuTarget alloc] init];
        NSMenu* bar = [NSApp mainMenu];
        if (!bar || [bar numberOfItems] == 0) return;
        NSMenu* app = [[bar itemAtIndex:0] submenu];
        if (!app) return;
        NSInteger aboutAt = -1;
        bool haveSettings = false;
        for (NSMenuItem* it in [app itemArray]) {
            if ([it action] == @selector(orderFrontStandardAboutPanel:) || [it action] == @selector(showAbout:)) {
                [it setTitle:nsText(aboutText)];
                [it setTarget:gTarget];
                [it setAction:@selector(showAbout:)];
                aboutAt = [app indexOfItem:it];
            } else if ([[it keyEquivalent] isEqualToString:@","]) {  // (SDL's Preferences item, which does nothing)
                [it setTitle:nsText(settingsText)];
                [it setTarget:gTarget];
                [it setAction:@selector(showSettings:)];
                [it setKeyEquivalentModifierMask:NSEventModifierFlagCommand];
                haveSettings = true;
            }
        }
        if (!haveSettings) {
            NSInteger at = aboutAt + 1;
            if (at < [app numberOfItems] && [[app itemAtIndex:at] isSeparatorItem]) ++at;  // after About and its line
            NSMenuItem* s = [[NSMenuItem alloc] initWithTitle:nsText(settingsText) action:@selector(showSettings:) keyEquivalent:@","];
            [s setTarget:gTarget];
            [app insertItem:s atIndex:at];
            [app insertItem:[NSMenuItem separatorItem] atIndex:at + 1];
            [s release];
        }
        // The program's menus use Cmd+W (Close the tab) and Cmd+M: SDL's Window menu gives them up.
        for (NSMenuItem* top in [bar itemArray])
            for (NSMenuItem* it in [[top submenu] itemArray])
                if ([it action] == @selector(performClose:) || [it action] == @selector(performMiniaturize:)) [it setKeyEquivalent:@""];
    }
}
