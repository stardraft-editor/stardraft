// The Classic triggers page on this system (a map page: the frame, the copies and the flow are sdl_map_pages.inc's, and this
// file is included there before them). The page's model and its commands are the core's (core/views_classic_triggers.inc:
// CtModel and the ctModel* functions, which the Windows page uses too); the two lists and the head are the core's views,
// with kit widgets. This file is the SDL program's part, as classic_triggers.inc is the Windows program's.
// Not on this system yet (the seam calls are the stubs here, and each one says so): the search box, the filter, the entries'
// editor (the type, the values and their pop-outs), the list pickers (the Forces multi-pick too), the block dialog, the
// right-click menus and the keys of the lists.

void sdlMapPageChanged();  // (sdl_map_pages.inc: the page's change goes into the map)

class SdlClassicTrigPage;
SdlClassicTrigPage* gSdlTrigPage = nullptr;  // the page that shows (its model is the copy's)

// The seam calls the page's commands need that this system does not have yet (the Windows program's list pickers, pop-outs,
// the block dialog and the menus).
void sdlTrigNotHere(const wchar_t* what) { setMessage(std::wstring(what) + L" is not on this system yet"); }

// The names the lists show and the search matches (the map's units, locations, switches and forces; the page's strings).
ClassicNames sdlTrigNames(const SdlClassicTrigModel& m) {
    ClassicNames n;
    n.unit = [](uint32_t u) { return narrow(unitDisplayName(int(u))); };
    n.location = [](uint32_t l) {
        const int i = int(l) - 1;
        return i >= 0 && i < gState.ed->locationCount() ? gState.ed->locationName(i) : std::string();
    };
    n.string = [&m](uint32_t id) { return narrow(sdlPlainGameText(m.strings.get(id))); };  // (without the codes)
    n.switchName = [](uint32_t s) { return gState.ed->switchName(int(s)); };
    n.force = [](uint32_t f) { return gState.ed->force(int(f)).name; };
    n.ai = &m.ai;
    return n;
}

// The head's player labels, in the page's order.
std::vector<std::wstring> sdlTrigPlayerLabels(const SdlClassicTrigModel& m) {
    const ClassicNames n = sdlTrigNames(m);
    std::vector<std::wstring> labels;
    for (int g = 0; g < kCtBoxCount; ++g) labels.push_back(widen(classicPlayerName(uint32_t(kCtBoxGroups[g]), n)));
    return labels;
}

// The page's model commands (core) run with this host: the page's list, head and map follow them.
void sdlTrigSelect(int index);
void sdlTrigUpdateHeader();
void sdlTrigInvalidate();
class SdlClassicTrigModelHost final : public CtModelHost {
public:
    ClassicNames names() override;
    void filled() override { sdlTrigInvalidate(); }
    void selectTrigger(int i) override { sdlTrigSelect(i); }
    void updateHeader() override { sdlTrigUpdateHeader(); }
    void changed() override { sdlMapPageChanged(); }
    void invalidate() override { sdlTrigInvalidate(); }
    void message(const std::wstring& text) override { setMessage(text); }
    bool ctrl() override;
    bool shift() override;
};
SdlClassicTrigModelHost gSdlTrigModelHost;

// The lists' program part (CtListHost): the commands of the trigger list are the core's; the entries' commands wait for the
// editor (the stubs say so).
class SdlClassicTrigListHost final : public CtListHost {
public:
    const std::vector<ClassicTrigger>& triggers() const override;
    const std::vector<int>& shown() const override;
    const ClassicTrigger* selectedTrigger() const override;
    int selectedIndex() const override;
    bool marked(int i) const override;
    int entryKind() const override;
    int entryIndex() const override;
    bool collapsed(int i) const override;
    ClassicNames names() const override;
    std::wstring chipText(const ClassicParam& p, uint32_t v) override;
    std::wstring chipTip(const ClassicParam& p) override { return ctKindLabel(p.kind, p.name); }
    void clickTrigger(int i) override { ctModelClickTrigger(model(), gSdlTrigModelHost, i); }
    void toggleTrigger(int i) override { ctModelToggleOne(model(), gSdlTrigModelHost, i); }
    void duplicateTrigger(int i) override { ctModelDuplicateTrigger(model(), gSdlTrigModelHost, i); }
    void deleteTrigger(int i) override { ctModelDeleteTrigger(model(), gSdlTrigModelHost, i); }
    void moveShown(int from, int to) override { ctModelMoveShown(model(), gSdlTrigModelHost, from, to); }
    void addTrigger() override { ctModelAddTrigger(model(), gSdlTrigModelHost); }
    void triggerMenu(int, draw::Point) override { sdlTrigNotHere(L"The menu"); }
    void selectEntry(int kind, int i) override;
    void toggleEntry(int, int) override { sdlTrigNotHere(L"Turning a condition or action off"); }
    void duplicateEntry(int, int) override { sdlTrigNotHere(L"Duplicating a condition or action"); }
    void deleteEntry(int, int) override { sdlTrigNotHere(L"Deleting a condition or action"); }
    void moveEntry(int, int, int) override { sdlTrigNotHere(L"Moving a condition or action"); }
    void moveRow(int, bool) override { sdlTrigNotHere(L"Moving an action into or out of a block"); }
    void editBlock(int) override { sdlTrigNotHere(L"Editing a block"); }
    void entryMenu(int, int, draw::Point) override { sdlTrigNotHere(L"The menu"); }
    void addClick(int, draw::Rect) override { sdlTrigNotHere(L"Adding a condition or action"); }
    void chipClick(int, int, int, draw::Rect) override { sdlTrigNotHere(L"Choosing a value"); }
    void toggleCollapsed(int i) override;
    void entriesLaid(int) override;

private:
    SdlClassicTrigModel& model() const;
};
SdlClassicTrigListHost gSdlTrigListHost;

// The head's player boxes and its Turn off and Delete buttons (the model's commands, then the map).
void sdlTrigPlayerBox(int box, bool on);
void sdlTrigTurnOff();
void sdlTrigDelete();

// The page: the trigger list at the left (and its note under it), the selected trigger's head, its conditions and actions at
// the right. The lists take the height that is left (as classic_triggers.inc's ctLayout places the Windows page).
class SdlClassicTrigPage final : public draw::View {
public:
    explicit SdlClassicTrigPage(SdlClassicTrigModel& m)
        : m_(m), triggers_(gSdlTrigListHost, true), entries_(gSdlTrigListHost, false), head_(sdlTrigPlayerLabels(m), makeCallbacks()) {
        gSdlTrigPage = this;
        head_.forces.setTip(L"Forces: the forces that run the trigger");
        noteText_ = sdlTrigNote(m);
        note_.setText(noteText_);
        add(triggers_, {0, 0, 0, 0});
        add(entries_, {0, 0, 0, 0});
        add(head_, {0, 0, 0, 0});
        add(note_, {0, 0, 0, 0});
        ctModelFill(m, gSdlTrigModelHost);  // the rows of the list (the Windows page fills it in WM_CREATE too)
        sdlTrigInvalidate();
        selectTrigger(m.shown.empty() ? -1 : m.shown[0]);
    }
    ~SdlClassicTrigPage() override {
        if (gSdlTrigPage == this) gSdlTrigPage = nullptr;
    }

    SdlClassicTrigModel& model() { return m_; }
    int preferredWidth() const { return scale(960); }
    int preferredHeight() const {
        const int pad = scale(stardraft::kit::kDialogPad), step = scale(stardraft::kit::kFormRowH), gap = scale(8);
        return pad + scale(40) + 2 * step + gap + scale(120) + pad;
    }
    unsigned mods() const { return host() ? host()->modifiers() : 0; }

    void invalidateLists() {
        triggers_.invalidate();
        entries_.invalidate();
    }

    // The selected trigger: its players, its header and its entries (the first condition); the list shows it.
    void selectTrigger(int index) {
        m_.sel = index >= 0 && index < int(m_.trig.triggers.size()) ? index : -1;
        m_.collapsed.clear();
        const ClassicTrigger* t = ctModelSelected(m_);
        for (int g = 0; g < kCtBoxCount; ++g) {
            head_.players[size_t(g)]->setOn(t && t->runsFor(kCtBoxGroups[g]));
            head_.players[size_t(g)]->setEnabled(t != nullptr);
        }
        updateHeader();
        entries_.resetScroll();
        const auto at = std::find(m_.shown.begin(), m_.shown.end(), m_.sel);
        if (at != m_.shown.end()) triggers_.revealShown(int(at - m_.shown.begin()));
        selectEntry(0, t && t->conditionCount() ? 0 : -1);
        sdlMapPageChanged();
    }
    void selectEntry(int kind, int i) {
        m_.kind = kind, m_.entry = i;
        entries_.invalidate();
    }
    void updateHeader() {
        const ClassicTrigger* t = ctModelSelected(m_);
        head_.title.setText(t ? tr(L"Trigger ") + std::to_wstring(m_.sel + 1) : std::wstring(L"None"));
        head_.count.setText(!t ? std::wstring()
                               : widen(std::string(t->disabled() ? "Turned off, " : "") + std::to_string(t->actionCount()) +
                                       (t->actionCount() == 1 ? " action" : " actions")));
        head_.turnOff.setText(t && t->disabled() ? L"Turn on" : L"Turn off");
        for (stardraft::kit::Button* b : {&head_.turnOff, &head_.deleteTrig, &head_.forces}) b->setEnabled(t != nullptr);
    }

    // The page's parts at its size (the head's places are its own, from 0; see classic_triggers.inc's ctLayout).
    void resized() override { place(); }
    void paint(draw::Canvas& c) override { c.fill({0, 0, width(), height()}, stardraft::kit::theme().bg); }
    void place() {
        const int W = width(), H = height();
        const int pad = scale(stardraft::kit::kDialogPad), rowH = scale(stardraft::kit::kFieldH), step = scale(stardraft::kit::kFormRowH),
                  gap = scale(8);
        const int leftW = std::clamp(W * 3 / 10, scale(280), scale(460));
        const int x0 = pad + leftW + scale(24), rightW = std::max(scale(600), W - x0 - pad);
        const bool noted = !noteText_.empty();
        const int noteH = noted ? scale(44) : 0;
        const int listBottom = H - pad - (noted ? noteH + gap : 0);
        setRect(triggers_, {pad, pad, pad + leftW, std::max(pad, listBottom)});
        setRect(note_, {pad, listBottom + gap, pad + leftW, listBottom + gap + noteH});
        // Right: the head, and the entries under it.
        const int delW = scale(150), turnW = scale(96);
        const int headW = rightW - delW - turnW - 3 * gap;
        const int headH = scale(40) + 2 * step;
        setRect(head_, {x0, pad, x0 + rightW, pad + headH});
        auto put = [](draw::View& v, int px, int py, int w, int hh) { v.setFrame({px, py, px + std::max(0, w), py + std::max(0, hh)}); };
        put(head_.title, 0, 0, std::min(headW, scale(150)), scale(28));
        put(head_.count, scale(160), scale(6), std::max(0, headW - scale(160)), rowH);
        put(head_.deleteTrig, rightW - delW, scale(2), delW, rowH);
        put(head_.turnOff, rightW - delW - gap - turnW, scale(2), turnW, rowH);
        const int colW = scale(124);
        for (int g = 0; g < 8; ++g) put(*head_.players[size_t(g)], g * colW, scale(40), colW - gap, rowH);
        put(*head_.players[8], 0, scale(40) + step, colW - gap, rowH);
        put(head_.forces, colW, scale(40) + step, scale(120), rowH);
        const int y = pad + headH + gap;
        const int entriesH = std::clamp(entries_.contentHeight(), scale(120), std::max(scale(120), H - pad - y));
        setRect(entries_, {x0, y, x0 + rightW, y + entriesH});
    }

private:
    static void setRect(draw::View& v, draw::Rect r) {
        const draw::Rect f = v.frame();
        if (f.left != r.left || f.top != r.top || f.right != r.right || f.bottom != r.bottom) v.setFrame(r);
    }
    // The note under the list: the EUD values the triggers hold (they stay as they are).
    static std::wstring sdlTrigNote(const SdlClassicTrigModel& m) {
        int eud = 0;
        for (const ClassicTrigger& t : m.trig.triggers) {
            for (int i = 0; i < t.conditionCount(); ++i) eud += classicEud(t.conditions[size_t(i)]);
            for (int i = 0; i < t.actionCount(); ++i) eud += classicEud(t.actions[size_t(i)]);
        }
        return eud ? std::to_wstring(eud) + L" conditions and actions have EUD values. They stay as they are." : std::wstring();
    }
    static CtHeadView::Callbacks makeCallbacks() {
        CtHeadView::Callbacks cb;
        cb.player = [](int box, bool on) { sdlTrigPlayerBox(box, on); };
        cb.turnOff = [] { sdlTrigTurnOff(); };
        cb.deleteTrig = [] { sdlTrigDelete(); };
        cb.forces = [] { sdlTrigNotHere(L"Choosing the forces from a list"); };  // (the Windows page's list picker)
        return cb;
    }

    SdlClassicTrigModel& m_;
    CtListView triggers_;
    CtListView entries_;
    CtHeadView head_;
    std::wstring noteText_;
    stardraft::kit::Label note_{std::wstring(), stardraft::kit::LabelTone::Dim};
};

// --- The host's methods (they need the page, which is made when the page opens) ----------------------------------------

ClassicNames SdlClassicTrigModelHost::names() { return sdlTrigNames(gSdlTrigPage->model()); }
bool SdlClassicTrigModelHost::ctrl() { return gSdlTrigPage && (gSdlTrigPage->mods() & draw::kCtrl) != 0; }
bool SdlClassicTrigModelHost::shift() { return gSdlTrigPage && (gSdlTrigPage->mods() & draw::kShift) != 0; }

void sdlTrigSelect(int index) {
    if (gSdlTrigPage) gSdlTrigPage->selectTrigger(index);
}
void sdlTrigUpdateHeader() {
    if (gSdlTrigPage) gSdlTrigPage->updateHeader();
}
void sdlTrigInvalidate() {
    if (gSdlTrigPage) gSdlTrigPage->invalidateLists();
}
void sdlTrigPlayerBox(int box, bool on) {
    if (!gSdlTrigPage) return;
    ctModelPlayerBox(gSdlTrigPage->model(), gSdlTrigModelHost, box, on);
    sdlMapPageChanged();
}
void sdlTrigTurnOff() {
    if (!gSdlTrigPage) return;
    ctModelToggleTriggers(gSdlTrigPage->model(), gSdlTrigModelHost);
    sdlMapPageChanged();
}
void sdlTrigDelete() {
    if (!gSdlTrigPage) return;
    ctModelDeleteTriggers(gSdlTrigPage->model(), gSdlTrigModelHost);
    sdlMapPageChanged();
}

SdlClassicTrigModel& SdlClassicTrigListHost::model() const { return gSdlTrigPage->model(); }
const std::vector<ClassicTrigger>& SdlClassicTrigListHost::triggers() const { return model().trig.triggers; }
const std::vector<int>& SdlClassicTrigListHost::shown() const { return model().shown; }
const ClassicTrigger* SdlClassicTrigListHost::selectedTrigger() const { return ctModelSelected(model()); }
int SdlClassicTrigListHost::selectedIndex() const { return model().sel; }
bool SdlClassicTrigListHost::marked(int i) const { return model().marked.count(i) != 0; }
int SdlClassicTrigListHost::entryKind() const { return model().kind; }
int SdlClassicTrigListHost::entryIndex() const { return model().entry; }
bool SdlClassicTrigListHost::collapsed(int i) const { return model().collapsed.count(i) != 0; }
ClassicNames SdlClassicTrigListHost::names() const { return sdlTrigNames(model()); }
std::wstring SdlClassicTrigListHost::chipText(const ClassicParam& p, uint32_t v) {
    return ctChipTextOf(p, v, names(), !ctPickerIsList(p.kind) || ctIsChoiceOf(*gState.ed, model().ai, p.kind, v));
}
void SdlClassicTrigListHost::selectEntry(int kind, int i) { gSdlTrigPage->selectEntry(kind, i); }
void SdlClassicTrigListHost::toggleCollapsed(int i) {
    SdlClassicTrigModel& m = model();
    if (m.collapsed.count(i)) m.collapsed.erase(i);
    else m.collapsed.insert(i);
    sdlMapPageChanged();
}
void SdlClassicTrigListHost::entriesLaid(int) {
    if (gSdlTrigPage) gSdlTrigPage->place();  // the entries' height changed: the page places them again
}
