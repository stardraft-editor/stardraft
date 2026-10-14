// Types Korean with macOS's 2-Set Korean input source (CI only: it selects an input source and posts keys).
// usage: ime_keys_mac <keys>   (<keys>: the 2-set layout's keys, "gksrnrdj" for 한국어; a space commits)
// Build: clang ime_keys_mac.c -framework Carbon -framework ApplicationServices -o ime_keys_mac

#include <ApplicationServices/ApplicationServices.h>
#include <Carbon/Carbon.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int selectKorean(void) {
    const void* keys[] = {kTISPropertyInputSourceID};
    CFStringRef id = CFSTR("com.apple.inputmethod.Korean.2SetKorean");
    const void* values[] = {id};
    CFDictionaryRef filter = CFDictionaryCreate(NULL, keys, values, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFArrayRef list = TISCreateInputSourceList(filter, true);  // (also the ones not enabled yet)
    CFRelease(filter);
    if (!list || CFArrayGetCount(list) == 0) return fprintf(stderr, "no 2-Set Korean input source\n"), 1;
    TISInputSourceRef src = (TISInputSourceRef)CFArrayGetValueAtIndex(list, 0);
    OSStatus e = TISEnableInputSource(src), s = TISSelectInputSource(src);
    printf("2-Set Korean: enable %d, select %d\n", (int)e, (int)s);
    CFRelease(list);
    return s != noErr;
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
