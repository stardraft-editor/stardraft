#pragma once
// SDL's application delegate (SDLAppDelegate) and the methods that the program adds to it, in one place
// (mac_app_delegate.mm). The other macOS parts give their handlers here and do not change the delegate themselves:
// - applicationDockMenu: the Dock icon's menu (mac_dock.mm).
// - application:continueUserActivity:restorationHandler: a Spotlight result that the user opens (mac_dist.mm).
// The maps that Finder opens and the files dropped on the Dock icon stay SDL's application:openFile: (an
// SDL_EVENT_DROP_FILE, as a drop on the window). Each function adds its method to the delegate's class once (the
// delegate must be there: after SDL_Init) and sets the handler; false: the method is not the program's (no delegate,
// or SDL has its own continueUserActivity, which stays). Only for the macOS .mm files; on the UI thread.

#import <Cocoa/Cocoa.h>

bool macDelegateDockMenu(NSMenu* (*menu)());
bool macDelegateUserActivity(BOOL (*open)(NSUserActivity* activity));
