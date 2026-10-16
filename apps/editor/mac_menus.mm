// The macOS menus of the SDL program (mac_menus.h). Built without ARC (CMake's default for .mm files).

#import <Cocoa/Cocoa.h>

#include <SDL3/SDL.h>

#include <climits>

#include "mac_menus.h"

namespace {
void (*gPick)(int) = nullptr;
int gOpen = 0;                   // the menus that are open now (menuWillOpen, menuDidClose)
int gPopupPick = INT_MIN;        // the item that the open context menu's pick gave
NSInteger gPopupHighlight = INT_MIN;  // the context menu's highlighted item (its tag)
bool gPopupOpen = false;

NSString* nsText(const std::string& s) {
    NSString* t = [NSString stringWithUTF8String:s.c_str()];
    return t ? t : @"";
}

NSEventModifierFlags maskOf(unsigned mods) {
    NSEventModifierFlags m = 0;
    if (mods & kMacCmd) m |= NSEventModifierFlagCommand;
    if (mods & kMacShift) m |= NSEventModifierFlagShift;
    if (mods & kMacOption) m |= NSEventModifierFlagOption;
    if (mods & kMacControl) m |= NSEventModifierFlagControl;
    return m;
}
}  // namespace

// The items' target: a pick of the menu bar goes to the program after the menu closed; a context menu's pick is kept
// for macPopupMenu.
@interface StarDraftMenuTarget : NSObject
- (void)pick:(id)sender;
- (void)popupPick:(id)sender;
@end

@implementation StarDraftMenuTarget
- (void)pick:(id)sender {
    if (gPick) gPick(int([sender tag]));
}
- (void)popupPick:(id)sender {
    gPopupPick = int([sender tag]);
}
@end

// Counts the open menus. The program's menus (keysOff) are not searched for key equivalents: their keys go to the
// window, where the view with the focus gets them first (a text field's Cmd+C).
@interface StarDraftMenuDelegate : NSObject <NSMenuDelegate>
@property(nonatomic) BOOL keysOff;
@property(nonatomic) BOOL popup;
@end

@implementation StarDraftMenuDelegate
- (void)menuWillOpen:(NSMenu*)menu {
    (void)menu;
    ++gOpen;
}
- (void)menuDidClose:(NSMenu*)menu {
    (void)menu;
    if (gOpen > 0) --gOpen;
}
- (BOOL)menuHasKeyEquivalent:(NSMenu*)menu forEvent:(NSEvent*)event target:(id*)target action:(SEL*)action {
    (void)menu, (void)event, (void)target, (void)action;
    return !self.keysOff;
}
- (void)menu:(NSMenu*)menu willHighlightItem:(NSMenuItem*)item {
    (void)menu;
    if (self.popup) gPopupHighlight = item && [item isEnabled] && ![item hasSubmenu] ? [item tag] : INT_MIN;
}
@end

namespace {
StarDraftMenuTarget* gTarget = nil;  // (kept for the program's life: a menu item does not keep its target)
StarDraftMenuDelegate* gKeysOff = nil;
StarDraftMenuDelegate* gKeysOn = nil;
StarDraftMenuDelegate* gPopupDelegate = nil;

void makeObjects() {
    if (gTarget) return;
    [NSApplication sharedApplication];
    gTarget = [[StarDraftMenuTarget alloc] init];
    gKeysOff = [[StarDraftMenuDelegate alloc] init];
    gKeysOff.keysOff = YES;
    gKeysOn = [[StarDraftMenuDelegate alloc] init];
    gPopupDelegate = [[StarDraftMenuDelegate alloc] init];
    gPopupDelegate.popup = YES;
}

// A menu of the items (autoreleased). `act`: the items' action.
NSMenu* makeMenu(NSString* title, const std::vector<MacMenuItem>& items, StarDraftMenuDelegate* delegate, SEL act) {
    NSMenu* m = [[[NSMenu alloc] initWithTitle:title] autorelease];
    [m setAutoenablesItems:NO];
    [m setDelegate:delegate];
    for (const MacMenuItem& it : items) {
        if (it.separator) {
            [m addItem:[NSMenuItem separatorItem]];
            continue;
        }
        NSMenuItem* mi = [[[NSMenuItem alloc] initWithTitle:nsText(it.title) action:nullptr keyEquivalent:@""] autorelease];
        if (!it.sub.empty()) {
            [mi setSubmenu:makeMenu(nsText(it.title), it.sub, delegate, act)];
        } else {
            [mi setTarget:gTarget];
            [mi setAction:act];
            [mi setTag:it.tag];
            if (!it.key.empty()) {
                [mi setKeyEquivalent:nsText(it.key)];
                [mi setKeyEquivalentModifierMask:maskOf(it.mods)];
            }
            [mi setState:it.checked ? NSControlStateValueOn : NSControlStateValueOff];
        }
        [mi setEnabled:it.enabled];
        [m addItem:mi];
    }
    return m;
}

// An item of the application menu or the Window menu: a system action (target nil: the application or the key
// window), or one of the program's (target: the program).
NSMenuItem* systemItem(NSMenu* to, const std::string& title, SEL action, NSString* key, NSEventModifierFlags mask) {
    NSMenuItem* mi = [[[NSMenuItem alloc] initWithTitle:nsText(title) action:action keyEquivalent:key] autorelease];
    [mi setKeyEquivalentModifierMask:mask];
    [to addItem:mi];
    return mi;
}
NSMenuItem* programItem(NSMenu* to, const std::string& title, int tag, NSString* key, NSEventModifierFlags mask) {
    NSMenuItem* mi = systemItem(to, title, @selector(pick:), key, mask);
    [mi setTarget:gTarget];
    [mi setTag:tag];
    return mi;
}

void addMenu(NSMenu* bar, NSMenu* menu) {
    NSMenuItem* top = [[[NSMenuItem alloc] initWithTitle:[menu title] action:nullptr keyEquivalent:@""] autorelease];
    [top setSubmenu:menu];
    [bar addItem:top];
}

// A key equivalent as text: "cmd+shift+S", "cmd+Backspace".
std::string keyText(NSMenuItem* mi) {
    NSString* k = [mi keyEquivalent];
    if ([k length] == 0) return {};
    const NSEventModifierFlags m = [mi keyEquivalentModifierMask];
    std::string out;
    if (m & NSEventModifierFlagControl) out += "ctrl+";
    if (m & NSEventModifierFlagOption) out += "option+";
    if (m & NSEventModifierFlagShift) out += "shift+";
    if (m & NSEventModifierFlagCommand) out += "cmd+";
    const unichar c = [k characterAtIndex:0];
    switch (c) {
    case '\b': return out + "Backspace";
    case '\t': return out + "Tab";
    case '\r': return out + "Enter";
    case 0x1b: return out + "Esc";
    case ' ': return out + "Space";
    case NSDeleteFunctionKey: return out + "Del";
    case NSLeftArrowFunctionKey: return out + "Left";
    case NSRightArrowFunctionKey: return out + "Right";
    case NSUpArrowFunctionKey: return out + "Up";
    case NSDownArrowFunctionKey: return out + "Down";
    case NSHomeFunctionKey: return out + "Home";
    case NSEndFunctionKey: return out + "End";
    default: break;
    }
    if (c >= NSF1FunctionKey && c <= NSF35FunctionKey) return out + "F" + std::to_string(int(c - NSF1FunctionKey) + 1);
    return out + [[k uppercaseString] UTF8String];
}

void menuLines(NSMenu* m, const std::string& path, std::vector<std::string>& out) {
    for (NSMenuItem* mi in [m itemArray]) {
        if ([mi isSeparatorItem]) continue;
        const std::string title = [[mi title] UTF8String];
        const std::string at = path.empty() ? title : path + " > " + title;
        if ([mi hasSubmenu] && !path.empty() && [[mi submenu] numberOfItems] > 0) {
            menuLines([mi submenu], at, out);
            continue;
        }
        if ([mi hasSubmenu] && path.empty()) {
            out.push_back(at);
            menuLines([mi submenu], at, out);
            continue;
        }
        std::string line = at;
        if (const std::string k = keyText(mi); !k.empty()) line += " [" + k + "]";
        if (![mi isEnabled]) line += " (gray)";
        if ([mi state] == NSControlStateValueOn) line += " (checked)";
        out.push_back(line);
    }
}

// After a context menu: a mouse button that went up while the menu had the mouse is up for SDL too (the menu took its
// event), so a view does not go on as if it were still held.
void releaseHeldButtons() {
    const SDL_MouseButtonFlags held = SDL_GetMouseState(nullptr, nullptr);
    const NSUInteger pressed = [NSEvent pressedMouseButtons];
    NSWindow* win = [NSApp keyWindow] ? [NSApp keyWindow] : [NSApp mainWindow];
    if (!win || !held) return;
    const NSPoint at = [win mouseLocationOutsideOfEventStream];
    struct Button {
        SDL_MouseButtonFlags sdl;
        NSUInteger ns;
        NSEventType up;
        NSInteger number;
    };
    const Button buttons[] = {{SDL_BUTTON_LMASK, 1, NSEventTypeLeftMouseUp, 0},
                              {SDL_BUTTON_RMASK, 2, NSEventTypeRightMouseUp, 1},
                              {SDL_BUTTON_MMASK, 4, NSEventTypeOtherMouseUp, 2}};
    for (const Button& b : buttons) {
        if (!(held & b.sdl) || (pressed & b.ns)) continue;
        NSEvent* e = [NSEvent mouseEventWithType:b.up location:at modifierFlags:0 timestamp:[[NSProcessInfo processInfo] systemUptime]
                                    windowNumber:[win windowNumber] context:nil eventNumber:0 clickCount:1 pressure:0];
        if (b.number != 0) {  // (an "other" or right button event carries its number through a CGEvent)
            CGEventRef cg = CGEventCreateCopy([e CGEvent]);
            CGEventSetIntegerValueField(cg, kCGMouseEventButtonNumber, b.number);
            e = [NSEvent eventWithCGEvent:cg];
            CFRelease(cg);
        }
        if (e) [NSApp postEvent:e atStart:NO];
    }
}
}  // namespace

void macSetMenuBar(const MacMenuTexts& t, int aboutTag, int settingsTag, int nextTabTag, const std::vector<MacMenuItem>& menus,
                   const std::vector<MacMenuItem>& tabs, void (*pick)(int tag)) {
    @autoreleasepool {
        makeObjects();
        if (gOpen > 0 || gPopupOpen) return;
        gPick = pick;
        NSMenu* bar = [[[NSMenu alloc] initWithTitle:@""] autorelease];

        NSMenu* app = [[[NSMenu alloc] initWithTitle:nsText(t.app)] autorelease];
        [app setAutoenablesItems:NO];
        [app setDelegate:gKeysOn];
        programItem(app, t.about, aboutTag, @"", 0);
        [app addItem:[NSMenuItem separatorItem]];
        programItem(app, t.settings, settingsTag, @",", NSEventModifierFlagCommand);
        [app addItem:[NSMenuItem separatorItem]];
        NSMenuItem* services = systemItem(app, t.services, nullptr, @"", 0);
        NSMenu* servicesMenu = [[[NSMenu alloc] initWithTitle:nsText(t.services)] autorelease];
        [services setSubmenu:servicesMenu];
        [NSApp setServicesMenu:servicesMenu];
        [app addItem:[NSMenuItem separatorItem]];
        systemItem(app, t.hide, @selector(hide:), @"h", NSEventModifierFlagCommand);
        systemItem(app, t.hideOthers, @selector(hideOtherApplications:), @"h", NSEventModifierFlagCommand | NSEventModifierFlagOption);
        systemItem(app, t.showAll, @selector(unhideAllApplications:), @"", 0);
        [app addItem:[NSMenuItem separatorItem]];
        systemItem(app, t.quit, @selector(terminate:), @"q", NSEventModifierFlagCommand);  // (SDL: the quit request, which asks about changes)
        addMenu(bar, app);

        for (const MacMenuItem& m : menus) addMenu(bar, makeMenu(nsText(m.title), m.sub, gKeysOff, @selector(pick:)));

        NSMenu* win = [[[NSMenu alloc] initWithTitle:nsText(t.window)] autorelease];
        [win setAutoenablesItems:NO];
        [win setDelegate:gKeysOn];
        systemItem(win, t.minimize, @selector(performMiniaturize:), @"m", NSEventModifierFlagCommand);
        systemItem(win, t.zoom, @selector(performZoom:), @"", 0);
        [win addItem:[NSMenuItem separatorItem]];
        [programItem(win, t.nextTab, nextTabTag, @"`", NSEventModifierFlagCommand) setEnabled:tabs.size() > 1];
        if (!tabs.empty()) {
            [win addItem:[NSMenuItem separatorItem]];
            for (const MacMenuItem& tab : tabs) [programItem(win, tab.title, tab.tag, @"", 0) setState:tab.checked ? NSControlStateValueOn : NSControlStateValueOff];
        }
        [win addItem:[NSMenuItem separatorItem]];
        systemItem(win, t.bringAll, @selector(arrangeInFront:), @"", 0);
        addMenu(bar, win);

        [NSApp setMainMenu:bar];
    }
}

bool macMenuOpen() { return gOpen > 0 || gPopupOpen; }

int macPopupMenu(const std::vector<MacMenuItem>& items) {
    @autoreleasepool {
        makeObjects();
        if (gPopupOpen) return INT_MIN;
        NSMenu* m = makeMenu(@"", items, gPopupDelegate, @selector(popupPick:));
        gPopupPick = INT_MIN, gPopupHighlight = INT_MIN, gPopupOpen = true;
        const BOOL picked = [m popUpMenuPositioningItem:nil atLocation:[NSEvent mouseLocation] inView:nil];
        gPopupOpen = false;
        releaseHeldButtons();
        if (gPopupPick != INT_MIN) return gPopupPick;
        return picked ? int(gPopupHighlight) : INT_MIN;
    }
}

std::vector<std::string> macMenuBarLines() {
    std::vector<std::string> out;
    @autoreleasepool {
        if (NSMenu* bar = [NSApp mainMenu]) menuLines(bar, "", out);
    }
    return out;
}
