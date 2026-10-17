// The Triggers workspace on this system: the core's views, as the Windows program has them (core/views_triggers_tree.inc:
// the tree at the left; core/views_triggers_pane.inc: the selected trigger's blocks, with a kit scroll bar where the
// Windows window has its bar, and the info strip under it; core/views_triggers_pick.inc: the block picker) on the core's
// model (core/triggers_model.inc: the project's triggers.json loaded, checked, saved and changed, the tree's commands,
// the clipboard, the undo steps and the menus). The menus show as kit menus, the pickers as the program's pop-outs and
// dialogs; the block help page (core/views_triggers_help.inc: BlockHelpView) in a window of its own, and the code pane
// (TrigCodeView) at the right. A value, the name at the top and the variable form's fields open their pop-outs as on
// Windows (core/triggers_values.inc: the list picker with the entries' icons, the value pop-out of
// core/views_value_pop.inc, the button pop-out of core/views_data_button_pop.inc; Tab goes to the next value), a value
// block has its menu, and a value of one unit is picked on the map. The values show their entries' pictures. Included
// by main_sdl.cpp after
// sdl_data.inc (SdlBarFrame); registerWorkspaceView(Workspace::Triggers, gSdlTriggers) puts it in the window, and
// --trig <root>:<index>[:<child>...] and --trigsel <action> select a node and a block for a snapshot, and --trigpop <n>
// shows the pop-out of its value n.


// --- The block help page (core/views_triggers_help.inc: BlockHelpView) ----------------------------------------------

BlockHelpView gSdlBlockHelp;
bool gSdlBlockHelpOpen = false;

void sdlCloseBlockHelp() {
    if (gSdlBlockHelpOpen && gHost) gHost->closeWindow(gSdlBlockHelp);
}
// Opens the help page for a block (or shows another block on it): 460 x 600 px, as the Windows page. An open page
// shows the block and stays where it is.
void sdlShowBlockHelp(const std::string& id) {
    if (!TriggerDef::find(id) || !gRoot || !gHost) return;
    gSdlBlockHelp.setBlock(id);
    if (gSdlBlockHelpOpen) return;
    gSdlBlockHelp.onClose = [] { sdlCloseBlockHelp(); };
    gSdlBlockHelpOpen = true;
    gHost->openWindow(*gRoot, gSdlBlockHelp, draw::WindowSpec{draw::WindowKind::Dialog, draw::Rect(0, 0, scale(460), scale(600)), L"Block help"},
                      [] { gSdlBlockHelpOpen = false; });
}
// F1 in the pane: the selected block's page (F1 again, or Esc, closes it).
void sdlTrigPaneHelp() {
    bool close = false;
    const std::string id = trigPaneHelpPick(gSdlBlockHelpOpen, gSdlBlockHelp.block(), close);
    if (close) sdlCloseBlockHelp();
    if (!id.empty()) sdlShowBlockHelp(id);
}

// --- The block picker's host (core/views_triggers_pick.inc) ------------------------------------------------------------

struct SdlBlockPickHost final : BlockPickHost {
    void help(const std::string& id) override { sdlShowBlockHelp(id); }
};
SdlBlockPickHost gSdlBlockPickHost;
BlockPickView* gSdlBlockPick = nullptr;  // the picker while it is open

// The picker's window as the Windows program sizes it: most of the window (the Windows program: of the screen), up to
// 1280 x 900 with its frame (the frame of a dialog: 14 x 39 px at 100 %).
draw::Point sdlBlockPickSize() {
    const int w = gRoot ? gRoot->width() : scale(1280), h = gRoot ? gRoot->height() : scale(900);
    return {std::min(scale(1280), w * 9 / 10) - scale(14), std::min(scale(900), h * 9 / 10) - scale(39)};
}

std::string showBlockPicker(bool condition, const std::string& startCategory, const std::wstring& search) {
    if (!gRoot) return "";
    return runBlockPicker(*gRoot, gSdlBlockPickHost, condition, startCategory, search, sdlBlockPickSize(), true, gSdlBlockPick);
}

// --- The pane's host (core/views_triggers_pane.inc: TrigPaneHost) ------------------------------------------------------

void sdlTriggersPlace();
void sdlTrigRevealNow();
extern TrigInfoView gSdlTrigInfo;

struct SdlTrigPaneHost final : TrigPaneHost {
    void loadProjectData() override { dataLoadForProject(); }
    std::wstring gameString(int id) override { return statString(id); }
    const std::vector<Button>& buttonSet(int set) override { return dataExtras().buttons.set(set); }
    draw::Image entryIcon(const std::string& dat, uint32_t v) override { return datHasIcons(dat) ? sdlDataImage(dataRefPicture(dat, v)) : draw::Image{}; }
    void layoutCheck() override {  // (the pane asks as it paints: the strip is placed after the frame)
        if (gSdlTrigInfo.shown() != (trigInfoHeight() > 0)) shellPost(sdlTriggersPlace);
        shellPost(sdlTrigRevealNow);  // (trigRevealBlock: after this paint, the pane's lines are the new ones)
    }
    void infoChanged() override { trigInfoRepaint(); }
    void paintNow() override {}  // (the window paints every frame: the hit areas are the last frame's)
    void blockShown(const std::string& def) override {  // an open help page follows the block
        if (gSdlBlockHelpOpen && !def.empty() && def != gSdlBlockHelp.block()) sdlShowBlockHelp(def);
    }
    void toggleCode() override { showTriggerCode(); }
    void applyFix(TriggerCostFix fix) override { trigApplyCostFix(fix); }
    void addFromPage(int action, RECT button) override { trigAddFromPane(action, button); }
    void editTitle() override { trigEditTitle(); }
    void editVariable(TrigVarHit h) override { trigEditVariableField(h); }
    void editValue(TrigParamHit h) override { trigEditFlow(h); }
    void changed() override { trigChanged(); }
    void duplicate() override { trigDuplicate(); }
    void deleteBlock() override { trigDeleteLine(); }
    void moveBlock(int delta) override { trigMoveLine(delta); }
    void addWithPicker(TrigListRef list, int at) override { trigAddWithPicker(list, at); }
    std::string pickBlock(bool condition, const std::wstring& search) override { return showBlockPicker(condition, {}, search); }
    void addPicked(bool condition, const std::string& id) override {
        const auto [ref, at] = trigAddTarget(condition);
        trigAddTo(ref, at, *TriggerDef::find(id));
    }
    void valueBlockMenu(TrigParamHit h) override { trigValueBlockMenu(h); }
    void menu(draw::Point pt) override;
    void drop() override { trigDrop(); }
    void help() override { sdlTrigPaneHelp(); }
    void undo(bool redo) override { sdlUndoSteps(redo, 1); }  // (sdl_history.inc: the status bar says the step's name)
};
SdlTrigPaneHost gSdlTrigPaneHost;
TrigPaneView gSdlTrigPane(gSdlTrigPaneHost);
TrigInfoView gSdlTrigInfo(gSdlTrigPaneHost);
TrigCodeView gSdlTrigCode;  // the code pane (View > Show the code, the pane's </> button)

// --- The workspace's view: the tree, the splitter, the pane (with its bar) and the info strip --------------------------

class SdlTriggersWorkspace final : public draw::View {
public:
    SdlTriggersWorkspace() : paneFrame_(gSdlTrigPane) {
        add(gTrigTree, {0, 0, 0, 0});
        add(paneFrame_, {0, 0, 0, 0});
        add(gSdlTrigInfo, {0, 0, 0, 0});
        add(gSdlTrigCode, {0, 0, 0, 0});
        gSdlTrigCode.setVisible(false);
    }
    // The Windows program's layoutTriggerEditor: the tree as wide as the Data list, a 5 px gap with the line in it,
    // the pane, the info strip under the pane while a block is selected, and the code pane at the right when it is
    // shown (2/5 of the width, at least 420 px, at most half).
    void place() {
        const int tw = treeW(), gap = scale(5), ih = trigInfoHeight(), w = width(), h = height();
        const int cw = gDock.triggerCode ? std::min(w / 2, std::max(scale(420), w * 2 / 5)) : 0;
        const int pr = std::max(tw + gap + 1, w - cw);
        gTrigTree.setFrame({0, 0, tw, h});
        paneFrame_.setFrame({tw + gap, 0, pr, std::max(1, h - ih)});
        gSdlTrigInfo.setFrame({tw + gap, std::max(0, h - ih), pr, h});
        gSdlTrigInfo.setVisible(ih > 0);
        gSdlTrigCode.setFrame({w - cw + scale(1), 0, std::max(w - cw + scale(1) + 1, w), h});
        gSdlTrigCode.setVisible(cw > 0);
        invalidate();
    }
    void resized() override { place(); }
    void paint(draw::Canvas& c) override {
        c.fill({0, 0, width(), height()}, kBg);
        c.fill({treeW() + scale(2), 0, treeW() + scale(3), height()}, kBorder);
    }
    // The splitter between the tree and the pane: a drag changes the tree's width (the Data list's, saved with the dock).
    draw::Cursor cursor(draw::Point) override { return draw::Cursor::SizeWE; }
    bool mouse(const draw::MouseEvent& e) override {
        switch (e.kind) {
        case draw::MouseKind::Down:
            if (e.button != 0) return false;
            grab_ = e.pt.x;
            if (draw::Host* h = host()) h->capture(*this, true);
            return true;
        case draw::MouseKind::Move:
            if (grab_ >= 0) {
                const int dx = MulDiv(e.pt.x - grab_, 96, int(shellDpi()));
                const int nw = std::clamp(gDock.dataListW + dx, kMinSideW, kMaxSideW);
                if (nw != gDock.dataListW) {
                    grab_ += MulDiv(nw - gDock.dataListW, int(shellDpi()), 96);
                    gDock.dataListW = nw;
                    place();
                }
            }
            return true;
        case draw::MouseKind::Up:
            if (grab_ >= 0) {
                grab_ = -1;
                if (draw::Host* h = host()) h->capture(*this, false);
                saveDock();
            }
            return true;
        default: return false;
        }
    }
    void captureLost() override { grab_ = -1; }

private:
    static int treeW() { return scale(gDock.dataListW); }
    SdlBarFrame paneFrame_;
    int grab_ = -1;
};
SdlTriggersWorkspace gSdlTriggers;

void sdlTriggersPlace() { gSdlTriggers.place(); }

// --- What the core asks of this program (core/triggers_model.inc) ------------------------------------------------------

void trigPaneRepaint() { gSdlTrigPane.invalidate(); }  // (also core/dock_prefs.inc: trigZoomReset)
void trigInfoRepaint() {
    gSdlTrigInfo.invalidate();
}
// The code pane: the set's code again (when the pane is shown); View > Show the code and </> show or hide it.
void trigCodeRefresh(bool force) {
    if (gDock.triggerCode && gWorkspace == Workspace::Triggers) gSdlTrigCode.refresh(force);
}
void showTriggerCode() {
    gDock.triggerCode = !gDock.triggerCode;
    saveDock();
    gSdlTriggers.place();
    trigCodeRefresh();
    trigPaneRepaint();  // (the </> button's color)
}

// trigShowCodeLine: the pane moves to the block after its next paint (the hit areas and lines are the last paint's).
std::optional<TrigBlockRef> gSdlTrigReveal;
void trigRevealBlock(const TrigBlockRef& ref) { gSdlTrigReveal = ref; }
void sdlTrigRevealNow() {
    if (!gSdlTrigReveal) return;
    const TrigBlockRef ref = *gSdlTrigReveal;
    gSdlTrigReveal.reset();
    trigScrollToBlock(ref, gSdlTrigPane.height());
    gSdlTrigPane.invalidate();
}

// --- The values' pop-outs (core/triggers_values.inc: which one a value opens, with its spec) ---------------------------

int gSdlTrigAdvance = 0;  // Tab (+1) or Shift+Tab (-1) closed the last pop-out
ValuePopView* gSdlTrigValuePop = nullptr;  // the value pop-out while it is open (the snapshots reach it)

// A point of the pane in the main window.
draw::Point sdlTrigPanePoint(draw::Point at) {
    const draw::Point o = gSdlTrigPane.origin();
    return {o.x + at.x, o.y + at.y};
}
// The list picker with each entry's icon (the units, the .dat entries, a set's buttons), as the Windows showListPicker.
bool sdlTrigPickAt(const std::wstring& title, std::vector<ListPickItem> items, const std::string& current, draw::Point at, std::string& out) {
    auto icon = [](const ListPickItem& it) { return it.iconDat.empty() || it.icon < 0 ? draw::Image{} : sdlDataImage(dataRefPicture(it.iconDat, uint32_t(it.icon))); };
    if (gPopShot.wanted) {  // (a snapshot: the picker shows in the picture, sdl_popshot.inc)
        auto v = std::make_unique<ListPickView>(title, std::move(items), current);
        v->iconOf = icon;
        const draw::Point size = v->outerSize();
        popShotTake(std::move(v), at, size);
        return false;
    }
    ListPickView v(title, std::move(items), current);
    v.iconOf = icon;
    const bool picked = runListPick(v, at);
    if (v.advance()) gSdlTrigAdvance = v.advance();
    if (picked) out = v.result();
    return picked;
}
bool trigPopList(const std::wstring& title, std::vector<ListPickItem> items, const std::string& current, draw::Point at, std::string& out) {
    return sdlTrigPickAt(title, std::move(items), current, sdlTrigPanePoint(at), out);
}
// The value pop-out; "A text of the game..." lists the Strings tab's entries.
std::vector<ListPickItem> sdlTrigGameTexts() {
    dataLoadForProject();
    return entryItems("strings", true);
}
bool trigPopValue(const ValuePopSpec& spec, draw::Point at, std::string& out) {
    if (gPopShot.wanted) {  // (a snapshot: the pop-out shows in the picture)
        auto v = std::make_unique<ValuePopView>(spec);
        v->setPlace(sdlTrigPanePoint(at));
        v->gameTexts = sdlTrigGameTexts;
        const draw::Point size = v->outerSize();
        popShotTake(std::move(v), sdlTrigPanePoint(at), {size.x - 2, size.y - 2});
        return false;
    }
    int advance = 0;
    const bool ok = runValuePop(spec, sdlTrigPanePoint(at), out, &gSdlTrigValuePop, sdlTrigGameTexts, &advance);
    if (advance) gSdlTrigAdvance = advance;
    return ok;
}
std::string trigPopValueBlock(draw::Point at, const std::string& current) {
    std::string picked;
    return sdlTrigPickAt(L"Search values from the game", valueBlockPickItems(), current, sdlTrigPanePoint(at), picked) ? picked : std::string();
}
int trigPopAdvance() {
    const int d = gSdlTrigAdvance;
    gSdlTrigAdvance = 0;
    return d;
}
void trigPanePaintNow() {}  // (the window paints every frame)

// The button pop-out's program parts (the Windows ones are data_extras.inc's ButtonPop): the pictures and names of the
// project's data, and the list pickers under their box.
struct SdlButtonPopHost final : ButtonPopHost {
    draw::View* view = nullptr;  // the pop-out
    draw::Point place{0, 0};     // where its window is in the main window
    draw::Image icon(const char* dat, int entry) override { return sdlDataImage(dataRefPicture(dat, uint32_t(entry))); }
    draw::Image argIcon(const char* kind, int v) override { return sdlDataImage(dataArgPicture(kind, v)); }
    std::wstring argName(const char* kind, int v) override { return ::argName(kind, v); }
    std::wstring stringLine(int id) override { return statString(id); }
    std::string stringBytes(int id) override {
        const auto& st = dataExtras().strings;
        return id >= 1 && id <= st.count() ? st.text(id - 1) : std::string();
    }
    std::wstring cost(const Button& b, int type) override {
        return buttonCostText(b, type, [](const std::string& dat, const std::string& name, int e) { return datValue(dat, name, e, 0); });
    }
    std::vector<ListPickItem> entryItems(const char* kind, bool none) override { return ::entryItems(kind, none); }
    std::vector<Button> buttonSet(int set) override { return dataExtras().buttons.set(set); }
    std::wstring buttonLabel(const Button& b) override { return buttonRowTextFrom(b, b.enabledString ? statString(b.enabledString) : L""); }
    bool choose(const std::wstring& prompt, std::vector<ListPickItem> items, const std::string& current, draw::Rect box, std::string& out) override {
        const draw::Point o = view ? view->origin() : draw::Point{0, 0};
        return sdlTrigPickAt(prompt, std::move(items), current, {place.x + o.x + box.left, place.y + o.y + box.bottom + scale(2)}, out);
    }
};
// A whole button (Replace a button): 560 x 470 px, as the Windows pop-out opens.
bool trigPopButton(std::string& text, draw::Point at) {
    dataLoadForProject();  // the icons and strings come from the project
    Button b;
    if (!buttonFromText(text, b)) b = Button{1, 0, kButtonNewCondition, kButtonNewAction, 0, 0, 0, 0};
    static SdlButtonPopHost shotHost;  // (a snapshot's pop-out outlives this call)
    if (gPopShot.wanted) {
        shotHost.place = sdlTrigPanePoint(at);
        auto v = std::make_unique<ButtonPopView>(shotHost, b);
        shotHost.view = v.get();
        popShotTake(std::move(v), shotHost.place, {scale(560), scale(470)});
        return false;
    }
    SdlButtonPopHost h;
    ButtonPopView view(h, b);
    h.view = &view, h.place = sdlTrigPanePoint(at);
    bool accepted = false;
    view.onDone = [&](bool ok) {
        accepted = ok;
        shellClosePopout(view);
    };
    shellRunPopout(view, h.place.x, h.place.y, scale(560), scale(470), &view, nullptr);
    if (accepted) text = buttonToText(view.button());
    return accepted;
}
static bool sdlTrigFocusIn(const draw::View& v) {
    if (v.hasFocus()) return true;
    for (const draw::View* c : v.children())
        if (sdlTrigFocusIn(*c)) return true;
    return false;
}
bool trigPaneFocused() { return gSdlTrigPane.hasFocus(); }
bool trigTreeFocused() { return sdlTrigFocusIn(gTrigTree); }  // (the tree, or its rename field)
void trigFocusTree() { gTrigTree.focus(); }
void trigFocusPane() { gSdlTrigPane.focus(); }
// The question before a node is deleted: the core message box, as the Windows program's MB_YESNO | MB_ICONQUESTION |
// MB_DEFBUTTON2 with the names Delete and Cancel (Cancel has the focus).
msgbox::Box sdlTrigDeleteBox(const std::wstring& question) {
    return msgbox::makeBox(question, {shellTr(L"&Delete"), shellTr(L"Cancel")}, msgbox::Icon::Question, 1);
}
bool trigAskDelete(const std::wstring& question) {
    return gRoot && msgbox::runMessageBox(*gRoot, shellTr(L"Delete"), sdlTrigDeleteBox(question), nullptr) == 0;
}
bool trigPickFromPane(const std::wstring& title, std::vector<ListPickItem> items, draw::Point at, std::string& out) {
    return trigPopList(title, std::move(items), "", at, out);
}

// A menu model (core/menu_model.inc) as kit menu items; a pick runs `pick` with the item's id.
std::vector<kit::MenuItem> sdlTrigMenuKit(const MenuModel& items, const std::function<void(int)>& pick) {
    std::vector<kit::MenuItem> out;
    for (const MenuItem& it : items) {
        kit::MenuItem k;
        if (it.kind == MenuItem::Kind::Separator) k.separator = true;
        else {
            for (size_t i = 0; i < it.label.size(); ++i)  // the access keys go; "&&" is an "&"
                if (it.label[i] != L'&') k.label += it.label[i];
                else if (i + 1 < it.label.size() && it.label[i + 1] == L'&') k.label += L'&', ++i;
            k.shortcut = it.shortcut, k.checked = it.checked;
            if (it.kind == MenuItem::Kind::Popup) k.sub = sdlTrigMenuKit(it.items, pick);
            else {
                k.enabled = !it.grayed && it.id != 0;
                const int id = it.id;
                k.run = [pick, id] { pick(id); };
            }
        }
        out.push_back(std::move(k));
    }
    return out;
}
// (With Report feedback at the end, as the Windows menus: sdl_menus.inc.)
void sdlTrigPopup(draw::View& from, draw::Point at, const std::vector<kit::MenuItem>& items, const std::wstring& pane) {
    sdlPopupMenu(from, at, items, sdlFeedbackOn(from, at, pane));
}

void trigTreeMenuAt(const std::vector<int>& path) {
    if (!gTE.set.at(path)) return;
    const draw::Point at = gHost ? gHost->mouse(gTrigTree) : draw::Point{0, 0};
    sdlTrigPopup(gTrigTree, at, sdlTrigMenuKit(trigTreeMenuModel(path), [path](int id) { trigTreeMenuPick(path, id); }), L"the triggers");
}
void SdlTrigPaneHost::menu(draw::Point pt) {
    auto blocks = std::make_shared<std::vector<const TriggerDef*>>();
    const MenuModel m = trigPaneMenuModel(*blocks);
    if (m.empty()) return;
    sdlTrigPopup(gSdlTrigPane, pt, sdlTrigMenuKit(m, [blocks](int id) { trigPaneMenuPick(id, *blocks); }), L"the trigger");
}

// The workspace shows (setWorkspace, sdl_pending.inc): the active project's triggers, read when they are not the ones
// loaded, and the tree made again with the selection kept (as the Windows program's setWorkspace).
void sdlTriggersShown() {
    trigLoadSet();
    trigRefreshTree(gTE.sel.empty() ? std::vector<int>{1} : gTE.sel);
    gSdlTriggers.place();
}

// --trig <root>:<index>[:<child>...]: that node selected; --trigsel <action>: then that action of the trigger selected
// with its first value focused and the mouse on its row (the Windows datashots trig: and trigsel:), for a snapshot.
void sdlTrigSelect(const std::string& node, const std::string& action) {
    if (gWorkspace != Workspace::Triggers) return;
    if (!node.empty()) {
        std::vector<int> path;
        for (size_t at = 0; at <= node.size();) {
            const size_t end = std::min(node.find(':', at), node.size());
            path.push_back(std::atoi(node.substr(at, end - at).c_str()));
            at = end + 1;
        }
        trigRefreshTree(path);
    }
    if (!action.empty()) {
        gTP.sel = {{{}, 1}, std::atoi(action.c_str())};
        gTP.hoverRow = gTP.sel;
        gTP.focus = {}, gTP.focus.ref.index = -1;
    }
    gSdlTriggers.place();
}
// After a frame (the pane's hit areas are made as it paints): the selected block's first value is focused.
void sdlTrigFocusFirstValue() {
    if (gTP.sel.index < 0) return;
    if (const auto order = trigTabOrder(gTP.sel); !order.empty()) gTP.focus = order[0];
}
// --trigpop n: then value n (from 0, in Tab's order) of the selected block is clicked (n -2: the words of its text; -100 - f,
// --trigpop var:f: field f of the selected variable's form: 0 kind, 1 size, 2 default value, 3 description, 4 holds, 10.. a player's), and
// the pop-out it opens shows in the picture (sdl_popshot.inc), as a click opens it.
void sdlTrigPopShot(int n) {
    if (n <= -100) {  // (-100 - field: a field of the selected variable's form, TrigVarHit)
        for (const TrigVarHit& h : gTP.varHits)
            if (h.field == -100 - n) return void(popShotOpen([h] { trigEditVariableField(h); }));
        return;
    }
    if (gTP.sel.index < 0) return;
    auto order = trigTabOrder(gTP.sel);
    if (n == -2) {  // (the words are hit areas of the pane, not values that Tab goes to)
        order.clear(), n = 0;
        for (const TrigParamHit& h : gTP.params)
            if (trigSameBlock(h.ref, gTP.sel) && h.token == -2) order.push_back(h);
    }
    if (order.empty()) return;
    const TrigParamHit h = order[size_t(std::clamp(n, 0, int(order.size()) - 1))];
    popShotOpen([h] { trigEditFlow(h); });
}
