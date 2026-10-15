// The Briefing workspace on this system: the page is the core's view (core/views_briefing_page.inc: BriefingPageView,
// with the edit model of core/views_briefing.inc) in the window's workspace area (registered with
// registerWorkspaceView); this file is its host: the sounds through SDL's audio (sdl_files.inc), the list pickers as
// pop-outs, the sound picker, the time's value pop-out, the question before Delete and the units' icons
// (sdl_pictures.inc). Edit's commands (briefCommand) go to the page. Included by main_sdl.cpp after sdl_map_pages.inc.

void importSoundsSdl();  // (below: Import sounds... of the sound picker; the dialog answers later)

class SdlBriefingHost final : public BriefingPageHost {
public:
    ~SdlBriefingHost() override {
        for (auto& [id, s] : voices_)
            if (s) SDL_DestroyAudioStream(s);
    }
    uint32_t soundLength(const std::string& name) override {
        const SdlSound* d = sdlSound(name);
        return d ? d->ms : 0;
    }
    int startSound(const std::string& name, uint32_t fromMs) override {
        const SdlSound* d = sdlSound(name);
        if (!d) return 0;
        SDL_AudioStream* s = openPlaybackSdl(&d->spec);  // (none in a test run)
        if (!s) return 0;
        // From where the sound is at that time, in whole frames.
        const int frame = SDL_AUDIO_FRAMESIZE(d->spec);
        const uint64_t skip = uint64_t(fromMs) * uint64_t(d->spec.freq) / 1000 * uint64_t(frame);
        if (skip < d->pcm.size()) SDL_PutAudioStreamData(s, d->pcm.data() + skip, int(d->pcm.size() - skip));
        SDL_FlushAudioStream(s);
        SDL_ResumeAudioStreamDevice(s);
        voices_[++next_] = s;
        return next_;
    }
    void stopSound(int voice) override {
        auto it = voices_.find(voice);
        if (it == voices_.end()) return;
        if (it->second) SDL_DestroyAudioStream(it->second);
        voices_.erase(it);
    }
    // The pickers open as the program's pop-out windows (core/views_list_pick.inc: runListPick), at the place in the window.
    bool pickOne(const std::wstring& title, std::vector<ListPickItem> items, const std::string& current, const draw::View& from, draw::Point at,
                 std::string& out) override {
        ListPickView v(title, std::move(items), current);
        const draw::Point o = from.origin();
        if (!runListPick(v, {o.x + at.x, o.y + at.y})) return false;
        out = v.result();
        return true;
    }
    // A sound of the map, an import, or one of the game's (as the Windows program's pickSound, sound_files.inc).
    bool pickSound(const draw::View& from, draw::Point at, const std::string& current, std::string& out) override {
        std::vector<ListPickItem> items{{L"None", L"", ""}};
        for (const auto& w : mapSounds()) items.push_back({widen(w), gState.ed && gState.ed->archiveFile(w) ? L"Map" : L"Game", w});
        items.push_back({L"Import sounds\x2026", L"", "\x01"});
        items.push_back({L"Select a game sound\x2026", L"", "\x02"});
        std::string picked;
        if (!pickOne(L"Sound", items, current, from, at, picked)) return false;
        if (picked == "\x01") {  // (the system's dialog answers later: the sounds come into the map, then the user picks one)
            importSoundsSdl();
            return false;
        }
        if (picked == "\x02") {
            std::vector<ListPickItem> game;
            for (const auto& g : gameSounds()) game.push_back({widen(g), L"", g});
            if (!pickOne(L"Game sound", game, current, from, at, picked)) return false;
        }
        out = picked;
        return true;
    }
    bool editSeconds(draw::View& from, draw::Rect field, const std::wstring& title, uint32_t current, uint32_t& out) override {
        wchar_t b[32];
        std::swprintf(b, 32, L"%g", current / 1000.0);
        std::string text = narrow(b);
        if (!shellEditValueBelow(from, field, title, text)) return false;
        out = briefMsOfSeconds(text);
        return true;
    }
    bool confirm(const std::wstring& title, const std::wstring& text, const std::wstring& yes) override {
        return gRoot && kit::messageBox(*gRoot, shellTr(title.c_str()), shellTr(text.c_str()), {yes, L"Cancel"}, kit::MessageIcon::Warning) == 0;
    }
    draw::Bitmap* unitIcon(int unit) override { return unit >= 0 && unit < 228 ? paletteBitmap(Mode::Units, unit, 0) : nullptr; }

private:
    std::map<int, SDL_AudioStream*> voices_;
    int next_ = 0;
};

SdlBriefingHost gSdlBriefingHost;
BriefingPageView gSdlBriefing(gSdlBriefingHost);

// Import sounds... in the sound picker: the Files page's import (sdl_files.inc), into the map shown.
void importSoundsSdl() { gFilesHost.importSounds(); }

// The Briefing workspace is left: the preview stops (setWorkspace calls it).
void briefingStopSdl() { gSdlBriefing.stop(); }

// The Files page's use of a sound: the Briefing workspace, at the action that plays it.
void SdlFilesHost::showUse(const FilesUse& use) {
    setWorkspace(Workspace::Briefing);
    gSdlBriefing.showAction(use.trig, use.act);
}

// Edit's commands in the Briefing workspace (core/commands_core.inc).
bool briefCommand(int id) { return gSdlBriefing.command(id); }

// --briefing [demo:]<row>[:<ms>]: the page selects that row of the map's first briefing, and the preview goes to that time
// (for a snapshot). demo: the open map gets a briefing of each kind of action first (not saved).
void sdlBriefingSelect(std::string arg) {
    if (arg.rfind("demo:", 0) == 0 && gState.ed) {
        arg = arg.substr(5);
        std::vector<BriefingTrigger> b(1);
        b[0].players[0] = true;
        auto add = [&](uint8_t type, uint32_t time, const std::string& text, int slot = 0, uint16_t unit = 0) {
            BriefingAction a = briefNewAction(type);
            if (time) a.time = time;
            a.text = text, a.slot = slot, a.unit = unit;
            b[0].actions.push_back(a);
        };
        add(BriefingAction::Objectives, 0, "\x04" "Destroy\x01 the Zerg base.\nKeep Jim Raynor alive.");
        add(BriefingAction::ShowPortrait, 0, "", 0, 0);
        add(BriefingAction::ShowPortrait, 0, "", 1, 20);
        add(BriefingAction::Wait, 1500, "");
        add(BriefingAction::TextMessage, 6000, "\x03" "Commander\x01, the colony is under attack.\n\x13" "Hold the \x06line\x01 until the fleet arrives.");
        add(BriefingAction::SpeakingPortrait, 4000, "", 1);
        add(BriefingAction::Transmission, 3000, "We need more minerals.", 0);
        add(BriefingAction::PlayWav, 0, "");
        add(BriefingAction::HidePortrait, 0, "", 1);
        add(BriefingAction::SkipTutorial, 0, "");
        gState.ed->setBriefings(b);
    }
    gSdlBriefing.setWho(briefFirstWho());
    briefRows(gSdlBriefing.who);
    int row = -1;
    unsigned ms = 0;
    std::sscanf(arg.c_str(), "%d:%u", &row, &ms);
    if (row >= 0 && row < int(gBriefRows.size())) gSdlBriefing.sel = row, gSdlBriefing.picked = {row}, gSdlBriefing.anchor = row;
    gSdlBriefing.t = ms;
}
