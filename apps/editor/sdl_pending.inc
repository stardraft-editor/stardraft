// The functions the core still needs whose surface is Win32-only (research/ports/MAC-BUILD-PLAN.md step 6): each one is
// this system's version until its surface is in the core, and its comment names the surface. The core declares them.
// Included by main_sdl.cpp after the core, next to sdl_main_view.inc.

// The workspaces (the core's setWorkspace; the Windows program's is in main.cpp, with its windows): Assets is Files, and
// a workspace the tab's mode does not have falls back to Map with the same message as Windows. The workspace shows its
// view (sdl_main_view.inc: the registered one, else "Not on this system yet"), and the map's panes show in Map only.
void openNewPageSdl(int kind);  // (sdl_new_page.inc: -1 the start page, 0 a classic project, 1 an EUD project)
void showOptionsSdl();  // (sdl_options.inc: Options > Program options, one window)
void showAboutSdl();    // (sdl_options.inc: Help > About StarDraft)
void showKitGallerySdl();  // (sdl_options.inc: View > Control kit)
void showFeedbackSdl(const std::wstring& on);  // (sdl_options.inc: Options > Report feedback, a modal dialog)
void previewStopSdl();  // (sdl_files.inc: a sound that plays stops when the Files workspace is left)
SDL_AudioDeviceID playbackDeviceSdl();  // (sdl_files.inc: the sounds' device, opened when a page with sounds shows)
void briefingStopSdl();  // (sdl_briefing.inc: the preview stops when the Briefing workspace is left)
bool sdlWorkspacePage(Workspace w);  // (sdl_map_pages.inc: the page of a map page workspace opens; false: none does)
void sdlProjectShown();  // (sdl_project.inc: the Project page reads the project's scripts again)
void sdlTriggersShown();  // (sdl_triggers.inc: the project's triggers, and the tree made again)
void setWorkspace(Workspace w) {
    if (w == Workspace::Assets) w = Workspace::Files;  // Assets is part of Files
    if (w != Workspace::Map && newPageCovers()) return;  // (the New page or the start page holds the Map workspace)
    if (w != Workspace::Files) previewStopSdl();
    if (w != Workspace::Briefing) briefingStopSdl();
    if (!commandInMode(mapPageCommandOf(w))) {
        if (w == Workspace::ClassicTriggers || w == Workspace::ClassicSettings)
            setMessage(w == Workspace::ClassicTriggers ? L"This project's map has no classic triggers. Use the Triggers workspace."
                                                       : L"The map's Data page is for classic maps. In a project, use the Data workspace.");
        else setMessage(L"This tab is a classic map. Data, Triggers and Project need a project. File > Project > Make this map a project makes one.");
        w = Workspace::Map;
    }
    // A map page: its view shows (made from the tab's working copy); another workspace: the page goes (the copy stays).
    if (!sdlWorkspacePage(w) && (w == Workspace::Properties || w == Workspace::Players || w == Workspace::ClassicSettings || w == Workspace::ClassicTriggers)) w = Workspace::Map;
    gWorkspace = w;
    // The pages that play sounds: the device opens now, so the first sound starts at once (a Bluetooth headset's
    // link takes a moment to wake up when sound begins).
    if (w == Workspace::Briefing || w == Workspace::Files) playbackDeviceSdl();
    if (w == Workspace::Project) sdlProjectShown();
    if (w == Workspace::Triggers) sdlTriggersShown();
    shellStatusChanged();  // the map's parts show in Map only
    shellRelayout();
    shellMenusChanged();  // Edit's items follow the workspace (and with them Ctrl+C / X / V)
    shellRedrawTabs();
}

// A tab with this project closed: the Data and Triggers workspaces drop what they hold of it (core/data_model.inc,
// core/triggers_model.inc).
void dataForgetProject(const EudProject* pr);
void sdlDataForgetProject(const EudProject* pr);  // (sdl_data.inc: also the number entry)
void forgetProject(const EudProject* pr) {
    trigForgetProject(pr);
    sdlDataForgetProject(pr);
}
