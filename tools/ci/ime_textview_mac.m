// A native macOS text field (NSTextView) for the IME workflow: the same keys go to it as to stardraft-next, to tell
// what the input method does from what SDL and the program do. It takes the focus for <seconds>, then prints its
// text ("text view: ...") and quits. CI only.
// usage: ime_textview_mac <seconds>
// Build: clang -fobjc-arc ime_textview_mac.m -framework Cocoa -o ime_textview_mac

#import <Cocoa/Cocoa.h>
#include <stdio.h>
#include <stdlib.h>

// A text view that prints each call of the input method (to compare with SDL's text client).
@interface LoggingTextView : NSTextView
@end
@implementation LoggingTextView
- (void)insertText:(id)s replacementRange:(NSRange)r {
    NSLog(@"tv insertText: %@ (%d, %d)", [s isKindOfClass:[NSAttributedString class]] ? [s string] : s, (int)r.location, (int)r.length);
    [super insertText:s replacementRange:r];
}
- (void)setMarkedText:(id)s selectedRange:(NSRange)sel replacementRange:(NSRange)r {
    NSLog(@"tv setMarkedText: %@ sel (%d, %d) replace (%d, %d)", [s isKindOfClass:[NSAttributedString class]] ? [s string] : s,
          (int)sel.location, (int)sel.length, (int)r.location, (int)r.length);
    [super setMarkedText:s selectedRange:sel replacementRange:r];
}
- (void)unmarkText { NSLog(@"tv unmarkText"); [super unmarkText]; }
- (NSRange)selectedRange { NSRange r = [super selectedRange]; NSLog(@"tv selectedRange (%d, %d)", (int)r.location, (int)r.length); return r; }
- (NSRange)markedRange { NSRange r = [super markedRange]; NSLog(@"tv markedRange (%d, %d)", (int)r.location, (int)r.length); return r; }
- (BOOL)hasMarkedText { BOOL b = [super hasMarkedText]; NSLog(@"tv hasMarkedText %d", (int)b); return b; }
- (NSAttributedString*)attributedSubstringForProposedRange:(NSRange)r actualRange:(NSRangePointer)a {
    NSAttributedString* s = [super attributedSubstringForProposedRange:r actualRange:a];
    NSLog(@"tv attributedSubstring (%d, %d): %@", (int)r.location, (int)r.length, [s string]);
    return s;
}
- (NSAttributedString*)attributedString { NSLog(@"tv attributedString"); return [super attributedString]; }
- (void)doCommandBySelector:(SEL)sel { NSLog(@"tv doCommandBySelector %@", NSStringFromSelector(sel)); [super doCommandBySelector:sel]; }
- (NSRect)firstRectForCharacterRange:(NSRange)r actualRange:(NSRangePointer)a {
    NSLog(@"tv firstRect (%d, %d)", (int)r.location, (int)r.length);
    return [super firstRectForCharacterRange:r actualRange:a];
}
@end

int main(int argc, char** argv) {
    @autoreleasepool {
        const double seconds = argc > 1 ? atof(argv[1]) : 20;
        NSApplication* app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        NSWindow* window = [[NSWindow alloc] initWithContentRect:NSMakeRect(100, 100, 600, 200)
                                                       styleMask:NSWindowStyleMaskTitled
                                                         backing:NSBackingStoreBuffered
                                                           defer:NO];
        NSTextView* view = [[LoggingTextView alloc] initWithFrame:NSMakeRect(0, 0, 600, 200)];
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
