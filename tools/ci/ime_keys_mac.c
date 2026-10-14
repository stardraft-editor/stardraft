// Types Korean with macOS's 2-Set Korean input source (CI only: it selects an input source and posts keys).
// usage: ime_keys_mac <keys>   (<keys>: the 2-set layout's keys, "gksrnrdj" for 한국어; a space commits)
// Build: clang ime_keys_mac.c -framework Carbon -framework ApplicationServices -o ime_keys_mac

#include <ApplicationServices/ApplicationServices.h>
#include <Carbon/Carbon.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// Enables the input source with that id (all installed ones are searched); its status, or -1 when there is none.
static OSStatus enable(CFStringRef id) {
    const void* keys[] = {kTISPropertyInputSourceID};
    const void* values[] = {id};
    CFDictionaryRef filter = CFDictionaryCreate(NULL, keys, values, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFArrayRef list = TISCreateInputSourceList(filter, true);
    CFRelease(filter);
    OSStatus s = list && CFArrayGetCount(list) ? TISEnableInputSource((TISInputSourceRef)CFArrayGetValueAtIndex(list, 0)) : -1;
    if (list) CFRelease(list);
    return s;
}

static int selectKorean(void) {
    // The Korean input method first (2-Set Korean is one of its input modes).
    printf("Korean input method: enable %d\n", (int)enable(CFSTR("com.apple.inputmethod.Korean")));
    const void* keys[] = {kTISPropertyInputSourceID};
    CFStringRef id = CFSTR("com.apple.inputmethod.Korean.2SetKorean");
    const void* values[] = {id};
    CFDictionaryRef filter = CFDictionaryCreate(NULL, keys, values, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFArrayRef list = TISCreateInputSourceList(filter, true);  // (also the ones not enabled yet)
    CFRelease(filter);
    if (!list || CFArrayGetCount(list) == 0) return fprintf(stderr, "no 2-Set Korean input source\n"), 1;
    OSStatus e = TISEnableInputSource((TISInputSourceRef)CFArrayGetValueAtIndex(list, 0));
    CFRelease(list);
    usleep(500000);
    // (selected from a list made again: an input source enabled just now may not be selectable from the old one)
    filter = CFDictionaryCreate(NULL, keys, values, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    list = TISCreateInputSourceList(filter, false);
    CFRelease(filter);
    OSStatus s = list && CFArrayGetCount(list) ? TISSelectInputSource((TISInputSourceRef)CFArrayGetValueAtIndex(list, 0)) : -1;
    TISInputSourceRef now = TISCopyCurrentKeyboardInputSource();
    CFStringRef nowId = now ? (CFStringRef)TISGetInputSourceProperty(now, kTISPropertyInputSourceID) : NULL;
    char name[200] = "";
    if (nowId) CFStringGetCString(nowId, name, sizeof name, kCFStringEncodingUTF8);
    printf("2-Set Korean: enable %d, select %d, now %s; can post keys: %s\n", (int)e, (int)s, name, AXIsProcessTrusted() ? "yes" : "no");
    if (list) CFRelease(list);
    return 0;
}

static CGKeyCode keyCode(char c) {
    static const char* const kLetters = "asdfhgzxcv bqweryt123465=97-80]ou[ip lj'k;\\,/nm.";  // US layout key codes 0..47
    if (c == ' ') return kVK_Space;
    const char* p = strchr(kLetters, c);
    return p ? (CGKeyCode)(p - kLetters) : 0xFFFF;
}

int main(int argc, char** argv) {
    if (argc < 2) return fprintf(stderr, "usage: ime_keys_mac <keys>\n"), 2;
    if (selectKorean()) return 1;
    usleep(800000);
    for (const char* c = argv[1]; *c; ++c) {
        const CGKeyCode k = keyCode(*c);
        if (k == 0xFFFF) continue;
        CGEventRef down = CGEventCreateKeyboardEvent(NULL, k, true), up = CGEventCreateKeyboardEvent(NULL, k, false);
        CGEventPost(kCGHIDEventTap, down), usleep(60000);
        CGEventPost(kCGHIDEventTap, up), usleep(120000);
        CFRelease(down), CFRelease(up);
    }
    printf("sent %zu keys\n", strlen(argv[1]));
    return 0;
}
