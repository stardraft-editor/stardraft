// A snapshot's input lines (sdl_menus.inc: sdlInputLine): after each --key, --type, --click or --drag, what the
// workspace shows holds now, as one line of text that tools/sdl_snapshots.py's CHECKS read (the Windows self-test reads
// the same parts of the model): the open map (its tileset and size); the Data workspace's open entry, its changed
// values and the undo steps; the selected trigger's blocks with their values; the Classic triggers page's triggers and
// the selected one's players; the Files page's search and how many game sounds it shows; the map's briefings. Included
// by main_sdl.cpp after the workspaces' files.

namespace {

// A trigger block as text: its id and its values, with the blocks it holds ("act.setresources(P1,1,50,0)").
std::string shotBlockText(const TriggerCall& b) {
    std::string s = b.def + "(";
    for (size_t i = 0; i < b.args.size(); ++i) s += (i ? "," : "") + b.args[i];
    s += ")";
    for (const auto& list : b.lists) {
        s += "{";
        for (size_t i = 0; i < list.size(); ++i) s += (i ? "; " : "") + shotBlockText(list[i]);
        s += "}";
    }
    return s;
}

std::string shotDataState() {
    DataModel& d = dataState();
    const DatTable* t = curTable();
    if (!t) return "data: no table";
    const int entry = d.datTab >= 0 && size_t(d.datTab) < d.tabEntry.size() ? d.tabEntry[size_t(d.datTab)] : 0;
    std::string s = "data: " + t->schema().name + " " + std::to_string(entry) + ":";
    int changed = 0;
    for (const DatField& f : t->schema().fields)
        if (f.covers(entry) && t->changed(f, entry))
            s += " " + f.name + "=" + std::to_string(t->value(f, entry)) + " (stock " + std::to_string(t->stock(f, entry)) + ")", ++changed;
    if (!changed) s += " no changed values";
    return s + "; " + std::to_string(d.history.undo.size()) + " undo steps, " + std::to_string(d.history.redo.size()) + " redo steps";
}

std::string shotTriggersState() {
    const TriggerNode* n = trigSelectedNode();
    if (!n) return "triggers: nothing selected";
    std::string s = "triggers: \"" + n->name + "\"";
    if (n->kind != TriggerNode::Kind::Trigger) return s + " (" + std::to_string(n->children.size()) + " in it)";
    auto list = [&](const char* what, const std::vector<TriggerCall>& blocks) {
        s += std::string("; ") + std::to_string(blocks.size()) + " " + what + ":";
        for (const TriggerCall& b : blocks) s += " " + shotBlockText(b);
    };
    list("conditions", n->conditions);
    list("actions", n->actions);
    return s;
}

std::string shotClassicState() {
    if (!gSdlTrigPage) return "classic triggers: no page";
    const CtModel& m = gSdlTrigPage->model();
    auto players = [](const ClassicTrigger& t) {
        std::string p;
        for (size_t g = 0; g < t.players.size(); ++g)
            if (t.players[g]) p += " " + std::to_string(g < 8 ? g + 1 : g);  // (players 1-8; 17 all players, 18-21 forces)
        return p.empty() ? std::string(" none") : p;
    };
    // The page's triggers, the selected one, and the map's triggers (each one's players: the page's change is in the map).
    std::string s = "classic triggers: the page has " + std::to_string(m.trig.triggers.size()) + ", selected " + std::to_string(m.sel + 1);
    if (m.sel >= 0 && size_t(m.sel) < m.trig.triggers.size()) {
        const ClassicTrigger& t = m.trig.triggers[size_t(m.sel)];
        s += " (players" + players(t) + "; " + std::to_string(t.conditionCount()) + " conditions, " + std::to_string(t.actionCount()) + " actions)";
    }
    const ClassicTriggers inMap = gState.ed->classicTriggers();
    s += "; the map has " + std::to_string(inMap.triggers.size()) + ":";
    for (const ClassicTrigger& t : inMap.triggers) s += " [players" + players(t) + "]";
    return s;
}

std::string shotBriefingState() {
    const auto b = gState.ed->briefings();
    std::string s = "briefing: " + std::to_string(b.size()) + " briefings";
    for (const BriefingTrigger& t : b) {
        s += "; " + std::to_string(t.actions.size()) + " actions:";
        for (const BriefingAction& a : t.actions) s += " " + std::to_string(int(a.type));
    }
    return s;
}

}  // namespace

// The line's state part (empty: none for this workspace).
std::string sdlShotState() {
    const bool map = gState.ed && gState.ed->isOpen();
    switch (gWorkspace) {
    case Workspace::Data: return activeProject() ? shotDataState() : "";
    case Workspace::Triggers: return activeProject() ? shotTriggersState() : "";
    case Workspace::ClassicTriggers: return map ? shotClassicState() : "";
    case Workspace::Files: return "files: search \"" + narrow(gFilesPage.searchText()) + "\", " + std::to_string(gFilesPage.gameSoundsShown()) + " game sounds";
    case Workspace::Briefing: return map ? shotBriefingState() : "";
    default:
        return map ? "map: " + narrow(kTilesetNames[gState.ed->era() & 7]) + " " + std::to_string(gState.ed->width()) + " x " +
                         std::to_string(gState.ed->height())
                   : "";
    }
}
