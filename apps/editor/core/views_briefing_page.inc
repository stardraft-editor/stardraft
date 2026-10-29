// The Briefing workspace's page as a view (portable, core.inc): the SDL program shows it (sdl_briefing.inc is its host);
// the Windows program still has its own window (briefing_view.inc), with the same layout, sizes and texts, made from
// the same model (core/views_briefing.inc).
//  Left: which briefing (a dropdown of the map's briefings), New briefing and Delete, and the players it is for.
//  Middle: its actions in order, each a block with its start time and its sentence, the values as chips; the selected
//  action's text in the game text editor (core/views_game_text.inc) under the list. Actions are selected (Ctrl, Shift),
//  dragged, copied, cut, pasted, duplicated, turned off and moved (Alt+Up/Down).
//  Right: the briefing window as a wireframe at the preview's time (the portraits, the objectives, the message), Go to
//  the start, Play and the time, and the timeline to seek.

// What the page needs of the program.
struct BriefingPageHost {
    virtual ~BriefingPageHost() = default;
    virtual uint32_t soundLength(const std::string& name) = 0;  // ms; 0 when the sound is not found (it is decoded once)
    // A sound starts playing `fromMs` into it; returns its voice (0: none plays, as in a test run).
    virtual int startSound(const std::string& name, uint32_t fromMs) = 0;
    virtual void stopSound(int voice) = 0;
    // A list picker at `at` (in the coordinates of `from`).
    virtual bool pickOne(const std::wstring& title, std::vector<ListPickItem> items, const std::string& current, const draw::View& from, draw::Point at,
                         std::string& out) = 0;
    // A sound of the map or of the game (the map's, an import, a game sound).
    virtual bool pickSound(const draw::View& from, draw::Point at, const std::string& current, std::string& out) = 0;
    // A time in seconds, typed under `field` (a part of `from`); out in ms.
    virtual bool editSeconds(draw::View& from, draw::Rect field, const std::wstring& title, uint32_t current, uint32_t& out) = 0;
    // A question with the action's button and Cancel: true when the action is chosen.
    virtual bool confirm(const std::wstring& title, const std::wstring& text, const std::wstring& yes) = 0;
    // A unit type's command icon (nullptr: none).
    virtual draw::Bitmap* unitIcon(int unit) = 0;
};

class BriefingPageView final : public draw::View {
public:
    explicit BriefingPageView(BriefingPageHost& h) : host_(h), text_(textSpec()) {
        add(text_, {0, 0, 0, 0});
        text_.setVisible(false);
        text_.setColors(draw::rgb(184, 184, 232), draw::rgb(0x11, 0x11, 0x11));  // (main.cpp's kRichDefault and kRichBack)
        text_.onChange = [this] { textDirty_ = true, previewText(); };  // the preview as it is typed
        text_.onFocusLost = [this] { commitText(); };                     // one undo step
    }
    ~BriefingPageView() override { stop(); }

    // The selected action's text: a game text editor in the briefing's colors (the line ends as the text has them).
    static GameTextSpec textSpec() {
        GameTextSpec s;
        s.palette = TextPalette::Scenario, s.multiline = true, s.align = true;
        return s;
    }

    // --- The state ---------------------------------------------------------------------------------------------------
    std::array<bool, 27> who{};       // the briefing shown: its triggers' player list (0-7 players, 17 all, 18-21 forces)
    bool chosen = false;              // a briefing is chosen (else the first one is, when the view paints)
    int player = 0;                   // whose briefing the preview plays (one it is for)
    int music = -1;                   // the preview's music: -1 the player's race's (as in the game), 0-2 a race's, 3 none
    int sel = -1;                    // the focused action: index into the rows (its text shows)
    std::set<int> picked;             // the selected rows
    int anchor = -1;                  // where a Shift+click range starts
    bool playing = false;
    uint32_t t = 0;                   // the preview's time (ms)
    int listScroll = 0;               // the first action row shown
    float wheelCarry_ = 0;            // an exact scroll's part of a row (draw::exactSteps)
    std::vector<BriefingAction> clip;  // copied actions

    GameTextView& textBox() { return text_; }
    uint32_t total() const { return total_; }

    // The briefing shown changes (the selection, the scroll and the time start again).
    void setWho(const std::array<bool, 27>& w) {
        shownFor_ = shellState().ed.get();
        who = w, chosen = true;
        sel = -1, picked.clear(), anchor = -1, textFor_ = -1, listScroll = 0;
        player = briefPreviewPlayer(w);
        t = 0;
    }
    // An action of the map's briefings shows selected (the Files page's use of a sound).
    void showAction(int trig, int act) {
        if (trig < 0 || trig >= int(briefings().size())) return;
        commitText();
        setWho(briefings()[size_t(trig)].players);
        briefRows(who);
        for (size_t i = 0; i < gBriefRows.size(); ++i)
            if (gBriefRows[i].trig == trig && gBriefRows[i].act == act) selectRange(int(i), 1);
        invalidate();
    }

    // Edit's commands (the text box keeps its own copy and paste).
    bool command(int id) {
        if (text_.hasFocusInside()) {
            // The box's own keys do it (as the menu's items send the native box its messages on Windows).
            draw::View& box = text_.isRaw() ? static_cast<draw::View&>(text_.rawBox()) : text_.preview();
            auto press = [&box](draw::Key k, unsigned mods) { box.key({k, true, false, mods}); };
            switch (id) {
            case ID_EDIT_COPY: press(draw::Key('C'), draw::kCtrl); return true;
            case ID_EDIT_CUT: press(draw::Key('X'), draw::kCtrl); return true;
            case ID_EDIT_PASTE: press(draw::Key('V'), draw::kCtrl); return true;
            case ID_EDIT_DELETE: press(draw::Key::Delete, 0); return true;
            case ID_EDIT_SELECTALL: press(draw::Key('A'), draw::kCtrl); return true;
            }
            return false;
        }
        switch (id) {
        case ID_EDIT_COPY: copy(false); return true;
        case ID_EDIT_CUT: copy(true); return true;
        case ID_EDIT_PASTE: paste(); return true;
        case ID_EDIT_DUPLICATE: duplicate(); return true;
        case ID_EDIT_DELETE: deleteRows(); return true;
        case ID_EDIT_SELECTALL: selectRange(0, int(gBriefRows.size())), invalidate(); return true;
        }
        return false;
    }

    // --- Playing -----------------------------------------------------------------------------------------------------
    void play(bool on) {
        if (on && t >= total_) t = 0;
        if (on) {  // every sound decoded before the clock starts, so none starts late
            for (const BriefingEvent& e : timeline_)
                if (isSound(e)) host_.soundLength(e.a->wav);
            if (const std::string m = musicFile(); !m.empty()) host_.soundLength(m);
        }
        playing = on;
        last_ = nowMs();
        cancelTick();
        if (on) scheduleTick();
        syncSounds(true);
        invalidate();
    }
    void seek(uint32_t to) {
        t = std::min(to, total_);
        last_ = nowMs();
        syncSounds(true);
        invalidate();
    }
    // The program quits: the sounds and the music stop (their streams go before SDL's audio does).
    void silence() { playing = false, stopSounds(); }
    // The workspace is left: the preview stops (the text that was typed goes in first).
    void stop() {
        commitText();
        cancelTick();
        playing = false;
        stopSounds();
    }

    // The text being typed goes into the map now (one undo step): a Save, a quit or a tab switch while the box has the
    // keyboard keeps it.
    void commitTyping() { commitText(); }

    // The portraits' pictures go (the SDL program: their textures before the GPU device goes).
    void releasePictures() { portraits_.clear(); }

    // --- The view ------------------------------------------------------------------------------------------------------
    bool focusable() const override { return true; }

    void paint(draw::Canvas& c) override {
        hits_.clear(), wheels_.clear();
        ensureWho();
        timeline_ = briefingTimeline(briefings(), player, playerForces(), &total_);
        const auto& rows = briefRows(who);
        if (sel >= int(rows.size())) sel = int(rows.size()) - 1;
        std::erase_if(picked, [&](int r) { return r >= int(rows.size()); });
        c.fill({0, 0, width(), height()}, kBg);
        paintSide(c);
        paintActions(c);
        paintRight(c);
    }

    bool mouse(const draw::MouseEvent& e) override {
        mouse_ = e.pt;
        if (e.kind == draw::MouseKind::Wheel) {
            // Over a value of a list: the item before or after. (Not while the wheel scrolls the list: a chip that the
            // scroll brings under the still mouse is not changed; the mouse moves first.)
            for (auto it = wheels_.rbegin(); it != wheels_.rend() && !listWheeling_; ++it)
                if (it->first.contains(e.pt)) {
                    const int steps = e.exact ? draw::exactSteps(e, float(scale(26)), valueWheelCarry_) : int(e.wheel);
                    if (steps) it->second(steps > 0 ? -1 : 1);
                    return true;
                }
            const int rows = e.exact ? draw::exactSteps(e, float(scale(26)), wheelCarry_) : int(e.wheel) * 2;  // (exact: macOS's trackpad)
            if (rows < 0 && allShown_) return true;
            listScroll = std::max(0, listScroll - rows);
            if (rows) listWheeling_ = true, wheelAt_ = e.pt;
            invalidate();
            return true;
        }
        if (e.kind == draw::MouseKind::Leave) {
            over_ = {};
            invalidate();
            return true;
        }
        if (e.kind == draw::MouseKind::Move) {
            if (listWheeling_ && (e.pt.x != wheelAt_.x || e.pt.y != wheelAt_.y)) listWheeling_ = false;
            if (grab_.down && grab_.drag) {
                if (!grab_.moved && grab_.click) grab_.click();  // a row: select it first, then drag what is selected
                grab_.moved = true, grab_.drag(e.pt);
                return true;
            }
            const Hit* h = hitAt(e.pt, false);
            const draw::Rect part = h ? h->r : draw::Rect{};
            if (part.left != over_.left || part.top != over_.top || part.right != over_.right || part.bottom != over_.bottom) over_ = part, invalidate();
            return true;
        }
        if ((e.kind == draw::MouseKind::Down || e.kind == draw::MouseKind::DoubleClick) && e.button == 0) {
            if (!hasFocus()) focus();
            mods_ = e.mods;
            const Hit* h = hitAt(e.pt, true);
            if (!h) return true;
            grab_ = {h->drag, h->click, true, false};
            dragFrom_ = dragTo_ = -1;
            if (host()) host()->capture(*this, true);
            if (grab_.drag && !grab_.click) grab_.moved = true, grab_.drag(e.pt);
            return true;
        }
        if (e.kind == draw::MouseKind::Up && e.button == 0 && grab_.down) {
            // The press's state first: letting go of the mouse calls captureLost, which forgets it.
            auto click = grab_.click;
            const bool moved = grab_.moved;
            const int from = dragFrom_, to = dragTo_;
            grab_ = {};
            dragFrom_ = dragTo_ = -1;
            if (host()) host()->capture(*this, false);
            if (from >= 0) {  // rows dropped: the selected ones (or the one dragged) go there
                if (!picked.count(from)) picked = {from}, sel = from;
                if (to >= 0) moveTo(to);
                invalidate();
            } else if (!moved && click) {
                click();
            }
            return true;
        }
        return false;
    }
    void captureLost() override { grab_ = {}, dragFrom_ = dragTo_ = -1; }

    bool key(const draw::KeyEvent& e) override {
        if (!e.down) return false;
        if ((e.mods & draw::kAlt) && (e.key == draw::Key::Up || e.key == draw::Key::Down)) return nudge(e.key == draw::Key::Up ? -1 : 1), true;
        if (e.mods & (draw::kCtrl | draw::kAlt | draw::kSuper)) return false;
        if (e.key == draw::Key::Space) return play(!playing), true;
        if (e.key == draw::Key::Delete) return deleteRows(), true;
        if ((e.key == draw::Key::Up || e.key == draw::Key::Down) && !gBriefRows.empty()) {
            const int n = std::clamp(sel + (e.key == draw::Key::Up ? -1 : 1), 0, int(gBriefRows.size()) - 1);
            sel = n, picked = {n}, anchor = n, textFor_ = -1, followSel_ = true;
            invalidate();
            return true;
        }
        return false;
    }
    draw::Cursor cursor(draw::Point pt) override {
        const Hit* h = hitAt(pt, false);
        return h && h->hand ? draw::Cursor::Hand : draw::Cursor::Arrow;
    }
    std::wstring tip(draw::Point pt) override {
        const Hit* h = hitAt(pt, false);
        return h ? h->tip : std::wstring();
    }

private:
    // A part of the last paint the mouse can use: a click, a drag (with the mouse's place), its tooltip.
    struct Hit {
        draw::Rect r;
        std::function<void(draw::Point)> drag;
        std::function<void()> click;
        std::wstring tip;
        bool hand = false;
    };
    const Hit* hitAt(draw::Point p, bool active) const {
        for (auto it = hits_.rbegin(); it != hits_.rend(); ++it)
            if (it->r.contains(p) && (!active || it->drag || it->click)) return &*it;
        return nullptr;
    }
    bool hot(draw::Rect r) const { return r.contains(mouse_); }
    void hit(draw::Rect r, std::function<void()> click, const std::wstring& tip = L"", std::function<void(draw::Point)> drag = nullptr) {
        const bool hand = click || drag;
        hits_.push_back({r, std::move(drag), std::move(click), tip, hand});
    }
    static uint64_t nowMs() {
        return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    // --- The model's edits ---------------------------------------------------------------------------------------------
    // A map opened (or changed) since the last look: its first briefing (none: a new one for player 1).
    void ensureWho() {
        const Editor* ed = shellState().ed.get();
        if (chosen && ed == shownFor_) return;
        if (ed != shownFor_) cancelTick(), playing = false, stopSounds(), textDirty_ = false;
        setWho(briefFirstWho());
    }
    // A change: the map's briefings with `edit` applied (one undo step).
    void edit(const std::function<void(std::vector<BriefingTrigger>&)>& f) {
        if (!shellState().ed) return;
        std::vector<BriefingTrigger> b = briefings();
        f(b);
        shellState().ed->setBriefings(b);
        invalidate();
        updateTitle();
    }
    bool selected(int& trig, int& act) const {
        if (sel < 0 || sel >= int(gBriefRows.size())) return false;
        trig = gBriefRows[size_t(sel)].trig, act = gBriefRows[size_t(sel)].act;
        return true;
    }
    void editSelected(const std::function<void(BriefingAction&)>& f) {
        int ti, ai;
        if (!selected(ti, ai)) return;
        edit([&](std::vector<BriefingTrigger>& b) {
            if (size_t(ti) < b.size() && size_t(ai) < b[size_t(ti)].actions.size()) f(b[size_t(ti)].actions[size_t(ai)]);
        });
    }
    std::vector<int> pickedRows() const { return briefPickedRowsOf(picked, sel); }
    void selectRange(int first, int count) {
        picked.clear();
        for (int i = 0; i < count; ++i) picked.insert(first + i);
        sel = count ? first : -1, anchor = first, textFor_ = -1;
    }
    void copy(bool cut) {
        const auto rows = pickedRows();
        if (rows.empty()) return;
        clip = briefActionsOf(rows);
        if (!cut) return setMessage(L"Copied " + std::to_wstring(rows.size()) + L" actions");
        edit([&](std::vector<BriefingTrigger>& b) { briefEraseRows(b, who, rows); });
        selectRange(std::min(rows.front(), std::max(0, int(gBriefRows.size()) - int(rows.size()) - 1)), 0);
        setMessage(L"Cut " + std::to_wstring(rows.size()) + L" actions");
    }
    void insert(int at, const std::vector<BriefingAction>& acts) {
        if (acts.empty()) return;
        if (std::none_of(who.begin(), who.end(), [](bool v) { return v; })) return setMessage(L"First, select the players of the briefing");
        edit([&](std::vector<BriefingTrigger>& b) { at = briefInsertRows(b, who, at, acts); });
        briefRows(who);
        selectRange(at, int(acts.size()));
    }
    void paste() {
        const auto rows = pickedRows();
        insert(rows.empty() ? int(gBriefRows.size()) : rows.back() + 1, clip);
    }
    void duplicate() {
        const auto rows = pickedRows();
        if (!rows.empty()) insert(rows.back() + 1, briefActionsOf(rows));
    }
    void deleteRows() {
        const auto rows = pickedRows();
        if (rows.empty()) return;
        edit([&](std::vector<BriefingTrigger>& b) { briefEraseRows(b, who, rows); });
        picked.clear(), sel = -1, textFor_ = -1;
    }
    void moveTo(int to) {
        const auto rows = pickedRows();
        if (rows.empty()) return;
        int first = 0;
        edit([&](std::vector<BriefingTrigger>& b) { first = briefMoveRows(b, who, rows, to); });
        briefRows(who);
        selectRange(first, int(rows.size()));
    }
    void nudge(int delta) {
        const auto rows = pickedRows();
        if (!rows.empty()) moveTo(briefNudgeTo(rows, delta));
    }
    // A click on row i: select (Ctrl adds, Shift a range), and the preview goes to its start (`at`; UINT32_MAX: a chip's
    // click, the preview stays and the selection too when the row is in it).
    void clickRow(int i, uint32_t at) {
        commitText();
        const bool chip = at == UINT32_MAX;
        if (!chip && (mods_ & draw::kShift) && anchor >= 0) {
            picked.clear();
            for (int k = std::min(anchor, i); k <= std::max(anchor, i); ++k) picked.insert(k);
        } else if (!chip && (mods_ & draw::kCtrl)) {
            if (!picked.erase(i)) picked.insert(i);
            anchor = i;
        } else if (!picked.count(i) || !chip) {
            picked = {i}, anchor = i;
        }
        if (sel != i) textFor_ = -1;
        sel = i;
        if (!chip && !playing) seek(at);
        invalidate();
    }
    // The text box's text goes in (one undo step), once it changed.
    void commitText() {
        if (!textDirty_) return;
        textDirty_ = false;
        const std::string now = text_.bytes();
        editSelected([&](BriefingAction& x) { x.text = now; });
    }
    // While typing: the preview shows the text (in the shown copy only; it becomes one undo step when the box loses the
    // keyboard).
    void previewText() {
        int ti, ai;
        if (!textDirty_ || !selected(ti, ai) || size_t(ti) >= gBriefData.size() || size_t(ai) >= gBriefData[size_t(ti)].actions.size()) return;
        BriefingAction& x = gBriefData[size_t(ti)].actions[size_t(ai)];
        const std::string now = text_.bytes();
        if (now == x.text) return;
        x.text = now;
        invalidate();
    }
    // A check box of "For": the briefing's triggers are for these players now.
    void toggleWho(int k) {
        auto now = who;
        now[size_t(k)] = !now[size_t(k)];
        const auto before = who;
        const auto list = briefList();
        if (std::find(list.begin(), list.end(), now) != list.end() && briefActionCount(before))
            setMessage(L"A briefing for these players already exists. The actions of both briefings are now in the existing briefing.");
        if (briefActionCount(before)) edit([&](std::vector<BriefingTrigger>& b) { briefRetarget(b, before, now); });
        const int keep = sel;
        setWho(now);
        sel = keep;
        invalidate();
    }

    // --- Sounds --------------------------------------------------------------------------------------------------------
    struct Voice {
        const BriefingAction* a = nullptr;
        uint32_t at = 0;
        int id = 0;
    };
    static bool isSound(const BriefingEvent& e) {
        return e.a && !e.a->wav.empty() && (e.a->type == BriefingAction::PlayWav || e.a->type == BriefingAction::Transmission);
    }
    void stopVoices() {  // (the actions' sounds; the music goes on)
        for (const Voice& v : voices_)
            if (v.id) host_.stopSound(v.id);
        voices_.clear();
    }
    void stopSounds() {
        stopVoices();
        if (musicVoice_) host_.stopSound(musicVoice_);
        musicVoice_ = 0, musicPlays_.clear();
    }
    // The race whose briefing music plays (0 Zerg, 1 Terran, 2 Protoss): the chosen one, else the player's (the game's
    // choice; Terran for a race the player selects or a random one). -1: none.
    int musicRace() const {
        if (music >= 0) return music < 3 ? music : -1;
        const auto& ed = shellState().ed;
        const Editor::Race r = ed && player >= 0 && player < 8 ? ed->player(player).race : Editor::Race::Terran;
        return r == Editor::Race::Zerg ? 0 : r == Editor::Race::Protoss ? 2 : 1;
    }
    std::string musicFile() const {
        static const char* const kFiles[3] = {"music\\zrdyroom.wav", "music\\trdyroom.wav", "music\\prdyroom.wav"};
        const int r = musicRace();
        return r < 0 ? std::string() : kFiles[r];
    }
    // The music under the briefing, from the start again at its end (as the game plays it), from where it is at time t.
    // A seek starts it again at its new place; while the timeline is dragged, at most each quarter second (each start
    // queues the rest of the track), and a tick puts it in its place after the drag.
    void syncMusic(bool restart) {
        const std::string f = playing && total_ && shellState().ed ? musicFile() : std::string();  // (no music for a briefing of no length)
        const uint32_t len = f.empty() ? 0 : host_.soundLength(f);
        const uint32_t round = len ? t / len : 0;
        const uint64_t now = nowMs();
        if (len && f == musicPlays_ && round == musicRound_) {
            const int64_t off = int64_t(musicFrom_) + int64_t(now - musicSince_) - int64_t(t % len);  // (how far the music is from t)
            if ((off > -300 && off < 300) || now - musicSince_ < 250) return;
        }
        if (musicVoice_) host_.stopSound(musicVoice_);
        musicVoice_ = 0, musicPlays_.clear();
        if (!len) return;
        musicPlays_ = f, musicRound_ = round, musicFrom_ = t % len, musicSince_ = now;
        musicVoice_ = host_.startSound(f, musicFrom_);
    }
    // The sounds that play at the current time, started (or kept) at their place: each for as long as its file is (the
    // action's own length is often 0 or rounded). After a seek (restart), a sound starts from where it is at that time.
    void syncSounds(bool restart) {
        if (restart) stopVoices();
        if (!playing || !shellState().ed) return stopSounds();
        std::vector<const BriefingEvent*> now;
        for (const BriefingEvent& e : timeline_) {
            if (!isSound(e)) continue;
            uint32_t len = host_.soundLength(e.a->wav);
            if (!len) len = e.length;
            if (t >= e.at && t < e.at + len) now.push_back(&e);
        }
        for (auto it = voices_.begin(); it != voices_.end();) {
            const bool still = std::any_of(now.begin(), now.end(), [&](const BriefingEvent* e) { return e->a == it->a && e->at == it->at; });
            if (!still) {
                if (it->id) host_.stopSound(it->id);
                it = voices_.erase(it);
            } else {
                ++it;
            }
        }
        for (const BriefingEvent* e : now) {
            if (std::any_of(voices_.begin(), voices_.end(), [&](const Voice& v) { return v.a == e->a && v.at == e->at; })) continue;
            voices_.push_back({e->a, e->at, host_.startSound(e->a->wav, restart ? t - e->at : 0)});
        }
        syncMusic(restart);
    }
    void scheduleTick() {
        if (host()) tickId_ = host()->timer(30, [this] { tick(); });
    }
    void cancelTick() {
        if (tickId_ && host()) host()->cancelTimer(tickId_);
        tickId_ = 0;
    }
    void tick() {
        tickId_ = 0;
        if (!playing) return;
        const uint64_t now = nowMs();
        // At most 100 ms a tick: when the program stops for a moment, the briefing waits rather than jumps.
        t += uint32_t(std::min<uint64_t>(now - last_, 100)), last_ = now;
        if (t >= total_) {
            t = total_;
            play(false);
            return;
        }
        syncSounds(false);
        invalidate();
        scheduleTick();
    }

    // --- Painting: the kit's parts with their click areas (as ui_kit.inc's on Windows) ------------------------------
    int smallButton(draw::Canvas& c, int x, int y, int h, const std::wstring& text, std::function<void()> click, const std::wstring& tip,
                    kitpaint::Tone tone = kitpaint::Tone::Normal) {
        const draw::Rect r = kitpaint::smallButtonRect(c, x, y, h, text);
        kitpaint::drawButtonFace(c, r, text, tone == kitpaint::Tone::Primary ? kitpaint::Tone::Normal : tone,
                                 hot(r) ? kitpaint::ButtonState::Hot : kitpaint::ButtonState::Normal);
        hit(r, std::move(click), tip);
        return r.right;
    }
    void check(draw::Canvas& c, draw::Rect r, const std::wstring& label, bool on, std::function<void()> click, const std::wstring& tip = L"") {
        const int s = scale(14);
        const draw::Rect b{r.left, (r.top + r.bottom - s) / 2, r.left + s, (r.top + r.bottom - s) / 2 + s};
        const bool over = hot(r);
        kitpaint::drawCheckMark(c, b, on, over, true);
        c.text({b.right + scale(8), r.top, r.right, r.bottom}, label, on || over ? kText : kDim, draw::kMiddle | draw::kEllipsis);
        hit(r, std::move(click), tip);
    }
    void pickBox(draw::Canvas& c, draw::Rect r, const std::wstring& text, std::function<void(draw::Rect)> click, const std::wstring& tip) {
        kitpaint::drawPickFace(c, r, text, false, hot(r));
        hit(r, [r, click] { click(r); }, tip);
    }
    // A square button with an icon glyph (boxed: a fill when on or under the mouse).
    void glyphButton(draw::Canvas& c, draw::Rect r, kitpaint::Ico g, bool on, std::function<void()> click, const std::wstring& tip) {
        r = kitpaint::square(r);
        const bool over = hot(r);
        if (on) kitpaint::drawBox(c, r, kSel, kAccentLine);
        else if (over) kitpaint::drawBox(c, r, kCtlHot, draw::kNone);
        kitpaint::drawGlyph(c, r, g, on ? kAccentBright : over ? kText : kDim, 14);
        hit(r, std::move(click), tip);
    }
    // A bare icon button of a row.
    void rowButton(draw::Canvas& c, draw::Rect r, kitpaint::Ico g, const std::wstring& tip, std::function<void()> click, kitpaint::Tone tone, bool on) {
        kitpaint::drawIconFace(c, r, g, tone, on, hot(kitpaint::square(r)));
        hit(kitpaint::square(r), std::move(click), tip);
    }
    // A group's name over the side panel's parts, in the small font. Returns the y under it.
    int sideGroup(draw::Canvas& c, int x, int y, int w, const std::wstring& name) {
        const draw::Font of = c.setFont(c.font({draw::Face::Ui, -(shellUiFontLfHeight() * 5 / 6)}));
        c.text({x, y, x + w, y + scale(15)}, name, kDim, draw::kMiddle | draw::kEllipsis);
        c.setFont(of);
        return y + scale(18);
    }
    // A point under a box (where its picker opens).
    static draw::Point below(draw::Rect r) { return {r.left, r.bottom + scale(2)}; }
    bool pick(const std::wstring& title, std::vector<ListPickItem> items, const std::string& current, draw::Point at, std::string& out) {
        return host_.pickOne(title, std::move(items), current, *this, at, out);
    }

    // Left: the briefing (a dropdown of the map's briefings), a new one, and whom it is for.
    void paintSide(draw::Canvas& c) {
        const int listW = scale(220);
        c.fill({0, 0, listW, height()}, kPanel);
        c.fill({0, 0, 1, height()}, kBorder);
        c.fill({listW - 1, 0, listW, height()}, kBorder);
        const int x = scale(14), w = listW - scale(28);
        int y = scale(12);
        y = sideGroup(c, x, y, w, L"Briefing") + scale(4);
        const int n = briefActionCount(who);
        pickBox(c, {x, y, x + w, y + scale(28)}, whoName(who),
                [this](draw::Rect r) {
                    std::vector<ListPickItem> items;
                    const auto list = briefList();
                    for (size_t i = 0; i < list.size(); ++i)
                        items.push_back({whoName(list[i]) + L"   " + std::to_wstring(briefActionCount(list[i])) + L" actions", L"", std::to_string(i)});
                    std::string got;
                    if (items.empty() || !pick(L"Briefing", items, "", below(r), got)) return;
                    commitText();
                    setWho(list[size_t(std::stoi(got))]);
                    play(false);
                },
                n ? std::to_wstring(n) + L" actions" : L"None");
        y += scale(34);
        const int ax = smallButton(c, x, y, scale(24), L"New briefing",
                                   [this] {
                                       const std::array<bool, 27> w2 = briefNewWho();  // (none left: nobody yet, the user chooses below)
                                       commitText();
                                       setWho(w2);
                                       play(false);
                                   },
                                   L"Show a new briefing for the first player with no briefing of their own");
        if (n)
            smallButton(c, ax + scale(8), y, scale(24), L"Delete",
                        [this] {
                            if (!host_.confirm(L"Delete briefing", L"Delete the briefing for " + whoName(who) + L"?", L"&Delete")) return;
                            const auto gone = who;
                            edit([&](std::vector<BriefingTrigger>& b) { std::erase_if(b, [&](const BriefingTrigger& tr) { return tr.players == gone; }); });
                            setWho(briefFirstWho());
                        },
                        L"Delete the briefing and its actions", kitpaint::Tone::Danger);
        y += scale(36);
        // Whom it is for: a check box for each; the briefing's triggers change with them.
        y = sideGroup(c, x, y, w, L"For") + scale(4);
        const int rh = scale(26);
        for (int o : {17, 18, 19, 20, 21}) {
            check(c, {x, y, x + w, y + rh}, ownerName(o), who[size_t(o)], [this, o] { toggleWho(o); }, o == 17 ? L"Every player" : L"The players of this force");
            y += rh;
        }
        y += scale(6);
        for (int p = 0; p < 8; ++p) {
            const uint32_t rgb = shellState().ed ? shellState().ed->playerRgb(p) : 0x808080;
            c.fill({x + w - scale(10), y + scale(9), x + w, y + rh - scale(9)}, draw::rgb(int(rgb >> 16 & 255), int(rgb >> 8 & 255), int(rgb & 255)));
            check(c, {x, y, x + w - scale(16), y + rh}, ownerName(p), who[size_t(p)], [this, p] { toggleWho(p); });
            y += rh;
        }
    }

    // The preview's width: what the actions do not need (they keep 440 px), as tall as the page lets it be (with the
    // player box, the transport and the timeline under it).
    int previewWidth() const {
        const int listW = scale(220);
        const int byHeight = (height() - scale(12 + 34 + 10 + 32 + 8 + 64 + 12)) * 640 / 520;
        return std::max(scale(420), std::min({width() - listW - scale(440), byHeight, scale(1280)}));
    }

    // Middle: the actions, then the selected one's text.
    void paintActions(draw::Canvas& c) {
        const auto& rows = gBriefRows;
        const auto& b = briefings();
        const int listW = scale(220), rowH = scale(26);
        const int midRight = width() - previewWidth() - scale(24);
        const int x0 = listW + scale(18), colRight = midRight - scale(10);
        int y = scale(14);
        // The selected action's text box sits at the bottom; the list takes the height above it.
        int selTrig, selAct;
        const bool selText = selected(selTrig, selAct) && briefHasText(b[size_t(selTrig)].actions[size_t(selAct)].type);
        const int textTop = height() - (selText ? scale(170) : 0);
        const int listBottom = textTop - rowH - scale(14);  // room for "+ action"
        // Each row's start time, from the preview's timeline.
        std::map<std::pair<int, int>, uint32_t> startOf;
        for (const auto& e : timeline_) startOf[{e.trigger, e.action}] = e.at;
        if (sel >= 0 && sel < listScroll) listScroll = sel;
        const int first = std::clamp(listScroll, 0, std::max(0, int(rows.size()) - 1));
        listScroll = first;
        int lastShown = first - 1;
        const int gutter = scale(58);  // the start times, left of the blocks
        rowTops_.clear();
        const draw::Font mono = c.font({draw::Face::Mono, (10 * int(shellDpi()) + 36) / 72});
        for (int i = first; i < int(rows.size()) && (i == first || y + scale(40) <= listBottom); ++i) {
            lastShown = i;
            const BriefRow& row = rows[size_t(i)];
            const BriefingAction& a = b[size_t(row.trig)].actions[size_t(row.act)];
            if (i > first && rows[size_t(i - 1)].trig != row.trig) {  // a second trigger of the briefing's: a line between
                c.line(x0 + gutter, y + scale(1), colRight, y + scale(1), kBorder);
                y += scale(6);
            }
            rowTops_.push_back(y);
            const bool on = picked.count(i) > 0 || i == sel, dim = a.disabled();
            const auto st = startOf.find({row.trig, row.act});
            const uint32_t at = st != startOf.end() ? st->second : 0;
            bool playingNow = false;
            if (playing && st != startOf.end() && t >= at) {
                if (i + 1 >= int(rows.size())) playingNow = true;
                else {
                    const auto nx = startOf.find({rows[size_t(i + 1)].trig, rows[size_t(i + 1)].act});
                    playingNow = (nx != startOf.end() ? nx->second : 0) > t;
                }
            }
            // The sentence, laid out in the block (it wraps), then the block under it.
            const int bx = x0 + gutter, bw = colRight - bx, textX = bx + scale(26), textW = bw - scale(26) - scale(100);
            const int lineH = scale(22), padY = scale(9);
            const auto words = briefWords(a);
            std::vector<draw::Rect> placed;
            int x = textX, ly = y + padY;
            for (const BriefWordSpec& w : words) {
                const bool chip = w.kind != BriefWordSpec::Kind::Word;
                const int iconW = w.kind == BriefWordSpec::Kind::Unit && host_.unitIcon(a.unit) ? scale(20) : 0, padX = chip ? scale(5) : 0;
                const int ww = c.measure(w.text) + 2 * padX + iconW;
                if (!chip && !w.text.empty() && (w.text[0] == L'.' || w.text[0] == L':') && x > textX) x -= scale(5);
                if (x > textX && x + ww > textX + textW) x = textX, ly += lineH;
                placed.push_back({x, ly, x + ww, ly + lineH});
                x += ww + (chip ? scale(2) : 0) + scale(4);
            }
            const draw::Rect head{bx, y, bx + bw, ly + lineH + padY};
            c.roundBox(head, on ? kSel : kCtl, on ? kAccent : kBorder, float(scale(4)));
            if (!dim) c.fill({head.left + scale(2), head.top + scale(5), head.left + scale(4), head.bottom - scale(5)}, briefTypeColor(a.type));
            if (playingNow) c.fill({head.left - scale(4), head.top + scale(5), head.left - scale(2), head.bottom - scale(5)}, kGood);
            const bool over = hot(head);
            if ((on || over) && dragFrom_ < 0) kitpaint::drawGrip(c, head.left + scale(7), head.top + padY + lineH / 2 - scale(6), on);  // the drag handle
            // The start time.
            {
                const draw::Font of = c.setFont(mono);
                c.text({x0, head.top + padY, bx - scale(6), head.top + padY + lineH}, st != startOf.end() ? briefTime(at) : L"", kDim, draw::kMiddle);
                c.setFont(of);
            }
            // Click: select (Ctrl adds, Shift a range); drag: move the selected rows.
            hit(head, [this, i, at] { clickRow(i, at); }, L"", [this, first](draw::Point pt) { dragOver(first, pt); });
            // The words: values are chips; a click selects the row and opens the value's picker.
            for (size_t k = 0; k < words.size(); ++k) {
                const BriefWordSpec& w = words[k];
                const draw::Rect r = placed[k];
                const bool chip = w.kind != BriefWordSpec::Kind::Word;
                const int padX = chip ? scale(5) : 0;
                draw::Bitmap* icon = w.kind == BriefWordSpec::Kind::Unit ? host_.unitIcon(a.unit) : nullptr;
                if (chip) {
                    kitpaint::drawChipFace(c, {r.left, r.top + scale(2), r.right, r.bottom - scale(2)}, hot(r), dim);
                    hit(r, [this, i, r, w, a] {
                        clickRow(i, UINT32_MAX);
                        openValue(w, a, r);
                    }, w.tip);
                    if (w.kind == BriefWordSpec::Kind::Unit || w.kind == BriefWordSpec::Kind::Slot || w.kind == BriefWordSpec::Kind::Modifier)
                        wheels_.push_back({r, [this, i, w, a](int step) {
                                               clickRow(i, UINT32_MAX);
                                               stepValue(w, a, step);
                                           }});
                }
                if (icon) fittedBitmap(c, *icon, {r.left + padX - scale(1), r.top + scale(3), r.left + padX + scale(17), r.bottom - scale(3)});
                c.text({r.left + padX + (icon ? scale(20) : 0), r.top, r.right - padX, r.bottom}, w.text, dim ? kFaint : chip ? kAccent : kText, draw::kMiddle);
            }
            // On the row under the mouse, or selected: on/off, duplicate, delete.
            if ((on || over) && dragFrom_ < 0) {
                const draw::Rect line{head.left, head.top + padY - scale(2), head.right - scale(4), head.top + padY + scale(24)};
                hit(line, nullptr);  // (the mouse entering the row paints it again)
                auto act = [this, i](int k) {
                    return [this, i, k] {
                        clickRow(i, UINT32_MAX);
                        if (k == 0) editSelected([](BriefingAction& x) { x.flags ^= 2; });
                        else if (k == 1) duplicate();
                        else deleteRows();
                    };
                };
                rowButton(c, kitpaint::rowActionRect(line, 0, 3), UiIcon::Power,dim ? L"Turn on" : L"Turn off", act(0), kitpaint::Tone::Quiet, dim);
                rowButton(c, kitpaint::rowActionRect(line, 1, 3), UiIcon::Duplicate, L"Duplicate (Ctrl+D)", act(1), kitpaint::Tone::Normal, false);
                rowButton(c, kitpaint::rowActionRect(line, 2, 3), UiIcon::Delete, L"Delete (Del)", act(2), kitpaint::Tone::Danger, false);
            }
            y = head.bottom + scale(5);
        }
        listEnd_ = y;
        if (dragFrom_ >= 0 && dragTo_ >= 0) {  // where the dragged rows go
            const int k = dragTo_ - first;
            const int ly = k >= 0 && k < int(rowTops_.size()) ? rowTops_[size_t(k)] : y;
            kitpaint::drawDropLine(c, x0 + gutter, colRight, ly - scale(3));
        }
        allShown_ = lastShown >= int(rows.size()) - 1;  // the wheel scrolls no further
        if (sel > lastShown && sel < int(rows.size()) && followSel_) {  // the selected row below: scroll to it
            listScroll += sel - lastShown;
            invalidate();
        }
        followSel_ = false;
        // The scroll bar, when not all rows show: drag the thumb or click the track.
        if (!(first == 0 && allShown_) && !rows.empty()) {
            const int n = int(rows.size()), shown = std::max(1, lastShown - first + 1);
            const int top = scale(14), bottom = std::max(top + scale(40), listBottom), tx = colRight + scale(3);
            const int trackH = bottom - top, thumbH = std::max(scale(28), trackH * shown / n);
            const int span = std::max(1, n - shown);
            const int thumbTop = top + (trackH - thumbH) * std::min(first, span) / span;
            const draw::Rect track{tx, top, tx + scale(6), bottom};
            draw::Rect thumb{tx, thumbTop, tx + scale(6), thumbTop + thumbH};
            const bool lit = hot(track) || grab_.down;
            if (lit) c.fill(track, kPanel);
            else thumb.left = thumb.right - std::max(2, thumb.width() / 2);
            kitpaint::drawBox(c, thumb, lit ? kThumbHot : kThumb, draw::kNone, 3);
            hits_.push_back({{tx - scale(4), top, tx + scale(10), bottom},
                             [this, top, trackH, thumbH, span](draw::Point pt) {
                                 const int room = std::max(1, trackH - thumbH);
                                 listScroll = std::clamp(int(std::lround(double(pt.y - top - thumbH / 2) * span / room)), 0, span);
                                 invalidate();
                             },
                             nullptr, L"", false});
        }
        if (rows.empty()) {  // no actions: what a briefing does, above the button
            const wchar_t* note = L"This briefing has no actions. Add actions to show text, portraits and sounds before the game starts.";
            const int h = c.measureBox(note, colRight - (x0 + gutter), draw::kWrap).y;
            c.text({x0 + gutter, y + scale(4), colRight, y + scale(4) + h}, note, kDim, draw::kWrap);
            y += scale(4) + h + scale(6);
        }
        // Add an action: after the selected ones, or at the end.
        const draw::Rect addRow{x0 + gutter, y, colRight, y + scale(30)};
        kitpaint::drawAddRowFace(c, addRow, L"Add an action", hot(addRow));
        hit(addRow, [this, y] {
            std::string got;
            if (!pick(L"Action", briefTypeItems(), "", {scale(240), y + scale(26) + scale(2)}, got)) return;
            const auto s = pickedRows();
            insert(s.empty() ? int(gBriefRows.size()) : s.back() + 1, {briefNewAction(uint8_t(std::stoi(got)))});
        });
        // The selected action's text: the editor (its colors under it).
        if (selText) {
            const BriefingAction& a = b[size_t(selTrig)].actions[size_t(selAct)];
            const int h = gameTextHeight(c, textSpec(), colRight - x0, scale(110));
            const draw::Rect r{x0, textTop + scale(4), colRight, textTop + scale(4) + h};
            if (textFor_ != sel) {
                textFor_ = sel;
                text_.setBytes(a.text);
                textDirty_ = false;
            }
            const draw::Rect now = text_.frame();
            if (now.left != r.left || now.top != r.top || now.right != r.right || now.bottom != r.bottom) text_.setFrame(r);
            text_.setVisible(true);
        } else {
            text_.setVisible(false);
        }
    }
    // A row dragged over the list: the place it goes (before row dragTo_).
    void dragOver(int first, draw::Point pt) {
        if (dragFrom_ < 0) dragFrom_ = sel;
        int to = first + int(rowTops_.size());
        for (size_t k = 0; k < rowTops_.size(); ++k) {
            const int next = k + 1 < rowTops_.size() ? rowTops_[k + 1] : listEnd_;
            if (pt.y < (rowTops_[k] + next) / 2) {
                to = first + int(k);
                break;
            }
        }
        dragTo_ = std::clamp(to, 0, int(gBriefRows.size()));
        invalidate();
    }
    // The unit types a briefing can show (a mod's too: every named one), as the unit picker lists them.
    static std::vector<ListPickItem> unitItems() {
        std::vector<ListPickItem> items;
        const AppState& s = shellState();
        if (!s.ed) return items;
        const UnitTypes& types = s.ed->unitTypes();
        for (uint16_t u = 0; u < types.size(); ++u)
            if (!types[u].name.empty()) items.push_back({entryName("units", u) + L"   #" + std::to_wstring(u), L"", std::to_string(u), "units", u});
        return items;
    }
    // Show portrait's choices: a mod's named portraits (each as a unit that has it), else the unit types.
    static std::vector<ListPickItem> unitChoices(int unit) {
        std::vector<ListPickItem> items = briefPortraitItems(unit);
        return items.empty() ? unitItems() : items;
    }
    // The wheel over a value chip of a list (unit, slot, transmission duration): the item before or after in its list.
    void stepValue(const BriefWordSpec& w, const BriefingAction& a, int step) {
        using K = BriefWordSpec::Kind;
        const std::vector<ListPickItem> items = w.kind == K::Unit ? unitChoices(a.unit) : w.kind == K::Slot ? briefSlotItems() : briefModifierItems();
        const std::string now = std::to_string(w.kind == K::Unit ? int(a.unit) : w.kind == K::Slot ? int(a.slot) : int(a.modifier));
        int at = -1;
        for (size_t k = 0; k < items.size(); ++k)
            if (items[k].value == now) at = int(k);
        const int to = std::clamp(at < 0 ? 0 : at + step, 0, int(items.size()) - 1);
        if (items.empty() || to == at) return;
        const int v = std::stoi(items[size_t(to)].value);
        editSelected([&](BriefingAction& x) {
            if (w.kind == K::Unit) x.unit = uint16_t(v);
            else if (w.kind == K::Slot) x.slot = v;
            else x.modifier = uint8_t(v);
        });
    }
    // A value chip's click: its picker under it (the text: the editor under the list gets the keyboard).
    void openValue(const BriefWordSpec& w, const BriefingAction& a, draw::Rect r) {
        using K = BriefWordSpec::Kind;
        const draw::Point at{r.left, r.bottom + scale(4)};
        std::string got;
        switch (w.kind) {
        case K::Word: break;
        case K::Time: {
            uint32_t v;
            if (host_.editSeconds(*this, r, w.title, a.time, v)) editSelected([&](BriefingAction& x) { x.time = v; });
            break;
        }
        case K::Slot:
            if (pick(L"Slot", briefSlotItems(), std::to_string(a.slot), at, got)) editSelected([&](BriefingAction& x) { x.slot = std::stoi(got); });
            break;
        case K::Text:
            textFor_ = -1;  // the box shows this row's text (the next paint), then gets the caret
            if (host()) host()->timer(1, [this] {
                    text_.focusText();
                    draw::View& box = text_.isRaw() ? static_cast<draw::View&>(text_.rawBox()) : text_.preview();
                    box.key({draw::Key('A'), true, false, draw::kCtrl});  // all of it selected
                });
            invalidate();
            break;
        case K::Sound:
            if (host_.pickSound(*this, at, a.wav, got)) {
                const uint32_t len = host_.soundLength(got);
                editSelected([&](BriefingAction& x) { x.wav = got, x.wavTime = len; });
            }
            break;
        case K::Unit:
            if (pick(briefPortraitNames().empty() ? L"Search unit types" : L"Search portraits", unitChoices(a.unit), std::to_string(a.unit), at, got))
                editSelected([&](BriefingAction& x) { x.unit = uint16_t(std::stoi(got)); });
            break;
        case K::Modifier:
            if (pick(L"Transmission duration", briefModifierItems(), "", at, got)) editSelected([&](BriefingAction& x) { x.modifier = uint8_t(std::stoi(got)); });
            break;
        }
    }

    // Right: the briefing window (as a player of the briefing's sees it), its transport and timeline.
    void paintRight(draw::Canvas& c) {
        const int previewW = previewWidth(), rowH = scale(26);
        const int px = width() - previewW - scale(14);
        int top = scale(12);
        const auto players = whoPlayers(who);
        if (players.size() > 1) {  // a force or all players: whose briefing to play
            pickBox(c, {px, top + scale(2), px + scale(200), top + rowH - scale(2)}, ownerName(player),
                    [this, players](draw::Rect r) {
                        std::vector<ListPickItem> items;
                        for (int p : players) items.push_back({ownerName(p), L"", std::to_string(p)});
                        std::string got;
                        if (pick(L"Preview as", items, std::to_string(player), below(r), got)) player = std::stoi(got), seek(0);
                    },
                    L"The player to preview. The preview plays the briefings for that player, for its force and for all players.");
            top += rowH + scale(6);
        }
        const draw::Rect pv{px, top, px + previewW, top + previewW * 520 / 640};  // the 640 x 480 screen, and a margin under it
        paintWindow(c, pv);
        int ty = pv.bottom + scale(10);
        const int bs = scale(32);
        glyphButton(c, {px, ty, px + bs, ty + bs}, UiIcon::ToStart, false, [this] { seek(0); }, L"Go to the start");
        glyphButton(c, {px + bs + scale(6), ty, px + 2 * bs + scale(6), ty + bs}, playing ? UiIcon::Pause : UiIcon::Play, false, [this] { play(!playing); },
                    playing ? L"Pause (Space)" : L"Play (Space)");
        const int mw = std::min(scale(150), previewW / 2);
        c.text({px + 2 * bs + scale(18), ty, px + previewW - mw - scale(8), ty + bs}, briefTime(t) + L" / " + briefTime(total_), kDim, draw::kMiddle);
        static const wchar_t* const kRaceNames[3] = {L"Zerg", L"Terran", L"Protoss"};
        const int race = musicRace();
        pickBox(c, {px + previewW - mw, ty + scale(2), px + previewW, ty + bs - scale(2)}, std::wstring(L"Music: ") + (race < 0 ? L"None" : kRaceNames[race]),
                [this](draw::Rect r) {
                    std::vector<ListPickItem> items{{L"The player's race", L"", "-1"}};
                    for (int k : {1, 0, 2}) items.push_back({kRaceNames[k], L"", std::to_string(k)});
                    items.push_back({L"None", L"", "3"});
                    std::string got;
                    if (pick(L"Music", items, std::to_string(music), below(r), got)) music = std::stoi(got), syncSounds(true), invalidate();
                },
                L"The music of the preview. The game plays the briefing music of the player's race.");
        ty += bs + scale(8);
        paintTimeline(c, {px, ty, px + previewW, ty + scale(64)});
    }

    // A picture fitted into `box` (made smaller to fit, centered).
    static void fittedBitmap(draw::Canvas& c, draw::Bitmap& bm, draw::Rect box) {
        if (bm.width() <= 0 || bm.height() <= 0) return;
        const double k = std::min({double(box.width()) / bm.width(), double(box.height()) / bm.height(), 1.0});
        const int w = std::max(1, int(bm.width() * k)), h = std::max(1, int(bm.height() * k));
        const int x = box.left + (box.width() - w) / 2, y = box.top + (box.height() - h) / 2;
        c.bitmap(bm, {0, 0, bm.width(), bm.height()}, {x, y, x + w, y + h}, true);
    }
    // A portrait video's frame as a bitmap (kept for the last few videos' frames).
    draw::Bitmap* portraitBitmap(const PortraitVideo& v, int frame) {
        if (portraits_.size() > 3000) portraits_.clear();  // a few videos' frames, not every video shown since the start
        frame = std::clamp(frame, 0, int(v.frames.size()) - 1);
        auto& slot = portraits_[{v.serial, frame}];
        if (!slot) {
            slot = std::make_unique<draw::Bitmap>(v.width, v.height);
            const std::vector<uint8_t>& px = v.frames[size_t(frame)];
            uint32_t* out = slot->pixels();
            for (size_t i = 0; i < size_t(v.width) * size_t(v.height) && i * 4 + 3 < px.size(); ++i)
                out[i] = uint32_t(px[i * 4]) | uint32_t(px[i * 4 + 1]) << 8 | uint32_t(px[i * 4 + 2]) << 16 | 0xFF000000u;
            slot->opaque = true;
            slot->changed();
        }
        return slot.get();
    }

    // The briefing window, as a wireframe at time t: the game's 640 x 480 layout, scaled to `r`. Portrait slots 1 and 4
    // on the sides, 2 and 3 on top; the message in the middle; Objectives at the bottom left.
    void paintWindow(draw::Canvas& c, draw::Rect r) {
        const BriefingState s = briefingStateAt(timeline_, t);
        c.fill(r, draw::rgb(10, 11, 13));
        c.roundBox(r, draw::kNone, kBorder, float(scale(6)));
        const double k = r.width() / 640.0;
        auto at = [&](int x0, int y0, int x1, int y1) { return draw::Rect{r.left + int(x0 * k), r.top + int(y0 * k), r.left + int(x1 * k), r.top + int(y1 * k)}; };
        // The game's font: about 14 px on its 480-line screen, lines 19 px apart.
        const int fpx = std::max(scale(10), int(14 * k));
        const draw::Font oldFont = c.setFont(c.font({draw::Face::Ui, fpx}));
        const draw::Color frame = draw::rgb(84, 58, 54), talk = draw::rgb(230, 40, 30), textColor = draw::rgb(196, 204, 255);  // the game's frames, quieter
        auto panel = [&](draw::Rect b, draw::Color line, int lw = 1) {
            c.fill(b, draw::rgb(18, 19, 22));
            c.roundBox(b, draw::kNone, line, float(std::max(2, int(4 * k))), lw);
        };
        // Portrait slots.
        static const int slots[4][4] = {{38, 102, 157, 209}, {189, 30, 308, 137}, {340, 30, 461, 137}, {483, 102, 603, 209}};
        for (int i = 0; i < 4; ++i) {
            const draw::Rect f = at(slots[i][0], slots[i][1], slots[i][2], slots[i][3]);
            const auto& p = s.portraits[size_t(i)];
            const bool talking = p.shown && p.speaking;
            panel(f, talking ? talk : frame, talking ? std::max(2, int(2 * k)) : 1);
            if (p.shown) {
                const int in = std::max(2, int(3 * k));
                const PortraitVideo* video = nullptr;
                int frameAt = 0;
                if (briefPortraitFrame(p, t, video, frameAt))
                    if (draw::Bitmap* bm = portraitBitmap(*video, frameAt)) c.bitmap(*bm, {0, 0, bm->width(), bm->height()}, {f.left + in, f.top + in, f.right - in, f.bottom - in}, true);
                if (talking) c.roundBox(f, draw::kNone, talk, float(std::max(2, int(4 * k))), std::max(2, int(2 * k)));
            }
            hit(f, nullptr, p.shown ? L"Slot " + std::to_wstring(i + 1) + L": " + entryName("units", p.unit) : L"Slot " + std::to_wstring(i + 1));
        }
        // Objectives: as high as the game's (300-463) so it shows as many lines, moved down to clear the message box.
        const draw::Rect obj = at(33, 336, 272, 499);
        panel(obj, frame);
        const int olh = std::max(1, int(19 * k));
        const draw::Rect oh{obj.left + int(10 * k), obj.top + int(8 * k), obj.right - int(8 * k), obj.top + int(8 * k) + olh};
        c.text(oh, L"Objectives", textColor, draw::kMiddle);
        drawGameText(c, {obj.left + int(10 * k), oh.bottom + int(6 * k), obj.right - int(8 * k), obj.bottom}, s.objectives, draw::rgb(220, 220, 220), olh, true);
        // The message: 6.5 lines; longer text scrolls up, so that its end shows when its time is over.
        const draw::Rect msg = at(183, 169, 462, 308);
        panel(msg, s.textSlot >= 0 ? draw::rgb(150, 64, 50) : frame);
        const int lh = std::max(1, int(19 * k)), inner = lh * 13 / 2;  // the game's lines: 19 px apart
        draw::Rect tr{msg.left + int(10 * k), msg.top + std::max(0, int(msg.height() - inner) / 2), msg.right - int(8 * k), 0};
        tr.bottom = tr.top + inner;
        if (!s.text.empty()) {  // (no text: the box stays empty, as the game shows it)
            c.pushClip({0, 0, 0, 0});  // measure only
            const int full = drawGameText(c, tr, s.text, textColor, lh, true);
            c.popClip();
            const int off = briefTextScroll(full, inner, double(t - std::min(t, s.textSince)), s.textFor);
            c.pushClip(tr);
            drawGameText(c, {tr.left, tr.top - off, tr.right, tr.bottom}, s.text, textColor, lh, true);
            c.popClip();
        }
        c.setFont(oldFont);
    }

    // The timeline: a row per kind of action, a block per action (its length), the playhead; click or drag to seek.
    void paintTimeline(draw::Canvas& c, draw::Rect r) {
        const uint32_t span = std::max<uint32_t>(total_, 1000);
        c.fill(r, draw::rgb(14, 14, 16));
        c.roundBox(r, draw::kNone, kBorder, float(scale(4)));
        const int x0 = r.left + scale(8), x1 = r.right - scale(8);
        auto xAt = [&](uint32_t ms) { return x0 + int(int64_t(ms) * (x1 - x0) / span); };
        const int laneH = scale(10), ly = r.top + scale(8);
        for (const BriefingEvent& e : timeline_) {
            const int lane = briefLane(e.a->type);
            const draw::Color col = lane == 0 ? kAccent : lane == 1 ? draw::rgb(90, 94, 100) : kGood;
            const draw::Rect bl{xAt(e.at), ly + lane * (laneH + scale(3)), std::max(xAt(e.at) + scale(3), xAt(e.at + e.length)), ly + lane * (laneH + scale(3)) + laneH};
            int row = -1;
            for (size_t i = 0; i < gBriefRows.size(); ++i)
                if (gBriefRows[i].trig == e.trigger && gBriefRows[i].act == e.action) row = int(i);
            c.fill(bl, col, picked.count(row) ? 255 : 130);
        }
        // Seconds.
        const int ty = ly + 3 * (laneH + scale(3)) + scale(2);
        uint32_t step = span > 120000 ? 30000 : span > 40000 ? 10000 : span > 10000 ? 5000 : 1000;
        while (span / step > 60) step *= 2;  // (a very long briefing: at most about 60 marks)
        for (uint64_t at = 0; at <= span; at += step) {
            const uint32_t ms = uint32_t(at);
            c.line(xAt(ms), ty, xAt(ms), ty + scale(4), draw::rgb(70, 74, 80));
            c.text({xAt(ms) - scale(30), ty + scale(4), xAt(ms) + scale(30), ty + scale(20)}, briefTime(ms), kDim, draw::kMiddle | draw::kCenter);
        }
        const int ph = xAt(t);
        c.line(ph, r.top + scale(3), ph, r.bottom - scale(3), draw::rgb(240, 240, 240), 2);
        hit(r, nullptr, L"Click or drag to go to a time",
            [this, x0, x1, span](draw::Point pt) { seek(uint32_t(std::clamp<int64_t>(int64_t(pt.x - x0) * span / std::max(1, x1 - x0), 0, span))); });
    }

    BriefingPageHost& host_;
    GameTextView text_;
    const Editor* shownFor_ = nullptr;  // the map the briefing shown was chosen for
    int textFor_ = -1;                  // the row the text box shows
    bool textDirty_ = false;
    uint64_t last_ = 0;
    int tickId_ = 0;
    std::vector<BriefingEvent> timeline_;
    uint32_t total_ = 0;
    std::vector<Voice> voices_;
    int musicVoice_ = 0;      // the music's voice (0: none, or a test run)
    std::string musicPlays_;  // the music file that plays ("": none)
    uint32_t musicRound_ = 0;  // which time it plays from the start (t / its length)
    uint32_t musicFrom_ = 0;   // where in the track it started (ms)
    uint64_t musicSince_ = 0;  // when it started (nowMs)
    std::vector<Hit> hits_;
    // The values the wheel steps through (a chip of a list's values): its place, and the step (-1 the item before).
    std::vector<std::pair<draw::Rect, std::function<void(int)>>> wheels_;
    float valueWheelCarry_ = 0;
    bool listWheeling_ = false;  // the wheel scrolled the list and the mouse has not moved since (at wheelAt_)
    draw::Point wheelAt_{};
    struct {
        std::function<void(draw::Point)> drag;
        std::function<void()> click;
        bool down = false, moved = false;
    } grab_;  // a part with a drag, while the mouse is down on it
    draw::Point mouse_{-1, -1};
    draw::Rect over_{};
    unsigned mods_ = 0;               // the keys held at the last press (Ctrl adds, Shift a range)
    int dragFrom_ = -1, dragTo_ = -1;  // the row being dragged, and the place it goes (before row dragTo_)
    std::vector<int> rowTops_;         // the shown rows' tops (dragging finds the place from these)
    int listEnd_ = 0;
    bool allShown_ = true;   // the last row is in view
    bool followSel_ = false;  // the selected row moved by a key: keep it in view
    std::map<std::pair<uint64_t, int>, std::unique_ptr<draw::Bitmap>> portraits_;
};
