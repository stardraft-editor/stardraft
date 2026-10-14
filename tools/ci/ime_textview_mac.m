// A native macOS text field (NSTextView) for the IME workflow: the same keys go to it as to stardraft-next, to tell
// what the input method does from what SDL and the program do. It takes the focus for <seconds>, then prints its
// text ("text view: ...") and quits. CI only.
// usage: ime_textview_mac <seconds>
// Build: clang -fobjc-arc ime_textview_mac.m -framework Cocoa -o ime_textview_mac

#import <Cocoa/Cocoa.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    @autoreleasepool {
        const double seconds = argc > 1 ? atof(argv[1]) : 20;
        NSApplication* app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        NSWindow* window = [[NSWindow alloc] initWithContentRect:NSMakeRect(100, 100, 600, 200)
                                                       styleMask:NSWindowStyleMaskTitled
                                                         backing:NSBackingStoreBuffered
                                                           defer:NO];
        NSTextView* view = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 600, 200)];
        [window setContentView:view];
        [window makeKeyAndOrderFront:nil];
        [window makeFirstResponder:view];
        [app activateIgnoringOtherApps:YES];
        printf("ready: the text view has the focus\n");
        fflush(stdout);
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(seconds * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
            printf("text view: %s\n", [[view string] UTF8String]);
            fflush(stdout);
            [app terminate:nil];
        });
        [app run];
    }
    return 0;
}
