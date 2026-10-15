// The Triggers workspace on this system: the core's views, as the Windows program has them (core/views_triggers_tree.inc:
// the tree at the left; core/views_triggers_pane.inc: the selected trigger's blocks, with a kit scroll bar where the
// Windows window has its bar, and the info strip under it; core/views_triggers_pick.inc: the block picker) on the core's
// model (core/triggers_model.inc: the project's triggers.json loaded, checked, saved and changed, the tree's commands,
// the clipboard, the undo steps and the menus). The menus show as kit menus, the pickers as the program's pop-outs and
// dialogs; the block help page (core/views_triggers_help.inc: BlockHelpView) in a window of its own, and the code pane
// (TrigCodeView) at the right. A value, the name at the top and the variable form's fields change in the text prompt
// (the values' pop-outs are not on this system yet: a value block's menu, the text pop-out, the kinds and the sizes
// of the variable form), and a value of one unit is picked on the map. Not on this system yet either: the entries'
// pictures on the values. Included by main_sdl.cpp after
// sdl_data.inc (SdlBarFrame); registerWorkspaceView(Workspace::Triggers, gSdlTriggers) puts it in the window, and
// --trig <root>:<index>[:<child>...] and --trigsel <action> select a node and a block for a snapshot.

void sdlTrigNotHere(const wchar_t* what) { setMessage(std::wstring(what) + L": not on this system yet"); }

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
void sdlTrigEditTitle();
void sdlTrigEditVariable(TrigVarHit h);
void sdlTrigEditValue(TrigParamHit h);
extern TrigInfoView gSdlTrigInfo;

struct SdlTrigPaneHost final : TrigPaneHost {
    void loadProjectData() override { dataLoadForProject(); }
    std::wstring gameString(int id) override { return statString(id); }
    const std::vector<Button>& buttonSet(int set) override { return dataExtras().buttons.set(set); }
    draw::Image entryIcon(const std::string&, uint32_t) override { return {}; }  // (the entries' pictures: not on this system yet)
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
    void editTitle() override { sdlTrigEditTitle(); }
    void editVariable(TrigVarHit h) override { sdlTrigEditVariable(h); }
    void editValue(TrigParamHit h) override { sdlTrigEditValue(h); }
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
    void valueBlockMenu(TrigParamHit) override { sdlTrigNotHere(L"The value block's menu"); }
    void menu(draw::Point pt) override;
    void drop() override { trigDrop(); }
    void help() override { sdlTrigPaneHelp(); }
    void undo(bool redo) override { trigUndo(redo); }
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

// --- Changing values: the text prompt (the values' pop-outs are not on this system yet) ---------------------------

// The name at the top of a trigger's or variable's page.
void sdlTrigEditTitle() {
    TriggerNode* n = trigSelectedNode();
    if (!n || n->kind == TriggerNode::Kind::Category || !gRoot) return;
    std::wstring text = widen(n->name);
    TextPromptView* shown = nullptr;
    if (runTextPrompt(*gRoot, n->kind == TriggerNode::Kind::Variable ? L"Name   letters, digits and _" : L"Name", text, true, shown))
        trigRenameNode(gTE.sel, narrow(text));
}
// A field of the variable form: the default value (a unit: picked on the map) and the description. The kind, the
// size and the players' values have their pop-outs on Windows only so far.
void sdlTrigEditVariable(TrigVarHit h) {
    TriggerNode* v = trigSelectedNode();
    if (!v || v->kind != TriggerNode::Kind::Variable || !gRoot) return;
    const bool perPlayer = v->variable.kind == TriggerVariable::Kind::PerPlayer && (h.field == 4 || (h.field >= 10 && h.field < 18));
    if (h.field == 0 || h.field == 1 || perPlayer) return sdlTrigNotHere(L"This field's pop-out");
    if (h.field == 2 && v->variable.kind == TriggerVariable::Kind::Unit) return trigStartUnitPick({}, -1);
    const auto kind = v->variable.kind;
    std::wstring title = L"Description", text = widen(v->comment);
    if (h.field == 2) {
        title = kind == TriggerVariable::Kind::Constant ? L"Value" : kind == TriggerVariable::Kind::Array ? L"Default value of each item" : L"Default value";
        if (kind == TriggerVariable::Kind::Flag) title += L"   1 on, 0 off";
        if (kind == TriggerVariable::Kind::Text) title = L"Text";
        text = widen(v->variable.initial.empty() && kind != TriggerVariable::Kind::Text ? std::string("0") : v->variable.initial);
    }
    TextPromptView* shown = nullptr;
    if (!runTextPrompt(*gRoot, title, text, true, shown)) return;
    v = trigSelectedNode();
    if (!v || v->kind != TriggerNode::Kind::Variable) return;
    (h.field == 2 ? v->variable.initial : v->comment) = narrow(text);
    trigChanged();
}
// A value of a block (a chip): one unit is picked on the map; another value is typed as its code in the text prompt.
void sdlTrigEditValue(TrigParamHit h) {
    gTP.focus = h, gTP.sel = h.ref;
    gSdlTrigPane.invalidate();
    if (h.bracket) return sdlTrigNotHere(L"The value block's menu");
    TriggerCall* call = trigHitCall(h);
    const int hp = h.token != -1 ? h.param : trigHitParam(h);  // (a part of a text: the whole text)
    const TriggerDef* d = call ? TriggerDef::find(call->def) : nullptr;
    if (!d || hp < 0 || hp >= int(d->params.size()) || hp >= int(call->args.size()) || !gRoot) return;
    const std::string typeName = triggerParamType(*d, call->args, hp);
    const TriggerType* type = triggerType(typeName);
    if (type && type->picker == TriggerPicker::UnitRef && h.token == -1) return trigStartUnitPick(h.ref, h.param, h.inner);
    std::wstring text = widen(call->args[size_t(hp)]);
    TextPromptView* shown = nullptr;
    if (!runTextPrompt(*gRoot, widen(d->params[size_t(hp)].name), text, true, shown)) return;
    if (TriggerCall* c = trigHitCall(h); c && hp < int(c->args.size())) {
        c->args[size_t(hp)] = narrow(text);
        if (h.token != -1) c->normalizeText();
        trigChanged();
    }
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
    ListPickView v(title, std::move(items), "");
    const draw::Point o = gSdlTrigPane.origin();
    if (!runListPick(v, {o.x + at.x, o.y + at.y})) return false;
    out = v.result();
    return true;
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
void sdlTrigPopup(draw::View& from, draw::Point at, const std::vector<kit::MenuItem>& items) {
    ImDrawList list(ImGui::GetDrawListSharedData());  // (the menu's text measured with a canvas of its own)
    kit_sdl::ImguiCanvas measure(list, nullptr);
    kit::Menu::popup(from, at, items, kit::Menu::measure(items, &measure));
}

void trigTreeMenuAt(const std::vector<int>& path) {
    if (!gTE.set.at(path)) return;
    const draw::Point at = gHost ? gHost->mouse(gTrigTree) : draw::Point{0, 0};
    sdlTrigPopup(gTrigTree, at, sdlTrigMenuKit(trigTreeMenuModel(path), [path](int id) { trigTreeMenuPick(path, id); }));
}
void SdlTrigPaneHost::menu(draw::Point pt) {
    auto blocks = std::make_shared<std::vector<const TriggerDef*>>();
    const MenuModel m = trigPaneMenuModel(*blocks);
    if (m.empty()) return;
    sdlTrigPopup(gSdlTrigPane, pt, sdlTrigMenuKit(m, [blocks](int id) { trigPaneMenuPick(id, *blocks); }));
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
