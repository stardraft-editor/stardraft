// --- Search boxes: kit text fields ------------------------------------------------------------------------------------
// The Locations filter (app.filter) and the palette search are kit text fields (apps/kit/text_field.h) in view windows
// (ui::kViewClass). The field fills the window; at its right an x clears it while there is text. Every change of the
// text calls onChange, as EN_CHANGE did: the typing, the paste, the x and setText (a native SetWindowText sent
// EN_CHANGE too), so the list refreshes at once.
// Included by main.cpp before rebuildList (after the tokens and the App).

// The kit's widgets draw with the editor's tokens and the window's scale (apps/kit/theme.h); the editor calls this when
// its colors, its theme or its DPI change.
void syncKitTheme() {
    syncKitThemeTokens(app.dpi ? int(app.dpi) : 96);
}


SearchBox gFilterBox;     // the Locations filter
SearchBox gPalSearchBox;  // the palette search (Units, Doodads, Sprites)


DropdownBox gSectionBox;  // the palette's section box (Units, Doodads, Sprites)

// The message being dispatched is a mouse wheel one (set by the message loop around DispatchMessageW).
bool gWheelDispatch = false;

// Whether a search box's field has the keyboard (the message loop leaves its keys to it, as to an edit control).
// (The tool strip's number field counts too: a typed digit goes to it, not to the Player shortcuts or Ctrl+Z.)
bool searchBoxFocused() { return gFilterBox.field().hasFocus() || gPalSearchBox.field().hasFocus() || gToolStripView.typing() || gFilesPage.typing(); }
// A kit dropdown's list is open (any: the section box's, a page's): it takes the typed letters, as an open combo box's
// list did.
bool dropdownListOpen() { return stardraft::kit::Dropdown::anyListOpen(); }
