// The message box's Windows host: questions, errors and notes in the theme (MessageBoxW is always light). Included by
// main.cpp after view_win32.inc; main.cpp declares themedMessageBox() near the top, so the code before this file can
// use it.
//
// themedMessageBox() is a drop-in for MessageBoxW: the same flags (MB_OK, MB_OKCANCEL, MB_YESNO, MB_YESNOCANCEL,
// MB_RETRYCANCEL; MB_ICONERROR, MB_ICONWARNING, MB_ICONQUESTION, MB_ICONINFORMATION; MB_DEFBUTTON2 and 3) and the
// same return values (IDOK, IDCANCEL, IDYES, IDNO, IDRETRY). `names` gives the buttons other names (verbs: "Save",
// "Don't save", "Cancel"), from left to right; the return values do not change.
//
// The box is the core's view (core/views_message_box.inc: MessageView in a kit Dialog, runMessageBox), opened modal
// over the top window of `owner` (Host::runModal: the owner is disabled while the box shows; the box is centered on
// it, or on the screen's work area when the owner does not show). The crash question at start (crash.inc), the map
// file question and the update prompts (update.inc) come through here too.

void (*gMsgBoxHook)(HWND) = nullptr;  // datashots and self-tests: called when the box shows (capture it, press a key)

int themedMessageBox(HWND owner, const std::wstring& text, const std::wstring& caption, UINT flags, const std::vector<std::wstring>& names) {
    msgbox::Box b;
    b.text = text;
    switch (flags & MB_ICONMASK) {
    case MB_ICONERROR: b.icon = msgbox::Icon::Error; break;
    case MB_ICONWARNING: b.icon = msgbox::Icon::Warning; break;
    case MB_ICONQUESTION: b.icon = msgbox::Icon::Question; break;
    case MB_ICONINFORMATION: b.icon = msgbox::Icon::Info; break;
    }
    switch (flags & MB_TYPEMASK) {
    case MB_OKCANCEL: b.buttons = {{L"OK", IDOK}, {L"Cancel", IDCANCEL}}, b.cancel = 1; break;
    case MB_YESNO: b.buttons = {{L"&Yes", IDYES}, {L"&No", IDNO}}, b.cancel = 1; break;
    case MB_YESNOCANCEL: b.buttons = {{L"&Yes", IDYES}, {L"&No", IDNO}, {L"Cancel", IDCANCEL}}, b.cancel = 2; break;
    case MB_RETRYCANCEL: b.buttons = {{L"&Retry", IDRETRY}, {L"Cancel", IDCANCEL}}, b.cancel = 1; break;
    default: b.buttons = {{L"OK", IDOK}}, b.cancel = 0; break;
    }
    for (size_t k = 0; k < names.size() && k < b.buttons.size(); ++k)
        if (!names[k].empty()) b.buttons[k].name = names[k];
    const UINT def = flags & MB_DEFMASK;
    b.focus = std::min<int>(int(b.buttons.size()) - 1, def == MB_DEFBUTTON2 ? 1 : def == MB_DEFBUTTON3 ? 2 : def == MB_DEFBUTTON4 ? 3 : 0);

    // The owner is the top window (a box from a child window disables its top window, as MessageBoxW does): the box
    // opens from an anchor in it.
    struct Anchor {
        stardraft::draw::View view;
        ui::Win32Host host{view};
    } anchor;
    anchor.host.ownerWindow(owner ? GetAncestor(owner, GA_ROOT) : nullptr);
    HDC dc = GetDC(nullptr);  // (to size the window for the text and the buttons, in the UI font)
    int r = 0;
    {
        ui::Paint p(dc);
        r = msgbox::runMessageBox(anchor.view, caption, std::move(b), dc ? &*p : nullptr, [](msgbox::MessageView& v) {
            if (gMsgBoxHook)
                if (HWND w = ui::Win32Host::windowOf(v)) gMsgBoxHook(w);
        });
    }
    if (dc) ReleaseDC(nullptr, dc);
    return r;
}
