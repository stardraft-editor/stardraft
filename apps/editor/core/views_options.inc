// Options > Program options (Ctrl+,): the options of the program in one window (portable, core.inc; the snap and placement
// options are in the Options menu). A view: its rows are kit widgets (check boxes, dropdowns, text fields, a slider,
// buttons), its section headers, labels, notes and accent swatches are painted on the canvas, and it scrolls with the
// wheel above a footer with Close. The window shows one tab at a time (setTab: the sections of that tab): on Windows and
// Linux under its own tab strip (showTabStrip), on macOS as a Settings window with toolbar tabs (sdl_options.inc), with
// no footer, as high as the tab's content (onTabHeight). A change applies at once, through the same command as the menu (the
// host runs command(ID_OPT_...)), so the shortcuts and the tests that call the commands get the same result. Close, Esc
// and Enter close the window. The host (options_window.inc on Windows) runs the commands, the folder pickers and the
// list pickers, and tells the view what the options are now (values()).

// The accents: a name and the swatch's color (0x00BBGGRR).
struct OptionsAccent {
    std::wstring name;
    draw::Color color = draw::kNone;
};

// The options as they are now (the host fills it before each paint).
struct OptionsValues {
    bool light = false;
    int accent = 0;
    std::vector<OptionsAccent> accents;
    int defaultAccent = 0;     // the accent that is the default (its tooltip says so)
    bool systemTheme = false;  // the Theme row has Automatic: the system's theme (macOS); it is the default then
    bool themeAuto = false;    // the theme is the system's
    bool middleDrag = true;   // the middle button drags the map (else it pans it)
    int volume = 0;           // every sound's volume, 0 to 100
    int language = 0;         // the chosen language (its choice index, ids.inc's ID_OPT_LANG + index)
    std::vector<std::pair<int, std::wstring>> languages;  // the choices offered: (choice index, name)
    bool restartDue = false;  // the chosen language is not the one in use
    int unitNames = 0;        // how lists name a unit with a custom name (0 to 3)
    int unitLimit[2] = {3400, 1700};  // standard maps, EUD projects
    bool openLast = false;
    bool openSession = false;  // Open the last session's tabs on start
    bool tintIcons = false;   // Appearance: the units' icons in the accent color, not the game's yellow (TintIcons)
    bool diagnostics = false; // Graphics and diagnostics: the frames a second and the memory in use in the status bar (ShowDiagnostics)
    bool askStartLocations = true;  // Saving maps: a save asks when the map has no start locations (AskStartLocations)
    bool manyStartLocations = false;  // Placing units: more than one start location for each player (ManyStartLocations)
    int detailedTiles = 5;    // Painting terrain: the chance (0 to 100%) of a detailed tile variant (stardraft/isom.h)
    int gameMode = 0;         // Plugins > Cosmonarchy BW: 0 not loaded, 1 loaded (core/cmbw.inc cmbwMode)
    bool checkUpdates = false;
    bool previews = false;    // Updates > Previews: the pre-releases too (else the stable releases only)
    int crashReports = 0;     // 0 ask, 1 send, 2 do not send
    bool gpuRow = false;      // the Graphics row shows (Windows: its drivers)
    int gpuDriver = 0;        // Graphics: 0 automatic, 1 Direct3D 12, 2 software, 3 Vulkan (ID_GPU_DRIVER + it)
    std::wstring gpuInUse;    // the driver and video card in use ("Vulkan, NVIDIA GeForce GTX 770")
    bool mapsWithSD2 = false; // .scm and .scx open with StarDraft 2
    bool mapsRow = true;      // the Maps row shows (the file association: the Windows program's)
    std::wstring projectsFolder;  // the working folder (the default one when none is set)
    std::wstring installInUse;    // the StarCraft folder in use ("": none)
    std::wstring installNext;     // the folder used from the next start ("": the same)
    std::wstring cmbwFolder;      // Mods: the Cosmonarchy install in use, else the one it opens from
    std::wstring cmbwNext;        // ... the one used from the next start ("": the same)
    bool cmbwOpen = false;        // ... the mod's data is open (a new folder: from the next start; else at once)
    bool overrideOn = false;      // ... ISOM terrain rules override: a terrain rules file over the built-in rules
    std::wstring overrideFile;    // ... its file ("": none chosen)
    bool passagesOn = false;      // ... Ramp and bridge mappings: a passages file (the Ramp and bridge mapper's) for the tools
    std::wstring passagesFile;    // ... its file ("": none chosen)
};

// The program's part of the options window (options_window.inc): the values, the commands and the pickers.
class OptionsHost {
public:
    virtual ~OptionsHost() = default;
    // The options as they are now.
    virtual void values(OptionsValues& v) = 0;
    // A menu command (ids.inc): the theme, the animations, the language, crash reports, Restart now, Check now, the
    // StarCraft folder and the maps' program. The same path as the menu's.
    virtual void command(int id) = 0;
    // The accent (its swatch was clicked); the view's own theme follows it.
    virtual void setAccent(int i) = 0;
    // A unit name form, a unit limit (k: 0 standard maps, 1 EUD projects) and the volume (at once; the program saves it).
    virtual void setUnitNames(int i) = 0;
    virtual void setUnitLimit(int k, int v) = 0;
    virtual void setVolume(int v) = 0;
    // Painting terrain's Detailed tiles (0 to 100%; GitHub #257).
    virtual void setDetailedTiles(int percent) { (void)percent; }
    // The working folder: a folder chosen by the user, or the default one.
    virtual void browseWorkingFolder() = 0;
    virtual void useDefaultWorkingFolder() = 0;
    // The window's title bar and scroll bar follow the theme (the Windows host; a no-op elsewhere).
    virtual void themeShown(bool light) { (void)light; }
    // Closes the window (Close, Esc, Enter).
    virtual void close() = 0;
};

namespace optionsview {

// The sections of the window, in their order (the headers), and the tabs: each tab shows some sections, in this order.
enum Section : unsigned {
    kStart = 1u << 0, kAppearance = 1u << 1, kMouse = 1u << 2, kUnitNames = 1u << 3, kUnitLimit = 1u << 4,
    kWorkingFolder = 1u << 5, kStarCraftFolder = 1u << 6, kUpdates = 1u << 7, kPlugins = 1u << 9,
    kKeyboard = 1u << 10, kReset = 1u << 11, kLanguage = 1u << 12, kGraphics = 1u << 13, kAllSections = (1u << 14) - 1,
};
struct Tab {
    const wchar_t* name;  // the tab's name (macOS: the toolbar item's label and the window's title)
    unsigned sections;
};
// (by what they are about: the program's start, language and sound; how it looks and draws; the input; the maps; the
// simulation; the plugins; the folders; the updates and crash reports)
inline const Tab kTabs[] = {
    {L"General", kStart | kLanguage | kReset},
    {L"Appearance", kAppearance | kGraphics},
    {L"Mouse and keyboard", kMouse | kKeyboard},
    {L"Map", kUnitNames | kUnitLimit},
    {L"Plugins", kPlugins},
    {L"Folders", kWorkingFolder | kStarCraftFolder},
    {L"Updates", kUpdates},
};
constexpr int kTabCount = int(sizeof kTabs / sizeof kTabs[0]);

// The names of the unit name forms (the dropdown's items).
const wchar_t* const kUnitNameForms[4] = {L"Terran Marine", L"Terran Marine (Custom Name)", L"Custom Name (Terran Marine)", L"Custom Name"};

// Simulate mining's numbers (its panel: sdl_mining_panel.inc): their ranges and default values (the settings
// SimWorkers, SimWorkersPerField, and each boost search's kSimSearchKeys).
constexpr int kSimWorkersMax = 60, kSimPerFieldMax = 8, kSimSearchMin = 1, kSimSearchMax = 600, kSimSearchDefault = 15;
// Each boost search's own search time (position, cargo, cycle; the settings SimSearchPosition, SimSearchCargo and
// SimSearchCycle; default value kSimSearchDefault), set in the mining panel's Boost searches.
inline const wchar_t* const kSimSearchKeys[3] = {L"SimSearchPosition", L"SimSearchCargo", L"SimSearchCycle"};
inline int simBoostSeconds(int kind) {
    int s = kSimSearchDefault;
    prefs::number(kSimSearchKeys[kind], s, kSimSearchMin, kSimSearchMax);
    return s;
}

// A number box with - and + (docs/dev/CONTROLS.md, "Step box"): - at the left end, the value in the middle, + at the
// right end; a click on - or + (or the mouse wheel on the box) steps the value. `hot`, `down`: 0 none, 1 -, 2 +.
inline draw::Rect stepBoxMinus(draw::Rect r) { return {r.left, r.top, r.left + (r.bottom - r.top), r.bottom}; }
inline draw::Rect stepBoxPlus(draw::Rect r) { return {r.right - (r.bottom - r.top), r.top, r.right, r.bottom}; }
inline void paintStepBox(draw::Canvas& c, draw::Rect r, const std::wstring& text, int hot, int down, bool atMin, bool atMax, bool focus = false) {
    kitpaint::drawFieldBox(c, r, focus ? kitpaint::Field::Focus : hot ? kitpaint::Field::Hot : kitpaint::Field::Normal);
    auto end = [&](int part, draw::Rect b, const wchar_t* sign, bool off) {  // (the signs as the simulation bar's speed has them)
        const draw::Rect in{b.left + scale(2), b.top + scale(2), b.right - scale(2), b.bottom - scale(2)};
        if (!off && down == part) c.roundBox(in, kCtlDown, draw::kNone, float(scale(stardraft::kit::kRadius)));
        else if (!off && hot == part) c.roundBox(in, kCtlHot, draw::kNone, float(scale(stardraft::kit::kRadius)));
        c.text(b, sign, off ? kFaint : hot == part ? kText : kDim, draw::kCenter | draw::kMiddle);
    };
    end(1, stepBoxMinus(r), L"\x2212", atMin);
    end(2, stepBoxPlus(r), L"+", atMax);
    c.text({stepBoxMinus(r).right, r.top, stepBoxPlus(r).left, r.bottom}, text, kText, draw::kCenter | draw::kMiddle | draw::kEllipsis);
}

// A step box as a view (Program options > Simulation): the value from `lo` to `hi` in steps of 1; onChange with the
// new value after each step.
class StepBox final : public draw::View {
public:
    int value = 0, lo = 0, hi = 100;
    std::function<std::wstring(int)> format;
    std::function<void(int)> onChange;
    void set(int v) {
        if (v != value) value = v, invalidate();
    }
    void paint(draw::Canvas& c) override {
        paintStepBox(c, {0, 0, width(), height()}, format ? format(value) : std::to_wstring(value), hot_, down_, value <= lo, value >= hi, hasFocus());
    }
    bool mouse(const draw::MouseEvent& e) override {
        const draw::Rect r{0, 0, width(), height()};
        auto in = [&](draw::Rect b) { return e.pt.x >= b.left && e.pt.x < b.right && e.pt.y >= b.top && e.pt.y < b.bottom; };
        const int part = e.kind == draw::MouseKind::Leave ? 0 : in(stepBoxMinus(r)) ? 1 : in(stepBoxPlus(r)) ? 2 : in(r) ? 3 : 0;
        if (part != hot_) hot_ = part, invalidate();
        if (e.kind == draw::MouseKind::Wheel && e.wheel != 0) return step(e.wheel > 0 ? 1 : -1), true;
        if ((e.kind == draw::MouseKind::Down || e.kind == draw::MouseKind::DoubleClick) && e.button == 0 && (part == 1 || part == 2)) {
            down_ = part, invalidate();
            step(part == 1 ? -1 : 1);
            return true;
        }
        if (e.kind == draw::MouseKind::Up) down_ = 0, invalidate();
        return part != 0;
    }
    void captureLost() override { down_ = 0, invalidate(); }
    // The keyboard: a Tab stop; Up or Right one more, Down or Left one less, Home and End the range's ends.
    bool focusable() const override { return true; }
    void focusChanged(bool) override { invalidate(); }
    bool key(const draw::KeyEvent& e) override {
        if (!e.down) return false;
        if (e.key == draw::Key::Up || e.key == draw::Key::Right) return step(1), true;
        if (e.key == draw::Key::Down || e.key == draw::Key::Left) return step(-1), true;
        if (e.key == draw::Key::Home) return step(lo - value), true;
        if (e.key == draw::Key::End) return step(hi - value), true;
        return false;
    }

private:
    int hot_ = 0, down_ = 0;
    void step(int d) {
        const int v = std::clamp(value + d, lo, hi);
        if (v == value) return;
        value = v, invalidate();
        if (onChange) onChange(v);
    }
};

}  // namespace optionsview

class OptionsView final : public draw::View {
public:
    bool scrollsContent() const override { return true; }  // (its rows scroll: a dropdown in them waits for a rest)
    explicit OptionsView(OptionsHost& host) : host_(host) {
        lightSeg_[0].setText(L"Dark"), lightSeg_[0].setTip(L"Dark theme (default)");
        lightSeg_[1].setText(L"Light"), lightSeg_[1].setTip(L"Light theme");
        lightSeg_[2].setText(L"Automatic"), lightSeg_[2].setTip(L"The theme of the system, dark or light (default)");
        lightSeg_[2].onClick = [this] { host_.command(ID_THEME_SYSTEM); };
        middleSeg_[0].setText(L"Drag map"), middleSeg_[0].setTip(L"The map moves with the mouse");
        middleSeg_[1].setText(L"Pan map"), middleSeg_[1].setTip(L"The farther the mouse is from where you pressed, the faster the map moves toward it (default)");
        crashSeg_[0].setText(L"Ask"), crashSeg_[0].setTip(L"Ask at the next start after a crash (default)");
        crashSeg_[1].setText(L"Send"), crashSeg_[1].setTip(L"Send an anonymous report after a crash: the version, Windows, the video card, where it crashed and the last log lines; no maps");
        crashSeg_[2].setText(L"Do not send"), crashSeg_[2].setTip(L"The reports stay on this computer");
        for (int i = 0; i < 2; ++i) {
            lightSeg_[i].onClick = [this, i] { host_.command(i ? ID_THEME_LIGHT : ID_THEME_DARK); };
            middleSeg_[i].onClick = [this, i] { host_.command(ID_MIDDLE_MODE + (i == 0 ? 1 : 0)); };
        }
        for (int i = 0; i < 3; ++i) crashSeg_[i].onClick = [this, i] { host_.command(ID_CRASH_MODE + i); };
        gpuSeg_[0].setText(L"Automatic"),
            gpuSeg_[0].setTip(L"Direct3D 12 on newer video cards, Vulkan on older ones. If the video card cannot use it: the other, then software drawing (default).");
        gpuSeg_[1].setText(L"Direct3D 12"), gpuSeg_[1].setTip(L"Direct3D 12. If the video card cannot use it: Vulkan, then software drawing.");
        gpuSeg_[2].setText(L"Vulkan"), gpuSeg_[2].setTip(L"Vulkan. If the video card cannot use it: Direct3D 12, then software drawing.");
        gpuSeg_[3].setText(L"Software"), gpuSeg_[3].setTip(L"Software drawing (WARP): slow, but it does not use the video card");
        for (int i = 0; i < 4; ++i) gpuSeg_[i].onClick = [this, i] { host_.command(ID_GPU_DRIVER + kGpuValue[i]); };
        for (auto* b : allButtons()) add(*b, {0, 0, 0, 0}), b->setVisible(false);
        lastProject_.setLabel(L"Open the last project on start");
        tintIcons_.setLabel(L"In the accent color");
        tintIcons_.setTip(L"The units' icons in the palettes and the Data workspace: in the accent color, not the game's yellow");
        tintIcons_.onChange = [this](bool) { host_.command(ID_OPT_TINTICONS); };
        diagnostics_.setLabel(L"Show system diagnostics");
        diagnostics_.setTip(L"The frames a second and the memory that StarDraft uses, at the right of the status bar");
        diagnostics_.onChange = [this](bool) { host_.command(ID_OPT_DIAGNOSTICS); };
        lastProject_.onChange = [this](bool) { host_.command(ID_OPT_LASTPROJECT); };
        lastSession_.setLabel(L"Open the last session's tabs on start");
        lastSession_.setTip(L"The maps and projects that were open when StarDraft 2 closed open again");
        lastSession_.onChange = [this](bool) { host_.command(ID_OPT_LASTSESSION); };
        askStart_.setLabel(L"Ask about missing start locations");
        askStart_.setTip(L"Before a save: a map that StarCraft will not start (players without start locations) asks to place them");
        askStart_.onChange = [this](bool) { host_.command(ID_OPT_ASKSTARTLOC); };
        manyStarts_.setLabel(L"Many start locations for each player");
        manyStarts_.setTip(L"Off: a new start location for a player moves the start location that the player has");
        manyStarts_.onChange = [this](bool) { host_.command(ID_OPT_MANYSTARTS); };
        // Plugins: Cosmonarchy BW, loaded or not (Game Standard or Cosmonarchy: ID_GAME_MODE + 0 or 1, core/cmbw.inc gameMode)
        cmbwPlugin_.setLabel(L"Cosmonarchy BW (CMBW)");
        cmbwPlugin_.onChange = [this](bool on) { host_.command(ID_GAME_MODE + (on ? 1 : 0)); };
        override_.setLabel(L"ISOM terrain rules override");
        override_.setTip(L"Terrain rules from a file (Tools > Cosmonarchy > Custom isom terrain editor) in place of the built-in ones");
        override_.onChange = [this](bool) { host_.command(ID_OPT_TERRAINOVERRIDE); };
        passages_.setLabel(L"Ramp and bridge mappings");
        passages_.setTip(L"The mod's ramps and bridges from a file (Tools > Cosmonarchy > Ramp and bridge mapper) for the Ramp brush and the Bridge tool");
        passages_.onChange = [this](bool) { host_.command(ID_OPT_PASSAGES); };
        updates_.setLabel(L"Look for updates");
        updates_.setTip(L"At start and one time each hour (release builds only)");
        updates_.onChange = [this](bool) { host_.command(ID_OPT_UPDATECHECK); };
        previews_.setLabel(L"Previews");
        previews_.setTip(L"Pre-releases too, not only the stable releases");
        previews_.onChange = [this](bool) { host_.command(ID_OPT_PREVIEWS); };
        for (auto* c : {static_cast<draw::View*>(&lastProject_), static_cast<draw::View*>(&lastSession_), static_cast<draw::View*>(&tintIcons_), static_cast<draw::View*>(&diagnostics_), static_cast<draw::View*>(&askStart_), static_cast<draw::View*>(&manyStarts_), static_cast<draw::View*>(&cmbwPlugin_), static_cast<draw::View*>(&override_), static_cast<draw::View*>(&passages_),
                        static_cast<draw::View*>(&updates_), static_cast<draw::View*>(&previews_)})
            add(*c, {0, 0, 0, 0}), c->setVisible(false);
        language_.onChange = [this](int i) {
            if (i >= 0 && i < int(langChoices_.size())) host_.command(ID_OPT_LANG + langChoices_[size_t(i)]);
        };
        unitNames_.onChange = [this](int i) { host_.setUnitNames(i); };
        add(language_, {0, 0, 0, 0}), language_.setVisible(false);
        add(unitNames_, {0, 0, 0, 0}), unitNames_.setVisible(false);
        std::vector<stardraft::kit::DropdownItem> forms;
        for (const wchar_t* n : optionsview::kUnitNameForms) forms.push_back({n});
        unitNames_.setItems(forms);
        slider_.setRange(0, 100), slider_.setStep(1);
        slider_.onChange = [this](int v) { host_.setVolume(v), invalidate(); };
        add(slider_, {0, 0, 0, 0}), slider_.setVisible(false);
        // The accent's swatches: one Tab stop (item_focus.inc); Left and Right pick the color before or after.
        accentStop_.name = L"Accent color";
        accentStop_.step = [this](int d) {
            if (!accents_.empty()) host_.setAccent(std::clamp(accentChosen_ + d, 0, int(accents_.size()) - 1));
            invalidate();
        };
        accentStop_.outline = [this] {
            if (accentChosen_ < 0 || accentChosen_ >= int(accentRects_.size())) return draw::Rect{0, 0, accentStop_.width(), accentStop_.height()};
            const draw::Rect r = accentRects_[size_t(accentChosen_)], f = accentStop_.frame();
            return draw::Rect{r.left - f.left - scale(3), r.top - f.top - scale(3), r.right - f.left + scale(3), r.bottom - f.top + scale(3)};
        };
        add(accentStop_, {0, 0, 0, 0}), accentStop_.setVisible(false);
        for (int k = 0; k < 2; ++k) {
            limit_[k].setFilter([](wchar_t c) { return c >= L'0' && c <= L'9'; });
            limit_[k].setMaxLength(5);
            limit_[k].setAlignRight(true);
            limit_[k].onEnter = [this, k] { commitLimit(k); };
            limit_[k].onFocusLost = [this, k] { commitLimit(k); };
            // Escape gives back the saved value (the window stays open): the typed number is not kept.
            limit_[k].onEscape = [this, k] { limit_[k].setText(std::to_wstring(limitValue_[k])); };
            add(limit_[k], {0, 0, 0, 0}), limit_[k].setVisible(false);
        }
        detail_.setFilter([](wchar_t c) { return c >= L'0' && c <= L'9'; });
        detail_.setMaxLength(3);
        detail_.onEnter = [this] { commitDetail(); };
        detail_.onFocusLost = [this] { commitDetail(); };
        detail_.onEscape = [this] { detail_.setText(std::to_wstring(detailValue_)); };
        add(detail_, {0, 0, 0, 0}), detail_.setVisible(false);
        folder_.setReadOnly(true), folder_.setTabStop(false);
        install_.setReadOnly(true), install_.setTabStop(false);
        cmbw_.setReadOnly(true), cmbw_.setTabStop(false), overrideFile_.setReadOnly(true), overrideFile_.setTabStop(false);
        passagesFile_.setReadOnly(true), passagesFile_.setTabStop(false);
        add(folder_, {0, 0, 0, 0}), add(install_, {0, 0, 0, 0}), add(cmbw_, {0, 0, 0, 0}), add(overrideFile_, {0, 0, 0, 0}), add(passagesFile_, {0, 0, 0, 0});
        folder_.setVisible(false), install_.setVisible(false), cmbw_.setVisible(false), overrideFile_.setVisible(false), passagesFile_.setVisible(false);
        browse_.setText(L"Browse\x2026"), browse_.setTip(L"Select the folder for new projects");
        browse_.onClick = [this] { host_.browseWorkingFolder(); };
        defaultFolder_.setText(L"Default"), defaultFolder_.setTip(L"Documents\\StarDraft");
        defaultFolder_.onClick = [this] { host_.useDefaultWorkingFolder(); };
        installBrowse_.setText(L"Browse\x2026"), installBrowse_.setTip(L"Select the StarCraft: Remastered folder");
        installBrowse_.onClick = [this] { host_.command(ID_OPT_INSTALL); };
        cmbwBrowse_.setText(L"Browse\x2026"), cmbwBrowse_.setTip(L"Select the Cosmonarchy BW folder");
        cmbwBrowse_.onClick = [this] { host_.command(ID_OPT_CMBWFOLDER); };
        overrideBrowse_.setText(L"Browse\x2026"), overrideBrowse_.setTip(L"Select a terrain rules file (*.isom)");
        overrideBrowse_.onClick = [this] { host_.command(ID_OPT_TERRAINOVERRIDEFILE); };
        passagesBrowse_.setText(L"Browse\x2026"), passagesBrowse_.setTip(L"Select a passages file (*.scpass)");
        passagesBrowse_.onClick = [this] { host_.command(ID_OPT_PASSAGESFILE); };
        check_.setText(L"Check now"), check_.setTip(L"Look for a newer release now");
        check_.onClick = [this] { host_.command(ID_HELP_UPDATES); };
        restart_.setText(L"Restart now"), restart_.setTip(L"Start StarDraft 2 again in the new language. Your open maps and projects open again.");
        restart_.onClick = [this] { host_.command(ID_OPT_RESTART); };
        bar_.onScroll = [this](int pos) { scrollTo(pos); };  // (the scroll bar: shown when the content is taller than its room)
        add(bar_, {0, 0, 0, 0});
        bar_.setVisible(false);
        reset_.setText(L"Reset all program settings\x2026"), reset_.setTone(stardraft::kit::Tone::Danger);
        reset_.setTip(L"Give every program setting its default value and restart (a question first)");
        reset_.onClick = [this] { host_.command(ID_OPT_RESETALL); };
        maps_.setText(L"Open maps with StarDraft 2"), maps_.setTip(L"Make StarDraft 2 the program that opens .scm and .scx maps");
        maps_.onClick = [this] { host_.command(ID_OPT_MAPSDEFAULT); };
        close_.setText(L"Close"), close_.setTone(stardraft::kit::Tone::Primary);
        close_.onClick = [this] { host_.close(); };
        for (auto* b : {&browse_, &defaultFolder_, &installBrowse_, &cmbwBrowse_, &overrideBrowse_, &passagesBrowse_, &check_, &restart_, &reset_, &maps_, &close_}) b->setVisible(false);
        // The tabs (showTabStrip): fixed pages, in the order of kTabs.
        std::vector<stardraft::kit::Tab> tabs;
        for (const optionsview::Tab& t : optionsview::kTabs) tabs.push_back({t.name});
        strip_.setTabs(std::move(tabs));
        strip_.movable = false, strip_.fitText = true;
        strip_.onSelect = [this](int i) { setTab(i); };
        add(strip_, {0, 0, 0, 0}), strip_.setVisible(false);
    }

    // The window's size (the host asks): the content's height and what the footer takes.
    int contentHeight() const { return height_; }

    // One tab (0 to optionsview::kTabCount - 1): only its sections; in a Settings window (macOS) with no footer (the
    // window's close button closes it). -1: every section and Close (the tests' whole page).
    void setTab(int t) {
        t = t >= 0 && t < optionsview::kTabCount ? t : -1;
        if (t >= 0) strip_.select(t);
        if (t == tab_) return;
        tab_ = t, scroll_ = 0, tabHeight_ = 0;
        invalidate();
    }
    int tab() const { return tab_; }
    // Windows and Linux: the tabs in a strip at the top, above the tab's sections, with Close in the footer (a tab is
    // shown: setTab). Ctrl+Tab and Ctrl+Shift+Tab go to the next tab and the one before.
    void showTabStrip(bool on) {
        if (on == stripOn_) return;
        stripOn_ = on, scroll_ = 0;
        invalidate();
    }
    // A tab's content changed its height (px, after a paint): the window takes that height.
    std::function<void(int height)> onTabHeight;

    // The part of the window named `name` at its last paint (view coordinates, scrolled): the tests click them.
    bool part(const std::string& name, draw::Rect& r) const {
        const auto it = parts_.find(name);
        if (it == parts_.end()) return false;
        r = it->second;
        return true;
    }
    bool hasPart(const std::string& name) const { return parts_.count(name) != 0; }
    // Scrolls the content to `s` (the tests scroll a part into view; the wheel and the bar do it too).
    void scrollTo(int s) {
        s = std::clamp(s, 0, maxScroll());
        if (s != scroll_) scroll_ = s, invalidate();
    }
    int scroll() const { return scroll_; }
    // The window was closed: the next one starts at the top (no paint: the window is going), and a tab gives its height
    // again (onTabHeight: the new window takes it).
    void resetScroll() { scroll_ = 0, tabHeight_ = 0; }
    // The fields and check boxes the tests drive.
    stardraft::kit::TextField& limitField(int k) { return limit_[k]; }

    void paint(draw::Canvas& c) override;
    bool mouse(const draw::MouseEvent& e) override;
    bool key(const draw::KeyEvent& e) override {
        if (!e.down) return false;
        if (e.key == draw::Key::Tab && !(e.mods & (draw::kCtrl | draw::kAlt))) return tabScroll((e.mods & draw::kShift) != 0);
        if (stripOn_ && tab_ >= 0 && e.key == draw::Key::Tab && (e.mods & draw::kCtrl) && !(e.mods & draw::kAlt)) {
            const int n = optionsview::kTabCount;
            setTab((tab_ + ((e.mods & draw::kShift) ? n - 1 : 1)) % n);
            strip_.focus();  // (the focus was maybe on an option of the tab that goes)
            return true;
        }
        if (e.key == draw::Key::Escape || e.key == draw::Key::Enter) return host_.close(), true;
        // A Settings window (macOS): Cmd+W closes it, as each window there.
        if (tab_ >= 0 && e.key == static_cast<draw::Key>('W') && (e.mods & draw::kSuper)) return host_.close(), true;
        return false;
    }
    // Tab through the options in the order of the rows, also to the ones that are scrolled out of view: the content
    // scrolls to show the next one (the window's Tab knows only the shown ones). After the last option: Close (the
    // window's Tab); from Close: the first option (Shift+Tab: the last). With the tab strip: from Close the strip, from
    // the strip the first option. false: the window's Tab does it.
    bool tabScroll(bool back) {
        const int n = int(tabOrder_.size());
        if (n == 0) return false;
        int i = -1;
        for (int k = 0; k < n; ++k)
            if (focusIn(*tabOrder_[size_t(k)].first)) i = k;
        int j;
        if (i >= 0) j = i + (back ? -1 : 1);
        else if (close_.hasFocus() && stripOn_ && !back) return false;  // (the window's Tab goes round to the tab strip)
        else if (close_.hasFocus()) j = back ? n - 1 : 0;
        else if (stripOn_ && strip_.hasFocus() && !back) j = 0;
        else return false;
        while (j >= 0 && j < n && !tabOrder_[size_t(j)].first->focusable()) j += back ? -1 : 1;  // (a read-only path, a gray button)
        if (j < 0 || j >= n) return false;
        auto [v, r] = tabOrder_[size_t(j)];
        const int pad = scale(stardraft::kit::kDialogPad);
        if (j == 0) scrollTo(0);  // (the first option: the top of the content, its heading too)
        else if (r.top < contentTop_) scrollTo(scroll_ + r.top - contentTop_ - pad);
        else if (r.bottom > contentBottom_) scrollTo(scroll_ + r.bottom - contentBottom_ + pad);
        if (!v->visible()) v->setVisible(true);  // (the next paint places it where the scroll shows it)
        v->focus();
        if (v->hasFocus()) v->tabbedTo(back);
        invalidate();
        return true;
    }
    static bool focusIn(const draw::View& v) {
        if (v.hasFocus()) return true;
        for (const draw::View* c : v.children())
            if (focusIn(*c)) return true;
        return false;
    }
    // Tab's order: the rows top down, each left to right (the children are placed in rows by paint).
    std::vector<draw::View*> tabChildren() const override { return byScreen(children()); }
    std::wstring tip(draw::Point pt) override {
        const int i = accentAt(pt);
        return i >= 0 && i < int(accents_.size()) ? accents_[size_t(i)].name + (i == defaultAccent_ ? L" (default)" : L"") : std::wstring();
    }
    draw::Cursor cursor(draw::Point pt) override { return accentAt(pt) >= 0 ? draw::Cursor::Hand : draw::Cursor::Arrow; }
    void resized() override { invalidate(); }
    bool vscroll(int code, int pos, int track, int page) override;
    bool windowScrollBar(int& range, int& pageSize, int& pos) override {
        range = std::max(0, height_ - 1), pageSize = std::max(0, contentBottom_ - contentTop_), pos = scroll_;
        return true;
    }

private:
    // The buttons (the segmented choices, the actions) and the children that are not rows.
    std::vector<stardraft::kit::Button*> allButtons() {
        std::vector<stardraft::kit::Button*> out;
        for (int i = 0; i < 2; ++i) out.push_back(&lightSeg_[i]), out.push_back(&middleSeg_[i]);
        out.push_back(&lightSeg_[2]);
        for (int i = 0; i < 3; ++i) out.push_back(&crashSeg_[i]);
        for (int i = 0; i < 4; ++i) out.push_back(&gpuSeg_[i]);
        for (auto* b : {&browse_, &defaultFolder_, &installBrowse_, &cmbwBrowse_, &overrideBrowse_, &passagesBrowse_, &check_, &restart_, &reset_, &maps_, &close_}) out.push_back(b);
        return out;
    }
    int maxScroll() const { return std::max(0, height_ - std::max(0, contentBottom_ - contentTop_)); }
    int accentAt(draw::Point pt) const {
        if (pt.y < contentTop_ || pt.y >= contentBottom_) return -1;  // (a swatch scrolled under the tab strip or the footer)
        for (size_t i = 0; i < accentRects_.size(); ++i)
            if (accentRects_[i].contains(pt)) return int(i);
        return -1;
    }
    void commitLimit(int k) {
        const std::wstring t = limit_[k].text();
        const int v = t.empty() ? 1 : std::clamp(int(std::wcstol(t.c_str(), nullptr, 10)), 1, 65535);
        if (v != limitValue_[k]) host_.setUnitLimit(k, v);
        limitValue_[k] = v;
        limit_[k].setText(std::to_wstring(v));
    }
    void commitDetail() {
        const std::wstring t = detail_.text();
        const int v = t.empty() ? detailValue_ : std::clamp(int(std::wcstol(t.c_str(), nullptr, 10)), 0, 100);
        if (v != detailValue_) host_.setDetailedTiles(v);
        detailValue_ = v;
        detail_.setText(std::to_wstring(v));
    }
    // Places a child at `r` (view coordinates) for this paint; a child is shown when it is wholly in the content (the
    // tab strip and the footer are not drawn over; `bottom`: a child out of the content, the strip and Close). A child
    // that is not placed in a paint is hidden at its end.
    void place(draw::View& v, draw::Rect r, bool show, int bottom = -1) {
        placed_.push_back(&v);
        if (show && &v != &close_ && &v != &strip_) tabOrder_.push_back({&v, r});
        const bool vis = show && r.top >= (bottom < 0 ? contentTop_ : 0) && r.bottom <= (bottom < 0 ? contentBottom_ : bottom);
        if (v.visible() != vis) v.setVisible(vis);
        if (vis) v.setFrame(r);
    }
    void hideUnplaced() {
        for (draw::View* ch : children())  // (not the scroll bar: the paint's end shows or hides it)
            if (ch != &bar_ && ch->visible() && std::find(placed_.begin(), placed_.end(), ch) == placed_.end()) ch->setVisible(false);
        placed_.clear();
    }
    void sync(const OptionsValues& v);

    OptionsHost& host_;
    // (Graphics' segments in their order: Automatic, Direct3D 12, Vulkan, Software; their settings' values)
    static constexpr int kGpuValue[4] = {0, 1, 3, 2};
    stardraft::kit::Button lightSeg_[3], middleSeg_[2], crashSeg_[3], gpuSeg_[4];  // (lightSeg_[2]: Automatic, with systemTheme)
    stardraft::kit::Button browse_, defaultFolder_, installBrowse_, cmbwBrowse_, overrideBrowse_, passagesBrowse_, check_, restart_, reset_, maps_, close_;
    stardraft::kit::ScrollBar bar_;  // at the content's right when it is taller than its room
    stardraft::kit::CheckBox lastProject_, lastSession_, tintIcons_, diagnostics_, askStart_, manyStarts_, cmbwPlugin_, override_, passages_, updates_, previews_;
    stardraft::kit::TabStrip strip_;
    stardraft::kit::Dropdown language_{{}, -1}, unitNames_{{}, -1};
    stardraft::kit::Slider slider_;
    stardraft::kit::TextField limit_[2], detail_, folder_, install_, cmbw_, overrideFile_, passagesFile_;
    std::vector<int> langChoices_;     // the choice index of each language dropdown item
    std::vector<OptionsAccent> accents_;
    std::vector<draw::Rect> accentRects_;
    int hoverAccent_ = -1;
    int accentChosen_ = -1;  // the chosen accent, at the last paint
    PartStop accentStop_;    // the swatches' Tab stop
    std::vector<std::pair<draw::View*, draw::Rect>> tabOrder_;  // the options shown in this paint, in order, and their places
    int defaultAccent_ = 0;
    int systemThemeShown_ = -1;  // (the Theme buttons' tips were set for systemTheme)
    int lightChosen_ = -1, middleChosen_ = -1, crashChosen_ = -1, gpuChosen_ = -1;  // the segmented choices' Primary buttons (sync)
    int limitValue_[2] = {3400, 1700};
    int detailValue_ = 5;
    int tab_ = -1;          // setTab
    bool stripOn_ = false;  // showTabStrip
    int tabHeight_ = 0;     // the height that onTabHeight got last
    int scroll_ = 0;        // the content's scroll position (px)
    int height_ = 0;        // the content's full height (px), at the last paint
    int contentTop_ = 0;    // the top of the content (under the tab strip), at the last paint
    int contentBottom_ = 0; // the bottom of the content (above the footer), at the last paint
    bool lightShown_ = false, lightKnown_ = false;
    bool uiBuilt_ = false;
    std::map<std::string, draw::Rect> parts_;  // where each option is (view px, scrolled), for the tests
    std::vector<draw::View*> placed_;          // the children placed in this paint
};

// The rows of the options: the same places as the window had.
void OptionsView::sync(const OptionsValues& v) {
    // A segmented choice's Primary button is the chosen one (set only when the choice changes: a tone change repaints).
    const int light = v.systemTheme && v.themeAuto ? 2 : v.light ? 1 : 0, middle = v.middleDrag ? 0 : 1, crash = v.crashReports;
    if (light != lightChosen_) {
        lightChosen_ = light;
        for (int i = 0; i < 3; ++i) lightSeg_[i].setTone(i == light ? stardraft::kit::Tone::Primary : stardraft::kit::Tone::Normal);
    }
    if (int(v.systemTheme) != systemThemeShown_) {  // (with Automatic, Automatic is the default: not Dark)
        systemThemeShown_ = int(v.systemTheme);
        lightSeg_[0].setTip(v.systemTheme ? L"Dark theme" : L"Dark theme (default)");
    }
    defaultAccent_ = v.defaultAccent;
    if (middle != middleChosen_) {
        middleChosen_ = middle;
        for (int i = 0; i < 2; ++i) middleSeg_[i].setTone(i == middle ? stardraft::kit::Tone::Primary : stardraft::kit::Tone::Normal);
    }
    if (crash != crashChosen_) {
        crashChosen_ = crash;
        for (int i = 0; i < 3; ++i) crashSeg_[i].setTone(i == crash ? stardraft::kit::Tone::Primary : stardraft::kit::Tone::Normal);
    }
    if (v.gpuDriver != gpuChosen_) {
        gpuChosen_ = v.gpuDriver;
        for (int i = 0; i < 4; ++i) gpuSeg_[i].setTone(kGpuValue[i] == v.gpuDriver ? stardraft::kit::Tone::Primary : stardraft::kit::Tone::Normal);
    }
    cmbwPlugin_.setOn(v.gameMode == 1), override_.setOn(v.overrideOn);
    if (const std::wstring f = v.overrideFile.empty() ? std::wstring(L"\x2014") : v.overrideFile; overrideFile_.text() != f) overrideFile_.setText(f);
    passages_.setOn(v.passagesOn);
    if (const std::wstring f = v.passagesFile.empty() ? std::wstring(L"\x2014") : v.passagesFile; passagesFile_.text() != f) passagesFile_.setText(f);
    accents_ = v.accents;
    lastProject_.setOn(v.openLast), lastSession_.setOn(v.openSession), tintIcons_.setOn(v.tintIcons), diagnostics_.setOn(v.diagnostics), askStart_.setOn(v.askStartLocations), manyStarts_.setOn(v.manyStartLocations), updates_.setOn(v.checkUpdates), previews_.setOn(v.previews);
    // The languages offered (rebuilt when they change), the chosen one.
    if (langChoices_.size() != v.languages.size() || !std::equal(langChoices_.begin(), langChoices_.end(), v.languages.begin(), [](int a, const auto& b) {
            return a == b.first;
        })) {
        std::vector<stardraft::kit::DropdownItem> items;
        langChoices_.clear();
        for (const auto& [choice, name] : v.languages) items.push_back({name}), langChoices_.push_back(choice);
        language_.setItems(items);
    }
    int lang = -1;
    for (size_t i = 0; i < langChoices_.size(); ++i)
        if (langChoices_[i] == v.language) lang = int(i);
    if (language_.value() != lang) language_.setValue(lang);
    if (unitNames_.value() != v.unitNames) unitNames_.setValue(v.unitNames);
    for (int k = 0; k < 2; ++k) {
        limitValue_[k] = v.unitLimit[k];
        if (!limit_[k].hasFocus() && limit_[k].text() != std::to_wstring(v.unitLimit[k])) limit_[k].setText(std::to_wstring(v.unitLimit[k]));
    }
    detailValue_ = v.detailedTiles;
    if (!detail_.hasFocus() && detail_.text() != std::to_wstring(v.detailedTiles)) detail_.setText(std::to_wstring(v.detailedTiles));
    if (slider_.value() != v.volume) slider_.setValue(v.volume);
    auto pick = [](stardraft::kit::Dropdown& d, const int* values, size_t n, int value) {
        int at = -1;
        for (size_t i = 0; i < n; ++i)
            if (values[i] == value) at = int(i);
        if (d.value() != at) d.setValue(at);
    };
    if (folder_.text() != v.projectsFolder) folder_.setText(v.projectsFolder);
    const std::wstring inst = v.installInUse.empty() ? std::wstring(L"\x2014") : v.installInUse;
    if (install_.text() != inst) install_.setText(inst);
    const std::wstring mod = v.cmbwFolder.empty() ? std::wstring(L"\x2014") : v.cmbwFolder;
    if (cmbw_.text() != mod) cmbw_.setText(mod);
}

void OptionsView::paint(draw::Canvas& c) {
    OptionsValues v;
    host_.values(v);
    if (!lightKnown_ || lightShown_ != v.light) lightKnown_ = true, lightShown_ = v.light, host_.themeShown(v.light);
    sync(v);
    const int W = width(), H = height();
    c.fill({0, 0, W, H}, kBg);
    const int pad = scale(stardraft::kit::kDialogPad), rowH = scale(stardraft::kit::kFormRowH), fieldH = scale(stardraft::kit::kFieldH);
    const int btnH = scale(stardraft::kit::kButtonH), gap = scale(stardraft::kit::kButtonGap);
    const bool tabbed = tab_ >= 0, settings = tabbed && !stripOn_;  // (settings: macOS's Settings window, no footer)
    const unsigned shown = tabbed ? optionsview::kTabs[tab_].sections : optionsview::kAllSections;
    const int footerH = settings ? 0 : pad + btnH + pad;
    const int stripH = stripOn_ ? scale(32) : 0;
    contentTop_ = stripOn_ ? pad + stripH : 0;
    contentBottom_ = std::max(contentTop_, H - footerH);
    const int x0 = pad, labelW = scale(210), colRight = W - pad, fx = x0 + labelW;
    int y = contentTop_ + pad - scroll_;
    parts_.clear();
    accentRects_.clear();
    const draw::Font of = c.setFont(c.font({}));  // (the UI font; the notes use the small one)
    auto fieldTop = [&](int rowY) { return rowY + (rowH - fieldH) / 2; };
    auto buttonTop = [&](int rowY) { return rowY + (rowH - btnH) / 2; };
    auto header = [&](const wchar_t* name) {
        y = kitpaint::drawSectionHeader(c, x0, colRight, y, rowH, name);
    };
    auto label = [&](const wchar_t* name) { c.text({x0, y, x0 + labelW, y + rowH}, name, kDim, draw::kMiddle | draw::kEllipsis); };
    // A note under a row: the labels' size and color (a smaller font was too small to read).
    auto note = [&](int x, int noteY, const std::wstring& text) {
        c.text({x, noteY, colRight, noteY + scale(20)}, text, kDim, draw::kMiddle | draw::kEllipsis);
    };
    // A button of a row at x (its own width), its top at `top`.
    auto buttonAt = [&](stardraft::kit::Button& b, int x, int top, int h, const std::string& part) {
        const int w = b.preferredWidth(c);
        const draw::Rect r{x, top, x + w, top + h};
        place(b, r, true);
        parts_[part] = r;
        return r.right;
    };
    // A small button in a row (ui::smallButton): its text's width + 20, at most 24 high, in the row's middle.
    auto smallAt = [&](stardraft::kit::Button& b, int x, int rowY, const std::string& part) {
        const int bh = std::min(rowH, scale(24)), w = c.measure(b.text()) + scale(20), top = rowY + (rowH - bh) / 2;
        const draw::Rect r{x, top, x + w, top + bh};
        place(b, r, true);
        parts_[part] = r;
        return r.right;
    };
    // A segmented choice: one button for each choice, the chosen one Primary, joined (as the tool strip's choices).
    auto segmented = [&](int rowY, stardraft::kit::Button* bs, int n, const std::string& part) {
        int x = fx;
        for (int i = 0; i < n; ++i) {
            const int w = bs[i].preferredWidth(c);
            const draw::Rect r{x, fieldTop(rowY), x + w, fieldTop(rowY) + fieldH};
            place(bs[i], r, true);
            bs[i].setJoined(i > 0, i < n - 1);
            parts_[part + "." + std::to_string(i)] = r;
            x += w;
        }
    };
    auto checkRow = [&](int rowY, stardraft::kit::CheckBox& cb, const std::string& part) {
        const draw::Rect cr{x0, rowY, x0 + scale(24) + c.measure(cb.label()), rowY + rowH};  // as ui::check: the box and the label
        place(cb, cr, true);
        parts_[part] = cr;
    };
    placed_.clear();
    tabOrder_.clear();
    if (stripOn_) place(strip_, {pad, pad, colRight, pad + stripH}, true, H);
    c.pushClip({0, contentTop_, W, contentBottom_});  // the content scrolls under the tab strip, above the footer

    // Start.
    if (shown & optionsview::kStart) {
        header(L"Start");
        label(L"Projects");  // (a label and its value, as the Maps row under it)
        {
            const draw::Rect cr{fx, y, fx + scale(24) + c.measure(lastProject_.label()), y + rowH};
            place(lastProject_, cr, true);
            parts_["lastproject"] = cr;
        }
        y += rowH;
        {
            const draw::Rect cr{fx, y, fx + scale(24) + c.measure(lastSession_.label()), y + rowH};
            place(lastSession_, cr, true);
            parts_["lastsession"] = cr;
        }
        y += rowH;
        // Maps (.scm, .scx): the program a double-click opens them with (where the system lets the program say it).
        if (v.mapsRow) {
            label(L"Maps");
            if (v.mapsWithSD2) note(fx, y + (rowH - scale(20)) / 2, L"Maps (.scm, .scx) open with StarDraft 2");
            else smallAt(maps_, fx, y, "maps");
            y += rowH;
        }
        y += pad;
    }

    // Language and sound: the language (a choice that applies at the next start; the names of the languages are in
    // their own language) and the volume.
    if (shown & optionsview::kLanguage) {
        header(L"Language and sound");
        label(L"Language");
        {
            const draw::Rect box{fx, fieldTop(y), std::min<int>(colRight, fx + scale(260)), fieldTop(y) + fieldH};
            place(language_, box, true);
            parts_["language"] = box;
        }
        if (v.restartDue) {
            const int x = parts_["language"].right + scale(12);
            smallAt(restart_, x, y, "restart");
        }
        y += rowH;
        if (v.restartDue) {  // (only while the chosen language waits for a restart: Restart now is beside it)
            note(fx, y - scale(4), L"The new language shows after a restart");
            y += scale(20);
        }
        // The volume: its name and value, a slider.
        {
            const int rowH = scale(stardraft::kit::kRowH);  // a dense row, as the Data form's volume row
            const int right = std::min<int>(colRight, fx + scale(260));
            c.text({x0, y, x0 + labelW - scale(48), y + rowH}, L"Volume", kDim, draw::kMiddle);
            c.text({x0 + labelW - scale(56), y, x0 + labelW - scale(4), y + rowH}, std::to_wstring(v.volume) + L"%", kText, draw::kMiddle | draw::kRight);
            const int x1 = std::max(fx + scale(8) + scale(60), right - scale(8));
            const draw::Rect area{fx + scale(8) - scale(8), y, x1 + scale(8), y + rowH};
            place(slider_, area, true);
            parts_["volume"] = area;
            y += rowH;
        }
        y += pad;
    }

    // Appearance: the theme, the accent color, the units' icons.
    if (shown & optionsview::kAppearance) {
        header(L"Appearance");
        label(L"Theme");
        segmented(y, lightSeg_, v.systemTheme ? 3 : 2, "theme");
        y += rowH;
        // The accent: the swatches (a ring around the chosen one and the one under the mouse).
        label(L"Accent color");
        {
            const int s = scale(20), top = y + (rowH - s) / 2;
            for (size_t i = 0; i < v.accents.size(); ++i) {
                const int x = fx + int(i) * (s + scale(8));
                const draw::Rect sw{x, top, x + s, top + s};
                const bool chosen = int(i) == v.accent, over = hoverAccent_ == int(i);
                if (chosen || over) {
                    draw::Rect ring = sw;
                    ring.left -= scale(3), ring.top -= scale(3), ring.right += scale(3), ring.bottom += scale(3);
                    c.roundBox(ring, draw::kNone, chosen ? kText : kBorderHot, float(scale(stardraft::kit::kRadius)));
                }
                c.roundBox(sw, v.accents[i].color, draw::kNone, float(scale(stardraft::kit::kRadius)));
                accentRects_.push_back(sw);
                parts_["accent." + std::to_string(i)] = sw;
            }
            accentChosen_ = v.accent;
            if (!accentRects_.empty())  // (a stop over the swatches and the ring's room around them)
                place(accentStop_, {accentRects_.front().left - scale(4), top - scale(4), accentRects_.back().right + scale(4), top + s + scale(4)}, true);
            if (v.accent >= 0 && v.accent < int(v.accents.size()))
                note(fx + int(v.accents.size()) * (s + scale(8)) + scale(4), y + (rowH - scale(20)) / 2, v.accents[size_t(v.accent)].name);
        }
        y += rowH;
        label(L"Unit icons");
        {
            const draw::Rect cr{fx, y, fx + scale(24) + c.measure(tintIcons_.label()), y + rowH};
            place(tintIcons_, cr, true);
            parts_["tinticons"] = cr;
        }
        y += rowH;
        y += pad;
    }

    // Graphics and diagnostics: the driver that draws the program (Windows: a computer that stopped with an old video
    // card's Direct3D 12, 0.8.5; the one in use under it), and the status bar's frames a second and memory.
    if (shown & optionsview::kGraphics) {
        header(L"Graphics and diagnostics");
        if (v.gpuRow) {
            label(L"Graphics");
            segmented(y, gpuSeg_, 4, "graphics");
            y += rowH;
            note(fx, y - scale(4), shellTr(L"In use") + L": " + v.gpuInUse);  // (the driver and card: their names; a new choice shows here after a restart)
            y += scale(20);
        }
        label(L"Diagnostics");
        {
            const draw::Rect cr{fx, y, fx + scale(24) + c.measure(diagnostics_.label()), y + rowH};
            place(diagnostics_, cr, true);
            parts_["diagnostics"] = cr;
        }
        y += rowH + pad;
    }

    // Reset: every program setting back to its default value, then StarDraft 2 starts again (a question first).
    if (shown & optionsview::kReset) {
        header(L"Reset");
        smallAt(reset_, x0, y, "reset");
        y += rowH;
        note(x0, y - scale(4), L"Your maps, projects and recent lists stay as they are");
        y += scale(20) + pad;
    }

    // Mouse: what holding the middle button does on the map (the middle button is the only one that moves it).
    if (shown & optionsview::kMouse) {
        header(L"Mouse");
        label(L"Middle mouse click and hold");
        segmented(y, middleSeg_, 2, "middle");
        y += rowH + pad;
    }

    // Keyboard: the shortcuts cannot change yet (a note that they can soon).
    if (shown & optionsview::kKeyboard) {
        header(L"Keyboard");
        note(x0, y, L"Soon you can change the shortcuts here");
        y += scale(20) + pad;
    }

    // Unit names: a unit with a custom name (the map's unit settings), in every list that names it.
    if (shown & optionsview::kUnitNames) {
        header(L"Unit names");
        label(L"Custom names");
        {
            const draw::Rect box{fx, fieldTop(y), std::min<int>(colRight, fx + scale(300)), fieldTop(y) + fieldH};
            place(unitNames_, box, true);
            parts_["unitnames"] = box;
            y += rowH + pad;
        }
    }

    // Map unit limit: placing, spraying and pasting units stop there (GitHub feedback #103).
    if (shown & optionsview::kUnitLimit) {
        header(L"Map unit limit");
        for (int k = 0; k < 2; ++k) {
            label(k == 0 ? L"Standard maps" : L"EUD projects");
            const draw::Rect box{fx, fieldTop(y), std::min<int>(colRight, fx + scale(120)), fieldTop(y) + fieldH};
            place(limit_[k], box, true);
            parts_[k == 0 ? "unitlimit.classic" : "unitlimit.eud"] = box;
            y += rowH;
        }
        note(x0, y - scale(4), L"StarCraft without mods takes at most 3400 units in a map.");
        y += scale(20) + pad;
        header(L"Saving maps");
        checkRow(y, askStart_, "askstartlocations");
        y += rowH + pad;
    }

    // The working folder: where new projects go.
    if (shown & optionsview::kWorkingFolder) {
        header(L"Working folder");
        const int bw = browse_.preferredWidth(c), dw = defaultFolder_.preferredWidth(c);
        const draw::Rect field{x0, fieldTop(y), colRight - bw - dw - 2 * gap, fieldTop(y) + fieldH};
        place(folder_, field, true);
        const int by = buttonTop(y);
        place(defaultFolder_, {colRight - dw, by, colRight, by + btnH}, true);
        place(browse_, {colRight - dw - gap - bw, by, colRight - dw - gap, by + btnH}, true);
        parts_["workfolder"] = field;
        parts_["browse.workfolder"] = browse_.frame(), parts_["default.workfolder"] = defaultFolder_.frame();
        y += rowH;
        note(x0, y - scale(4), L"New projects go into this folder. A project that you open from another folder stays where it is.");
        y += scale(20) + pad;
    }

    // StarCraft folder.
    if (shown & optionsview::kStarCraftFolder) {
        header(L"StarCraft folder");
        const int bw = installBrowse_.preferredWidth(c);
        const draw::Rect field{x0, fieldTop(y), colRight - bw - gap, fieldTop(y) + fieldH};
        place(install_, field, true);
        const int by = buttonTop(y);
        place(installBrowse_, {colRight - bw, by, colRight, by + btnH}, true);
        parts_["install"] = {colRight - bw, by, colRight, by + btnH};
        y += rowH;
        note(x0, y - scale(4),
             !v.installNext.empty() ? L"After a restart: " + v.installNext : std::wstring(L"A new folder is used after a restart"));
        y += scale(20) + pad;
    }

    // Updates: the check at start and each hour (the top bar's Update shows a newer release), the track, and crash reports.
    if (shown & optionsview::kUpdates) {
        header(L"Updates and crash reports");
        {
            const draw::Rect cr{x0, y, x0 + scale(24) + c.measure(updates_.label()), y + rowH};  // as ui::check
            place(updates_, cr, true);
            parts_["updatecheck"] = cr;
            smallAt(check_, cr.right + scale(16), y, "check");
        }
        y += rowH;
        checkRow(y, previews_, "previews");
        y += rowH;
        // Crash reports (GitHub feedback #45): a crash always keeps its report on this computer; ask, send or not.
        label(L"Crash reports");
        segmented(y, crashSeg_, 3, "crashreports");
        y += rowH;
        y += pad;
    }

    // Plugins: an early preview (a line says so), each one loaded or not, what it does under it, and its
    // settings while it is loaded. Cosmonarchy BW (the only switch for the mod's parts: maps open and are made as the
    // mod's) and the mod's folder (core/cmbw.inc).
    if (shown & optionsview::kPlugins) {
        header(L"Plugins");
        c.text({x0, y, colRight, y + scale(20)}, L"Early feature preview: report the issues that you find", kText, draw::kMiddle | draw::kEllipsis);
        y += scale(20) + scale(6);
        checkRow(y, cmbwPlugin_, "plugin.cmbw");
        y += rowH;
        note(x0 + scale(24), y - scale(4), L"When loaded, maps open with the CMBW data, and new maps are CMBW maps");
        y += scale(20);
        if (v.gameMode != 1) y += pad;  // (the section's end: no folder row below)
    }
    if ((shown & optionsview::kPlugins) && v.gameMode == 1) {  // (the Cosmonarchy folder: only while it is loaded)
        c.text({x0 + scale(24), y, x0 + labelW, y + rowH}, L"Cosmonarchy folder", kDim, draw::kMiddle | draw::kEllipsis);
        const int cw = cmbwBrowse_.preferredWidth(c);
        const draw::Rect cf{fx, fieldTop(y), colRight - cw - gap, fieldTop(y) + fieldH};
        place(cmbw_, cf, true);
        const int cy = buttonTop(y);
        place(cmbwBrowse_, {colRight - cw, cy, colRight, cy + btnH}, true);
        parts_["cmbwfolder"] = {colRight - cw, cy, colRight, cy + btnH};
        y += rowH;
        note(fx, y - scale(4),
             !v.cmbwNext.empty() ? L"After a restart: " + v.cmbwNext
             : v.cmbwOpen        ? std::wstring(L"The Prerelease or Release folder. A new folder is used after a restart.")
                                 : std::wstring(L"The Prerelease or Release folder"));
        y += scale(20);
        // ISOM terrain rules override: a terrain rules file over the built-in rules (the maps opened from then on).
        checkRow(y, override_, "plugin.override");
        y += rowH;
        const int ow = overrideBrowse_.preferredWidth(c);
        const draw::Rect of{x0 + scale(24), fieldTop(y), colRight - ow - gap, fieldTop(y) + fieldH};
        place(overrideFile_, of, true);
        const int oy = buttonTop(y);
        place(overrideBrowse_, {colRight - ow, oy, colRight, oy + btnH}, true);
        parts_["overridefile"] = {colRight - ow, oy, colRight, oy + btnH};
        y += rowH;
        note(x0 + scale(24), y - scale(4), L"Maps opened after a change use the file's terrain rules");
        y += scale(20);
        // Ramp and bridge mappings: a passages file for the Ramp brush and the Bridge tool (at once, in the open maps too;
        // hidden with the tools: ids.inc).
        if (kRampBridgeTools) {
            checkRow(y, passages_, "plugin.passages");
            y += rowH;
            const int pw = passagesBrowse_.preferredWidth(c);
            place(passagesFile_, {x0 + scale(24), fieldTop(y), colRight - pw - gap, fieldTop(y) + fieldH}, true);
            const int py = buttonTop(y);
            place(passagesBrowse_, {colRight - pw, py, colRight, py + btnH}, true);
            parts_["passagesfile"] = {colRight - pw, py, colRight, py + btnH};
            y += rowH;
            note(x0 + scale(24), y - scale(4), L"The Ramp brush and the Bridge tool use its ramps and bridges at once");
            y += scale(20);
        } else {
            for (draw::View* v : {static_cast<draw::View*>(&passages_), static_cast<draw::View*>(&passagesFile_), static_cast<draw::View*>(&passagesBrowse_)})
                if (v->visible()) v->setVisible(false);
        }
        y += pad;
    }

    height_ = y + scroll_ - contentTop_;
    c.popClip();
    // The footer: Close (the content scrolls under it, a line between). A Settings window has none: it takes the tab's
    // height.
    if (!settings && height_ > contentBottom_ - contentTop_) c.fill({0, contentBottom_, W, contentBottom_ + 1}, kBorder);
    if (!settings) {
        const int w = close_.preferredWidth(c);
        const draw::Rect r{colRight - w, H - pad - btnH, colRight, H - pad};
        place(close_, r, true, H);
        parts_["close"] = r;
    }
    hideUnplaced();
    std::stable_sort(tabOrder_.begin(), tabOrder_.end(), [](const auto& a, const auto& b) {  // (in a row: left to right)
        const int ma = a.second.top + a.second.bottom, mb = b.second.top + b.second.bottom;
        return ma != mb ? ma < mb : a.second.left < b.second.left;
    });
    c.setFont(of);
    if (settings && height_ != tabHeight_) {
        tabHeight_ = height_;
        if (onTabHeight) onTabHeight(height_);
    }
    // The scroll: a bar's page is the content's height above the footer; the bar shows at the content's right when
    // there is more to scroll to (the wheel scrolls too).
    if (scroll_ > maxScroll()) scroll_ = maxScroll(), invalidate();
    const bool more = maxScroll() > 0;
    bar_.setVisible(more);
    if (more) {
        bar_.setFrame({W - scale(stardraft::kit::kScrollBarW), contentTop_, W, contentBottom_});
        bar_.setRange(height_, contentBottom_ - contentTop_);
        bar_.setPos(scroll_);
    }
}

bool OptionsView::mouse(const draw::MouseEvent& e) {
    if (e.kind == draw::MouseKind::Move || e.kind == draw::MouseKind::Leave) {
        const int i = e.kind == draw::MouseKind::Leave ? -1 : accentAt(e.pt);
        if (i != hoverAccent_) hoverAccent_ = i, invalidate();
        return false;
    }
    if (e.kind == draw::MouseKind::Wheel) {
        scrollTo(scroll_ - int(draw::wheelDistance(e, float(scale(40) * 3))));
        return true;
    }
    if (e.kind == draw::MouseKind::Down && e.button == 0) {
        const int i = accentAt(e.pt);
        if (i >= 0) {
            host_.setAccent(i);
            return true;
        }
    }
    return false;
}

bool OptionsView::vscroll(int code, int /*pos*/, int track, int page) {
    int s = scroll_;
    if (!kitpaint::scrollRequest(code, s, scale(40), page, track)) return false;
    scrollTo(s);
    return true;
}
