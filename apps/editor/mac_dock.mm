// The Dock, Finder and the Settings window on macOS (mac_dock.h). Built without ARC (CMake's default for .mm files).

#import <Cocoa/Cocoa.h>
#include <objc/runtime.h>

#include <cmath>
#include <sstream>

#include "mac_dock.h"

namespace {
std::function<std::vector<MacDockItem>()> gDockItems;
std::function<void(const std::string&)> gDockOpen;
std::function<void(int)> gSettingsChosen;
bool gBuildRunning = false;

NSString* nsText(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()] ?: @""; }
std::string utf8(NSString* s) { return s ? std::string([s UTF8String]) : std::string(); }
}  // namespace

// The Dock tile while Build runs: the program's icon with a bar under it, in which a part moves (the build gives no
// percentage).
@interface StarDraftDockProgress : NSView
@property(nonatomic) double phase;  // 0 to 1: where the moving part is
@end

@implementation StarDraftDockProgress
- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    const NSRect b = [self bounds];
    [[NSApp applicationIconImage] drawInRect:b];
    const CGFloat h = std::round(b.size.height * 0.11), inset = std::round(b.size.width * 0.12);
    const NSRect track = NSMakeRect(b.origin.x + inset, b.origin.y + std::round(b.size.height * 0.08), b.size.width - 2 * inset, h);
    [[NSColor colorWithCalibratedWhite:0.1 alpha:0.75] setFill];
    [[NSBezierPath bezierPathWithRoundedRect:track xRadius:h / 2 yRadius:h / 2] fill];
    const NSRect in = NSInsetRect(track, 2, 2);
    const CGFloat w = std::round(in.size.width * 0.35);
    const CGFloat x = in.origin.x + (in.size.width - w) * (0.5 - 0.5 * std::cos(self.phase * 2 * M_PI));  // there and back
    [[NSColor controlAccentColor] setFill];
    [[NSBezierPath bezierPathWithRoundedRect:NSMakeRect(x, in.origin.y, w, in.size.height) xRadius:in.size.height / 2 yRadius:in.size.height / 2] fill];
}
@end

@interface StarDraftDockTarget : NSObject <NSToolbarDelegate>
@property(nonatomic, retain) NSTimer* timer;
@property(nonatomic, retain) NSArray<NSString*>* tabIds;
@property(nonatomic, retain) NSArray<NSString*>* tabLabels;
@property(nonatomic, retain) NSArray<NSString*>* tabSymbols;
- (void)openRecent:(id)sender;
- (void)tick:(NSTimer*)timer;
- (void)becameActive:(NSNotification*)n;
- (void)chooseTab:(id)sender;
@end

@implementation StarDraftDockTarget
- (void)openRecent:(id)sender {
    NSString* path = [sender representedObject];
    if (!path || !gDockOpen) return;
    [NSApp activateIgnoringOtherApps:YES];  // (the user chose it in the Dock: the program comes to the front)
    gDockOpen(utf8(path));
}
- (void)tick:(NSTimer*)timer {
    (void)timer;
    NSDockTile* tile = [NSApp dockTile];
    if (StarDraftDockProgress* v = (StarDraftDockProgress*)[tile contentView]; [v isKindOfClass:[StarDraftDockProgress class]]) {
        v.phase = std::fmod(v.phase + 0.04, 1.0);
        [tile display];
    }
}
- (void)becameActive:(NSNotification*)n {
    (void)n;
    if (!gBuildRunning) [[NSApp dockTile] setBadgeLabel:nil];  // (the user has seen the build's errors)
}
- (void)chooseTab:(id)sender {
    const NSInteger i = [self.tabIds indexOfObject:[sender itemIdentifier]];
    if (i != NSNotFound && gSettingsChosen) gSettingsChosen(int(i));
}
- (NSArray<NSToolbarItemIdentifier>*)toolbarAllowedItemIdentifiers:(NSToolbar*)toolbar {
    (void)toolbar;
    return self.tabIds;
}
- (NSArray<NSToolbarItemIdentifier>*)toolbarDefaultItemIdentifiers:(NSToolbar*)toolbar {
    (void)toolbar;
    return self.tabIds;
}
- (NSArray<NSToolbarItemIdentifier>*)toolbarSelectableItemIdentifiers:(NSToolbar*)toolbar {
    (void)toolbar;
    return self.tabIds;
}
- (NSToolbarItem*)toolbar:(NSToolbar*)toolbar itemForItemIdentifier:(NSToolbarItemIdentifier)ident willBeInsertedIntoToolbar:(BOOL)flag {
    (void)toolbar, (void)flag;
    const NSUInteger i = [self.tabIds indexOfObject:ident];
    if (i == NSNotFound) return nil;
    NSToolbarItem* item = [[[NSToolbarItem alloc] initWithItemIdentifier:ident] autorelease];
    [item setLabel:self.tabLabels[i]];
    [item setPaletteLabel:self.tabLabels[i]];
    [item setImage:[NSImage imageWithSystemSymbolName:self.tabSymbols[i] accessibilityDescription:self.tabLabels[i]]];
    [item setTarget:self];
    [item setAction:@selector(chooseTab:)];
    return item;
}
@end

namespace {
StarDraftDockTarget* gTarget = nil;  // (kept for the program's life: a menu item and a toolbar do not keep their target)

StarDraftDockTarget* target() {
    if (!gTarget) gTarget = [[StarDraftDockTarget alloc] init];
    return gTarget;
}

// A file's path in one form (absolute, no symbolic links), to compare the program's recent files with the system's.
NSString* pathKey(NSURL* url) { return [[[url URLByStandardizingPath] URLByResolvingSymlinksInPath] path] ?: @""; }

// The Dock icon's menu: the recent maps, a line, the recent projects (the Dock adds its own items under them). A file
// that is in `systemRecents` (the system's recent documents, which the Dock lists below) is not in it again.
NSMenu* makeDockMenu(const std::vector<MacDockItem>& items, NSArray<NSURL*>* systemRecents) {
    NSMenu* menu = [[[NSMenu alloc] initWithTitle:@""] autorelease];
    [menu setAutoenablesItems:NO];  // (an item's enabled state is the program's: no item while a dialog waits)
    NSMutableSet<NSString*>* listed = [NSMutableSet set];
    for (NSURL* u in systemRecents)
        if ([u isFileURL]) [listed addObject:pathKey(u)];
    bool maps = false;
    for (const MacDockItem& it : items) {
        if ([listed containsObject:pathKey([NSURL fileURLWithPath:nsText(it.path)])]) continue;
        if (it.project && maps && [menu numberOfItems] > 0 && ![[menu itemAtIndex:[menu numberOfItems] - 1] isSeparatorItem])
            [menu addItem:[NSMenuItem separatorItem]];
        maps |= !it.project;
        NSMenuItem* m = [[[NSMenuItem alloc] initWithTitle:nsText(it.title) action:@selector(openRecent:) keyEquivalent:@""] autorelease];
        [m setTarget:target()];
        [m setRepresentedObject:nsText(it.path)];
        [m setToolTip:nsText(it.path)];
        [m setEnabled:it.enabled ? YES : NO];
        [menu addItem:m];
    }
    return menu;
}

NSMenu* dockMenu(id self, SEL cmd, NSApplication* app) {
    (void)self, (void)cmd, (void)app;
    const std::vector<MacDockItem> items = gDockItems ? gDockItems() : std::vector<MacDockItem>();
    return makeDockMenu(items, [[NSDocumentController sharedDocumentController] recentDocumentURLs]);
}

// applicationDockMenu: on the application's delegate (SDL's SDLAppDelegate has none: the method is added to its class).
bool installDockMenu() {
    id delegate = [NSApp delegate];
    if (!delegate) return false;
    Class c = object_getClass(delegate);
    const SEL sel = @selector(applicationDockMenu:);
    if (class_addMethod(c, sel, (IMP)dockMenu, "@@:@")) return true;
    Method m = class_getInstanceMethod(c, sel);  // (it has one: ours takes its place)
    if (m) method_setImplementation(m, (IMP)dockMenu);
    return m != nullptr;
}

void showProgress(bool on) {
    NSDockTile* tile = [NSApp dockTile];
    StarDraftDockTarget* t = target();
    if (on) {
        StarDraftDockProgress* v = [[[StarDraftDockProgress alloc] initWithFrame:NSMakeRect(0, 0, [tile size].width, [tile size].height)] autorelease];
        [tile setContentView:v];
        [tile setBadgeLabel:nil];
        if (!t.timer) t.timer = [NSTimer scheduledTimerWithTimeInterval:0.1 target:t selector:@selector(tick:) userInfo:nil repeats:YES];
    } else {
        [t.timer invalidate];
        t.timer = nil;
        [tile setContentView:nil];  // the program's icon again
    }
    [tile display];
}

void endBuild(int errors, bool attention) {
    gBuildRunning = false;
    showProgress(false);
    NSDockTile* tile = [NSApp dockTile];
    [tile setBadgeLabel:errors > 0 ? [NSString stringWithFormat:@"%d", errors] : nil];
    if (attention && ![NSApp isActive]) [NSApp requestUserAttention:NSInformationalRequest];  // one jump
}
}  // namespace

void macDockSetup(std::function<std::vector<MacDockItem>()> items, std::function<void(const std::string& path)> open) {
    @autoreleasepool {
        gDockItems = std::move(items), gDockOpen = std::move(open);
        installDockMenu();
        static bool observing = false;
        if (!observing) {
            observing = true;
            [[NSNotificationCenter defaultCenter] addObserver:target() selector:@selector(becameActive:) name:NSApplicationDidBecomeActiveNotification object:nil];
        }
    }
}

void macDockBuild(bool running, int errors) {
    @autoreleasepool {
        if (running) {
            gBuildRunning = true;
            showProgress(true);
        } else endBuild(errors, true);
    }
}

namespace {
StarDraftDockTarget* settingsTarget(const std::vector<MacSettingsTab>& tabs) {
    StarDraftDockTarget* t = target();
    NSMutableArray* ids = [NSMutableArray array];
    NSMutableArray* labels = [NSMutableArray array];
    NSMutableArray* symbols = [NSMutableArray array];
    for (size_t i = 0; i < tabs.size(); ++i) {
        [ids addObject:[NSString stringWithFormat:@"stardraft.settings.%zu", i]];
        [labels addObject:nsText(tabs[i].label)];
        [symbols addObject:nsText(tabs[i].symbol)];
    }
    t.tabIds = ids, t.tabLabels = labels, t.tabSymbols = symbols;
    return t;
}

NSToolbar* settingsToolbar(const std::vector<MacSettingsTab>& tabs, int selected) {
    StarDraftDockTarget* t = settingsTarget(tabs);
    NSToolbar* tb = [[[NSToolbar alloc] initWithIdentifier:@"StarDraftSettings"] autorelease];
    [tb setDelegate:t];
    [tb setAllowsUserCustomization:NO];
    [tb setDisplayMode:NSToolbarDisplayModeIconAndLabel];
    if (selected >= 0 && selected < int([t.tabIds count])) [tb setSelectedItemIdentifier:t.tabIds[NSUInteger(selected)]];
    return tb;
}
}  // namespace

void macSettingsToolbar(void* nsWindow, const std::vector<MacSettingsTab>& tabs, int selected, std::function<void(int)> chosen) {
    @autoreleasepool {
        NSWindow* w = (NSWindow*)nsWindow;
        if (!w) return;
        gSettingsChosen = std::move(chosen);
        if (NSWindow* p = [w parentWindow]) [p removeChildWindow:w];  // a window of its own: it does not move with the main window
        [w setToolbarStyle:NSWindowToolbarStylePreference];
        [w setToolbar:settingsToolbar(tabs, selected)];
        [[w standardWindowButton:NSWindowMiniaturizeButton] setEnabled:NO];
        [[w standardWindowButton:NSWindowZoomButton] setEnabled:NO];
    }
}

void macSettingsContentHeight(void* nsWindow, double points, bool animate) {
    @autoreleasepool {
        NSWindow* w = (NSWindow*)nsWindow;
        if (!w || points <= 0) return;
        const CGFloat delta = CGFloat(std::round(points)) - [[w contentView] frame].size.height;
        if (std::fabs(delta) < 0.5) return;
        NSRect f = [w frame];
        f.origin.y -= delta, f.size.height += delta;  // (the top stays where it is)
        [w setFrame:f display:YES animate:animate ? YES : NO];
    }
}

std::string macDockCheck(const std::vector<MacSettingsTab>& tabs) {
    std::ostringstream out;
    @autoreleasepool {
        id d = [NSApp delegate];
        out << "dock: delegate " << (d ? utf8(NSStringFromClass([d class])) : std::string("none")) << "; application:openFile: "
            << ([d respondsToSelector:@selector(application:openFile:)] ? "yes" : "no") << "; application:openFiles: "
            << ([d respondsToSelector:@selector(application:openFiles:)] ? "yes" : "no") << "\n";
        const bool menuOk = [d respondsToSelector:@selector(applicationDockMenu:)];
        out << "dock: applicationDockMenu: " << (menuOk ? "yes" : "no");
        if (menuOk) {
            NSMenu* m = [d applicationDockMenu:NSApp];
            out << "; " << [m numberOfItems] << " items:";
            for (NSMenuItem* it in [m itemArray]) out << " " << ([it isSeparatorItem] ? std::string("|") : "\"" + utf8([it title]) + "\"");
        }
        out << "\n";
        // The first map as one of the system's recent documents (the Dock lists it): it is not in the menu again. And
        // the menu while a dialog waits: its items cannot be clicked.
        std::vector<MacDockItem> items = gDockItems ? gDockItems() : std::vector<MacDockItem>();
        if (!items.empty()) {
            NSMenu* m = makeDockMenu(items, @[[NSURL fileURLWithPath:nsText(items[0].path)]]);
            bool again = false;
            for (NSMenuItem* it in [m itemArray]) again |= utf8((NSString*)[it representedObject]) == items[0].path;
            out << "dock: \"" << items[0].title << "\" in the system's recent documents: " << (again ? "in the menu again (WRONG)" : "not in the menu again")
                << "; " << [m numberOfItems] << " items\n";
            for (MacDockItem& it : items) it.enabled = false;
            m = makeDockMenu(items, @[]);
            int on = 0, all = 0;
            for (NSMenuItem* it in [m itemArray])
                if (![it isSeparatorItem]) ++all, on += [it isEnabled] ? 1 : 0;
            out << "dock: while a dialog waits: " << on << " of " << all << " items can be clicked" << (on ? " (WRONG)" : "") << "\n";
        }
        // The tile while Build runs, then after a build with 3 errors (no jump), then as it was.
        gBuildRunning = true;
        showProgress(true);
        NSDockTile* tile = [NSApp dockTile];
        out << "dock: build running: tile " << ([[tile contentView] isKindOfClass:[StarDraftDockProgress class]] ? "shows the bar" : "has no bar (WRONG)")
            << ", timer " << (target().timer ? "on" : "off (WRONG)") << "\n";
        endBuild(3, false);
        out << "dock: build ended with 3 errors: badge \"" << utf8([tile badgeLabel]) << "\", tile "
            << ([tile contentView] ? "still has the bar (WRONG)" : "is the icon") << ", timer " << (target().timer ? "on (WRONG)" : "off") << "\n";
        [tile setBadgeLabel:nil];
        [tile display];
        // The Settings window's toolbar, in a window that never shows.
        NSWindow* w = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 600, 300) styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                                    backing:NSBackingStoreBuffered defer:YES];
        [w setReleasedWhenClosed:NO];
        [w setToolbarStyle:NSWindowToolbarStylePreference];
        NSToolbar* tb = settingsToolbar(tabs, 0);
        [w setToolbar:tb];
        // (a toolbar may make its items only when its window shows: then the delegate's items, as the toolbar gets them)
        NSMutableArray<NSToolbarItem*>* items = [NSMutableArray arrayWithArray:[tb items]];
        if ([items count] == 0)
            for (NSString* ident in [target() toolbarDefaultItemIdentifiers:tb])
                if (NSToolbarItem* it = [target() toolbar:tb itemForItemIdentifier:ident willBeInsertedIntoToolbar:NO]) [items addObject:it];
        out << "settings: toolbar style " << ([w toolbarStyle] == NSWindowToolbarStylePreference ? "preference" : "other (WRONG)") << "; "
            << [items count] << " items:";
        for (NSToolbarItem* it in items) out << " \"" << utf8([it label]) << "\"" << ([it image] ? "" : " (no icon: WRONG)");
        out << "; selected \"" << utf8([tb selectedItemIdentifier]) << "\"\n";
        [w setToolbar:nil];
        [w release];
    }
    return out.str();
}
