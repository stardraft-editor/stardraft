# StarDraft's change to SDL (run by FetchContent's PATCH_COMMAND in SDL's source folder; a second run changes
# nothing). macOS: SDL's text input client gives the input method no text back. 2-Set Korean's Hanja (Option+Return)
# commits the syllable, then reads it back to offer its Hanja: with nothing to read, it offers none. The client now
# keeps the text it last committed and gives back that (or the text being composed); SDL already turns the input
# method's replacement into a Backspace and the new text. Not sent to SDL: SDL takes no AI-written code.
# -DDEBUG_IME=ON (STARDRAFT_SDL_DEBUG_IME): SDL also logs each call of the input method (the IME workflow on macOS).

set(file src/video/cocoa/SDL_cocoakeyboard.m)
file(READ ${file} text)

if(DEBUG_IME AND text MATCHES "#if 0\n#define DEBUG_IME NSLog")
    string(REPLACE "#if 0\n#define DEBUG_IME NSLog" "#if 1 // StarDraft: STARDRAFT_SDL_DEBUG_IME\n#define DEBUG_IME NSLog" text "${text}")
    # (and what the input method asks the client about: the methods it would call)
    string(REPLACE "@implementation SDL3TranslatorResponder\n" "@implementation SDL3TranslatorResponder\n
- (BOOL)respondsToSelector:(SEL)aSelector
{
    BOOL r = [super respondsToSelector:aSelector];
    NSLog(@\"respondsToSelector: %@ %d\", NSStringFromSelector(aSelector), (int)r);
    return r;
}
" text "${text}")
    file(WRITE ${file} "${text}")
    message(STATUS "SDL: the input method's calls are logged")
endif()
if(text MATCHES "StarDraft: the text last committed")
    return()  # (already changed)
endif()

# Each change: the text as SDL has it, and as it becomes. A change that does not find its text stops the configure.
function(change old new)
    string(FIND "${text}" "${old}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "patch_sdl.cmake: ${file} is not as expected (SDL changed): look at the patch again")
    endif()
    string(REPLACE "${old}" "${new}" text "${text}")
    set(text "${text}" PARENT_SCOPE)
endfunction()

change("    NSString *_markedText;
    NSRange _markedRange;"
"    NSString *_markedText;
    NSString *_committedText;  // StarDraft: the text last committed (an input method reads it back to convert it)
    NSRange _markedRange;")

change("    // We're likely sending the composed text, so we reset the IME status.
    if ([self hasMarkedText]) {
        [self unmarkText];
    }"
"    // We're likely sending the composed text, so we reset the IME status.
    if ([self hasMarkedText]) {
        [self unmarkText];
    }
    _committedText = [aString isKindOfClass:[NSAttributedString class]] ? [aString string] : aString;
    _selectedRange = NSMakeRange([_committedText length], 0);")

change("    DEBUG_IME(@\"attributedSubstringFromRange: (%d, %d)\", (int)aRange.location, (int)aRange.length);
    return nil;"
"    DEBUG_IME(@\"attributedSubstringFromRange: (%d, %d)\", (int)aRange.location, (int)aRange.length);
    // The text being composed, else the text last committed: the part asked for.
    NSString *have = _markedText ? _markedText : _committedText;
    if (have && aRange.location != NSNotFound && aRange.location < [have length]) {
        NSRange r = NSIntersectionRange(aRange, NSMakeRange(0, [have length]));
        if (actualRange) {
            *actualRange = r;
        }
        return [[NSAttributedString alloc] initWithString:[have substringWithRange:r]];
    }
    return nil;")

file(WRITE ${file} "${text}")
message(STATUS "SDL: ${file} changed (the text for input methods)")
