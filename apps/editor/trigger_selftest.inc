// Part of main.cpp: the self-test's checks of the trigger editor's help and fixes (trigger_help.inc), a picked unit that
// is deleted, and a build error that selects its block. Called from runSelfTest with a project open.

void selfTestTriggerHelp(std::ofstream& log, const fs::path& dir, EudProject* pr) {
    command(ID_WS_TRIGGERS);
    for (int i = 0; i < 5; ++i) pump();
    auto addTrigger = [](const char* name, std::vector<TriggerCall> actions, std::vector<TriggerCall> conditions = {}) {
        TriggerNode t = TriggerSet::newTrigger(name);
        t.actions = std::move(actions);
        if (!conditions.empty()) t.conditions = std::move(conditions);
        gTE.set.roots[2].children.push_back(t);
        trigSave();
        const std::vector<int> path = {2, int(gTE.set.roots[2].children.size()) - 1};
        trigRefreshTree(path);
        return path;
    };
    auto removeTrigger = [](const std::vector<int>& path) {
        gTE.set.remove(path);
        trigSave();
        trigRefreshTree({2});
    };
    auto selectBlock = [](int index) {
        gTP.sel = {}, gTP.sel.list.list = 1, gTP.sel.index = index;
        InvalidateRect(gTE.pane, nullptr, FALSE), InvalidateRect(gTE.info, nullptr, FALSE);
        UpdateWindow(gTE.pane), UpdateWindow(gTE.info);
    };

    // T.7: F1 on a block opens its help page (without the focus); it follows the selection; F1 again closes it.
    {
        const auto path = addTrigger("Help", {trigMake("act.createunit"), trigMake("act.foreachunit")});
        SetFocus(gTE.pane);
        selectBlock(0);
        SendMessageW(gTE.pane, WM_KEYDOWN, VK_F1, 0);
        for (int i = 0; i < 5; ++i) pump();
        const bool opened = gBlockHelp.wnd && IsWindowVisible(gBlockHelp.wnd) && gBlockHelp.id == "act.createunit" && GetForegroundWindow() != gBlockHelp.wnd;
        if (gBlockHelp.wnd) {
            UpdateWindow(gBlockHelp.wnd);
            capture(dir / "88_block_help.png", gBlockHelp.wnd);
        }
        selectBlock(1);
        for (int i = 0; i < 5; ++i) pump();
        const bool follows = gBlockHelp.wnd && gBlockHelp.id == "act.foreachunit";
        if (gBlockHelp.wnd) UpdateWindow(gBlockHelp.wnd), capture(dir / "88_block_help_loop.png", gBlockHelp.wnd);
        SendMessageW(gTE.pane, WM_KEYDOWN, VK_F1, 0);
        const bool closes = !gBlockHelp.wnd;
        // In the block picker: F1 shows the selected block's page.
        static std::string pickerHelp;
        pickerHelp.clear();
        gBlockPickHook = [](HWND) {
            for (int i = 0; i < 5; ++i) pump();
            SendMessageW(gBlockPick->search, WM_KEYDOWN, VK_F1, 0);
            for (int i = 0; i < 5; ++i) pump();
            const TriggerDef* d = blockPickSelected();
            if (gBlockHelp.wnd && d && gBlockHelp.id == d->id) pickerHelp = d->id;
            gBlockPick->done = true;
        };
        showBlockPicker(false, {}, L"kill");
        gBlockPickHook = nullptr;
        if (gBlockHelp.wnd) DestroyWindow(gBlockHelp.wnd);
        int withExample = 0;
        for (const auto& d : TriggerDef::all()) withExample += !d.docs.empty() && !d.example.empty();
        log << "triggers help (F1): opens for the block " << (opened ? "yes" : "NO") << ", follows the selection " << (follows ? "yes" : "NO")
            << ", F1 closes it " << (closes ? "yes" : "NO") << ", in the block picker " << (!pickerHelp.empty() ? "yes" : "NO") << "; blocks with a description and an example "
            << withExample << " of " << TriggerDef::all().size() << (withExample == int(TriggerDef::all().size()) ? " yes" : " NO") << "\n";
        removeTrigger(path);
    }

    // T.8: a heavy unit loop shows its fixes; Apply makes the trigger cheaper.
    {
        TriggerCall each = trigMake("act.foreachunit");
        TriggerCall branch = trigMake("act.if");
        branch.lists[0] = {trigMake("cond.unittype", {"the unit", "37"})};
        branch.lists[1] = {trigMake("act.unitcolor", {"the unit", "P2"}), trigMake("act.unitsethp", {"the unit", "35"})};
        each.lists[0] = {branch};
        const auto path = addTrigger("Heavy", {each});
        const int before = TriggerSet::cost(*gTE.set.at(path));
        UpdateWindow(gTE.pane);
        const size_t shown = gTrigFixLinks.size();
        capture(dir / "89_trigger_heavy.png");
        bool applied = false;
        if (shown) {
            const RECT r = gTrigFixLinks[0].first;
            SendMessageW(gTE.pane, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM((r.left + r.right) / 2, (r.top + r.bottom) / 2));
            SendMessageW(gTE.pane, WM_LBUTTONUP, 0, MAKELPARAM((r.left + r.right) / 2, (r.top + r.bottom) / 2));
            for (int i = 0; i < 5; ++i) pump();
            const TriggerNode* t = gTE.set.at(path);
            applied = t && TriggerSet::cost(*t) < before;
            UpdateWindow(gTE.pane);
            capture(dir / "89_trigger_heavy_fixed.png");
        }
        log << "triggers lag: a heavy unit loop (cost " << before << ") shows fixes " << (shown ? "yes" : "NO") << ", Apply lowers the cost "
            << (applied ? "yes (" + std::to_string(TriggerSet::cost(*gTE.set.at(path))) + ")" : std::string("NO")) << "\n";
        removeTrigger(path);
    }

    // T.10: a picked bunker: a trigger kills it, a unit variable starts as it and another trigger uses the variable.
    // Deleting the bunker on the map reports all three; undo puts it back.
    if (app.ed) {
        const size_t count = app.ed->units().size();
        const auto bunker = app.ed->unitTypes().findByName("Terran Bunker");
        for (int k = 0; bunker && k < 12 && app.ed->units().size() == count; ++k)  // a place where it fits
            app.ed->placeUnit(*bunker, 0, (20 + 8 * (k % 4)) * 32, (20 + 10 * (k / 4)) * 32);
        bool built = false, flagged = false, cleared = false;
        if (app.ed->units().size() == count + 1) {
            const auto u = app.ed->units().back();
            const std::string pick = "@" + std::to_string(u.serial);
            TriggerNode door = TriggerSet::newVariable("door", TriggerVariable::Kind::Unit);
            door.variable.initial = pick;
            gTE.set.roots[0].children.push_back(door);
            const std::vector<int> doorPath = {0, int(gTE.set.roots[0].children.size()) - 1};
            const auto killPath = addTrigger("Kill the bunker", {trigMake("act.unitkill", {pick})}, {trigMake("cond.elapsed", {"AtLeast", "60"})});
            const auto usePath = addTrigger("Door open", {trigMake("act.display", {"The door is open.", "all"})}, {trigMake("cond.unitdead", {"door"})});
            std::vector<TriggerIssue> is;
            const std::string code = trigGeneratedCode(is);
            int errors = 0;
            for (const auto& i : is) errors += i.level == TriggerIssue::Level::Error;
            built = errors == 0 && code.find("pu_" + std::to_string(u.serial) + ".die();") != std::string::npos && code.find("v_door = pu_") != std::string::npos;
            app.ed->select(int(app.ed->units().size()) - 1, false);
            app.ed->deleteSelection();
            setWorkspace(Workspace::Map);
            setWorkspace(Workspace::Triggers);
            trigRefreshTree(killPath);
            UpdateWindow(gTE.pane);
            capture(dir / "90_picked_unit_gone.png");
            bool k = false, v = false, w = false;
            for (const auto& i : gTE.issues) {
                k |= i.node == gTE.set.at(killPath) && i.level == TriggerIssue::Level::Error;
                v |= i.node == gTE.set.at(doorPath) && i.level == TriggerIssue::Level::Error;
                w |= i.node == gTE.set.at(usePath) && i.message.find("not on the map now") != std::string::npos;
            }
            flagged = k && v && w;
            app.ed->undo();
            trigRefreshTree(killPath);
            cleared = std::none_of(gTE.issues.begin(), gTE.issues.end(), [](const TriggerIssue& i) { return i.level == TriggerIssue::Level::Error; });
            gTE.set.remove(usePath), gTE.set.remove(killPath), gTE.set.remove(doorPath);
            trigSave();
            trigRefreshTree({2});
            app.ed->undo();  // the bunker's placing
        }
        log << "triggers picked bunker: builds to code that uses it " << (built ? "yes" : "NO") << ", deleted on the map flags the trigger, the variable and its use "
            << (flagged ? "yes" : "NO") << ", undo clears them " << (cleared ? "yes" : "NO") << "\n";
    }

    // T.5: the Debug build setting checks indexes in the code.
    {
        TriggerNode list = TriggerSet::newVariable("slots", TriggerVariable::Kind::Array);
        list.variable.size = 4;
        gTE.set.roots[0].children.push_back(list);
        const auto path = addTrigger("Slots", {trigMake("act.listset", {"slots", "P1.ore", "SetTo", "1"})});
        const bool was = pr->debugBuild();
        pr->setDebugBuild(true);
        std::vector<TriggerIssue> is;
        const bool debugOn = trigGeneratedCode(is).find("v_slots[sd_idx(TrgPlayer(0).mineral, 4, ") != std::string::npos;
        pr->setDebugBuild(false);
        const bool debugOff = trigGeneratedCode(is).find("sd_idx") == std::string::npos;
        pr->setDebugBuild(was);
        removeTrigger(path);
        gTE.set.roots[0].children.pop_back();
        trigSave();
        log << "triggers debug build: indexes checked " << (debugOn ? "yes" : "NO") << ", not in a normal build " << (debugOff ? "yes" : "NO") << "\n";
    }

    // T.1: an error that only euddraft finds selects the block it came from.
    if (euddraft::find({})) {
        const auto path = addTrigger("Broken", {trigMake("act.display", {"Before", "all"}), trigMake("act.raw", {"SetResources(P1, Add, 5, Ore;"})});
        trigRefreshTree({1});
        command(ID_PROJ_COMPILE);
        for (int i = 0; i < 6000 && gBuilding; ++i) pump(), Sleep(20);
        for (int i = 0; i < 5; ++i) pump();
        const bool selected = gWorkspace == Workspace::Triggers && gTE.sel == path && gTP.sel.index == 1 && gTP.sel.list.via.empty() && gTP.sel.list.list == 1;
        UpdateWindow(gTE.pane);
        capture(dir / "91_build_error_selects_block.png");
        log << "triggers build error: euddraft's error line selects its block " << (selected ? "yes" : "NO") << " (" << gTrigBuildError << ")\n";
        removeTrigger(path);
    }
}
