// The other workspaces' entry points that the core calls (research/ports/MAC-BUILD-PLAN.md step 6): on this system
// their workspaces are not there yet, so each command says so in the status bar and does nothing else. Included by
// main_sdl.cpp after the core (its declarations are in core/commands_core.inc, core/documents.inc, core/map_input.inc).

namespace {
void notOnThisSystem() { setMessage(L"This is not on this system yet"); }
}  // namespace

bool briefCommand(int) {
    notOnThisSystem();
    return false;
}
bool classicCommandInProject(int) { return false; }  // (a query: no project of this system holds classic data yet)
