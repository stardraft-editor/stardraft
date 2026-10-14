# StarDraft's change to SDL (run by FetchContent's PATCH_COMMAND in SDL's source folder; a second run changes
# nothing). macOS: SDL's text input client gives the input method no text back, so an input method that reads the text
# it converts (2-Set Korean's Hanja, Option+Return) commits the syllable instead of offering its Hanja. The client now
# gives back the text being composed (the only text it has). Not sent to SDL: SDL takes no AI-written code.

set(file src/video/cocoa/SDL_cocoakeyboard.m)
file(READ ${file} text)
# -DDEBUG_IME=ON (STARDRAFT_SDL_DEBUG_IME): SDL logs each call of the input method (the IME workflow on macOS).
if(DEBUG_IME AND text MATCHES "#if 0
#define DEBUG_IME NSLog")
    string(REPLACE "#if 0
#define DEBUG_IME NSLog" "#if 1 // StarDraft: STARDRAFT_SDL_DEBUG_IME
#define DEBUG_IME NSLog" text "${text}")
    file(WRITE ${file} "${text}")
    message(STATUS "SDL: the input method's calls are logged")
endif()
if(text MATCHES "StarDraft: the text being composed")
    return()  # (already changed)
endif()
set(old "- (NSAttributedString *)attributedSubstringForProposedRange:(NSRange)aRange actualRange:(NSRangePointer)actualRange
{
    DEBUG_IME(@\"attributedSubstringFromRange: (%d, %d)\", (int)aRange.location, (int)aRange.length);
    return nil;
}")
set(new "- (NSAttributedString *)attributedSubstringForProposedRange:(NSRange)aRange actualRange:(NSRangePointer)actualRange
{
    DEBUG_IME(@\"attributedSubstringFromRange: (%d, %d)\", (int)aRange.location, (int)aRange.length);
    // StarDraft: the text being composed, the part of it asked for (an input method reads what it converts).
    if (_markedText && aRange.location != NSNotFound && aRange.location < [_markedText length]) {
        NSRange r = NSIntersectionRange(aRange, NSMakeRange(0, [_markedText length]));
        if (actualRange) {
            *actualRange = r;
        }
        return [[NSAttributedString alloc] initWithString:[_markedText substringWithRange:r]];
    }
    return nil;
}")
string(FIND "${text}" "${old}" at)
if(at EQUAL -1)
    message(FATAL_ERROR "patch_sdl.cmake: ${file} is not as expected (SDL changed): look at the patch again")
endif()
string(REPLACE "${old}" "${new}" text "${text}")
file(WRITE ${file} "${text}")
message(STATUS "SDL: ${file} changed (the text being composed for input methods)")
