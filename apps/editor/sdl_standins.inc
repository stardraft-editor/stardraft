// The other workspaces' entry points that the core calls (research/ports/MAC-BUILD-PLAN.md step 6): on this system
// their workspaces are not there yet, so each command says so in the status bar and does nothing else. Included by
// main_sdl.cpp after the core (its declarations are in core/commands_core.inc, core/documents.inc, core/map_input.inc).

namespace {
void notOnThisSystem() { setMessage(L"This is not on this system yet"); }
}  // namespace

void dataCopySelected() { notOnThisSystem(); }
void dataPasteSelected() { notOnThisSystem(); }
void trigCopy(bool) { notOnThisSystem(); }
void trigPaste() { notOnThisSystem(); }
void trigDuplicate() { notOnThisSystem(); }
bool briefCommand(int) {
    notOnThisSystem();
    return false;
}
bool classicCommandInProject(int) { return false; }  // (a query: no project of this system holds classic data yet)

// The button sets are not on this system yet (the map pages are: sdl_map_pages.inc; the New page is sdl_new_page.inc).
std::wstring buttonSetName(int s) { return L"Button set " + std::to_wstring(s); }
