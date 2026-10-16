// The methods that the program adds to SDL's application delegate (mac_app_delegate.h). Built without ARC (CMake's
// default for .mm files).

#include "mac_app_delegate.h"

#include <objc/runtime.h>

namespace {
NSMenu* (*gDockMenu)() = nullptr;
BOOL (*gUserActivity)(NSUserActivity*) = nullptr;
bool gDockMenuAdded = false, gUserActivityAdded = false;

NSMenu* dockMenu(id self, SEL cmd, NSApplication* app) {
    (void)self, (void)cmd, (void)app;
    return gDockMenu ? gDockMenu() : nil;
}

BOOL continueUserActivity(id self, SEL cmd, NSApplication* app, NSUserActivity* activity, id restore) {
    (void)self, (void)cmd, (void)app, (void)restore;
    return gUserActivity ? gUserActivity(activity) : NO;
}

// Adds `imp` as `sel` to the delegate's class. `replace`: an implementation that the class has already gets `imp`
// (else it stays, and the method is not the program's).
bool addMethod(SEL sel, IMP imp, const char* types, bool replace) {
    id delegate = [NSApp delegate];
    if (!delegate || !types) return false;
    Class c = object_getClass(delegate);
    if (class_addMethod(c, sel, imp, types)) return true;
    if (!replace) return false;
    Method m = class_getInstanceMethod(c, sel);
    if (m) method_setImplementation(m, imp);
    return m != nullptr;
}
}  // namespace

bool macDelegateDockMenu(NSMenu* (*menu)()) {
    gDockMenu = menu;
    // (SDL's delegate has no applicationDockMenu:; if a later SDL has one, the program's takes its place)
    if (!gDockMenuAdded) gDockMenuAdded = addMethod(@selector(applicationDockMenu:), (IMP)dockMenu, "@@:@", true);
    return gDockMenuAdded;
}

bool macDelegateUserActivity(BOOL (*open)(NSUserActivity* activity)) {
    gUserActivity = open;
    if (!gUserActivityAdded) {
        const SEL sel = @selector(application:continueUserActivity:restorationHandler:);
        const objc_method_description d = protocol_getMethodDescription(@protocol(NSApplicationDelegate), sel, NO, YES);
        gUserActivityAdded = addMethod(sel, (IMP)continueUserActivity, d.types, false);  // (SDL's own, if it has one, stays)
    }
    return gUserActivityAdded;
}
