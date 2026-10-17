// Undo, Redo and Edit > Undo history (Ctrl+Shift+H) in the SDL program: the steps of the workspace that shows (the
// model and the steps' names are core/undo_history.inc). The history is the program's list picker (a pop-out under the
// top bar's Undo icon); a pick undoes or redoes all the steps to it at once.

// `steps` steps back (redo: forward) in the workspace that shows, as that many Ctrl+Z (Ctrl+Y) do: the Data workspace's
// steps, the Triggers workspace's, else the map's. The status bar says the step's name ("Undo: Add 3 units"), or the
// number of steps.
void sdlUndoSteps(bool redo, size_t steps) {
    if (!steps) return;
    if (gWorkspace == Workspace::Data) {  // the Data workspace's own steps (sdl_data.inc), as the Windows program has them
        size_t done = 0;
        for (; done < steps; ++done) {
            const bool last = done + 1 == steps;
            const std::wstring name = steps == 1 ? workspaceNextStep(redo) : std::wstring();
            if (!sdlDataUndo(redo, last ? historyMessage(redo, steps, name) : std::wstring())) break;
        }
        if (done > 1 && done < steps) setMessage(historyMessage(redo, done, {}));
        return;
    }
    if (gWorkspace == Workspace::Triggers) {  // the Triggers workspace's own steps (core/triggers_model.inc)
        const std::wstring name = steps == 1 ? workspaceNextStep(redo) : std::wstring();
        const size_t can = redo ? gTrigHist.steps.size() - 1 - gTrigHist.at : gTrigHist.at;
        if (trigUndo(redo, steps)) setMessage(historyMessage(redo, std::min(steps, can), name));
        return;
    }
    if (!gState.ed || !gState.ed->isOpen()) return;
    if (gState.ed->inStroke()) return setMessage(L"Release the mouse button to undo or redo");
    typingFlush();  // a text being typed on a map page is a step of its own first: Undo takes it back
    Editor& ed = *gState.ed;
    const size_t can = redo ? ed.redoSteps() : ed.undoSteps();
    if (!can) return;
    const std::wstring name = steps == 1 ? workspaceNextStep(redo) : std::wstring();
    const std::vector<Doodad> doodads = ed.doodads();
    const std::vector<MapSprite> sprites = ed.sprites();
    if (redo) ed.redo(steps);
    else ed.undo(steps);
    keepItemSelection(gState, doodads, sprites);
    gState.terrainRev = 0;
    rebuildList(gState);
    gState.dirty = true;
    updateTitle();
    setMessage(historyMessage(redo, std::min(steps, can), name));
}

// Edit > Undo history: the list of the steps, under the top bar's Undo icon. A pick goes to that step.
void sdlShowUndoHistory() {
    if (gWorkspace == Workspace::Data) sdlDataCommitEdit();  // (a value being typed is a step of its own first)
    else if (gState.ed && gState.ed->isOpen()) {
        if (gState.ed->inStroke()) return setMessage(L"Release the mouse button to undo or redo");
        if (gWorkspace != Workspace::Triggers) typingFlush();
    }
    const UndoHistory h = workspaceHistory();
    if (h.done.empty() && h.redo.empty()) return setMessage(L"There are no steps to undo or redo");
    std::string current;
    std::vector<ListPickItem> items = historyItems(h, current);
    ListPickView v(tr(L"Search the steps"), std::move(items), current);
    draw::Point at{scale(8), scale(40)};
    if (gRoot) {  // under the Undo icon (core/views_topbar.inc: the fourth icon, after the file group)
        const draw::Point o = gRoot->topBar.origin();
        at = {o.x + scale(8) + 3 * scale(34) + scale(14), o.y + gRoot->topBar.height()};
    }
    if (!runListPick(v, at)) return;
    const int to = std::atoi(v.result().c_str()), now = std::atoi(current.c_str());
    if (to == now) return;
    sdlUndoSteps(to > now, size_t(std::abs(to - now)));
}
