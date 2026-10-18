// Help (core/views_help.inc): Help > Documentation (the index), F1 (the page of what has the focus: in the Data
// workspace, the field under the mouse or the field clicked last; else the index), the side bar's Help and the Data
// form's help icons. With a tab open, the pages show in the Help workspace (gHelpWsView, in the main window); on the
// start page, in the help window (gHelpView). A new topic shows in the one that shows. Its links that go outside open in the browser; Send feedback about this
// page opens Report feedback with the page named. --dialog help[:<topic>] is its snapshot (sdlHelpShot).

HelpView gHelpView;    // the help window's
HelpView gHelpWsView;  // the Help workspace's (registerWorkspaceView)
bool gHelpOpen = false;
int gHelpOpened = 0;  // the times the window opened in this run (a snapshot's lines: after its windows closed)

void showFeedbackSdl(const std::wstring& on, draw::View* owner);  // (sdl_options.inc)

// The window's parts: the links, Send feedback (once).
void sdlHelpWire() {
    static bool wired = false;
    if (wired) return;
    wired = true;
    changelog::setHtmlHooks(!gSilent);  // (the HTML view's measuring canvas and its thread; the help pages have no images)
    for (HelpView* v : {&gHelpView, &gHelpWsView}) {
        v->onOpenUrl = [](const std::string& url) {
            if (gSilent) std::printf("help: the browser opens %s\n", url.c_str());  // (a test run opens no browser)
            else SDL_OpenURL(url.c_str());
        };
        v->onFeedback = [v](const std::wstring& name) {
            if (gSilent) return void(std::printf("help: Report feedback on \"%s\"\n", narrow(name).c_str()));  // (a test sends nothing)
            showFeedbackSdl(name, v);
        };
    }
    gHelpView.closed = [] { gHelpOpen = false; };
    gHelpWsView.setInWorkspace(true);
}

// The help window at `topic` ("units-dat#hit-points"; "": the index). With no pages in this build: the page on the web.
void sdlShowHelp(const std::string& topic) {
    sdlHelpWire();
    if (stardraft::helpPages().empty()) {
        const stardraft::HelpTopic t = stardraft::helpSplitTopic(topic);
        gHelpView.onOpenUrl(stardraft::helpOnlineUrl(t.page.empty() ? "index" : t.page, t.anchor));
        return;
    }
    if (gRoot && gState.ed && gState.ed->isOpen() && !newPageCovers()) {  // a tab is open: the Help workspace
        if (gHelpOpen) gHelpView.close();
        gHelpWsView.show(topic);
        if (gWorkspace != Workspace::Help) ++gHelpOpened;
        setWorkspace(Workspace::Help);
        return;
    }
    gHelpView.show(topic);
    if (gHelpOpen || !gRoot || !gHost) return;
    gHelpOpen = true;
    ++gHelpOpened;
    const draw::Point sz = HelpView::size();
    const int h = std::min(sz.y, std::max(scale(360), gRoot->height() - scale(80)));
    gHost->openWindow(*gRoot, gHelpView, draw::WindowSpec{draw::WindowKind::Dialog, draw::Rect(0, 0, sz.x, h), L"Help"}, [] { gHelpOpen = false; });
}

// F1: the topic of what has the focus. The Data workspace: the field under the mouse, else the field clicked last
// (its .dat's page when the field has no place on it); else the index.
std::string sdlHelpContextTopic() {
    if (gWorkspace == Workspace::Triggers)  // the selected block's problem: its part of Trigger checks (roadmap R38)
        if (TriggerNode* t = trigSelected())
            if (const TriggerCall* c = trigBlock(*t, gTP.sel))
                if (const auto issues = trigIssuesOf(c); !issues.empty()) return trigIssueTopic(issues.front()->message);
    if (gWorkspace == Workspace::Data && curTable()) {
        const std::string dat = curTable()->schema().name;
        const std::string field = gSdlDataForm.helpField();
        stardraft::HelpTopic t = field.empty() ? stardraft::HelpTopic{} : stardraft::helpFieldTopic(dat, field);
        if (t.empty() && stardraft::helpPage(dat + "-dat")) t = {dat + "-dat", ""};
        if (!t.empty()) return t.page + (t.anchor.empty() ? "" : "#" + t.anchor);
    }
    return "";
}

bool sdlHelpRuns(int id) { return id == ID_HELP_DOCS || id == ID_HELP_CONTEXT; }
bool sdlHelpCommand(int id) {
    if (id == ID_HELP_DOCS) return sdlShowHelp(""), true;
    if (id == ID_HELP_CONTEXT) return sdlShowHelp(sdlHelpContextTopic()), true;
    return false;
}

// --- Snapshots and checks ---------------------------------------------------------------------------------------------

// --dialog help[:<topic>]: the help window at the topic (the index when none), at its own size. --dialog
// help:search:<words>: the search's list.
draw::View* sdlHelpShot(const std::string& arg, int& w, int& h) {
    if (arg != "help" && arg.rfind("help:", 0) != 0) return nullptr;
    sdlHelpWire();
    const draw::Point sz = HelpView::size();
    w = sz.x, h = sz.y;
    gHelpView.setFrame({0, 0, w, h});
    const std::string rest = arg.size() > 5 ? arg.substr(5) : "";
    if (rest.rfind("search:", 0) == 0) gHelpView.show(""), gHelpView.setSearch(widen(rest.substr(7)));
    else gHelpView.show(rest);
    return &gHelpView;
}

// After the picture (and after the inputs): what the help window shows (the Help workspace's view while it shows; "the
// window opened" counts both). Its page, the place, the scroll, the list's
// rows, the buttons, and each link of the page that goes to another help page.
void sdlHelpShotLines(bool any) {
    if (!any && !gHelpOpened) return;
    HelpView& v = gWorkspace == Workspace::Help ? gHelpWsView : gHelpView;
    std::printf("help: the window opened %d times, page %s, place \"%s\", title \"%s\"\n", gHelpOpened, v.page().c_str(), v.anchor().c_str(),
                v.title().c_str());
    if (!v.anchor().empty())
        std::printf("help: the place is at %d px, the page scrolled to %d px (of %d)\n", v.html().anchorY(v.anchor()), v.scroller().offsetY(),
                    v.scroller().maxOffsetY());
    std::printf("help: laid out %d x %d px, online %s\n", v.html().width(), v.html().contentHeight(), v.onlineUrl().c_str());
    std::printf("help: list of %d rows (%s), selected %d\n", v.rows(), v.searching() ? "search" : "pages", v.selectedRow());
    for (int i = 0; i < v.rows() && i < 40; ++i) std::printf("help: row %d \"%s\" %s\n", i, narrow(v.rowTitle(i)).c_str(), v.rowTopic(i).c_str());
    std::string buttons;
    for (const std::wstring& b : v.buttons()) buttons += (buttons.empty() ? "" : ", ") + narrow(b);
    std::printf("help: buttons %s; feedback names \"%s\"\n", buttons.c_str(), narrow(v.feedbackName()).c_str());
    int pageLinks = 0, outside = 0;
    for (const stardraft::html::Link& l : v.html().links()) (stardraft::helpLinkTarget(l.url).topic.empty() ? outside : pageLinks)++;
    std::printf("help: %d links to help pages, %d to other places\n", pageLinks, outside);
}

// --check helplinks: a click on the first link to another help page opens it in the window, Back goes back to the
// page before, a click on a row of the list opens its page, and Send feedback about this page names the page.
bool sdlHelpCheck(const std::string& what, std::string& out) {
    if (what != "helplinks") return false;
    out.clear();
    HelpView& v = gHelpView;
    sdlHelpWire();
    if (stardraft::helpPages().empty()) return out = "helplinks: no help pages in this build (WRONG)\n", true;
    v.setFrame({0, 0, HelpView::size().x, HelpView::size().y});
    v.show("units-dat");
    v.html().layout(v.scroller().viewportWidth());
    std::string target;
    const std::vector<stardraft::html::Link> links = v.html().links();
    for (const stardraft::html::Link& l : links)
        if (const stardraft::HelpLink t = stardraft::helpLinkTarget(l.url); !t.topic.empty() && t.topic.page != "units-dat") {
            target = t.topic.page;
            draw::MouseEvent e;
            e.pt = {(l.box.left + l.box.right) / 2, (l.box.top + l.box.bottom) / 2};
            for (const draw::MouseKind k : {draw::MouseKind::Move, draw::MouseKind::Down, draw::MouseKind::Up}) e.kind = k, v.html().mouse(e);
            break;
        }
    out += "helplinks: a click on the link to " + target + " shows " + v.page() + (v.page() == target && !target.empty() ? " (yes)" : " (WRONG)") + "\n";
    const bool back = v.back();
    out += std::string("helplinks: Back shows ") + v.page() + (back && v.page() == "units-dat" ? " (yes)" : " (WRONG)") + "\n";
    v.setSearch(L"");
    int row = -1;
    for (int i = 0; i < v.rows(); ++i)
        if (v.rowTopic(i) == "weapons-dat") row = i;
    v.clickRow(row);
    out += "helplinks: a click on the row of weapons-dat shows " + v.page() + (v.page() == "weapons-dat" ? " (yes)" : " (WRONG)") + "\n";
    v.setSearch(L"cooldown");
    const std::string first = v.rows() > 0 ? v.rowTopic(0) : "";
    v.clickRow(0);
    out += "helplinks: the search \"cooldown\" has " + std::to_string(v.rows()) + " rows; the first, " + first + ", shows " + v.page() + "#" + v.anchor() +
           (first == "weapons-dat#weapon-cooldown" && v.anchor() == "weapon-cooldown" ? " (yes)" : " (WRONG)") + "\n";
    out += "helplinks: the feedback names \"" + narrow(v.feedbackName()) + "\"" + (v.feedbackName() == L"Help page: Weapons (weapons.dat)" ? " (yes)" : " (WRONG)") + "\n";
    return true;
}
