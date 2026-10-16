// Build's parts that are this system's (the build itself is core/project_build.inc): the text being typed that goes in
// first, and the workspaces that show repaired files again. Included by main_sdl.cpp after sdl_data.inc.

// A text being typed on a map page goes into the map now (as the Windows program's map_pages.inc mapPageFlush).
void mapPageFlush() {
    if (!gSdlPage.ready || gSdlPage.ed != gState.ed.get()) return;
    sdlMapPageStopTyping();
    if (gSdlPage.kind == kSdlProps) sdlPropsAutoApply();
    else sdlMapPageCommit();
}
// The texts being typed go into their map now (a Save, a quit, a tab switch, Undo): a map page's field and the
// Briefing's text box.
void typingFlush() {
    mapPageFlush();
    gSdlBriefing.commitTyping();
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

// --build in a snapshot: Build (Ctrl+B) when this computer has euddraft (a test run never downloads it), then frames
// until the build ends; the output says the result (a failed build: "(WRONG)"), and the Log has euddraft's lines.
void sdlBuildShot() {
    if (!euddraft::find({})) return void(std::printf("build: skipped (no euddraft on this computer)\n"));
    gBuildResult = -1;
    command(ID_PROJ_COMPILE);
    if (!gBuilding) return void(std::printf("build: did not start: %s (WRONG)\n", narrow(gState.message).c_str()));
    const auto start = std::chrono::steady_clock::now();
    while (gBuilding && std::chrono::steady_clock::now() - start < std::chrono::minutes(5)) {
        runPosted();
        SDL_Delay(50);
    }
    runPosted();
    if (gBuilding) return void(std::printf("build: still running after 5 minutes (WRONG)\n"));
    std::printf("build: %s: %s\n", gBuildResult == 1 ? "built (yes)" : "failed (WRONG)", narrow(gState.message).c_str());
}
