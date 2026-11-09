// Mining's panel (sdl_simulation.inc includes it). It opens with the mining; the bar's gear shows or hides it. Under the
// bar: the workers on minerals (for each mineral field or in all, and their number), the races, the field labels on
// the map (frames a trip or minerals a minute) and the bases that mine. In the right side bar: the three boost searches
// (position, cargo, cycle: the pull request's Mining Lab searches), each with its search time, Show guides and Search.
// A Cosmonarchy BW map has no boost searches (its mining runs on StarDraft's own engine).
void simulateMiningCommand();
sim::MiningOptions simMiningOptions(uint32_t seed);

// The map's bases that can mine (MiningOptions: the start locations, by player, then the placed depots that are not
// near a start location), each a check box in the panel's Bases. The selection has a bit for each entry. At most
// kSimBasesMax bases mine at a time (bar_sim.cpp: kMaxBases).
struct SimBaseEntry {
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
    std::vector<bool> taken(size_t(kSimBasesMax), false);
    for (const sim::MiningBase& b : bases)
        if (b.startLocation && b.player >= 0 && b.player < kSimBasesMax) taken[size_t(b.player)] = true;
    std::vector<SimBaseEntry> starts, depots;
    for (size_t m = 0; m < bases.size(); ++m) {
        SimBaseEntry e;
        e.start = bases[m].startLocation;
        if (e.start) {
            if (bases[m].player < 0 || bases[m].player >= kSimBasesMax) continue;
            e.player = bases[m].player;
            starts.push_back(e);
            continue;
        }
        e.depot = int(depots.size()), e.player = -1;
        for (int p = 0; e.player < 0 && p < kSimBasesMax; ++p)
            if (!taken[size_t(p)]) e.player = p, taken[size_t(p)] = true;
        if (e.player < 0) e.player = int(m % size_t(kSimBasesMax));  // (more bases than players: as bar_sim.cpp)
        depots.push_back(e);
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
    if (!simSelectedBases()) gSim.playing = false;
    simRedraw();
}

// The panel under the bar: three rows (the workers, the races, the field labels), then the bases (three in a row).
// Its parts (gSimPanel.hot, .down): 1 and 2 the workers' - and +, 3 the races, 13 and 14 for each field and in all,
// 41 and 42 the field labels' two kinds, 60 and more the bases, 12 the rest of the panel.
struct SimPanelLayout {
    draw::Rect box, value[3], kind[2], metric[2], bases[kSimBaseEntriesMax], basesLabel;
    int rowY[3] = {};
};
SimPanelLayout simPanelLayout(const SimBarLayout& l, draw::Rect r) {
    SimPanelLayout p;
    const int w = std::min(r.width() - scale(16), scale(480)), pad = scale(12), row = scale(34), boxH = scale(26);
    const int left = std::max(r.left + scale(8), (l.bar.left + l.bar.right - w) / 2);
    int y = l.bar.bottom + scale(12);
    for (int k = 0; k < 3; ++k) {
        p.rowY[k] = y;
        p.value[k] = {left + w - pad - scale(k == 0 ? 120 : 250), y + scale(4), left + w - pad, y + scale(4) + boxH};
        y += row;
    }
    const int kx = left + w - pad - scale(250), kw = scale(65);
    for (int k = 0; k < 2; ++k) p.kind[k] = {kx + k * kw, p.value[0].top, kx + (k + 1) * kw, p.value[0].bottom};
    const int half = p.value[2].width() / 2;
    p.metric[0] = {p.value[2].left, p.value[2].top, p.value[2].left + half, p.value[2].bottom};
    p.metric[1] = {p.metric[0].right, p.value[2].top, p.value[2].right, p.value[2].bottom};
    const int n = int(gSimBaseEntries.size());
    for (int k = 0; k < n; ++k) {
        const int x = kx + scale(84 * (k % 3)), top = y + scale(4 + 28 * (k / 3));
        p.bases[k] = {x, top, x + scale(80), top + scale(24)};
    }
    p.basesLabel = {left + pad, y, kx - scale(8), y + scale(28)};
    p.box = {left, l.bar.bottom + scale(6), left + w, y + scale(8 + 28 * std::max(1, (n + 2) / 3))};
    return p;
}
int simPanelPart(const SimPanelLayout& p, draw::Point pt) {
    if (!p.box.contains(pt)) return 0;
    if (optionsview::stepBoxMinus(p.value[0]).contains(pt)) return 1;
    if (optionsview::stepBoxPlus(p.value[0]).contains(pt)) return 2;
    if (p.value[1].contains(pt)) return 3;
    for (int k = 0; k < 2; ++k) {
        if (p.kind[k].contains(pt)) return 13 + k;
        if (p.metric[k].contains(pt)) return 41 + k;
    }
    for (int k = 0; k < int(gSimBaseEntries.size()); ++k)
        if (p.bases[k].contains(pt)) return 60 + k;
    return 12;
}
bool simBasePart(int part) { return part >= 60 && part < 60 + kSimBaseEntriesMax; }

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

// The boost searches in the right side bar: a card for each, with its name, its search time (seconds), its state,
// Show guides, Search (Continue) and its progress.
struct SimBoostLayout {
    draw::Rect card[3], budget[3], state[3], check[3], button[3], progress[3];
};
SimBoostLayout simBoostLayout(draw::Rect r) {
    SimBoostLayout p;
    for (int k = 0; k < 3; ++k) {
        const int y = r.top + scale(42 + 102 * k);
        p.card[k] = {r.left + scale(8), y, r.right - scale(8), y + scale(94)};
        p.budget[k] = {r.right - scale(77), y + scale(8), r.right - scale(30), y + scale(34)};
        p.state[k] = {r.left + scale(20), y + scale(34), r.right - scale(18), y + scale(54)};
        p.check[k] = {r.left + scale(20), y + scale(58), r.left + scale(132), y + scale(84)};
        p.button[k] = {r.right - scale(106), y + scale(58), r.right - scale(18), y + scale(84)};
        p.progress[k] = {r.left + scale(20), y + scale(89), r.right - scale(18), y + scale(91)};
    }
    return p;
}
// The rail's parts: 8 to 10 Show guides, 21 to 23 Search, 31 to 33 the search times, 12 the rest.
int simBoostPart(const SimBoostLayout& p, draw::Rect r, draw::Point pt) {
    for (int k = 0; k < 3; ++k) {
        if (p.check[k].contains(pt)) return 8 + k;
        if (p.button[k].contains(pt)) return 21 + k;
        if (p.budget[k].contains(pt)) return 31 + k;
    }
    return r.contains(pt) ? 12 : 0;
}
bool simBoostBusy() { return simSkipRunning() || simEngine().boosting(); }
// Search (Continue) can start: nothing runs, the search has more to do, a base is selected (not a Cosmonarchy BW map).
bool simBoostCanSearch(int k) {
    return !modTab() && !simBoostBusy() && !simEngine().boostStatus(k).exhausted && simSelectedBases() != 0;
}
void simPaintBoosts(draw::Canvas&, draw::Rect);
void simPanelPress(int);
struct SimBoostView final : draw::View {
    void paint(draw::Canvas& c) override { simPaintBoosts(c, {0, 0, width(), height()}); }
    bool mouse(const draw::MouseEvent& e) override {
        const draw::Rect r{0, 0, width(), height()};
        const int part = e.kind == draw::MouseKind::Leave ? 0 : simBoostPart(simBoostLayout(r), r, e.pt);
        if (gSimPanel.hot != part) gSimPanel.hot = part, simRedraw();
        if (e.kind == draw::MouseKind::Up) gSimPanel.down = 0, simRedraw();
        if ((e.kind == draw::MouseKind::Down || e.kind == draw::MouseKind::DoubleClick) && e.button == 0 && part != 12) {
            if (part >= 21 && part <= 23 && !simBoostCanSearch(part - 21)) return true;
            gSimPanel.down = part;
            simPanelPress(part);
        }
        return true;
    }
    std::wstring tip(draw::Point pt) override {
        const draw::Rect r{0, 0, width(), height()};
        const SimBoostLayout p = simBoostLayout(r);
        const int part = simBoostPart(p, r, pt);
        if (part >= 8 && part <= 10) return tr(L"Show or hide the guides of this boost. The boost stays in use.");
        if (part >= 21 && part <= 23) return tr(L"Search for this boost at the selected bases. Continue searches on from where it stopped.");
        for (int k = 0; k < 3; ++k)
            if (p.state[k].contains(pt)) return tr(L"Workers that this boost made faster at the selected bases. A guide shows after its worker does its first command.");
        return {};
    }
} gSimBoostView;
draw::View* simMiningOverlayView() { return &gSimBoostView; }
void simInvalidateMiningControls() { gSimBoostView.invalidate(); }

// Each search time: a number field (Enter or another focus keeps it).
struct SimMiningControls {
    struct BudgetField final : kit::TextField {
        std::wstring tip(draw::Point) override { return tr(L"Search time in seconds for the next Search or Continue"); }
    } budget[3];
    SimMiningControls() {
        for (int k = 0; k < 3; ++k) {
            budget[k].setText(std::to_wstring(optionsview::kSimSearchDefault));
            budget[k].setMaxLength(3);
            budget[k].setFilter([](wchar_t c) { return c >= L'0' && c <= L'9'; });
            budget[k].onFocusLost = [this, k] { commit(k); };
            budget[k].onEnter = [this, k] { commit(k); };
        }
    }
    void commit(int k) {
        const std::wstring& text = budget[k].text();
        const int value = text.empty() ? optionsview::kSimSearchDefault
                                       : int(std::clamp<long>(std::wcstol(text.c_str(), nullptr, 10), optionsview::kSimSearchMin, optionsview::kSimSearchMax));
        prefs::setNumber(optionsview::kSimSearchKeys[k], value);
        budget[k].setText(std::to_wstring(value));
        simRedraw();
    }
} gSimMiningControls;
void simHideMiningControls() {
    gSimBoostView.setVisible(false);
    for (auto& field : gSimMiningControls.budget) {
        if (field.hasFocus() && gRoot) gRoot->map.focus();
        field.setVisible(false);
    }
}
// Each frame: the rail at the right of the map (under the minimap), while the mining's panel is open.
void simMiningControlsFrame() {
    if (!gRoot || !gSim.shown || gSim.combat || gSim.rush || modTab() || !gSimPanel.open || gRoot->map.width() < scale(180) ||
        gRoot->map.height() < scale(100))
        return simHideMiningControls();
    const draw::Rect map = gRoot->map.frame();
    const int right = gRoot->width(), left = std::min(map.right + scale(1), right - scale(280));
    int top = map.top;
    const draw::Rect mini = gRoot->minimap.frame();
    if (gRoot->minimap.shown() && mini.left >= left && mini.right <= right) top = std::max(top, mini.bottom + scale(8));
    const draw::Rect rail{left, top, right, map.bottom};
    if (gSimBoostView.parent() != gRoot || gRoot->children().back() != &gSimBoostView) gRoot->add(gSimBoostView, rail);
    else gSimBoostView.setFrame(rail);
    gSimBoostView.setVisible(true);
    const SimBoostLayout p = simBoostLayout({0, 0, gSimBoostView.width(), gSimBoostView.height()});
    for (int k = 0; k < 3; ++k) {
        auto& field = gSimMiningControls.budget[k];
        if (field.parent() != &gSimBoostView) gSimBoostView.add(field, {});
        field.setFrame(p.budget[k]);
        field.setVisible(true);
        const std::wstring value = std::to_wstring(optionsview::simBoostSeconds(k));
        if (!field.hasFocus() && field.text() != value) field.setText(value);
    }
}

// The workers that a boost's guides show (cargo and cycle guides show after their worker's first command).
int simDrawnBoostWorkers(int kind) {
    std::set<std::pair<int, int>> workers;
    for (const auto& guide : gSimGuides)
        if (guide.kind == kind) workers.insert({guide.base, guide.worker});
    return int(workers.size());
}
// A boost card's state: searching, how many workers it made faster, or nothing yet.
std::wstring simBoostState(int kind) {
    if (!simSelectedBases()) return tr(L"Select a base");
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
void simPaintPanel(draw::Canvas& c, const SimBarLayout& l, draw::Rect r) {
    const SimPanelLayout p = simPanelLayout(l, r);
    const SimSettings s = simSettings();
    const bool busy = simBoostBusy();
    c.roundBox(p.box, kPanel, kBorder, float(scale(8)));
    const wchar_t* const names[3] = {L"Workers on minerals", L"Races", L"Field labels"};
    for (int k = 0; k < 3; ++k)
        c.text({p.box.left + scale(12), p.rowY[k], (k == 0 ? p.kind[0].left : p.value[k].left) - scale(8), p.rowY[k] + scale(34)}, tr(names[k]), kText,
               draw::kMiddle | draw::kEllipsis);
    auto segment = [&](draw::Rect b, const wchar_t* text, bool on, int part) {
        c.roundBox(b, on ? kAccentFill : gSimPanel.hot == part ? kCtlHot : kCtl, on ? kAccent : kBorder, float(scale(4)));
        c.text(b, tr(text), on ? kText : kDim, draw::kCenter | draw::kMiddle | draw::kEllipsis);
    };
    segment(p.kind[0], L"Each field", s.kind == 0, 13);
    segment(p.kind[1], L"In all", s.kind == 1, 14);
    const int most = s.kind == 0 ? optionsview::kSimPerFieldMax : optionsview::kSimWorkersMax;
    const int hot = gSimPanel.hot == 1 ? 1 : gSimPanel.hot == 2 ? 2 : 0, down = gSimPanel.down == 1 ? 1 : gSimPanel.down == 2 ? 2 : 0;
    optionsview::paintStepBox(c, p.value[0], std::to_wstring(s.workers()), hot, down, s.workers() <= 1, s.workers() >= most);
    kitpaint::drawPickFace(c, p.value[1], s.races == 0 ? std::wstring(tr(L"As defined in map")) : std::wstring(kSimRaceNames[s.races]), false,
                           gSimPanel.hot == 3);
    segment(p.metric[0], L"Frames a trip", s.metric == 0, 41);
    segment(p.metric[1], L"Minerals a minute", s.metric == 1, 42);
    c.text(p.basesLabel, tr(L"Bases"), kText, draw::kMiddle);
    if (gSimBaseEntries.empty()) {
        const int x = p.kind[0].left;
        c.text({x, p.basesLabel.top, p.box.right - scale(12), p.basesLabel.bottom}, tr(L"None"), kDim, draw::kMiddle);
        return;
    }
    const unsigned selected = simSelectedBases();
    const bool full = simSelectedBaseCount() >= kSimBasesMax;
    for (int k = 0; k < int(gSimBaseEntries.size()); ++k) {
        const draw::Rect b = p.bases[k];
        const SimBaseEntry& e = gSimBaseEntries[size_t(k)];
        const uint32_t rgba = playerRgba(e.player, 255);
        const draw::Color color = draw::rgb(int(rgba >> 24), int(rgba >> 16 & 255), int(rgba >> 8 & 255));
        const bool on = (selected & (1u << k)) != 0, can = !busy && (on || !full), over = can && gSimPanel.hot == 60 + k;
        c.roundBox(b, over ? kCtlHot : kCtl, on ? color : kBorder, float(scale(4)));
        kitpaint::drawCheckMark(c, {b.left + scale(5), b.top + scale(5), b.left + scale(19), b.top + scale(19)}, on, over, can);
        c.fill({b.right - scale(5), b.top + scale(8), b.right - scale(2), b.bottom - scale(8)}, color);
        c.text({b.left + scale(24), b.top, b.right - scale(7), b.bottom}, simBaseName(e), can ? kText : kDim, draw::kMiddle | draw::kEllipsis);
    }
}
void simPaintBoosts(draw::Canvas& c, draw::Rect r) {
    const SimBoostLayout p = simBoostLayout(r);
    const SimSettings s = simSettings();
    c.fill(r, kPanel);
    c.text({r.left + scale(16), r.top + scale(10), r.right - scale(12), r.top + scale(32)}, tr(L"Boost searches"), kText, draw::kMiddle);
    const draw::Color accents[3] = {draw::rgb(240, 201, 99), draw::rgb(105, 205, 178), draw::rgb(185, 158, 235)};
    for (int k = 0; k < 3; ++k) {
        const draw::Rect card = p.card[k];
        c.roundBox(card, kCtl, kBorder, float(scale(5)));
        c.fill({card.left, card.top + scale(8), card.left + scale(3), card.bottom - scale(8)}, accents[k]);
        c.text({card.left + scale(12), card.top + scale(6), p.budget[k].left - scale(6), card.top + scale(36)}, tr(kSimBoostNames[k]), kText,
               draw::kMiddle | draw::kEllipsis);
        c.text({p.budget[k].right + scale(5), p.budget[k].top, card.right - scale(5), p.budget[k].bottom}, tr(L"s"), kDim, draw::kMiddle);
        const draw::Rect b = p.check[k];
        const int cs = scale(14), cy = (b.top + b.bottom - cs) / 2;
        kitpaint::drawCheckMark(c, {b.left, cy, b.left + cs, cy + cs}, s.guides[k], gSimPanel.hot == 8 + k, true);
        c.text({b.left + cs + scale(6), b.top, b.right, b.bottom}, tr(L"Show guides"), kText, draw::kMiddle | draw::kEllipsis);
        c.text(p.state[k], simBoostState(k), kDim, draw::kMiddle | draw::kEllipsis);
        const sim::BoostStatus st = simEngine().boostStatus(k);
        const bool can = simBoostCanSearch(k);
        c.roundBox(p.button[k], can && gSimPanel.hot == 21 + k ? kCtlHot : kPanel, kBorder, float(scale(4)));
        c.text(p.button[k], st.running ? tr(L"Searching\x2026") : st.finished ? tr(L"Continue") : tr(L"Search"), can ? kText : kDim,
               draw::kCenter | draw::kMiddle | draw::kEllipsis);
        c.fill(p.progress[k], kBorder);
        draw::Rect progress = p.progress[k];
        progress.right = progress.left + int(progress.width() * std::clamp(st.progress, 0.0, 1.0));
        if (progress.width() > 0) c.fill(progress, accents[k]);
    }
    if (std::chrono::steady_clock::now() < gSimShareUntil) {  // (a search at more than one base)
        const draw::Rect note{r.left + scale(8), r.top + scale(360), r.right - scale(8), r.top + scale(466)};
        c.roundBox(note, kCtl, kAccent, float(scale(6)));
        c.text({note.left + scale(12), note.top + scale(10), note.right - scale(12), note.bottom - scale(8)},
               tr(L"The selected bases share the processor. With fewer bases, each search does more work."), kText, draw::kWrap);
    }
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
// A card's Search (or Continue): the boost is in use, and its search starts now for its search time at each selected
// base; a search that finished goes on from where it stopped (the engine keeps what it found). The game plays on.
void simBoostSearchNow(int k) {
    if (!gSim.shown || gSim.combat || gSim.rush || !simBoostCanSearch(k)) return;
    gSimMiningControls.commit(k);
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
// The races' list (the panel's Races): a pick saves it and starts the mining again (its boosts are searched again).
void simMiningRacesMenu() {
    if (!gRoot || !gHost) return;
    const int races = simSettings().races;
    std::vector<kit::MenuItem> items;
    for (int k = 0; k < 4; ++k) {
        kit::MenuItem item;
        item.label = kSimRaceNames[k], item.checked = k == races;
        item.run = [k] {
            prefs::setNumber(L"SimRaces", k);
            shellPost([] { simKeepPlay(simulateMiningCommand), gSimPanel.open = gSim.shown; });
        };
        items.push_back(std::move(item));
    }
    ImDrawList list(ImGui::GetDrawListSharedData());  // (the menu's text measured with a canvas of its own)
    kit_sdl::ImguiCanvas measure(list, nullptr);
    kit::Menu::popup(*gRoot, gHost->mouse(*gRoot), items, kit::Menu::measure(items, &measure), {});
}
// A press on a part of the panel or of the rail. A change of the workers or the races starts the mining again (its
// boosts are searched again: they were for those workers); another selection of bases keeps them.
void simPanelPress(int part) {
    const SimSettings s = simSettings();
    if (part == 1 || part == 2) {
        const int most = s.kind == 0 ? optionsview::kSimPerFieldMax : optionsview::kSimWorkersMax;
        prefs::setNumber(s.kind == 0 ? L"SimWorkersPerField" : L"SimWorkers", std::clamp(s.workers() + (part == 1 ? -1 : 1), 1, most));
        shellPost([] { simKeepPlay(simulateMiningCommand); });
    } else if (part == 13 || part == 14) {
        if (s.kind != part - 13) {
            prefs::setNumber(L"SimWorkerKind", part - 13);
            shellPost([] { simKeepPlay(simulateMiningCommand); });
        }
    } else if (part == 3) {
        shellPost([] { simMiningRacesMenu(); });
    } else if (part >= 8 && part <= 10) {
        prefs::setNumber(kSimGuideKeys[part - 8], s.guides[part - 8] ? 0 : 1);
    } else if (part >= 21 && part <= 23) {
        simBoostSearchNow(part - 21);
    } else if (part == 41 || part == 42) {
        prefs::setNumber(L"SimPatchMetric", part - 41);
    } else if (simBasePart(part)) {
        const unsigned bit = 1u << (part - 60), selected = simSelectedBases();
        if (!(selected & bit) && simSelectedBaseCount() >= kSimBasesMax) return;  // (at most 8 bases mine)
        gSimSelectedBases = selected ^ bit;
        shellPost([] { simMiningReselect(); });
    }
    simRedraw();
}
// The mouse on the open panel (true: taken). The wheel on the workers' step box steps it. While a search or a skip
// runs, the workers, the races and the bases wait.
bool simPanelMouse(const draw::MouseEvent& e, const SimBarLayout& l, draw::Rect r) {
    if (!gSimPanel.open || gSim.combat || gSim.rush) return false;
    const int part = e.kind == draw::MouseKind::Leave ? 0 : simPanelPart(simPanelLayout(l, r), e.pt);
    if (part != gSimPanel.hot) gSimPanel.hot = part, simRedraw();
    if (e.kind == draw::MouseKind::Up && gSimPanel.down) {
        gSimPanel.down = 0, simRedraw();
        return part != 0;
    }
    if (!part) return false;
    const bool waits = simBoostBusy() && (part == 1 || part == 2 || part == 3 || part == 13 || part == 14 || simBasePart(part));
    if (e.kind == draw::MouseKind::Wheel && e.wheel != 0) {
        if (!waits && (part == 1 || part == 2)) simPanelPress(e.wheel > 0 ? 2 : 1);
        return true;
    }
    if ((e.kind == draw::MouseKind::Down || e.kind == draw::MouseKind::DoubleClick) && e.button == 0 && part != 12 && !waits) {
        gSimPanel.down = part;
        simPanelPress(part);
    }
    return true;
}
std::wstring simPanelTip(const SimBarLayout& l, draw::Rect r, draw::Point pt) {
    if (!gSimPanel.open || gSim.combat || gSim.rush) return {};
    const int part = simPanelPart(simPanelLayout(l, r), pt);
    if (part == 13) return tr(L"A number of workers for each mineral field of a base");
    if (part == 14) return tr(L"A number of workers for each base");
    if (part == 41) return tr(L"The last trip of a worker at each mineral field, delivery to delivery, in frames. Zoom in to see the labels.");
    if (part == 42) return tr(L"The minerals that each mineral field gives in a game minute, after the first 30 game seconds. Zoom in to see the labels.");
    if (simBasePart(part)) return tr(L"Mine at this base. At most 8 bases mine at the same time.");
    return {};
}
