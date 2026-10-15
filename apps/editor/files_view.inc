// The Files workspace, the program's side (included into main.cpp after briefing_view.inc). The page is a view in the core
// (core/views_files.inc: the table, the search, the References panel); here are its parts that need the program: the
// audio (sound_files.inc), the file dialog, the briefings, and the window that shows the page (a view window with the
// native vertical bar, which the window's host keeps in step with the page's scroll).

// The Files page's program parts (core/views_files.inc: FilesHost).
class FilesProgram final : public FilesHost {
public:
    uint32_t soundLength(const std::string& name) override { return ::soundLength(name); }
    bool previewPlaying(const std::string& name) override { return ::previewPlaying(name); }
    std::string previewedName() override { return gPreviewVoice.name; }
    void togglePreview(const std::string& name) override { previewToggle(name); }
    void previewStop() override { ::previewStop(); }
    std::vector<std::string> importSounds() override { return ::importSounds(app.main); }
    bool askName(const std::wstring& title, std::wstring& text) override { return prompt(title.c_str(), text); }
    std::vector<FilesUse> soundUses(const std::string& name) override { return soundUsesOf(name); }
    void showUse(const FilesUse& use) override {  // the Briefing workspace, at the action that uses the sound
        setWorkspace(Workspace::Briefing);
        briefSetWho(briefings()[size_t(use.trig)].players);
        briefRows(gBrief.who);
        const int row = briefRowOf(use.trig, use.act);
        if (row >= 0) briefSelectRange(row, 1);
        InvalidateRect(gBrief.wnd, nullptr, FALSE);
    }
    void soundsChanged() override {
        if (gBrief.wnd) InvalidateRect(gBrief.wnd, nullptr, FALSE);
    }
};
FilesProgram gFilesProgram;
FilesPage gFilesPage(gFilesProgram);

// The Files workspace's window: a view window (ui::kViewClass) that shows gFilesPage.
struct FilesWindow {
    HWND wnd = nullptr;
};
FilesWindow gFiles;

// After an undo or redo step: the sound files and the briefings that play them can be different.
void repaintSoundViews() {
    if (gFiles.wnd) InvalidateRect(gFiles.wnd, nullptr, FALSE);
    if (gBrief.wnd) InvalidateRect(gBrief.wnd, nullptr, FALSE);
}

void createFilesView() {
    if (gFiles.wnd) return;
    gFiles.wnd = CreateWindowExW(0, ui::kViewClass, L"", WS_CHILD | WS_CLIPCHILDREN | WS_VSCROLL, 0, 0, 1, 1, app.main, nullptr, app.inst, &gFilesPage);
    darkControl(gFiles.wnd, L"DarkMode_Explorer");  // the scroll bar in the theme
}
