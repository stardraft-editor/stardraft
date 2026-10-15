// Build's parts that are this system's (the build itself is core/project_build.inc): the text being typed that goes in
// first, and the workspaces that show repaired files again. Included by main_sdl.cpp after sdl_data.inc.

// A text being typed on a map page goes into the map now (as the Windows program's map_pages.inc mapPageFlush).
void mapPageFlush() {
    if (!gSdlPage.ready || gSdlPage.ed != gState.ed.get()) return;
    sdlMapPageStopTyping();
    if (gSdlPage.kind == kSdlProps) sdlPropsAutoApply();
    else sdlMapPageCommit();
}
// The Data workspace's Strings text and number entry (sdl_data.inc).
void stringsCommitEdit() { sdlStringsCommit(); }
bool dataCommitEdit() { return sdlDataCommitEdit(); }

// The repaired .dat files load now: the Data workspace shows them in the same tab and entry (else at its next show).
void buildDataReloaded(int tab, int entry) {
    if (gWorkspace != Workspace::Data) return;
    sdlDataLoad();
    if (tab >= 0 && tab < dataTabCount()) {
        sdlDataShowTab(tab);
        sdlDataSelect(entry);
    }
    sdlDataRebuildList();
    sdlDataRepaint();
}

// The repaired triggers.json loads now: the Triggers workspace's tree shows it (as the Windows program does). A euddraft
// error in the triggers' code selects its block with the core's trigShowBuildError (core/views_triggers_help.inc).
void buildTriggersReloaded() { trigRefreshTree({1}); }
