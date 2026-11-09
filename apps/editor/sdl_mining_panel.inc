// Mining's panel (sdl_simulation.inc includes it). It opens with the mining; the bar's gear shows or hides it. It is
// under the bar, over the map, made of the kit's controls. Mining: the workers on minerals (for each mineral field or in
// all, and their number), the races, the field labels on the map (frames a trip or minerals a minute) and the bases
// that mine. Boost searches: the three searches (position, cargo, cycle: the pull request's Mining Lab searches), each
// with its search time, Search (Continue), Show guides, its progress and its state. A Cosmonarchy BW map has no boost
// searches (its mining runs on StarDraft's own engine).
void simulateMiningCommand();
sim::MiningOptions simMiningOptions(uint32_t seed);

// The map's bases that can mine (MiningOptions: the start locations, by player, then the placed depots that are not
// near a start location), each a check box in the panel's Bases. The selection has a bit for each entry. At most
// kSimBasesMax bases mine at a time (bar_sim.cpp: kMaxBases).
struct SimBaseEntry {
    int x = 0, y = 0;  // the base's place (map pixels)
    bool start = false;
    int player = 0;  // a start location's player (0 to 7); a placed depot's: the player that colors it (bar_sim.cpp)
    int depot = -1;  // a placed depot: its bit in MiningOptions::placedDepotMask
};
constexpr int kSimBaseEntriesMax = 12, kSimBasesMax = 8;
std::vector<SimBaseEntry> gSimBaseEntries;
const Editor* gSimBaseMap = nullptr;  // the map of gSimBaseEntries
unsigned gSimSelectedBases = 0;
bool gSimBasesPending = false;  // a new mining: the entries and the selection again when its engine has the map
std::chrono::steady_clock::time_point gSimShareUntil{};  // the note on shared processor time shows until then
unsigned gSimShareTicket = 0;

// The entries from the engine's map (after setMap). The default selection: Player 1's start location, else the first
// entry.
void simResetBaseSelection() {
    gSimBaseMap = gState.ed.get();
    gSimBaseEntries.clear();
    gSimSelectedBases = 0;
    gSimShareUntil = {};
    ++gSimShareTicket;
    sim::MiningOptions every;  // (all the start locations and placed depots, no groups of mineral fields)
    every.mineralClusters = false;
    std::vector<sim::Race> races;
    const std::vector<sim::MiningBase> bases = simEngine().miningBases(every, races);
    // (each base's player as bar_sim.cpp gives it: a start location of Player 1 to 8 its own, another base the first
    // player that no start location has. A start location of Player 9 to 12 is not in the list: it mines only with
    // all the bases, the command line's)
    std::vector<bool> taken(size_t(kSimBasesMax), false);
    for (const sim::MiningBase& b : bases)
        if (b.startLocation && b.player >= 0 && b.player < kSimBasesMax) taken[size_t(b.player)] = true;
    std::vector<SimBaseEntry> starts, depots;
    int depot = 0;
    for (size_t m = 0; m < bases.size(); ++m) {
        SimBaseEntry e;
        e.x = bases[m].x, e.y = bases[m].y, e.start = bases[m].startLocation;
        const bool own = e.start && bases[m].player >= 0 && bases[m].player < kSimBasesMax;
        e.player = own ? bases[m].player : -1;
        for (int p = 0; e.player < 0 && p < kSimBasesMax; ++p)
            if (!taken[size_t(p)]) e.player = p, taken[size_t(p)] = true;
        if (e.player < 0) e.player = int(m % size_t(kSimBasesMax));  // (more bases than players: as bar_sim.cpp)
        if (!e.start) e.depot = depot++, depots.push_back(e);
        else if (own && std::none_of(starts.begin(), starts.end(), [&](const SimBaseEntry& s) { return s.player == e.player; }))
            starts.push_back(e);  // (a second start location of a player mines with the first: one entry)
    }
    std::stable_sort(starts.begin(), starts.end(), [](const SimBaseEntry& a, const SimBaseEntry& b) { return a.player < b.player; });
    for (const auto* list : {&starts, &depots})
        for (const SimBaseEntry& e : *list)
            if (int(gSimBaseEntries.size()) < kSimBaseEntriesMax && e.depot < 32) gSimBaseEntries.push_back(e);
    for (size_t k = 0; k < gSimBaseEntries.size() && !gSimSelectedBases; ++k)
        if (gSimBaseEntries[k].start && gSimBaseEntries[k].player == 0) gSimSelectedBases = 1u << k;
    if (!gSimSelectedBases && !gSimBaseEntries.empty()) gSimSelectedBases = 1;
}
// The selected entries (none: another map).
unsigned simSelectedBases() {
    if (gSimBaseMap != gState.ed.get()) return 0;
    return gSimSelectedBases & ((1u << gSimBaseEntries.size()) - 1);
}
// The mining has a base to play: a selected one, or (a map with no entries: all the bases mine) any.
bool simMiningHasBases() {
    if (gSimBaseMap == gState.ed.get() && gSimBaseEntries.empty()) return gSim_ && !simEngine().miningResult().empty();
    return simSelectedBases() != 0;
}
int simSelectedBaseCount() {
    int n = 0;
    for (unsigned mask = simSelectedBases(); mask; mask &= mask - 1) ++n;
    return n;
}
// The selection in the mining's options. A map with no bases: all of them (the engine says that the map has none).
void simSelectionOptions(sim::MiningOptions& o) {
    if (gSimBaseMap != gState.ed.get() || gSimBaseEntries.empty()) return;
    o.startPlayers = 0, o.placedDepotMask = 0;
    const unsigned selected = simSelectedBases();
    for (size_t k = 0; k < gSimBaseEntries.size(); ++k) {
        if (!(selected & (1u << k))) continue;
        const SimBaseEntry& e = gSimBaseEntries[k];
        if (e.start) o.startPlayers |= 1u << e.player;
        else o.placedDepotMask |= 1u << e.depot;
    }
}
// The start locations' players, of all entries and of the selected ones (a bit for each player; --script's simstate).
unsigned simStartPlayers(bool selectedOnly) {
    unsigned mask = 0;
    const unsigned selected = simSelectedBases();
    for (size_t k = 0; k < gSimBaseEntries.size(); ++k)
        if (gSimBaseEntries[k].start && (!selectedOnly || (selected & (1u << k)))) mask |= 1u << gSimBaseEntries[k].player;
    return gSimBaseMap == gState.ed.get() ? mask : 0;
}
std::wstring simBaseName(const SimBaseEntry& e) {
    wchar_t text[64];
    std::swprintf(text, 64, e.start ? tr(L"Player %d") : tr(L"Base %d"), e.start ? e.player + 1 : e.depot + 1);
    return text;
}
// A base's name in the report: its name in the panel, else "Base k" (`start`: "Base k (start)" for a start location).
std::wstring simBaseReportName(const sim::MiningBase& b, size_t k, bool start) {
    if (gSimBaseMap == gState.ed.get())
        for (const SimBaseEntry& e : gSimBaseEntries)
            if (e.x == b.x && e.y == b.y && e.start == b.startLocation) return simBaseName(e);
    wchar_t text[64];
    std::swprintf(text, 64, start && b.startLocation ? tr(L"Base %d (start)") : tr(L"Base %d"), int(k) + 1);
    return text;
}

// Another selection of bases: the game starts again on the same engine, so each base that mined before keeps its boost
// searches (bar_sim.cpp: beginMining). The play stays as it was (no base: it waits).
void simMiningReselect() {
    if (!gSim.shown || gSim.combat || gSim.rush || !gSim_) return;
    simSkipJoin();
    gSim.skipping = false;
    const uint32_t seed = gSim.seed;
    gSim.start = [seed](std::string& err) { return simEngine().startMining(simMiningOptions(seed), err); };
    std::string err;
    if (!simEngine().setMap(gSim.chk, err) || !gSim.start(err)) {
        if (gRoot) msgbox::ask(*gRoot, tr(L"Simulation"), widen(err), {tr(L"OK")}, msgbox::Icon::Error);
        return simClose();
    }
    gSim.bases = int(simEngine().miningResult().size());
    gSim.done = false, gSim.due = 0, gSim.last = std::chrono::steady_clock::now();
    if (!simMiningHasBases()) gSim.playing = false;
    simRedraw();
}

// The settings as the panel shows them (optionsview has their ranges and default values).
struct SimSettings {
    int kind = 0, perField = 1, total = 9, races = 0;  // the workers: 0 for each mineral field (perField), 1 in all (total)
    int metric = 0;                                    // the field labels: 0 frames a trip, 1 minerals a minute
    int search[3] = {};                                // each boost search's search time in seconds
    bool boost[3] = {}, guides[3] = {true, true, true};  // the boosts in use; their guides shown
    int workers() const { return kind == 0 ? perField : total; }
};
const wchar_t* const kSimBoostKeys[3] = {L"SimBoostPosition", L"SimBoostCargo", L"SimBoostCycle"};
const wchar_t* const kSimGuideKeys[3] = {L"SimShowPosition", L"SimShowCargo", L"SimShowCycle"};
const wchar_t* const kSimBoostNames[3] = {L"Position boost", L"Cargo boost", L"Cycle boost"};
SimSettings simSettings() {
    SimSettings s;
    prefs::number(L"SimWorkerKind", s.kind, 0, 1);
    prefs::number(L"SimWorkersPerField", s.perField, 1, optionsview::kSimPerFieldMax);
    prefs::number(L"SimWorkers", s.total, 1, optionsview::kSimWorkersMax);
    prefs::number(L"SimRaces", s.races, 0, 3);
    prefs::number(L"SimPatchMetric", s.metric, 0, 1);
    for (int k = 0; k < 3; ++k) {
        s.search[k] = optionsview::simBoostSeconds(k);
        prefs::flag(kSimBoostKeys[k], s.boost[k]);
        prefs::flag(kSimGuideKeys[k], s.guides[k]);
    }
    return s;
}

bool simBoostBusy() { return simSkipRunning() || simEngine().boosting(); }
// Search (Continue) can start: nothing runs, the search has more to do, a base is selected (not a Cosmonarchy BW map).
bool simBoostCanSearch(int k) {
    return !modTab() && !simBoostBusy() && !simEngine().boostStatus(k).exhausted && simMiningHasBases();
}
void simBoostSearchNow(int k);

// The workers that a boost's guides show (cargo and cycle guides show after their worker's first command).
int simDrawnBoostWorkers(int kind) {
    std::set<std::pair<int, int>> workers;
    for (const auto& guide : gSimGuides)
        if (guide.kind == kind) workers.insert({guide.base, guide.worker});
    return int(workers.size());
}
// A boost's state: searching, how many workers it made faster, or nothing yet.
std::wstring simBoostState(int kind) {
    if (!simMiningHasBases()) return tr(L"Select a base");
    const sim::BoostStatus st = simEngine().boostStatus(kind);
    if (st.running) return tr(L"Searching\x2026");
    if (!st.finished) return tr(L"Not searched");
    if (st.failed && st.found == 0) return tr(L"Could not search");
    wchar_t text[128];
    if (st.found == 0) std::swprintf(text, 128, L"%ls", tr(L"None found"));
    else if (st.found == 1) std::swprintf(text, 128, L"%ls", tr(L"1 worker faster"));
    else std::swprintf(text, 128, tr(L"%d workers faster"), st.found);
    std::wstring out = text;
    const int drawn = simDrawnBoostWorkers(kind);
    bool show = true;
    prefs::flag(kSimGuideKeys[kind], show);
    if (show && drawn < st.found) std::swprintf(text, 128, tr(L"%d of %d guides show"), drawn, st.found);
    else std::swprintf(text, 128, tr(L"%.1f s searched"), st.seconds);
    return out + L" \x00b7 " + text;
}

// A search time's next value up (dir 1) or down (-1): 1 to 5 one at a time, then 5 at a time to 60, then 30 at a time.
int simSearchStep(int v, int dir) {
    const int unit = (dir > 0 ? v >= 60 : v > 60) ? 30 : (dir > 0 ? v >= 5 : v > 5) ? 5 : 1;
    return dir > 0 ? (v / unit + 1) * unit : ((v + unit - 1) / unit - 1) * unit;
}

// A number box as the tool strip's (docs/dev/CONTROLS.md, "Number box"): the value at the right of a field (a small
// unit after it) and up and down arrows at the box's right. A click on the value types a number (Enter or leaving the
// box sets it, into the range; Esc gives back the value); the arrows, Up and Down and the mouse wheel step it.
class SimNumberBox final : public draw::View {
public:
    int lo = 1, hi = 100;
    const wchar_t* unit = L"";
    std::wstring tipText;
    std::function<int(int, int)> next;  // the value one step up (1) or down (-1) from a value; null: 1 at a time
    std::function<void(int)> onChange;  // a new value (typed or stepped)
    bool keepOnLeave = true;            // leaving the box sets a typed number (off: only Enter does; leaving drops it)
    SimNumberBox() {
        field_.setPlain(true), field_.setAlignRight(true), field_.setMaxLength(3);
        field_.setFilter([](wchar_t ch) { return ch >= L'0' && ch <= L'9'; });
        field_.onEnter = [this] { commit(); };
        field_.onFocusLost = [this] {
            if (keepOnLeave && !dropping_) commit();
            else field_.setText(std::to_wstring(value_));
        };
        field_.onEscape = [this] {
            field_.setText(std::to_wstring(value_));
            if (gRoot) gRoot->map.focus();
        };
        field_.onStep = [this](int dir) {
            commit();
            step(dir);
            field_.model().selectAll();
        };
        add(field_, {0, 0, 0, 0});
    }
    int value() const { return value_; }
    void setValue(int v) {
        if (v != value_) value_ = v, invalidate();
        const std::wstring text = std::to_wstring(v);
        if (!field_.hasFocus() && field_.text() != text) field_.setText(text);
    }
    // The keyboard goes back to the map; a typed number that is not set yet is dropped.
    void dropFocus() {
        if (!field_.hasFocus() || !gRoot) return;
        dropping_ = true;
        field_.setText(std::to_wstring(value_));
        gRoot->map.focus();
        dropping_ = false;
    }
    void setEnabled(bool on) {
        if (on == enabled_) return;
        if (!on) dropFocus();
        enabled_ = on;
        field_.setVisible(on);
        invalidate();
    }
    bool editing() const { return field_.hasFocus(); }
    // The typed number into the range (empty: the value as it was).
    void commit() {
        const std::wstring& text = field_.text();
        const int v = text.empty() ? value_ : int(std::clamp<long>(std::wcstol(text.c_str(), nullptr, 10), lo, hi));
        field_.setText(std::to_wstring(v));
        set(v);
    }
    void paint(draw::Canvas& c) override {
        const Parts p = parts(c);
        kitpaint::drawFieldBox(c, {0, 0, width(), height()}, field_.hasFocus() ? kitpaint::Field::Focus : hot_ && enabled_ ? kitpaint::Field::Hot : kitpaint::Field::Normal);
        if (field_.frame() != p.field) field_.setFrame(p.field);
        if (!enabled_) c.text(p.field, std::to_wstring(value_), kFaint, draw::kMiddle | draw::kRight);
        if (*unit) {
            const draw::Font of = c.setFont(c.font(kitpaint::smallFontDesc()));
            c.text({p.field.right + scale(2), 0, p.up.left, height()}, unit, kFaint, draw::kMiddle | draw::kLeft);
            c.setFont(of);
        }
        for (const int dir : {1, -1}) {
            const draw::Rect k = dir > 0 ? p.up : p.down;
            const bool can = enabled_ && (dir > 0 ? value_ < hi : value_ > lo), over = can && hotPart_ == dir;
            if (over) c.roundBox(k, kCtlHot, draw::kNone, float(scale(2)));
            c.icon(k, int(dir > 0 ? UiIcon::ChevronUp : UiIcon::ChevronDown), !can ? kFaint : over ? kText : kDim, scale(16));
        }
    }
    bool mouse(const draw::MouseEvent& e) override {
        const draw::Rect up = arrow(1), down = arrow(-1);
        const int part = e.kind == draw::MouseKind::Leave ? 0 : up.contains(e.pt) ? 1 : down.contains(e.pt) ? -1 : 0;
        const bool in = e.kind != draw::MouseKind::Leave;
        if (part != hotPart_ || in != hot_) hotPart_ = part, hot_ = in, invalidate();
        if (!enabled_ || !in) return in;
        if (e.kind == draw::MouseKind::Wheel && !e.horizontal) {
            wheel_ += e.wheel;  // (a trackpad's fractions add up to whole steps)
            for (; wheel_ >= 1; wheel_ -= 1) commit(), step(1);
            for (; wheel_ <= -1; wheel_ += 1) commit(), step(-1);
            return true;
        }
        if ((e.kind == draw::MouseKind::Down || e.kind == draw::MouseKind::DoubleClick) && e.button == 0) {
            if (part) commit(), step(part);
            else field_.focus(), field_.model().selectAll();  // (a click beside the number: it types one)
        }
        return true;
    }
    std::wstring tip(draw::Point pt) override {
        if (enabled_ && arrow(1).contains(pt)) return tr(L"One step more");
        if (enabled_ && arrow(-1).contains(pt)) return tr(L"One step less");
        return tipText;
    }

private:
    struct Parts {
        draw::Rect field, up, down;
    };
    draw::Rect arrow(int dir) const {
        const int spin = scale(14), pad = scale(4), mid = height() / 2;
        return dir > 0 ? draw::Rect{width() - spin, pad, width() - scale(2), mid} : draw::Rect{width() - spin, mid, width() - scale(2), height() - pad};
    }
    Parts parts(draw::Canvas& c) const {
        int unitW = 0;
        if (*unit) {
            const draw::Font of = c.setFont(c.font(kitpaint::smallFontDesc()));
            unitW = c.measure(unit) + scale(2);
            c.setFont(of);
        }
        const draw::Rect up = arrow(1);
        return {{scale(4), scale(2), up.left - scale(2) - unitW, height() - scale(2)}, up, arrow(-1)};
    }
    void set(int v) {
        if (v == value_) return;
        value_ = v, invalidate();
        if (onChange) onChange(v);
    }
    void step(int dir) {
        const int v = std::clamp(next ? next(value_, dir) : value_ + dir, lo, hi);
        field_.setText(std::to_wstring(v));
        set(v);
    }
    StripNumberField field_;
    int value_ = 0, hotPart_ = 0;
    bool enabled_ = true, hot_ = false, dropping_ = false;
    float wheel_ = 0;
};

// The mining's settings panel (the bar's gear), under the bar, over the map: Mining (the workers on minerals, the races,
// the field labels on the map, the bases that mine) and Boost searches (for each boost: its search time, Search or
// Continue, Show guides, its progress and its state). The kit's controls, in the rows of Program options.
void simMiningReselect();
// The panel's change of the workers or the races: the mining again, while it shows and no search or skip runs.
void simMiningRestart() {
    if (gSim.shown && !gSim.combat && !gSim.rush && !simBoostBusy()) simKeepPlay(simulateMiningCommand);
}
class SimMiningPanelView final : public draw::View {
public:
    kit::Button kind[2], metric[2], search[3];
    SimNumberBox workers, budget[3];
    kit::Dropdown races;
    kit::CheckBox guides[3], bases[kSimBaseEntriesMax];
    kit::ProgressBar progress[3];

    SimMiningPanelView() {
        const wchar_t* const kinds[2] = {L"Each field", L"In all"};
        const wchar_t* const kindTips[2] = {L"A number of workers for each mineral field of a base", L"A number of workers for each base"};
        const wchar_t* const metrics[2] = {L"Frames a trip", L"Minerals a minute"};
        const wchar_t* const metricTips[2] = {
            L"The last trip of a worker at each mineral field, delivery to delivery, in frames. Zoom in to see the labels.",
            L"The minerals that each mineral field gives in a game minute, after the first 30 game seconds. Zoom in to see the labels."};
        for (int i = 0; i < 2; ++i) {
            kind[i].setText(kinds[i]), kind[i].setTip(tr(kindTips[i])), kind[i].setJoined(i > 0, i < 1);
            kind[i].onClick = [i] {
                if (gRoot) gRoot->map.focus();
                if (simSettings().kind == i) return;
                prefs::setNumber(L"SimWorkerKind", i);
                shellPost([] { simMiningRestart(); });
            };
            metric[i].setText(metrics[i]), metric[i].setTip(tr(metricTips[i])), metric[i].setJoined(i > 0, i < 1);
            metric[i].onClick = [i] {
                prefs::setNumber(L"SimPatchMetric", i);
                simRedraw();
                if (gRoot) gRoot->map.focus();
            };
        }
        workers.keepOnLeave = false;
        workers.tipText = tr(L"The number of workers: 1 to 8 for each mineral field, 1 to 60 for each base. Click to type a number. The arrows and the mouse wheel change it.");
        workers.onChange = [](int v) {
            prefs::setNumber(simSettings().kind == 0 ? L"SimWorkersPerField" : L"SimWorkers", v);
            shellPost([] { simMiningRestart(); });  // (the mining again: its boosts were for those workers)
        };
        std::vector<kit::DropdownItem> items;
        for (const wchar_t* name : kSimRaceNames) items.push_back({name});
        races.setItems(std::move(items));
        races.wheelSteps = false;  // (each pick starts the mining again)
        races.onChange = [](int i) {
            prefs::setNumber(L"SimRaces", std::clamp(i, 0, 3));
            shellPost([] { simMiningRestart(); });
        };
        for (int k = 0; k < kSimBaseEntriesMax; ++k) {
            bases[k].setPlainLabel(true), bases[k].setTip(tr(L"Mine at this base. At most 8 bases mine at the same time."));
            bases[k].onChange = [this, k](bool) {
                const unsigned bit = 1u << k, selected = simSelectedBases();
                if (!(selected & bit) && simSelectedBaseCount() >= kSimBasesMax) return bases[k].setOn(false);  // (at most 8 bases mine)
                gSimSelectedBases = selected ^ bit;
                if (gRoot) gRoot->map.focus();
                shellPost([] { simMiningReselect(); });
            };
        }
        for (int k = 0; k < 3; ++k) {
            budget[k].unit = L"s", budget[k].lo = optionsview::kSimSearchMin, budget[k].hi = optionsview::kSimSearchMax;
            budget[k].next = simSearchStep;
            budget[k].tipText = tr(L"Search time in seconds for the next Search or Continue, 1 to 600. Click to type a number. The arrows and the mouse wheel change it.");
            budget[k].onChange = [k](int v) { prefs::setNumber(optionsview::kSimSearchKeys[k], v), simRedraw(); };
            search[k].setTip(tr(L"Search for this boost at the selected bases. Continue searches on from where it stopped."));
            search[k].onClick = [k] {
                simBoostSearchNow(k);
                if (gRoot) gRoot->map.focus();
            };
            guides[k].setLabel(L"Show guides"), guides[k].setPlainLabel(true);
            guides[k].setTip(tr(L"Show or hide the guides of this boost. The boost stays in use."));
            guides[k].onChange = [k](bool on) {
                prefs::setNumber(kSimGuideKeys[k], on ? 1 : 0);
                simRedraw();
                if (gRoot) gRoot->map.focus();
            };
        }
        for (auto* b : {&kind[0], &kind[1], &metric[0], &metric[1], &search[0], &search[1], &search[2]}) add(*b, {0, 0, 0, 0});
        add(workers, {0, 0, 0, 0}), add(races, {0, 0, 0, 0});
        for (auto& b : budget) add(b, {0, 0, 0, 0});
        for (auto& g : guides) add(g, {0, 0, 0, 0});
        for (auto& p : progress) add(p, {0, 0, 0, 0});
        for (auto& b : bases) add(b, {0, 0, 0, 0}), b.setVisible(false);
    }

    // The panel's height for its rows (the bases' rows, the note on shared processor time).
    static int heightFor(int baseCount, bool shareNote, bool boosts) {
        const int baseRows = std::max(1, (baseCount + 2) / 3);
        return scale(6) + (scale(kHeadH) + scale(6)) + 3 * scale(kRowH) + baseRows * scale(kBaseRowH) + scale(4) +
               (boosts ? scale(4) + scale(kHeadH) + scale(6) + 3 * (scale(kRowH) + scale(kNoteH)) + (shareNote ? scale(kShareH + 4) : 0) : 0) +
               scale(12);
    }
    // The content's height (heightFor) when it is taller than the panel: the wheel scrolls it.
    void setContentHeight(int h) {
        contentH_ = h;
        const int most = std::max(0, contentH_ - height());
        if (scroll_ > most) scroll_ = most, invalidate();
    }
    // The values and states from the settings and the engine (each frame), and the controls' places (`c` measures).
    void sync(draw::Canvas& c) {
        const SimSettings s = simSettings();
        const bool busy = simBoostBusy();
        for (int i = 0; i < 2; ++i) {
            kind[i].setTone(s.kind == i ? kit::Tone::Primary : kit::Tone::Normal), kind[i].setEnabled(!busy);
            metric[i].setTone(s.metric == i ? kit::Tone::Primary : kit::Tone::Normal);
        }
        workers.hi = s.kind == 0 ? optionsview::kSimPerFieldMax : optionsview::kSimWorkersMax;
        workers.setValue(s.workers()), workers.setEnabled(!busy);
        if (!races.listOpen()) races.setValue(s.races);
        races.setEnabled(!busy);
        if (races.hasFocus() && !races.listOpen() && gRoot) gRoot->map.focus();  // (a list closed with no change: Space plays again)
        const unsigned selected = simSelectedBases();
        const bool full = simSelectedBaseCount() >= kSimBasesMax;
        for (int k = 0; k < kSimBaseEntriesMax; ++k) {
            const bool shown = k < int(gSimBaseEntries.size());
            if (!shown) {
                if (bases[k].visible()) bases[k].setVisible(false);
                continue;
            }
            const std::wstring name = simBaseName(gSimBaseEntries[size_t(k)]);
            if (bases[k].label() != name) bases[k].setLabel(name);
            const bool on = (selected & (1u << k)) != 0;
            bases[k].setOn(on);
            if (bases[k].enabled() != (!busy && (on || !full))) bases[k].setEnabled(!busy && (on || !full));
        }
        for (int k = 0; k < 3; ++k) {
            budget[k].setValue(s.search[k]);
            const sim::BoostStatus st = simEngine().boostStatus(k);
            const std::wstring text = st.running ? tr(L"Searching\x2026") : st.finished ? tr(L"Continue") : tr(L"Search");
            if (search[k].text() != text) search[k].setText(text);
            if (search[k].enabled() != simBoostCanSearch(k)) search[k].setEnabled(simBoostCanSearch(k));
            guides[k].setOn(s.guides[k]);
            progress[k].setValue(float(std::clamp(st.progress, 0.0, 1.0)));
        }
        layout(c);
    }
    void paint(draw::Canvas& c) override {
        const int W = width(), H = height();
        c.roundBox({0, 0, W, H}, kPanel, kBorder, float(scale(8)));
        for (const Heading& h : headings_) kitpaint::drawSectionHeader(c, h.x0, h.right, h.y, scale(kHeadH), h.name);
        for (const RowLabel& l : labels_) c.text(l.r, l.name, kDim, draw::kMiddle | draw::kEllipsis);
        if (gSimBaseEntries.empty()) c.text(noBases_, L"None", kDim, draw::kMiddle);
        for (int k = 0; k < int(gSimBaseEntries.size()) && k < kSimBaseEntriesMax; ++k) {  // (each base's player color)
            const uint32_t rgba = playerRgba(gSimBaseEntries[size_t(k)].player, 255);
            c.roundBox(swatch_[k], draw::rgb(int(rgba >> 24), int(rgba >> 16 & 255), int(rgba >> 8 & 255)), kBorder, float(scale(2)));
        }
        for (int k = 0; k < 3; ++k)
            if (!status_[k].empty()) c.text(status_[k], simBoostState(k), kDim, draw::kMiddle | draw::kEllipsis);
        if (!share_.empty()) {
            c.roundBox(share_, kCtl, kAccent, float(scale(stardraft::kit::kRadius)));
            c.text({share_.left + scale(10), share_.top + scale(4), share_.right - scale(10), share_.bottom - scale(4)},
                   L"The selected bases share the processor. With fewer bases, each search does more work.", kText, draw::kWrap | draw::kMiddle);
        }
    }
    bool mouse(const draw::MouseEvent& e) override {  // (the map under it gets none)
        if (e.kind == draw::MouseKind::Wheel && !e.horizontal && contentH_ > height()) {
            const int most = contentH_ - height(), was = scroll_;
            scroll_ = std::clamp(scroll_ - int(std::lround(e.wheel * float(scale(kRowH)))), 0, most);
            if (scroll_ != was) invalidate(), simRedraw();
        }
        return e.kind != draw::MouseKind::Leave;
    }
    std::wstring tip(draw::Point pt) override {
        for (int k = 0; k < 3; ++k)
            if (status_[k].contains(pt) || progress[k].frame().contains(pt))
                return tr(L"Workers that this boost made faster at the selected bases. A guide shows after its worker does its first command.");
        return {};
    }
    // The keyboard is on one of the panel's controls.
    bool focusInside() const {
        for (const draw::View* v : children())
            if (v->hasFocus()) return true;
        if (workers.editing()) return true;
        for (const auto& b : budget)
            if (b.editing()) return true;
        return false;
    }

private:
    int contentH_ = 0, scroll_ = 0;
    static constexpr int kHeadH = 28, kRowH = 32, kBaseRowH = 28, kNoteH = 18, kShareH = 44, kLabelW = 150;
    struct Heading {
        int x0, right, y;
        const wchar_t* name;
    };
    struct RowLabel {
        draw::Rect r;
        const wchar_t* name;
    };
    std::vector<Heading> headings_;
    std::vector<RowLabel> labels_;
    draw::Rect status_[3], swatch_[kSimBaseEntriesMax], noBases_, share_;

    static void place(draw::View& v, draw::Rect r) {
        if (v.frame() != r) v.setFrame(r);
        if (!v.visible()) v.setVisible(true);
    }
    void layout(draw::Canvas& c) {
        headings_.clear(), labels_.clear();
        const int W = width(), pad = scale(12), x0 = pad, right = W - pad, fx = x0 + scale(kLabelW);
        const int rowH = scale(kRowH), fieldH = scale(stardraft::kit::kFieldH);
        auto fieldTop = [&](int y) { return y + (rowH - fieldH) / 2; };
        int y = scale(6) - scroll_;
        auto heading = [&](const wchar_t* name) {
            headings_.push_back({x0, right, y, name});
            y += scale(kHeadH) + scale(6);
        };
        auto label = [&](const wchar_t* name, int h) { labels_.push_back({{x0, y, fx - scale(8), y + h}, name}); };
        // (a choice of two: joined buttons, the chosen one Primary, as Program options' choices)
        auto segmented = [&](kit::Button* bs, int x) {
            for (int i = 0; i < 2; ++i) {
                const int w = bs[i].preferredWidth(c);
                place(bs[i], {x, fieldTop(y), x + w, fieldTop(y) + fieldH});
                x += w;
            }
            return x;
        };
        heading(L"Mining");
        label(L"Workers on minerals", rowH);
        {
            const int x = segmented(kind, fx) + scale(8);
            place(workers, {x, fieldTop(y), std::min(right, x + scale(64)), fieldTop(y) + fieldH});
        }
        y += rowH;
        label(L"Races", rowH);
        place(races, {fx, fieldTop(y), right, fieldTop(y) + fieldH});
        y += rowH;
        label(L"Field labels", rowH);
        segmented(metric, fx);
        y += rowH;
        // The bases: three in a row, each with its player's color before its check box.
        label(L"Bases", scale(kBaseRowH));
        const int n = std::min(int(gSimBaseEntries.size()), kSimBaseEntriesMax), colW = std::max(scale(40), (right - fx) / 3);
        noBases_ = n ? draw::Rect{} : draw::Rect{fx, y, right, y + scale(kBaseRowH)};
        for (int k = 0; k < n; ++k) {
            const int x = fx + colW * (k % 3), top = y + scale(kBaseRowH) * (k / 3), s = scale(10), mid = top + scale(kBaseRowH) / 2;
            swatch_[k] = {x, mid - s / 2, x + s, mid - s / 2 + s};
            place(bases[k], {x + s + scale(6), top, x + colW - scale(4), top + scale(kBaseRowH)});
        }
        y += scale(kBaseRowH) * std::max(1, (n + 2) / 3) + scale(8);
        if (modTab()) {  // (a Cosmonarchy BW map: no boost searches)
            for (int k = 0; k < 3; ++k) {
                for (draw::View* v : {static_cast<draw::View*>(&budget[k]), static_cast<draw::View*>(&search[k]), static_cast<draw::View*>(&guides[k]),
                                      static_cast<draw::View*>(&progress[k])})
                    if (v->visible()) v->setVisible(false);
                status_[k] = {};
            }
            budget[0].dropFocus(), budget[1].dropFocus(), budget[2].dropFocus();
            share_ = {};
            return;
        }
        // The boost searches: a row for each (its search time, Search or Continue, Show guides), its progress and state under it.
        heading(L"Boost searches");
        int buttonW = 0;
        for (const wchar_t* t : {L"Search", L"Continue", L"Searching\x2026"}) buttonW = std::max(buttonW, c.measure(t) + scale(28));
        buttonW = std::max(buttonW, scale(stardraft::kit::kButtonMinW));
        for (int k = 0; k < 3; ++k) {
            label(kSimBoostNames[k], rowH);
            const int bx = fx + scale(72) + scale(8), gx = bx + buttonW + scale(12);
            place(budget[k], {fx, fieldTop(y), fx + scale(72), fieldTop(y) + fieldH});
            place(search[k], {bx, fieldTop(y), bx + buttonW, fieldTop(y) + fieldH});
            place(guides[k], {gx, y, std::max(gx, right), y + rowH});
            y += rowH;
            const int barH = scale(8), mid = y + scale(kNoteH) / 2 - scale(4);
            place(progress[k], {x0, mid - barH / 2, fx - scale(16), mid - barH / 2 + barH});
            status_[k] = {fx, y - scale(4), right, y + scale(kNoteH) - scale(4)};
            y += scale(kNoteH);
        }
        share_ = std::chrono::steady_clock::now() < gSimShareUntil ? draw::Rect{x0, y + scale(4), right, y + scale(4) + scale(kShareH)} : draw::Rect{};
    }
} gSimPanelView;
draw::View* simMiningOverlayView() { return &gSimPanelView; }
void simInvalidateMiningControls() { gSimPanelView.invalidate(); }
void simHideMiningControls() {
    if (gSimPanelView.races.listOpen()) gSimPanelView.races.closeList();
    if (gRoot && gSimPanelView.visible() && gSimPanelView.focusInside()) {  // (a typed number that is not set: dropped)
        gSimPanelView.workers.dropFocus();
        for (auto& b : gSimPanelView.budget) b.dropFocus();
        gRoot->map.focus();
    }
    gSimPanelView.setVisible(false);
}
// Each frame: the panel under the bar (in the map's middle), while it is open; its values and states.
void simMiningControlsFrame() {
    if (!gRoot || !gSim.shown || gSim.combat || gSim.rush || !gSimPanel.open || gRoot->map.width() < scale(180) || gRoot->map.height() < scale(100))
        return simHideMiningControls();
    const draw::Rect map = gRoot->map.frame(), view{0, 0, map.width(), map.height()};
    const SimBarLayout l = simLayout(view);
    const int w = std::min(view.width() - scale(16), scale(480)), left = std::max(scale(8), (l.bar.left + l.bar.right - w) / 2);
    const int top = l.bar.bottom + scale(6);
    const int full = SimMiningPanelView::heightFor(int(gSimBaseEntries.size()), std::chrono::steady_clock::now() < gSimShareUntil, !modTab());
    const int h = std::min(full, view.height() - top - scale(4));
    const draw::Rect frame{map.left + left, map.top + top, map.left + left + w, map.top + top + std::max(0, h)};
    if (gSimPanelView.parent() != gRoot || gRoot->children().back() != &gSimPanelView) gRoot->add(gSimPanelView, frame);
    else if (gSimPanelView.frame() != frame) gSimPanelView.setFrame(frame);
    if (!gSimPanelView.visible()) gSimPanelView.setVisible(true);
    gSimPanelView.setContentHeight(full);
    ImDrawList list(ImGui::GetDrawListSharedData());  // (the controls' text measured with a canvas of its own)
    kit_sdl::ImguiCanvas measure(list, nullptr);
    gSimPanelView.sync(measure);
}

// A boost turned off (setting SimBoostPosition, SimBoostCargo, SimBoostCycle) is taken out. A search starts only from
// its own Search (Continue), never after another search.
void simBoostSync() {
    if (!gSim.shown || gSim.combat || gSim.rush || simSkipRunning() || modTab()) return;  // (a Cosmonarchy BW map: no boosts)
    const SimSettings s = simSettings();
    for (int k = 0; k < 3; ++k) {
        const sim::BoostStatus st = simEngine().boostStatus(k);
        if (!s.boost[k] && (st.running || st.finished)) simEngine().clearBoost(k), simRedraw();
    }
}
// A boost's Search (or Continue): the boost is in use, and its search starts now for its search time at each selected
// base; a search that finished goes on from where it stopped (the engine keeps what it found). The game plays on.
void simBoostSearchNow(int k) {
    if (!gSim.shown || gSim.combat || gSim.rush || !simBoostCanSearch(k)) return;
    gSimPanelView.budget[k].commit();
    prefs::setNumber(kSimBoostKeys[k], 1);
    gSim.boostFailed &= ~(1 << k);
    std::string error;
    if (!simEngine().startBoost(k, double(optionsview::simBoostSeconds(k)), error)) {
        gSim.boostFailed |= 1 << k;
        if (gRoot) shellPost([error] { msgbox::ask(*gRoot, tr(L"Simulation"), widen(error), {tr(L"OK")}, msgbox::Icon::Info); });
        return;
    }
    if (simSelectedBaseCount() > 1) {  // (the note on shared processor time, for 6 seconds)
        gSimShareUntil = std::chrono::steady_clock::now() + std::chrono::seconds(6);
        const unsigned ticket = ++gSimShareTicket;
        if (gHost)
            gHost->timer(6000, [ticket] {
                if (ticket == gSimShareTicket) gSimShareUntil = {}, simRedraw();
            });
    }
    gSim.done = false, gSim.due = 0;
    simRedraw();
}
