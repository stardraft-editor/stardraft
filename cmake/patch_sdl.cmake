# StarDraft's change to SDL (run by FetchContent's PATCH_COMMAND in SDL's source folder; a second run changes
# nothing). macOS: SDL's text input client gives the input method no text back. 2-Set Korean's Hanja (Option+Return)
# commits the syllable, then reads it back to offer its Hanja: with nothing to read, it offers none. The client now
# keeps the text it last committed and gives back that (or the text being composed); SDL already turns the input
# method's replacement into a Backspace and the new text. Not sent to SDL: SDL takes no AI-written code.
# -DDEBUG_IME=ON (STARDRAFT_SDL_DEBUG_IME): SDL also logs each call of the input method (the IME workflow on macOS).
# Windows: with the hint STARDRAFT_D3D12_WARP set, Direct3D 12 uses WARP (the software device of Windows) in place of the
# video card. The renderer asks for it when the video card has no Direct3D 12 or Vulkan that SDL can use.

# Each change: the text as SDL has it, and as it becomes. A change that does not find its text stops the configure.
function(change old new)
    string(FIND "${text}" "${old}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "patch_sdl.cmake: ${file} is not as expected (SDL changed): look at the patch again")
    endif()
    string(REPLACE "${old}" "${new}" text "${text}")
    set(text "${text}" PARENT_SCOPE)
endfunction()

set(file src/gpu/d3d12/SDL_gpu_d3d12.c)
file(READ ${file} text)
if(NOT text MATCHES "STARDRAFT_D3D12_WARP")
    change("    if (FAILED(res)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_GPU, \"D3D12: Failed to find adapter for D3D12Device\");"
"    if (SDL_GetHintBoolean(\"STARDRAFT_D3D12_WARP\", false)) {  // StarDraft: the software device
        IDXGIFactory4 *warpFactory = NULL;
        if (SUCCEEDED(res)) {
            IDXGIAdapter1_Release(adapter);
            adapter = NULL;
        }
        res = IDXGIFactory1_QueryInterface(factory, D3D_GUID(D3D_IID_IDXGIFactory4), (void **)&warpFactory);
        if (SUCCEEDED(res)) {
            res = IDXGIFactory4_EnumWarpAdapter(warpFactory, D3D_GUID(D3D_IID_IDXGIAdapter1), (void **)&adapter);
            IDXGIFactory4_Release(warpFactory);
        }
    }
    if (FAILED(res)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_GPU, \"D3D12: Failed to find adapter for D3D12Device\");")
    change("    if (FAILED(res)) {
        D3D12_INTERNAL_DestroyRenderer(renderer);
        CHECK_D3D12_ERROR_AND_RETURN(\"Could not find adapter for D3D12Device\", NULL);"
"    if (SDL_GetHintBoolean(\"STARDRAFT_D3D12_WARP\", false)) {  // StarDraft: the software device
        if (SUCCEEDED(res)) {
            IDXGIAdapter1_Release(renderer->adapter);
            renderer->adapter = NULL;
        }
        res = IDXGIFactory4_EnumWarpAdapter(renderer->factory, D3D_GUID(D3D_IID_IDXGIAdapter1), (void **)&renderer->adapter);
    }
    if (FAILED(res)) {
        D3D12_INTERNAL_DestroyRenderer(renderer);
        CHECK_D3D12_ERROR_AND_RETURN(\"Could not find adapter for D3D12Device\", NULL);")
    file(WRITE ${file} "${text}")
    message(STATUS "SDL: ${file} changed (Direct3D 12 can use WARP)")
endif()


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
    // (a copy: the input method changes its own string after the call)
    _committedText = [([aString isKindOfClass:[NSAttributedString class]] ? [aString string] : aString) copy];
    _selectedRange = NSMakeRange([_committedText length], 0);")

change("    if ([aString length] == 0) {
        [self unmarkText];
        return;
    }"
"    // StarDraft: marked text that replaces committed text (2-Set Korean's Hanja replaces the syllable it committed):
    // the committed characters go, as insertText's replacement does (one Backspace each).
    if (!_markedText && (int)replacementRange.location != -1 && replacementRange.length > 0) {
        for (NSUInteger i = 0; i < replacementRange.length; ++i) {
            SDL_SendKeyboardKey(0, SDL_GLOBAL_KEYBOARD_ID, 0, SDL_SCANCODE_BACKSPACE, true);
            SDL_SendKeyboardKey(0, SDL_GLOBAL_KEYBOARD_ID, 0, SDL_SCANCODE_BACKSPACE, false);
        }
        _committedText = nil;
    }

    if ([aString length] == 0) {
        [self unmarkText];
        return;
    }")

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

change("- (NSInteger)conversationIdentifier
{"
"// StarDraft: the whole text (the input method asks for it, as from a text view) and that it can be changed.
- (NSAttributedString *)attributedString
{
    NSString *have = _markedText ? _markedText : _committedText;
    DEBUG_IME(@\"attributedString: %@\", have);
    return [[NSAttributedString alloc] initWithString:have ? have : @\"\"];
}

- (BOOL)isEditable
{
    return YES;
}

- (NSInteger)conversationIdentifier
{")

change("- (NSArray *)validAttributesForMarkedText
{
    return [NSArray array];
}"
"- (NSArray *)validAttributesForMarkedText
{
    // StarDraft: as a text view, the marked text's underline and clause segments (an input method's candidates use them).
    return @[ NSUnderlineStyleAttributeName, NSUnderlineColorAttributeName, NSMarkedClauseSegmentAttributeName,
              NSTextAlternativesAttributeName ];
}")
change("- (NSRange)selectedRange
{
    return _selectedRange;
}"
"- (NSRange)selectedRange
{
    DEBUG_IME(@\"selectedRange: (%d, %d)\", (int)_selectedRange.location, (int)_selectedRange.length);
    return _selectedRange;
}")
change("- (NSRange)markedRange
{
    return _markedRange;
}"
"- (NSRange)markedRange
{
    // StarDraft: no marked text, no range (the range of the text that was marked stayed).
    const NSRange r = _markedText ? _markedRange : NSMakeRange(NSNotFound, 0);
    DEBUG_IME(@\"markedRange: (%d, %d)\", (int)r.location, (int)r.length);
    return r;
}")

file(WRITE ${file} "${text}")
message(STATUS "SDL: ${file} changed (the text for input methods)")
