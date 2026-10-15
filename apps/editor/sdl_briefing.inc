// The Briefing workspace on this system: the page is a view in the window's workspace area (registered with
// registerWorkspaceView). The edit model is core/views_briefing.inc (the rows, the regrouping, the timeline); this file
// draws the three columns with the canvas (the briefings by whom they are for, their actions in order, and the briefing
// window as a wireframe with its timeline and Play), selects and plays. Included by main_sdl.cpp after sdl_map_pages.inc.
//
// Not on this system yet (the Win32 page has them, briefing_view.inc): editing (the text box, the fields of the selected
// action, drag, copy, cut, paste, duplicate and moving with Alt), the portraits' pictures (the slots are frames), the
// scroll of the wireframe's text, the value pop-outs and the keyboard's transport keys.

#include <set>

namespace {
const wchar_t* sdlBriefTypeName(uint8_t t) {
    static const wchar_t* n[] = {L"No action", L"Wait", L"Play sound", L"Text message", L"Mission objectives", L"Show portrait", L"Hide portrait",
                                 L"Talking portrait", L"Transmission", L"Skip tutorial enabled"};
    return t < 10 ? n[t] : L"Unknown";
}
std::wstring sdlBriefTime(uint32_t ms) {
    wchar_t b[24];
    std::swprintf(b, 24, L"%u:%04.1f", ms / 60000, (ms % 60000) / 1000.0);
    return b;
}
// The text of an action as a single line (the game's color and control bytes are left out).
std::wstring sdlBriefPlain(const std::string& s) {
    std::string out;
    for (char ch : s)
        if (static_cast<unsigned char>(ch) >= 0x20) out += ch;
    return widen(out);
}
}  // namespace

struct SdlBriefingView final : draw::View {
    std::array<bool, 27> who{};          // the briefing shown: its player list
    bool chosen = false;                  // a briefing is chosen (else the first one is, when the view paints)
    const Editor* shownFor = nullptr;     // the map the choice was made for (a map change starts over)
    int player = 0;                       // whose briefing the preview plays
    int sel = -1;                         // the focused action: index into the rows of the briefing shown
    std::set<int> picked;                 // the selected rows
    int anchor = -1;                      // where a Shift+click range starts
    int listScroll = 0;                   // the first action row shown (rows)
    uint32_t t = 0, total = 0;            // the preview's time and its length (ms)
    bool playing = false;
    uint32_t lastTicks = 0;
    int timerId = 0;
    std::vector<BriefingEvent> timeline;  // the preview's events, as the map is now
    uint64_t timelineRev = ~0ull;
    int timelinePlayer = -1;
    std::array<int, 8> timelineForces{};
    struct Voice {
        int trig = 0, act = 0;
        uint32_t end = 0;
        SDL_AudioStream* stream = nullptr;
    };
    std::vector<Voice> voices;
    std::set<std::pair<int, int>> started;  // the sounds that started (or were passed) in this run of the preview

    // The layout (from the view's size): the briefings, the actions, the preview; the timeline and the Play row under it.
    int listW() const { return scale(190); }
    int actionsLeft() const { return listW(); }
    int actionsRight() const { return listW() + (width() - listW()) * 45 / 100; }
    int headH() const { return scale(30); }
    int rowH() const { return scale(22); }
    draw::Rect wireRect() const {
        const int pad = scale(10);
        const int x0 = actionsRight() + pad, avail = width() - x0 - pad;
        const int maxH = height() - scale(64) - pad * 2;
        int w = avail, h = w * 3 / 4;
        if (h > maxH) h = std::max(0, maxH), w = h * 4 / 3;
        return {x0, pad + headH(), x0 + w, pad + headH() + h};
    }
    draw::Rect timelineRect() const {
        const draw::Rect w = wireRect();
        return {w.left, w.bottom + scale(10), w.right, w.bottom + scale(10) + scale(14)};
    }
    draw::Rect playRect() const {
        const draw::Rect tl = timelineRect();
        return {tl.left, tl.bottom + scale(10), tl.left + scale(90), tl.bottom + scale(10) + scale(28)};
    }

    void refresh() {  // the timeline of the player shown, when the map or the player changed
        briefRows(who);  // the rows of the briefing shown (gBriefRows)
        const Editor* ed = shellState().ed.get();
        const uint64_t rev = ed ? ed->revision() : 0;
        const auto forces = playerForces();
        if (rev == timelineRev && player == timelinePlayer && forces == timelineForces) return;
        timelineRev = rev, timelinePlayer = player, timelineForces = forces;
        timeline = briefingTimeline(briefings(), player, forces, &total);
        if (t > total) t = total;
    }
    void choose(const std::array<bool, 27>& w) {  // a briefing shown: its first player the preview plays (a human or computer)
        stop();
        who = w, chosen = true, sel = -1, picked.clear(), anchor = -1, listScroll = 0, t = 0;
        const auto ps = whoPlayers(who);
        player = ps.empty() ? 0 : ps[0];
        for (int p : ps)
            if (shellState().ed && (shellState().ed->player(p).controller == Editor::Controller::Human || shellState().ed->player(p).controller == Editor::Controller::Computer)) {
                player = p;
                break;
            }
        timelineRev = ~0ull;
    }
    // A map opened or switched since the last look: the playback stops, and the first briefing of that map is shown
    // (the Win32 page's gBriefShownFor rule). Runs before every paint and click.
    void ensureWho() {
        const Editor* ed = shellState().ed.get();
        if (chosen && ed == shownFor) return;
        if (ed != shownFor) stop(), timeline.clear(), timelineRev = ~0ull, total = 0, t = 0;
        shownFor = ed;
        const auto list = briefList();
        if (!list.empty()) return choose(list.front());
        std::array<bool, 27> first{};
        first[0] = true;
        choose(first);
    }
    int rowCount() const { return int(gBriefRows.size()); }
    uint32_t startOf(int trig, int act) const {
        for (const BriefingEvent& e : timeline)
            if (e.trigger == trig && e.action == act) return e.at;
        return 0;
    }

    // --- Playing ----------------------------------------------------------------------------------------------------

    void stopSounds() {
        for (auto& v : voices)
            if (v.stream) SDL_DestroyAudioStream(v.stream);
        voices.clear();
    }
    void stop() {
        if (timerId && host()) host()->cancelTimer(timerId);
        timerId = 0, playing = false;
        stopSounds();
    }
    // The sounds that come due at the current time start (the ones passed before a seek or a start do not).
    void syncSounds() {
        for (size_t i = 0; i < timeline.size(); ++i) {
            const BriefingEvent& e = timeline[i];
            if (!e.a || e.a->wav.empty() || (e.a->type != BriefingAction::PlayWav && e.a->type != BriefingAction::Transmission)) continue;
            const std::pair<int, int> key{e.trigger, e.action};
            if (started.count(key) || t < e.at) continue;
            started.insert(key);
            const SdlSound* d = sdlSound(e.a->wav);
            uint32_t len = d ? d->ms : e.length;
            if (!d || t >= e.at + len) continue;  // (a sound that is over, or not found, does not start)
            SDL_AudioStream* s = openPlaybackSdl(&d->spec);
            if (!s) continue;
            SDL_PutAudioStreamData(s, d->pcm.data(), int(d->pcm.size()));
            SDL_FlushAudioStream(s);
            SDL_ResumeAudioStreamDevice(s);
            voices.push_back({e.trigger, e.action, e.at + len, s});
        }
        std::erase_if(voices, [&](const Voice& v) {
            if (t < v.end) return false;
            if (v.stream) SDL_DestroyAudioStream(v.stream);
            return true;
        });
    }
    void tick() {
        timerId = 0;
        const uint32_t now = SDL_GetTicks();
        if (playing) t += now - lastTicks;
        lastTicks = now;
        if (t >= total) t = total, playing = false, stopSounds();
        if (playing) syncSounds();
        invalidate();
        if (playing) schedule();
    }
    void schedule() {
        if (host()) timerId = host()->timer(30, [this] { tick(); });
    }
    void setPlaying(bool on) {
        if (on && t >= total) t = 0;
        stop();
        if (!on) return invalidate();
        playing = true, lastTicks = SDL_GetTicks();
        started.clear();
        for (const BriefingEvent& e : timeline)  // the sounds before the start are passed: they do not start
            if (e.at < t) started.insert({e.trigger, e.action});
        syncSounds();
        schedule();
        invalidate();
    }
    void seek(uint32_t to) {
        stop();
        t = std::min(to, total), lastTicks = SDL_GetTicks();
        started.clear();
        for (const BriefingEvent& e : timeline)
            if (e.at < t) started.insert({e.trigger, e.action});
        invalidate();
    }

    // --- Painting ---------------------------------------------------------------------------------------------------

    void paint(draw::Canvas& c) override {
        ensureWho();
        refresh();
        c.fill({0, 0, width(), height()}, kBg);
        paintList(c);
        paintActions(c);
        paintPreview(c);
    }
    void paintList(draw::Canvas& c) {
        c.fill({0, 0, actionsLeft(), height()}, kPanel);
        c.text({scale(10), 0, actionsLeft() - scale(10), headH()}, tr(L"Briefings"), kDim, draw::kMiddle);
        const auto list = briefList();
        for (size_t i = 0; i < list.size(); ++i) {
            const draw::Rect r{0, headH() + int(i) * rowH(), actionsLeft(), headH() + int(i + 1) * rowH()};
            const bool on = list[i] == who;
            if (on) c.fill(r, kSel);
            const std::wstring label = whoName(list[i]) + L"  (" + std::to_wstring(briefActionCount(list[i])) + L")";
            c.text({scale(10), r.top, r.right - scale(6), r.bottom}, label, on ? kText : kDim, draw::kMiddle | draw::kEllipsis);
        }
    }
    void paintActions(draw::Canvas& c) {
        const draw::Rect area{actionsLeft(), 0, actionsRight(), height()};
        c.fill(area, kBg);
        c.text({area.left + scale(10), 0, area.right - scale(10), headH()}, tr(L"Actions of ") + whoName(who), kDim, draw::kMiddle | draw::kEllipsis);
        const int visible = std::max(0, (height() - headH()) / rowH());
        const auto& b = briefings();
        for (int i = listScroll; i < rowCount() && i < listScroll + visible + 1; ++i) {
            const int top = headH() + (i - listScroll) * rowH();
            const draw::Rect r{area.left, top, area.right, top + rowH()};
            const BriefRow& row = gBriefRows[size_t(i)];
            const bool on = picked.count(i) != 0 || i == sel;
            if (on) c.fill(r, kRowHot);
            const BriefingAction& a = b[size_t(row.trig)].actions[size_t(row.act)];
            const std::wstring text = sdlBriefTime(startOf(row.trig, row.act)) + L"   " + sdlBriefTypeName(a.type);
            c.text({r.left + scale(10), r.top, r.left + scale(190), r.bottom}, text, a.disabled() ? kFaint : kText, draw::kMiddle | draw::kEllipsis);
            std::wstring summary = a.type == BriefingAction::Wait || a.type == BriefingAction::SkipTutorial ? L"" : sdlBriefPlain(a.text.empty() ? a.wav : a.text);
            c.text({r.left + scale(200), r.top, r.right - scale(10), r.bottom}, summary, kDim, draw::kMiddle | draw::kEllipsis);
        }
    }
    void paintPreview(draw::Canvas& c) {
        const draw::Rect w = wireRect();
        c.fill({actionsRight(), 0, width(), height()}, kPanel);
        c.fill(w, draw::rgb(10, 11, 13));
        c.frame(w, kBorder);
        const BriefingState s = briefingStateAt(timeline, t);
        const double k = (w.width()) / 640.0;
        auto at = [&](int x0, int y0, int x1, int y1) {
            return draw::Rect{w.left + int(x0 * k), w.top + int(y0 * k), w.left + int(x1 * k), w.top + int(y1 * k)};
        };
        // Portrait slots (the pictures are not on this system yet: frames, the talking one in red).
        static const draw::Rect slots[4] = {{38, 102, 157, 209}, {189, 30, 308, 137}, {340, 30, 461, 137}, {483, 102, 603, 209}};
        for (int i = 0; i < 4; ++i) {
            const draw::Rect f = at(slots[i].left, slots[i].top, slots[i].right, slots[i].bottom);
            const auto& p = s.portraits[size_t(i)];
            const bool talking = p.shown && p.speaking;
            c.fill(f, draw::rgb(18, 19, 22));
            c.frame(f, talking ? draw::rgb(230, 40, 30) : draw::rgb(84, 58, 54), talking ? 2 : 1);
        }
        const std::wstring objectives = sdlBriefPlain(s.objectives);
        const std::wstring message = sdlBriefPlain(s.text);
        const int px = std::max(scale(10), int(14 * k));
        const draw::Font oldFont = c.setFont(c.font({draw::Face::Ui, px}));
        if (!objectives.empty()) c.text(at(40, 300, 300, 470), objectives, draw::rgb(196, 204, 255), draw::kWrap | draw::kBottom);
        if (!message.empty()) c.text(at(180, 230, 460, 300), message, draw::rgb(196, 204, 255), draw::kWrap | draw::kMiddle);
        c.setFont(oldFont);
        // The timeline: the time played.
        const draw::Rect tl = timelineRect();
        c.fill(tl, kCtl);
        if (total) c.fill({tl.left, tl.top, tl.left + int(uint64_t(tl.width()) * t / total), tl.bottom}, kAccent);
        c.frame(tl, kBorder);
        // Play, and the time.
        const draw::Rect pr = playRect();
        c.roundBox(pr, kCtl, kBorder, float(scale(4)));
        c.text(pr, playing ? tr(L"Pause") : tr(L"Play"), kText, draw::kCenter | draw::kMiddle);
        c.text({pr.right + scale(10), pr.top, tl.right, pr.bottom}, sdlBriefTime(t) + L" / " + sdlBriefTime(total), kDim, draw::kMiddle);
    }

    // --- Input ------------------------------------------------------------------------------------------------------

    bool mouse(const draw::MouseEvent& e) override {
        ensureWho();
        refresh();
        if (e.kind == draw::MouseKind::Wheel) {
            if (e.pt.x >= actionsLeft() && e.pt.x < actionsRight()) {
                listScroll = std::clamp(listScroll - int(e.wheel * 3), 0, std::max(0, rowCount() - 1));
                invalidate();
                return true;
            }
            return false;
        }
        if (e.kind != draw::MouseKind::Down || e.button != 0) return e.kind == draw::MouseKind::Leave;
        const draw::Point p = e.pt;
        if (p.x < actionsLeft()) {  // the briefings
            const auto list = briefList();
            const int i = (p.y - headH()) / rowH();
            if (p.y >= headH() && i >= 0 && i < int(list.size())) choose(list[size_t(i)]), invalidate();
            return true;
        }
        if (p.x < actionsRight()) {  // the actions: a click selects, Shift extends, Ctrl toggles
            if (p.y < headH()) return true;
            const int i = listScroll + (p.y - headH()) / rowH();
            if (i < 0 || i >= rowCount()) return true;
            if (e.mods & draw::kShift) {
                const int from = anchor >= 0 ? anchor : i;
                picked.clear();
                for (int r = std::min(from, i); r <= std::max(from, i); ++r) picked.insert(r);
            } else if (e.mods & draw::kCtrl) {
                if (!picked.erase(i)) picked.insert(i);
                anchor = i;
            } else {
                picked = {i};
                anchor = i;
            }
            sel = i;
            invalidate();
            return true;
        }
        const draw::Rect tl = timelineRect(), pr = playRect();
        if (pr.contains(p)) return setPlaying(!playing), true;
        if (tl.contains(p) && total) return seek(uint32_t(uint64_t(p.x - tl.left) * total / std::max(1, tl.width()))), true;
        return false;
    }
    draw::Cursor cursor(draw::Point p) override {
        if (p.x < actionsRight() && p.x >= actionsLeft()) return draw::Cursor::Arrow;
        if (playRect().contains(p) || (timelineRect().contains(p) && total)) return draw::Cursor::Hand;
        return draw::Cursor::Arrow;
    }
    bool focusable() const override { return false; }
};

SdlBriefingView gSdlBriefing;

// The Briefing workspace is left: the preview stops (setWorkspace calls it).
void briefingStopSdl() { gSdlBriefing.stop(); }
