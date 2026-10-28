// The top tool bar (portable, core.inc): moved from main.cpp. Its instance (gTopBarView) is made by each program: the
// Windows program shows it in its own view window under the menu; the SDL program places it under its menu bar.
//
// The bar: file and edit commands as icons, then (in the Map workspace) the modes' control (its own view, placed at
// topBarModesRect), then Build and Debug mode (a project), and Report feedback at the right end.

// Defined by each program (main.cpp, sdl_menus.inc and sdl_main_view.inc).
void command(int id);
bool commandEnabled(int id);  // the command's menu item is enabled now (the icons are gray when it is not)
bool topBarModesShown();      // the modes' control shows in the bar now (Build goes after it then)
bool modeBarTabIn(bool back);  // Tab gives the modes' control the focus: its first mode (back: its last)

int topBarHeight() { return scale(40); }

struct TopItem {
    UiIcon icon;
    int cmd;
    const wchar_t* tip;
};
const TopItem kTopItems[] = {
    {UiIcon::New, ID_FILE_NEW, L"New project (Ctrl+N)"},       {UiIcon::Open, ID_FILE_OPEN, L"Open (Ctrl+O)"},
    {UiIcon::Save, ID_FILE_SAVE, L"Save (Ctrl+S)"},  // (Save as: in the File menu, Ctrl+Shift+S)
    {UiIcon::Undo, ID_EDIT_UNDO, L"Undo (Ctrl+Z)"},          {UiIcon::Redo, ID_EDIT_REDO, L"Redo (Ctrl+Y)"},
    {UiIcon::Cut, ID_EDIT_CUT, L"Cut (Ctrl+X)"},             {UiIcon::Copy, ID_EDIT_COPY, L"Copy (Ctrl+C)"},
    {UiIcon::Paste, ID_EDIT_PASTE, L"Paste (Ctrl+V)"},       {UiIcon::Delete, ID_EDIT_DELETE, L"Delete (Del)"},
    {UiIcon::Search, ID_QUICKPICK, L"Quick search: find the mode's units, terrains, doodads, sprites or locations (Q)"},
};
// Where the icons end: the modes' control starts there.
int topBarIconsWidth() {
    const int b = scale(32), gap = scale(2), group = scale(14);
    return scale(8) + int(std::size(kTopItems)) * (b + gap) + 4 * group;
}
// Where the modes' control is in the bar (the bar's coordinates): after the icons; Build goes after it, past a divider.
RECT topBarModesRect() {
    const int bw = scale(112), bh = scale(stardraft::kit::kButtonH), by = (topBarHeight() - bh) / 2, bx = topBarIconsWidth() + scale(10);
    return {bx, by, bx + kModeCount * bw, by + bh};
}

// The bar as a view: its click areas (the icons, Build, Debug, Report feedback) are its surface's hits, which its mouse,
// cursor and tips read.
// The Update button's text and tooltip (the release's version: its tag, else its title).
inline std::wstring topBarUpdateLabel() { return L"Update"; }  // (the canvas translates it)
inline std::wstring topBarUpdateTip(const releases::Release& r) {
    return std::wstring(tr(L"Show the release notes of the newer release")) + L" (" + widen(r.tag.empty() ? r.title : r.tag) + L")";
}

struct TopBarView final : stardraft::draw::View {
    kitui::Surface surface;
    RECT updateRc{};  // the Update button at the last paint (empty: not shown)
    void paint(stardraft::draw::Canvas& c) override {
        using stardraft::draw::Font;
        using stardraft::draw::kMiddle;
        namespace draw = stardraft::draw;
        const int w = width(), h = height();
        c.fill({0, 0, w, h}, kPanel);
        surface.begin();
        const int b = scale(32), gap = scale(2), group = scale(14);
        int x = scale(8);
        const int y = (h - b) / 2;
        for (size_t i = 0; i < std::size(kTopItems); ++i) {
            if (i == 3 || i == 5 || i == 8 || i == 9) {  // groups: file | undo redo | clipboard | delete | search
                const int lx = x + group / 2 - gap;
                c.fill({lx, y + scale(6), lx + 1, y + b - scale(6)}, kBorder);
                x += group;
            }
            const TopItem& it = kTopItems[i];
            const int cmd = it.cmd;
            kitui::iconButton(c, surface, {x, y, x + b, y + b}, it.icon, [cmd] { command(cmd); }, keyTip(tr(it.tip)), kitui::Icon::Bare, false,
                              commandEnabled(cmd), kitui::Tone::Normal);
            x += b + gap;
        }
        // The right end: Build (a project; its icon and name), then Report feedback in the corner.
        const Font ui = c.font({draw::Face::Ui, 0});  // the UI font (the message font)
        if (activeProject()) {
            const std::wstring text = gBuilding ? L"Building" : L"Build";
            const Font of2 = c.setFont(ui);
            const int w2 = scale(16) + scale(8) + c.measure(text) + scale(24);
            const RECT mr = topBarModesRect();
            // After the modes; where they are hidden (Data, Triggers, the pages), after the icons.
            const int divX = (topBarModesShown() ? int(mr.right) : topBarIconsWidth()) + scale(10);
            c.fill({divX, y + scale(6), divX + 1, y + b - scale(6)}, kBorder);  // the divider between the modes and Build
            const int bx = divX + scale(10);
            const RECT bb{bx, mr.top, bx + w2, mr.bottom};
            const bool bhot = !gBuilding && surface.hot(bb);
            kitpaint::drawButtonFace(c, bb, L"", kitui::Tone::Primary, gBuilding ? kitui::State::Disabled : bhot ? kitui::State::Hot : kitui::State::Normal, ui);
            const draw::Color col = gBuilding ? kFaint : kAccent;
            c.icon({bb.left + scale(12), bb.top, bb.left + scale(12) + scale(16), bb.bottom}, int(UiIcon::Build), col, scale(16));
            c.text({bb.left + scale(12) + scale(16) + scale(8), bb.top, bb.right, bb.bottom}, text, col, kMiddle);
            c.setFont(of2);
            surface.add({bb, nullptr, gBuilding ? std::function<void()>() : [] { command(ID_PROJ_COMPILE); },
                         L"Build the project's map with euddraft (Ctrl+B)", gBuilding ? draw::Cursor::Arrow : draw::Cursor::Hand});
            // Debug mode, beside Build: the project's Debug build (index checks) and Trace build (epTrace) together.
            const EudProject* pr = activeProject();
            const bool on = pr->debugBuild() || pr->traceBuild();
            const int side = int(bb.bottom - bb.top);
            const RECT dr{bb.right + scale(6), bb.top, bb.right + scale(6) + side, bb.bottom};
            // A toggle button: on, selected (the accent fill and line); off, a plain button.
            const bool dhot = surface.hot(dr);
            kitpaint::drawBox(c, dr, on ? kSel : dhot ? kCtlHot : kCtl, on ? kAccentLine : dhot ? kBorderHot : kBorder);
            c.icon(dr, int(UiIcon::Debug), on ? kAccentBright : dhot ? kText : kDim, scale(16));
            surface.add({dr, nullptr,
                         [this] {
                             EudProject* p = activeProject();
                             if (!p) return;
                             const bool now = p->debugBuild() || p->traceBuild();  // now (the Project page can change it)
                             p->setDebugBuild(!now), p->setTraceBuild(!now);
                             projectSave(*p, now ? L"Debug mode off: Build does not add index checks or the trace for epTrace"
                                                 : L"Debug mode on: Build adds index checks and the trace for epTrace");
                             projectFormRefresh();
                             invalidate();
                         },
                         on ? L"Debug mode is on: Build adds index checks and the trace for epTrace (turn it off for the map that you release)"
                            : L"Debug mode: Build adds index checks and the trace for epTrace (Project page > Build)",
                         draw::Cursor::Hand});
        }
        const int rx = w - scale(8);
        // The icon and its label "Report feedback" (GitHub feedback #96).
        const Font ofb = c.setFont(ui);
        const std::wstring fbText = L"Report feedback";
        const int fbw = b + c.measure(fbText) + scale(12);
        const RECT fb{rx - fbw, y, rx, y + b};
        const bool fhot = surface.hot(fb);
        if (fhot) kitpaint::drawBox(c, fb, kCtlHot, draw::kNone);
        c.icon({fb.left + scale(4), y, fb.left + scale(4) + b - scale(8), y + b}, int(UiIcon::Feedback), fhot ? kFeedback : kDim, scale(16));  // quiet; its color under the mouse
        c.text({fb.left + b - scale(2), y, fb.right - scale(6), y + b}, fbText, fhot ? kText : kDim, kMiddle);
        c.setFont(ofb);
        surface.add({fb, nullptr, [] { command(ID_HELP_FEEDBACK); }, L"Report feedback: what works, what does not, ideas", draw::Cursor::Hand});
        // Update, before it: a newer release is out (the update check at start and each hour found it). Tinted with the
        // accent; a click shows its release notes, with Install.
        updateRc = {};
        if (const std::optional<releases::Release>& up = releases::available()) {
            const Font ofu = c.setFont(ui);
            const std::wstring label = topBarUpdateLabel();
            const int bh = scale(stardraft::kit::kButtonH), uy = (h - bh) / 2;
            const int uw = scale(12) + scale(16) + scale(8) + c.measure(label) + scale(14);
            const RECT ub{fb.left - scale(8) - uw, uy, fb.left - scale(8), uy + bh};
            const bool uhot = surface.hot(ub);
            kitpaint::drawBox(c, ub, uhot ? kSelHot : kSel, kAccentLine);
            c.icon({ub.left + scale(12), ub.top, ub.left + scale(12) + scale(16), ub.bottom}, int(UiIcon::Update), kAccentBright, scale(16));
            c.text({ub.left + scale(12) + scale(16) + scale(8), ub.top, ub.right, ub.bottom}, label, kOnText, kMiddle);
            c.setFont(ofu);
            surface.add({ub, nullptr, [] { command(ID_UPDATE_NOTES); }, topBarUpdateTip(*up), draw::Cursor::Hand});
            updateRc = ub;
            std::swap(surface.hits[surface.hits.size() - 2], surface.hits.back());  // (Tab: Update, then Report feedback)
        }
        c.fill({0, h - 1, w, h}, kBorder);  // the line under the bar
        surface.paintFocus(c, *this);
    }
    bool mouse(const stardraft::draw::MouseEvent& e) override {
        // The bar has the focus while the part runs (a press); the release gives it to the map view.
        if (e.kind == stardraft::draw::MouseKind::Up) {
            if (e.button != 0) return false;
            shellFocusView();
            return true;
        }
        return surface.mouse(*this, e);
    }

    // The keyboard: Tab goes through the icons, the modes (their own view, between the icons and Build), Build, Debug
    // mode, Update and Report feedback; Enter and Space press the part with the focus.
    bool focusable() const override { return surface.stopCount() > 0; }
    bool focusOnClick() const override { return false; }  // (a press focuses the bar until the release: mouse)
    void focusChanged(bool) override { invalidate(); }
    bool key(const stardraft::draw::KeyEvent& e) override { return surface.key(*this, e); }
    std::wstring focusedItem() const override {
        const kitui::Hit* h = surface.focused();
        return h ? h->tip : std::wstring();
    }
    // Whether stop `i` is before the modes (they show between the icons and Build).
    bool beforeModes(int i) const {
        const auto s = surface.stops();
        return i >= 0 && i < int(s.size()) && s[size_t(i)]->r.left < int(topBarModesRect().left);
    }
    void tabbedTo(bool back) override {
        surface.tabbedTo(*this, back);
        if (!back && topBarModesShown() && !beforeModes(0)) modeBarTabIn(false);  // (no icon is usable: the modes first)
    }
    bool tabWithin(bool back) override {
        const int n = surface.stopCount(), i = surface.focus.index(n), j = i + (back ? -1 : 1);
        if (i < 0) return false;
        // Across the modes: they take the focus.
        if (topBarModesShown() && (back ? !beforeModes(i) && (j < 0 || beforeModes(j)) : beforeModes(i) && (j >= n || !beforeModes(j))))
            return modeBarTabIn(back), true;
        return surface.tabWithin(*this, back);
    }
    // The modes give the focus back (Tab after the last mode, Shift+Tab before the first): the first part after them
    // (back: the last part before them). false: there is none.
    bool focusBesideModes(bool back) {
        const int n = surface.stopCount();
        int k = -1;
        for (int i = 0; i < n; ++i)
            if (back ? beforeModes(i) : !beforeModes(i) && k < 0) k = i;
        if (k < 0) return false;
        focus();
        surface.focus.at = k, surface.focus.keys = true;
        invalidate();
        return true;
    }
    stardraft::draw::Cursor cursor(stardraft::draw::Point p) override { return surface.cursor(p); }
    std::wstring tip(stardraft::draw::Point p) override { return surface.tip(p); }
    bool hoverTip() const override { return true; }
};
