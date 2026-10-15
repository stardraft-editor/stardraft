// The Classic triggers page on this system (a map page: the frame, the copies and the flow are sdl_map_pages.inc's, and this
// file is included there before them). The page's model, its commands, its parts and their layout are the core's
// (core/views_classic_triggers.inc and core/views_classic_page.inc, which the Windows page uses too): the side (the search
// box, the count, the filter), the two lists, the head, the editor of the selected entry, the pickers, the pop-outs, the
// block dialog, the right-click menus and the keys of the lists. This file is the SDL program's part (CtPageHost), as
// classic_triggers.inc is the Windows program's. The text pop-out is the core's kit dialog (runTextDialog); the Windows
// program's is the game text editor with its colors.

void sdlMapPageChanged();  // (sdl_map_pages.inc: the page's change goes into the map)

class SdlClassicTrigPage;
SdlClassicTrigPage* gSdlTrigPage = nullptr;  // the page that shows (its model is the copy's)
CtValueView* gSdlCtValueView = nullptr;      // the number pop-out while it is open

// A snapshot's dialog (the number or text pop-out): in the middle of the window, as the dialog opens centered.
void sdlCtShotDialog(std::unique_ptr<stardraft::kit::Dialog> dlg, std::unique_ptr<draw::View> content, const std::wstring& title, draw::Point size) {
    const int rw = gRoot ? gRoot->width() : size.x, rh = gRoot ? gRoot->height() : size.y;
    popShotTake(std::move(dlg), {(rw - size.x) / 2, (rh - size.y) / 2}, size, title, std::move(content));
}
CtTextView* gSdlCtTextView = nullptr;        // the text pop-out while it is open

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

// The kit's menu items of the page's menu items: a tab in a text is its shortcut, id 0 is a separator; a pick runs `run`.
std::vector<stardraft::kit::MenuItem> sdlTrigMenuItems(const std::vector<CtMenuItem>& items, const std::function<void(int)>& run) {
    std::vector<stardraft::kit::MenuItem> out;
    for (const CtMenuItem& it : items) {
        stardraft::kit::MenuItem k;
        if (!it.id) {
            k.separator = true;
        } else {
            const size_t tab = it.text.find(L'\t');
            k.label = it.text.substr(0, tab);
            if (tab != std::wstring::npos) k.shortcut = it.text.substr(tab + 1);
            k.enabled = it.enabled;
            const int id = it.id;
            k.run = [run, id] {
                if (run) run(id);
            };
        }
        out.push_back(std::move(k));
    }
    return out;
}

// The page's commands' host (core/views_classic_page.inc, CtPageHost): the page's views and the program's state. Its
// methods that need the page are below it.
class SdlCtPageHost final : public CtPageHost {
public:
    ClassicNames names() override;
    void filled() override;
    void selectTrigger(int i) override;
    void updateHeader() override;
    void changed() override;
    void invalidate() override;
    void message(const std::wstring& text) override { setMessage(text); }
    bool ctrl() override;
    bool shift() override;

    CtModel& model() override;
    Editor& editor() override { return *gState.ed; }
    void selectEntry(int kind, int entry) override;
    void buildEditor() override;
    void resetListScroll() override;
    void entriesLaid() override;
    uint32_t soundLength(const std::string& name) override {
        const SdlSound* d = sdlSound(name);
        return d ? d->ms : 0;
    }
    // The pickers open as the program's pop-out windows (core/views_list_pick.inc: runListPick), at the place in the window.
    bool pickOne(const std::wstring& title, std::vector<ListPickItem> items, const std::string& current, const draw::View& from, draw::Point at,
                 std::string& out) override {
        const draw::Point o = from.origin();
        if (gPopShot.wanted) {  // (a snapshot: the picker shows in the picture, sdl_popshot.inc)
            auto v = std::make_unique<ListPickView>(title, std::move(items), current);
            const draw::Point size = v->outerSize();
            popShotTake(std::move(v), {o.x + at.x, o.y + at.y}, size);
            return false;
        }
        ListPickView v(title, std::move(items), current);
        if (!runListPick(v, {o.x + at.x, o.y + at.y})) return false;
        out = v.result();
        return true;
    }
    bool pickMany(const std::wstring& title, std::vector<ListPickItem> items, std::set<std::string>& checked, const std::wstring& okText,
                  const draw::View& from, draw::Point at) override {
        ListPickView v(title, std::move(items), "");
        v.setMulti(checked, okText);
        const draw::Point o = from.origin();
        if (!runListPick(v, {o.x + at.x, o.y + at.y})) return false;
        checked = v.checked();
        return true;
    }
    bool editNumber(const std::wstring& title, const std::wstring& label, const std::wstring& current, std::wstring& out) override {
        if (gPopShot.wanted) {  // (a snapshot: the dialog shows in the picture, in the middle of the window)
            auto view = std::make_unique<CtValueView>(label, current);
            auto dlg = std::make_unique<stardraft::kit::Dialog>();
            dlg->setContent(*view);
            std::vector<stardraft::kit::DialogButton> buttons;
            buttons.push_back({tr(L"Set"), kValueOk, true, false, false, valueLayout::kButtonW});
            buttons.push_back({tr(L"Cancel"), kValueCancel, false, true, false, valueLayout::kButtonW});
            dlg->setButtons(std::move(buttons));
            dlg->setInsets({12, 8, 15, 16, 0, 9});
            sdlCtShotDialog(std::move(dlg), std::move(view), title, {scale(valueLayout::kDialogW), scale(valueLayout::kDialogH)});
            return false;
        }
        return gRoot && runValueDialog(*gRoot, title, label, current, out, gSdlCtValueView) == kValueOk;
    }
    bool editText(const std::wstring& title, const std::wstring& label, std::string& text) override {
        if (gPopShot.wanted) {
            auto view = std::make_unique<CtTextView>(label, text);
            auto dlg = std::make_unique<stardraft::kit::Dialog>();
            ctTextDialogParts(*dlg, *view);
            sdlCtShotDialog(std::move(dlg), std::move(view), title, {scale(textLayout::kDialogW), scale(textLayout::kDialogH)});
            return false;
        }
        return gRoot && runTextDialog(*gRoot, title, label, text, gSdlCtTextView) == kValueOk;
    }
    int editBlock(BatchHost& bh, ClassicBatch& b, bool editing) override { return gRoot ? runBatchDialog(*gRoot, bh, b, editing, gCtBatchView) : kBatchCancel; }
    // The kit's menu (its item runs after the menu has closed, so after this returns).
    void menu(const draw::View& from, draw::Point at, const std::vector<CtMenuItem>& items, std::function<void(int)> run) override;
};
SdlCtPageHost gSdlTrigHost;
CtPageList gSdlTrigList(gSdlTrigHost);  // the lists' program part (core/views_classic_page.inc)

// The page: the side and the trigger list at the left (and the note under it), the selected trigger's head, its
// conditions and actions and the editor at the right; placed by the core's layout (ctPageLayout), as the Windows page.
class SdlClassicTrigPage final : public draw::View {
public:
    explicit SdlClassicTrigPage(SdlClassicTrigModel& m)
        : m_(m), side_(filterNames(m), filterGroups()), triggers_(gSdlTrigList, true), entries_(gSdlTrigList, false),
          head_(playerLabels(m), headCallbacks()), editor_(editorCallbacks()) {
        gSdlTrigPage = this;
        gSdlTrigList.list = &triggers_, gSdlTrigList.entries = &entries_;
        side_.onSearch = [](const std::wstring& q) {
            ctSetQuery(gSdlTrigHost, q);
            sdlMapPageChanged();
        };
        side_.onFilter = [](int group) {
            ctSetFilter(gSdlTrigHost, group);
            sdlMapPageChanged();
        };
        note_.setTone(stardraft::kit::LabelTone::Dim);
        note_.setWrap(true);
        note_.setText(ctNoteText(m, projectMode()));
        add(side_, {0, 0, 0, 0});
        add(triggers_, {0, 0, 0, 0});
        add(note_, {0, 0, 0, 0});
        add(head_, {0, 0, 0, 0});
        add(entries_, {0, 0, 0, 0});
        add(editor_, {0, 0, 0, 0});
        m.query.clear(), m.filter = -1, m.marked.clear(), m.anchor = -1;  // the search and the filter start empty, as their boxes do
        ctModelFill(m, gSdlTrigHost);  // the rows of the list (the Windows page fills it in WM_CREATE too)
        selectTrigger(m.shown.empty() ? -1 : m.shown[0]);
    }
    ~SdlClassicTrigPage() override {
        if (gSdlTrigPage == this) gSdlTrigPage = nullptr;
        gSdlTrigList.list = gSdlTrigList.entries = nullptr;
    }

    SdlClassicTrigModel& model() { return m_; }
    CtSideView& side() { return side_; }
    CtListView& triggers() { return triggers_; }
    CtListView& entries() { return entries_; }
    CtHeadView& head() { return head_; }
    CtEditorView& editor() { return editor_; }
    CtEditorView& editorView() { return editor_; }
    int menuWidth() const { return menuW_; }
    int preferredWidth() const { return scale(960); }
    int preferredHeight() const { return ctPageLayout(width(), height(), true, 0, 0).needH; }
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
        ctShowPlayers(head_, m_);
        ctUpdateHead(head_, m_);
        entries_.resetScroll();
        const auto at = std::find(m_.shown.begin(), m_.shown.end(), m_.sel);
        if (at != m_.shown.end()) triggers_.revealShown(int(at - m_.shown.begin()));
        selectEntry(0, t && t->conditionCount() ? 0 : -1);
        gSdlTrigHost.changed();
    }
    // The selected entry: the editor's height follows its values; the editor shows it.
    void selectEntry(int kind, int i) {
        m_.kind = kind, m_.entry = i;
        place();
        editor_.show(gSdlTrigHost);
        entries_.invalidate();
    }

    // The page's parts at its size (core: ctPageLayout; the head's parts are in its coordinates, from 0).
    void resized() override { place(); }
    void paint(draw::Canvas& c) override {
        c.fill({0, 0, width(), height()}, stardraft::kit::theme().bg);
        if (!menuW_) {  // the menus' width: their longest texts, measured once (a menu opens outside a paint)
            std::vector<CtMenuItem> all = ctTriggerMenuItems(false, true);
            for (const auto& more : {ctTriggerMenuItems(true, true), ctEntryMenuItems({true, true, false, true, true, true}),
                                     ctEntryMenuItems({true, false, true, true, true, true})})
                all.insert(all.end(), more.begin(), more.end());
            menuW_ = stardraft::kit::Menu::measure(sdlTrigMenuItems(all, nullptr), &c).size.x;
        }
    }
    void place() {
        const int step = scale(stardraft::kit::kFormRowH), gap = scale(8);
        const CtPageRects L = ctPageLayout(width(), height(), !note_.text().empty(), entries_.contentHeight(), ctEditorHeight(m_, step, gap));
        setRect(side_, L.side);
        setRect(triggers_, L.list);
        setRect(note_, L.note);
        setRect(head_, L.head);
        ctPlaceHead(head_, L.rightW);
        setRect(entries_, L.entries);
        setRect(editor_, L.editor);
    }

private:
    static void setRect(draw::View& v, draw::Rect r) {
        const draw::Rect f = v.frame();
        if (f.left != r.left || f.top != r.top || f.right != r.right || f.bottom != r.bottom) v.setFrame(r);
    }
    static std::vector<std::wstring> filterNames(const SdlClassicTrigModel& m) {
        std::vector<std::wstring> names;
        std::vector<int> groups;
        ctFilterItems(sdlTrigNames(m), names, groups);
        return names;
    }
    static std::vector<int> filterGroups() {  // (All triggers, then the groups: ctFilterItems' order)
        std::vector<int> groups{-1};
        groups.insert(groups.end(), std::begin(kCtGroups), std::end(kCtGroups));
        return groups;
    }
    // The head's player labels, in the page's order.
    static std::vector<std::wstring> playerLabels(const SdlClassicTrigModel& m) {
        const ClassicNames n = sdlTrigNames(m);
        std::vector<std::wstring> labels;
        for (int g = 0; g < kCtBoxCount; ++g) labels.push_back(widen(classicPlayerName(uint32_t(kCtBoxGroups[g]), n)));
        return labels;
    }
    static CtHeadView::Callbacks headCallbacks() {
        CtHeadView::Callbacks cb;
        cb.player = [](int box, bool on) {
            ctModelPlayerBox(gSdlTrigHost.model(), gSdlTrigHost, box, on);
            sdlMapPageChanged();
        };
        cb.turnOff = [] {
            ctModelToggleTriggers(gSdlTrigHost.model(), gSdlTrigHost);
            sdlMapPageChanged();
        };
        cb.deleteTrig = [] {
            ctModelDeleteTriggers(gSdlTrigHost.model(), gSdlTrigHost);
            sdlMapPageChanged();
        };
        cb.forces = [] {  // (the core's list picker, as the Windows page)
            if (gSdlTrigPage) ctForcesClick(gSdlTrigHost, gSdlTrigPage->head().forces);
            sdlMapPageChanged();
        };
        return cb;
    }
    static CtEditorView::Callbacks editorCallbacks() {
        CtEditorView::Callbacks cb;
        cb.type = [](uint8_t type) {
            ctSetType(gSdlTrigHost, type);
            sdlMapPageChanged();
        };
        cb.flag = [](uint8_t bit, bool on) {
            ctSetFlag(gSdlTrigHost, bit, on);
            sdlMapPageChanged();
        };
        cb.value = [](int param, const draw::View& field) {
            ctValueClick(gSdlTrigHost, param, field, {0, 0, field.width(), field.height()});
            sdlMapPageChanged();
        };
        return cb;
    }

    SdlClassicTrigModel& m_;
    CtSideView side_;
    CtListView triggers_;
    CtListView entries_;
    CtHeadView head_;
    CtEditorView editor_;
    stardraft::kit::Label note_;
    int menuW_ = 0;
};

// --- The host's methods (they need the page, which is made when the page opens) ----------------------------------------

ClassicNames SdlCtPageHost::names() { return gSdlTrigPage ? sdlTrigNames(gSdlTrigPage->model()) : ClassicNames{}; }
CtModel& SdlCtPageHost::model() { return gSdlTrigPage->model(); }
void SdlCtPageHost::filled() {
    if (!gSdlTrigPage) return;
    const CtModel& m = gSdlTrigPage->model();
    gSdlTrigPage->side().setCount(std::to_wstring(m.shown.size()) + L" of " + std::to_wstring(m.trig.triggers.size()));
    gSdlTrigPage->invalidateLists();
}
void SdlCtPageHost::selectTrigger(int i) {
    if (gSdlTrigPage) gSdlTrigPage->selectTrigger(i);
}
void SdlCtPageHost::updateHeader() {
    if (gSdlTrigPage) ctUpdateHead(gSdlTrigPage->head(), gSdlTrigPage->model());
}
// After a change: the lists and the head show it, and it goes into the map (not while a paste of several triggers runs).
void SdlCtPageHost::changed() {
    if (!gSdlTrigPage) return;
    gSdlTrigPage->invalidateLists();
    updateHeader();
    if (!gSdlTrigPage->model().hold) sdlMapPageChanged();
}
void SdlCtPageHost::invalidate() {
    if (gSdlTrigPage) gSdlTrigPage->invalidateLists();
}
bool SdlCtPageHost::ctrl() { return gSdlTrigPage && (gSdlTrigPage->mods() & draw::kCtrl) != 0; }
bool SdlCtPageHost::shift() { return gSdlTrigPage && (gSdlTrigPage->mods() & draw::kShift) != 0; }
void SdlCtPageHost::selectEntry(int kind, int entry) {
    if (gSdlTrigPage) gSdlTrigPage->selectEntry(kind, entry);
}
void SdlCtPageHost::buildEditor() {
    if (gSdlTrigPage) gSdlTrigPage->editorView().show(*this);
}
void SdlCtPageHost::resetListScroll() {
    if (gSdlTrigPage) gSdlTrigPage->triggers().resetScroll();
}
void SdlCtPageHost::entriesLaid() {
    if (gSdlTrigPage) gSdlTrigPage->place();  // the entries' height changed: the page places them again
}
void SdlCtPageHost::menu(const draw::View& from, draw::Point at, const std::vector<CtMenuItem>& items, std::function<void(int)> run) {
    const std::vector<stardraft::kit::MenuItem> kitItems = sdlTrigMenuItems(items, run);
    stardraft::kit::MenuLayout layout = stardraft::kit::Menu::measure(kitItems);  // (its height; the width is the measured one)
    if (gSdlTrigPage) layout.size.x = std::max(layout.size.x, gSdlTrigPage->menuWidth());
    stardraft::kit::Menu::popup(const_cast<draw::View&>(from), at, kitItems, layout);
}

// --classictrig trigger:kind:entry (the trigger's number in the map, from 1; kind 0 a condition, 1 an action; the entry
// from 0): the open page selects it, for a snapshot.
// --classictrig trigger:kind:entry:value (after a frame): value `value` of the entry is clicked in the editor, and its
// pop-out (a list, the number or the text dialog) shows in the picture (sdl_popshot.inc).
void sdlClassicTrigPopShot(const std::string& spec) {
    int t = 0, kind = 0, entry = -1, param = -1;
    if (!gSdlTrigPage || std::sscanf(spec.c_str(), "%d:%d:%d:%d", &t, &kind, &entry, &param) < 4 || param < 0) return;
    const draw::View* field = gSdlTrigPage->editor().field(param);
    popShotOpen([param, field] {
        if (field) ctValueClick(gSdlTrigHost, param, *field, {0, 0, field->width(), field->height()});
        else ctValuePopout(gSdlTrigHost, param);
    });
}
void sdlClassicTrigSelect(const std::string& spec) {
    int t = 0, kind = 0, entry = -1;
    if (!gSdlTrigPage || std::sscanf(spec.c_str(), "%d:%d:%d", &t, &kind, &entry) < 1) return;
    gSdlTrigPage->selectTrigger(t - 1);
    if (entry >= 0) gSdlTrigPage->selectEntry(kind ? 1 : 0, entry);
}
