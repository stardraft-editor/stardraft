// Types with macOS's input methods (CI only: it selects input sources and posts keys).
// usage: ime_keys_mac <sequence>
// <sequence>: keys as typed ("gksrnrdj" is 한국어 on 2-Set Korean; a space commits), and in braces:
//   {source:<id>}  that input source, enabled and selected (com.apple.inputmethod.Korean.2SetKorean,
//                  com.apple.keylayout.ABC, com.apple.inputmethod.SCIM.ITABC); the input method it is a mode of is
//                  enabled first
//   {optreturn}    Option+Return (2-Set Korean: the Hanja of the syllable being composed)
//   {return} {down} {wait}
// Build: clang ime_keys_mac.c -framework Carbon -framework ApplicationServices -o ime_keys_mac

#include <ApplicationServices/ApplicationServices.h>
#include <Carbon/Carbon.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static CFArrayRef sources(CFStringRef id, bool all) {
    const void* keys[] = {kTISPropertyInputSourceID};
    const void* values[] = {id};
    CFDictionaryRef filter = CFDictionaryCreate(NULL, keys, values, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFArrayRef list = TISCreateInputSourceList(filter, all);
    CFRelease(filter);
    return list;
}

static OSStatus enable(CFStringRef id) {
    CFArrayRef list = sources(id, true);  // (also the ones not enabled yet)
    OSStatus s = list && CFArrayGetCount(list) ? TISEnableInputSource((TISInputSourceRef)CFArrayGetValueAtIndex(list, 0)) : -1;
    if (list) CFRelease(list);
    return s;
}

static void selectSource(const char* name) {
    CFStringRef id = CFStringCreateWithCString(NULL, name, kCFStringEncodingUTF8);
    // The input method first (an input mode such as 2-Set Korean is one of its modes): the id without its last part.
    const char* dot = strrchr(name, '.');
    if (strncmp(name, "com.apple.inputmethod.", 22) == 0 && dot) {
        char parent[200];
        snprintf(parent, sizeof parent, "%.*s", (int)(dot - name), name);
        CFStringRef p = CFStringCreateWithCString(NULL, parent, kCFStringEncodingUTF8);
        printf("%s: enable %d\n", parent, (int)enable(p));
        CFRelease(p);
    }
    OSStatus e = enable(id);
    usleep(500000);
    // (selected from a list made again: an input source enabled just now may not be selectable from the old one)
    CFArrayRef list = sources(id, false);
    OSStatus s = list && CFArrayGetCount(list) ? TISSelectInputSource((TISInputSourceRef)CFArrayGetValueAtIndex(list, 0)) : -1;
    if (list) CFRelease(list);
    TISInputSourceRef now = TISCopyCurrentKeyboardInputSource();
    char nowName[200] = "";
    if (now) CFStringGetCString((CFStringRef)TISGetInputSourceProperty(now, kTISPropertyInputSourceID), nowName, sizeof nowName, kCFStringEncodingUTF8);
    printf("%s: enable %d, select %d, now %s\n", name, (int)e, (int)s, nowName);
    CFRelease(id);
    usleep(2500000);  // (the input method starts: keys before that lose their composition)
}

static void post(CGKeyCode k, CGEventFlags flags) {
    CGEventRef down = CGEventCreateKeyboardEvent(NULL, k, true), up = CGEventCreateKeyboardEvent(NULL, k, false);
    CGEventSetFlags(down, flags), CGEventSetFlags(up, flags);
    CGEventPost(kCGHIDEventTap, down), usleep(60000);
    CGEventPost(kCGHIDEventTap, up), usleep(150000);
    CFRelease(down), CFRelease(up);
}

static CGKeyCode keyCode(char c) {
    static const char* const kLetters = "asdfhgzxcv\x01" "bqweryt123465=97-80]ou[ip\x01lj'k;\\,/nm.";  // US key codes 0..47
    if (c == ' ') return kVK_Space;
    const char* p = strchr(kLetters, c);
    return p && *p != '\x01' ? (CGKeyCode)(p - kLetters) : 0xFFFF;
}

int main(int argc, char** argv) {
    if (argc < 2) return fprintf(stderr, "usage: ime_keys_mac <sequence>\n"), 2;
    printf("can post keys: %s\n", AXIsProcessTrusted() ? "yes" : "no");
    for (const char* c = argv[1]; *c;) {
        if (*c == '{') {
            const char* end = strchr(c, '}');
            if (!end) return fprintf(stderr, "no } in %s\n", c), 2;
            char token[240];
            snprintf(token, sizeof token, "%.*s", (int)(end - c - 1), c + 1);
            c = end + 1;
            if (strncmp(token, "source:", 7) == 0) selectSource(token + 7);
            else if (strcmp(token, "optreturn") == 0) {  // (the Option key itself goes down too: input methods watch it)
                CGEventRef optDown = CGEventCreateKeyboardEvent(NULL, kVK_Option, true), optUp = CGEventCreateKeyboardEvent(NULL, kVK_Option, false);
                CGEventSetFlags(optDown, kCGEventFlagMaskAlternate), CGEventSetFlags(optUp, 0);
                CGEventPost(kCGHIDEventTap, optDown), usleep(100000);
                post(kVK_Return, kCGEventFlagMaskAlternate);
                CGEventPost(kCGHIDEventTap, optUp), usleep(800000);
                CFRelease(optDown), CFRelease(optUp);
            }
            else if (strcmp(token, "return") == 0) post(kVK_Return, 0);
            else if (strcmp(token, "down") == 0) post(kVK_DownArrow, 0);
            else if (strcmp(token, "wait") == 0) usleep(500000);
            else return fprintf(stderr, "unknown key {%s}\n", token), 2;
            continue;
        }
        const CGKeyCode k = keyCode(*c++);
        if (k != 0xFFFF) post(k, 0);
    }
    printf("sent %s\n", argv[1]);
    return 0;
}
