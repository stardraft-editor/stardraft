// The Project workspace's script preview (portable, core.inc): the selected script's code, read only, in epScript's
// colors. The text is a kit text field (apps/kit/text_field.h) with colored spans; the view paints the panel behind it.
// The colors and the words are the ones the Triggers' code pane (RichEdit) uses: keywords, declarations, strings,
// comments, numbers, calls and types.

// The colors of epScript's coloring, as TypeScript's in VS Code (0x00BBGGRR). Light and dark themes.
struct ScriptColors {
    draw::Color text, control, declare, string, comment, number, call, type;
};
inline ScriptColors epScriptColors(bool light) {
    if (light)
        return {draw::rgb(30, 30, 30), draw::rgb(175, 0, 219), draw::rgb(0, 0, 220), draw::rgb(163, 21, 21), draw::rgb(0, 120, 0),
                draw::rgb(9, 134, 88), draw::rgb(121, 94, 38), draw::rgb(38, 127, 153)};
    return {draw::rgb(212, 212, 212), draw::rgb(197, 134, 192), draw::rgb(86, 156, 214), draw::rgb(206, 145, 120), draw::rgb(106, 153, 85),
            draw::rgb(181, 206, 168), draw::rgb(220, 220, 170), draw::rgb(78, 201, 176)};
}

// The spans of epScript's code (sorted, no overlap, every character in one; the text color for the plain runs). The
// words are ASCII: a character outside ASCII is plain text.
inline std::vector<stardraft::kit::Span> epScriptSpans(std::wstring_view code, bool light) {
    static const std::set<std::wstring> control = {L"if", L"else", L"for", L"foreach", L"while", L"return", L"break", L"continue", L"once",
                                                   L"switch", L"case", L"default"};
    static const std::set<std::wstring> declare = {L"function", L"var", L"const", L"static", L"object", L"import", L"as", L"true", L"false", L"this"};
    auto isAlpha = [](wchar_t c) { return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z'); };
    auto isDigit = [](wchar_t c) { return c >= L'0' && c <= L'9'; };
    auto isAlnum = [&](wchar_t c) { return isAlpha(c) || isDigit(c); };
    const ScriptColors col = epScriptColors(light);
    std::vector<stardraft::kit::Span> out;
    size_t plain = 0;  // the start of the text not yet in a span
    auto colored = [&](draw::Color c, size_t from, size_t to) {
        if (from > plain) out.push_back({plain, from - plain, col.text, false});
        out.push_back({from, to - from, c, false});
        plain = to;
    };
    const size_t n = code.size();
    size_t i = 0;
    while (i < n) {
        const wchar_t c = code[i];
        if (c == L'/' && i + 1 < n && code[i + 1] == L'/') {
            size_t e = code.find(L'\n', i);
            if (e == std::wstring_view::npos) e = n;
            colored(col.comment, i, e), i = e;
        } else if (c == L'"') {
            size_t e = i + 1;
            while (e < n && code[e] != L'"' && code[e] != L'\n') e += code[e] == L'\\' ? 2 : 1;
            e = std::min(n, e + 1);
            colored(col.string, i, e), i = e;
        } else if (isDigit(c)) {
            size_t e = i;
            while (e < n && isAlnum(code[e])) ++e;
            colored(col.number, i, e), i = e;
        } else if (isAlpha(c) || c == L'_') {
            size_t e = i;
            while (e < n && (isAlnum(code[e]) || code[e] == L'_')) ++e;
            const std::wstring word(code.substr(i, e - i));
            size_t k = e;
            while (k < n && code[k] == L' ') ++k;
            const bool call = k < n && code[k] == L'(';
            const bool type = word[0] >= L'A' && word[0] <= L'Z' && !call;
            if (control.count(word)) colored(col.control, i, e);
            else if (declare.count(word)) colored(col.declare, i, e);
            else if (call) colored(col.call, i, e);
            else if (type || (i > 1 && code[i - 2] == L':')) colored(col.type, i, e);  // a type, or the type after "name: "
            i = e;
        } else
            ++i;
    }
    if (plain < n) out.push_back({plain, n - plain, col.text, false});
    return out;
}

// The script preview: the code in a read-only text field with no box, its text 10 px in from the left (as the
// RichEdit's margin) and no wrapping; the field scrolls and shows its own bar.
class ProjectPreviewView final : public draw::View {
public:
    ProjectPreviewView() {
        field_.setPlain(true);
        field_.setMultiline(true);
        field_.setWrap(false);
        field_.setReadOnly(true);
        field_.setCopyWhole(true);
        field_.setFont({draw::Face::Mono, 0});
        add(field_, {0, 0, 0, 0});
    }
    stardraft::kit::TextField& field() { return field_; }

    // Shows `text` (the code) with its epScript colors, from its first line.
    void setCode(std::wstring text, bool light) {
        field_.setText(std::move(text));
        field_.model().setCaret(0);  // the top of the code (a set text leaves the caret at the end)
        field_.setSpans(epScriptSpans(field_.text(), light));
    }

    void resized() override {
        const int inset = std::max(0, scale(10) - stardraft::kit::px(2));  // the field's own padding is px(2)
        field_.setFrame({inset, 0, width(), height()});
    }
    void paint(draw::Canvas& c) override { c.fill({0, 0, width(), height()}, kPanel); }

private:
    stardraft::kit::TextField field_;
};

int projectPreviewWidth(int pageW);  // (the helpers below)

// The Project page's host: the program's part (project_view.inc has the Windows one). The page asks it for the active
// project's facts and asks it to act; the page itself draws the Files section and takes its clicks.
class ProjectHost {
public:
    virtual ~ProjectHost() = default;
    // The Files section's texts, as the page shows them (hasProject false: no project, and the page is empty).
    struct Files {
        bool hasProject = false, built = false;
        std::wstring name, folder, source, output, install, euddraft;
    };
    virtual Files files() = 0;
    virtual bool previewOn() = 0;                   // the script preview is shown (the </> button is on)
    virtual void togglePreview() = 0;
    virtual void openFolder() = 0;
    virtual void editOutput(draw::Rect field) = 0;  // the output file's name, typed in a box under the field
    virtual void browseOutput() = 0;
    virtual void defaultOutput() = 0;
    virtual void changeInstall() = 0;
    virtual void pageResized() = 0;
    // The Build section: euddraft's options and debug builds (on now), and the click that turns one over (set: its state
    // when the page was painted).
    enum class BuildOption { EudTurbo, MoreUnits, NoAirCollision, RemakeUnits, DebugBuild, TraceBuild };
    virtual bool buildOption(BuildOption o) = 0;
    virtual void toggleBuildOption(BuildOption o, bool set) = 0;
    // The background music: its plugin on, and the file's name and length in seconds (hasFile: a file is chosen).
    struct Music {
        bool on = false, hasFile = false;
        std::wstring name;
        double secs = 0;
    };
    virtual Music music() = 0;
    virtual void chooseMusic() = 0;
    virtual void clearMusic() = 0;
    // The files in memory (euddraft's dataDumper): each file's name and its address, as the page shows them.
    struct MemoryFile {
        std::wstring file, at;
    };
    virtual std::vector<MemoryFile> memoryFiles() = 0;
    virtual void removeMemoryFile(size_t i) = 0;
    virtual void addMemoryFile() = 0;
    // The scripts Build runs, in order: each one's name and facts (the page makes its note and its buttons from them).
    // file: a script's path; changed: a found script's last change; afterBuild: changed after the last build.
    struct ScriptRow {
        std::string name;
        std::wstring file, changed;
        bool selected = false, script = false, generated = false, linked = false, found = false, afterBuild = false;
    };
    virtual std::vector<ScriptRow> scripts() = 0;
    virtual void selectScript(const std::string& name) = 0;
    virtual void openScript(const std::string& name) = 0;  // in the user's editor
    virtual void showScript(const std::string& name) = 0;  // in its folder
    virtual void moveScript(const std::string& name, int by) = 0;
    virtual void removeScript(const std::string& name) = 0;
    virtual void newScript() = 0;
    virtual void linkScript() = 0;
    virtual void openSelectedScript() = 0;  // the selected script, double-clicked: in the editor (.eps, .py)
    // epTrace (the trace build's report): the section's facts as the page shows them.
    struct TraceLine {
        int64_t samples = 0;
        std::wstring where, code, tip;  // where the line is, its code, and its tip (samples and place)
    };
    struct Trace {
        bool shown = false, open = false;        // shown: a trace build, or a report; open: epTrace's window is open
        bool traceBuild = false, built = false;  // Build writes the trace file; the trace file is there
        std::wstring traceFile;                  // built: the file's name as the page shows it, and its time
        bool hasReport = false;
        int64_t total = 0;                       // the report's samples
        size_t lineCount = 0;                    // the report's lines
        std::wstring reportTime;                 // the report's time
        std::vector<TraceLine> lines;            // the lines with the most samples (at most 15), the most first
    };
    virtual Trace trace() = 0;
    virtual void startEpTrace() = 0;
    virtual void openReport() = 0;
    virtual void openTraceLine(size_t i) = 0;  // the line in the Triggers workspace (a trigger's code) or in its script
};

// The background music (bgmplayer) and files in memory (dataDumper) are off for now: not shown. A project that has them
// keeps its sections, and Build uses them.
constexpr bool kProjectMusicAndFiles = false;

// The Project page: the project's files, its build options, its scripts and epTrace's report. A view in a view window: it
// scrolls, and it takes the clicks of its own parts; the program does what they ask (ProjectHost).
class ProjectPageView final : public draw::View {
public:
    explicit ProjectPageView(ProjectHost& host) : host_(host) {}

    // The scroll position (the page's height is the one the last paint found).
    void scrollTo(int s) {
        const int s2 = std::clamp(s, 0, std::max(0, contentH_ - height()));
        if (s2 != scroll_) scroll_ = s2, invalidate();
    }

    // The tips of what the last paint put on the page (the self-tests read them).
    std::vector<std::wstring> tips() const {
        std::vector<std::wstring> out;
        for (const Hit& h : hits_)
            if (!h.tip.empty()) out.push_back(h.tip);
        return out;
    }

    void paint(draw::Canvas& c) override;
    bool mouse(const draw::MouseEvent& e) override;
    bool vscroll(int code, int pos, int track, int) override;
    bool windowScrollBar(int& range, int& page, int& pos) override {
        range = std::max(0, contentH_ - 1), page = height(), pos = scroll_;
        return true;
    }
    void resized() override { host_.pageResized(); }
    // The mouse capture went: a drag ends where it is (its row stays).
    void captureLost() override {
        if (drag_.active) drag_ = {}, invalidate();
    }
    bool hoverTip() const override { return true; }
    draw::Cursor cursor(draw::Point pt) override;
    std::wstring tip(draw::Point pt) override;
    // Report feedback names the part under the mouse by its tip; elsewhere on the page, the page.
    std::wstring feedbackName(draw::Point pt) override {
        const int hi = tipAt(pt);
        if (hi >= 0) return hits_[size_t(hi)].tip;
        return L"the Project page";
    }

private:
    struct Hit {
        draw::Rect r;
        std::function<void()> click;
        std::wstring tip;
        draw::Cursor cursor = draw::Cursor::Arrow;
        int grip = -1;  // a row's grip: the row's index (a press starts the drag that moves the row)
    };
    // A row's icon button at its right edge.
    struct RowAct {
        UiIcon icon;
        std::wstring tip;
        std::function<void()> click;
        bool danger = false, enabled = true;
    };
    struct Button {
        std::wstring text, tip;
        std::function<void()> click;
    };
    // The top part under the point (the cursor and the hover take it, as the old page's WM_SETCURSOR did).
    int hitAt(draw::Point pt) const {
        for (size_t i = hits_.size(); i-- > 0;)
            if (hits_[i].r.contains(pt)) return int(i);
        return -1;
    }
    // The top part a press acts on: one with a click or a grip (a disabled button passes the press on, as the old page's
    // hit lookup skipped the parts without a click).
    int clickAt(draw::Point pt) const {
        for (size_t i = hits_.size(); i-- > 0;)
            if (hits_[i].r.contains(pt) && (hits_[i].click || hits_[i].grip >= 0)) return int(i);
        return -1;
    }
    // The top part with a tip (a part without one, such as a row's cover, does not hide the tips under it).
    int tipAt(draw::Point pt) const {
        for (size_t i = hits_.size(); i-- > 0;)
            if (hits_[i].r.contains(pt) && !hits_[i].tip.empty()) return int(i);
        return -1;
    }
    // Whether the mouse is in `r` (the host's pointer, in this view's coordinates).
    bool hot(draw::Rect r) const { return host() && r.contains(host()->mouse(*this)); }
    static draw::Rect squareOf(draw::Rect r) {
        const int w = r.width(), h = r.height(), s = std::min(w, h);
        const int x = r.left + (w - s) / 2, y = r.top + (h - s) / 2;
        return {x, y, x + s, y + s};
    }
    static int buttonWidth(draw::Canvas& c, const std::wstring& text) {
        return std::max(scale(stardraft::kit::kButtonMinW), c.measure(text) + scale(28));
    }
    // A button's face (the kit's look: a tone Normal button, lit under the mouse).
    static void drawButton(draw::Canvas& c, draw::Rect r, const std::wstring& text, bool over) {
        c.roundBox(r, over ? kCtlHot : kCtl, over ? kBorderHot : kBorder, float(scale(stardraft::kit::kRadius)));
        c.text(r, text, kText, draw::kMiddle | draw::kCenter | draw::kEllipsis);
    }
    // The preview's toggle: a bare icon, accent while the preview is on.
    static void drawToggle(draw::Canvas& c, draw::Rect r, bool on, bool over) {
        if (over) c.roundBox(r, kCtlHot, draw::kNone, float(scale(stardraft::kit::kRadius)));
        c.icon(r, int(UiIcon::Code), over ? (on ? kAccentBright : kText) : (on ? kAccent : kDim), scale(16));
    }
    // The Build section (its heading, the options, the background music) from y; returns the y after it.
    int paintBuild(draw::Canvas& c, int y, int x0, int labelW, int rowH, int colRight);
    // The background music's row from y (kProjectMusicAndFiles); returns the y after it.
    int paintMusic(draw::Canvas& c, int y, int x0, int labelW, int rowH, int colRight);
    // A check box with its label: the whole row is its click area.
    void paintCheck(draw::Canvas& c, draw::Rect r, const std::wstring& label, bool on, const std::wstring& tip, std::function<void()> click);
    // The files in memory from y (kProjectMusicAndFiles); returns the y after them.
    int paintMemory(draw::Canvas& c, int y, int x0, int labelW, int rowH, int colRight);
    // The scripts Build runs, from y (a row moves by its grip); returns the y after them.
    int paintScripts(draw::Canvas& c, int y, int x0, int rowH, int colRight);
    // The epTrace section from y (shown only with a trace build or a report); returns the y after it.
    int paintTrace(draw::Canvas& c, int y, int x0, int labelW, int rowH, int colRight);
    // A row's icon buttons at its right edge: shown under the mouse, or always when `show`.
    void paintRowActions(draw::Canvas& c, draw::Rect row, const std::vector<RowAct>& acts, bool show);
    // A list row's ground (selected: the accent bar; under the mouse: the row's ground) and a grip's dots.
    static void listRowFace(draw::Canvas& c, draw::Rect r, bool selected, bool over);
    static void paintGrip(draw::Canvas& c, int x, int y, bool on);
    // A small button in a row (as wide as its text; not enabled: faint, no click): returns its right edge.
    int smallButton(draw::Canvas& c, int x, int y, int h, const std::wstring& text, std::function<void()> click, const std::wstring& tip = L"",
                    bool enabled = true);
    // Dragging a script row: from the grip's press, the row goes where the mouse is (the drop line), and on the release it moves.
    void startDrag(int from, draw::Point pt);
    void dragTo(draw::Point pt);
    void endDrag();

    struct Drag {
        bool active = false;
        int from = -1, to = -1;  // the row taken, and where it goes now (an index in the list with the row)
    };
    ProjectHost& host_;
    std::vector<Hit> hits_;  // what the last paint put under the mouse
    int hover_ = -1;         // the hit under the mouse at the last move
    int scroll_ = 0, contentH_ = 0;
    Drag drag_;
    std::vector<draw::Rect> scriptRows_;     // this paint's script rows (where a drag goes)
    std::vector<std::string> scriptNames_;   // this paint's scripts, in build order
};

void ProjectPageView::paint(draw::Canvas& c) {
    hits_.clear();
    const int w = width(), h = height();
    c.fill({0, 0, w, h}, kBg);
    const int pageW = w - projectPreviewWidth(w);
    const int x0 = scale(18), labelW = scale(170), rowH = scale(32), headH = scale(stardraft::kit::kRowH);
    const int colRight = std::min(pageW - x0, x0 + scale(1000));
    // The preview's toggle: </> at the top right of the page, as the Triggers workspace has it.
    const draw::Rect tog = squareOf({pageW - scale(48), scale(10), pageW - scale(16), scale(42)});
    const bool on = host_.previewOn();
    drawToggle(c, tog, on, hot(tog));
    hits_.push_back({tog, [this] { host_.togglePreview(); }, on ? L"Hide the script's code" : L"Show the selected script's code", draw::Cursor::Hand});

    int y = scale(16) - scroll_;
    const ProjectHost::Files f = host_.files();
    if (f.hasProject) {
        // The title: the project's name, and whether it is built.
        const draw::Font old = c.setFont(c.font({draw::Face::Ui, -(shellUiFontLfHeight() * 3 / 2)}));
        c.text({x0, y, colRight, y + scale(26)}, f.name.empty() ? std::wstring(L"(empty)") : f.name, kText, draw::kMiddle | draw::kEllipsis);
        c.setFont(old);
        c.text({x0, y + scale(24), colRight, y + scale(42)}, f.built ? L"EUD project  \x00B7  built" : L"EUD project  \x00B7  not built yet", kDim, draw::kMiddle);
        y += scale(56);

        c.text({x0, y, colRight, y + headH}, L"Files", kText, draw::kMiddle);
        c.fill({x0, y + headH - 1, colRight, y + headH}, kBorder);
        y += headH + scale(6);
        // A row: its label, a field (read only, or one to click), and buttons on the right.
        auto row = [&](const wchar_t* label, const std::wstring& text, bool edit, const std::vector<Button>& buttons, const std::wstring& tip,
                       const std::function<void(draw::Rect)>& editAct) {
            c.text({x0, y, x0 + labelW, y + rowH}, label, kDim, draw::kMiddle | draw::kEllipsis);
            int bx = colRight;
            for (auto it = buttons.rbegin(); it != buttons.rend(); ++it) bx -= buttonWidth(c, it->text) + scale(8);
            const draw::Rect field{x0 + labelW, y + scale(2), bx, y + rowH - scale(2)};
            if (edit) {
                c.roundBox(field, kCtl, hot(field) ? kBorderHot : kBorder, float(scale(stardraft::kit::kRadius)));
                hits_.push_back({field, [editAct, field] { editAct(field); }, tip, draw::Cursor::IBeam});
            } else if (!tip.empty())
                hits_.push_back({field, nullptr, tip, draw::Cursor::Arrow});
            const draw::Rect shown{field.left + (edit ? scale(8) : 0), field.top, field.right - scale(8), field.bottom};
            c.text(shown, text, edit ? kText : kDim, draw::kPathEllipsis | draw::kMiddle);
            const int by = y + (rowH - scale(stardraft::kit::kButtonH)) / 2;
            int rx = colRight;
            for (auto it = buttons.rbegin(); it != buttons.rend(); ++it) {
                const int bw = buttonWidth(c, it->text);
                const draw::Rect br{rx - bw, by, rx, by + scale(stardraft::kit::kButtonH)};
                drawButton(c, br, it->text, hot(br));
                hits_.push_back({br, it->click, it->tip, draw::Cursor::Hand});
                rx -= bw + scale(stardraft::kit::kButtonGap);
            }
            y += rowH + scale(4);
        };
        row(L"Folder", f.folder, false, {{L"Open folder", L"", [this] { host_.openFolder(); }}}, L"", nullptr);
        row(L"Source map", f.source, false, {}, L"", nullptr);
        row(L"Output file", f.output, true,
            {{L"Browse\x2026", L"", [this] { host_.browseOutput(); }}, {L"Default", L"In the project folder", [this] { host_.defaultOutput(); }}},
            L"Where Build writes the map", [this](draw::Rect field) { host_.editOutput(field); });
        row(L"StarCraft folder", f.install, false, {{L"Change\x2026", L"", [this] { host_.changeInstall(); }}},
            L"The folder that StarDraft 2 reads game data from", nullptr);
        row(L"euddraft", f.euddraft, false, {}, L"", nullptr);
        y += scale(12);
        y = paintBuild(c, y, x0, labelW, rowH, colRight);
        if (kProjectMusicAndFiles) y = paintMemory(c, y, x0, labelW, rowH, colRight);
        y = paintScripts(c, y, x0, rowH, colRight);
        y = paintTrace(c, y, x0, labelW, rowH, colRight);
    }
    contentH_ = y + scroll_;
    if (const int most = std::max(0, contentH_ - h); scroll_ > most) scroll_ = most, invalidate();  // a taller window: no scroll is left
}

int ProjectPageView::paintBuild(draw::Canvas& c, int y, int x0, int labelW, int rowH, int colRight) {
    using BO = ProjectHost::BuildOption;
    const int headH = scale(stardraft::kit::kRowH);
    // A section heading: its name and a line to the column's edge.
    c.text({x0, y, colRight, y + headH}, L"Build", kText, draw::kMiddle);
    c.fill({x0, y + headH - 1, colRight, y + headH}, kBorder);
    y += headH + scale(6);
    // The label column names the options (not an empty column).
    c.text({x0, y, x0 + labelW, y + rowH}, L"Options", kDim, draw::kMiddle | draw::kEllipsis);
    struct Option {
        BO o;
        std::wstring label, tip;
    };
    const std::vector<Option> options = {
        {BO::EudTurbo, L"EUD Turbo", L"A shorter trigger cycle: the triggers run more often (euddraft's eudTurbo)"},
        {BO::MoreUnits, L"More units and sprites", L"Room for more sprites, images, bullets and orders (euddraft's unlimiter)"},
        {BO::NoAirCollision, L"Air units do not push apart", L"Air units can stay on top of each other (euddraft's noAirCollision)"},
        {BO::RemakeUnits, L"New graphics for preplaced units",
         L"The game makes the preplaced units before the .dat edits, so they keep the old graphics. With this option, the map "
         L"removes each preplaced unit that the edits give other graphics, and makes it again on its place at the start. The map "
         L"view shows these units with the new graphics. A unit where the game cannot put a unit (on other units, or on ground "
         L"that it cannot stand on) moves near its place, or the game does not make it again."},
        {BO::DebugBuild, L"Debug build",
         L"The triggers check each list index that the game calculates. An index out of range shows a message on the screen, and the "
         L"triggers use the first item. Turn it off for the map that you release."},
        {BO::TraceBuild, L"Trace build (epTrace)",
         L"The scripts record each line that they run, and Build writes a trace file beside the map. epTrace reads it while the map "
         L"plays in StarCraft and shows which lines use the most time. The map runs more slowly: turn it off for the map that you release."},
    };
    for (const Option& op : options) {
        const bool set = host_.buildOption(op.o);
        const BO o = op.o;
        paintCheck(c, {x0 + labelW, y, colRight, y + rowH}, op.label, set, op.tip, [this, o, set] { host_.toggleBuildOption(o, set); });
        y += rowH;
    }
    y += scale(6);
    if (kProjectMusicAndFiles) y = paintMusic(c, y, x0, labelW, rowH, colRight);
    return y;
}

int ProjectPageView::paintMusic(draw::Canvas& c, int y, int x0, int labelW, int rowH, int colRight) {
    const ProjectHost::Music m = host_.music();
    // The label is green while the plugin is on (a changed setting).
    c.text({x0, y, x0 + labelW, y + rowH}, L"Background music", m.on ? kGood : kDim, draw::kMiddle | draw::kEllipsis);
    std::wstring text = L"None";
    if (m.hasFile) {
        const int s = int(m.secs);
        text = m.name + L"   " + std::to_wstring(s / 60) + L":" + (s % 60 < 10 ? L"0" : L"") + std::to_wstring(s % 60);
    }
    // The dropdown (a box to click, the name and the arrow), as the Data forms' pick boxes have it.
    const draw::Rect box{x0 + labelW, y + scale(2), colRight - scale(stardraft::kit::kIconBox + 8), y + rowH - scale(2)};
    const bool over = hot(box);
    c.roundBox(box, kCtl, over ? kBorderHot : kBorder, float(scale(stardraft::kit::kRadius)));
    c.text({box.left + scale(8), box.top, box.right - scale(22), box.bottom}, text, kText, draw::kMiddle | draw::kEllipsis);
    c.icon({box.right - scale(20), box.top, box.right - scale(6), box.bottom}, int(UiIcon::Dropdown), over ? kText : kDim, scale(9));
    hits_.push_back({box, [this] { host_.chooseMusic(); }, L"Plays again and again for every player (euddraft's bgmplayer)", draw::Cursor::Hand});
    if (m.hasFile) {  // the close button: no background music
        const draw::Rect x = squareOf({box.right + scale(4), box.top, box.right + scale(4 + stardraft::kit::kIconBox), box.bottom});
        const bool overX = hot(x);
        c.icon(x, int(UiIcon::Close), overX ? kBad : kDim, scale(14));
        hits_.push_back({x, [this] { host_.clearMusic(); }, L"No background music", draw::Cursor::Hand});
    }
    return y + rowH + scale(4);
}

int ProjectPageView::paintMemory(draw::Canvas& c, int y, int x0, int labelW, int rowH, int colRight) {
    const std::vector<ProjectHost::MemoryFile> files = host_.memoryFiles();
    c.text({x0, y, x0 + labelW, y + rowH}, L"Files in memory", files.empty() ? kDim : kGood, draw::kMiddle | draw::kEllipsis);
    int ly = y;
    for (size_t i = 0; i < files.size(); ++i) {
        const draw::Rect r{x0 + labelW - scale(6), ly, colRight, ly + rowH};
        listRowFace(c, r, false, hot(r));
        c.text({r.left + scale(6), r.top, r.right - scale(200), r.bottom}, files[i].file, kText, draw::kPathEllipsis | draw::kMiddle);
        c.text({r.right - scale(200), r.top, r.right - scale(40), r.bottom}, L"at " + files[i].at, kDim, draw::kRight | draw::kMiddle);
        paintRowActions(c, r, {{UiIcon::Close, L"Remove from the list", [this, i] { host_.removeMemoryFile(i); }, true}}, false);
        ly += rowH;
    }
    smallButton(c, x0 + labelW, ly, rowH, L"Add a file\x2026", [this] { host_.addMemoryFile(); },
                L"Loads a file's bytes into the game's memory when the map starts (euddraft's dataDumper)");
    return ly + rowH + scale(12);
}

int ProjectPageView::paintScripts(draw::Canvas& c, int y, int x0, int rowH, int colRight) {
    const int headH = scale(stardraft::kit::kRowH);
    // The heading: its name and a line to the column's edge, with the tip that says what Build does with the list.
    const int hy = y;
    c.text({x0, y, colRight, y + headH}, L"Scripts", kText, draw::kMiddle);
    c.fill({x0, y + headH - 1, colRight, y + headH}, kBorder);
    y += headH + scale(6);
    hits_.push_back({{x0, hy, x0 + scale(120), hy + headH}, nullptr,
                     L"Build runs these in order, from the top. onPluginStart runs once when the map starts; beforeTriggerExec and afterTriggerExec run each trigger cycle",
                     draw::Cursor::Arrow});
    const std::vector<ProjectHost::ScriptRow> list = host_.scripts();
    scriptRows_.clear(), scriptNames_.clear();
    for (size_t i = 0; i < list.size(); ++i) {
        const ProjectHost::ScriptRow& s = list[i];
        const std::string name = s.name;
        scriptNames_.push_back(name);
        const bool script = s.script, generated = s.generated, linked = s.linked;
        // The note: where the code comes from; for a script file, whether it is found and its last change (R5: a change
        // made in another editor after the last build shows in the accent color).
        std::wstring note, after;
        bool warn = false;
        if (generated) note = L"from the Data workspace";
        else if (!script) note = L"euddraft plugin";
        else if (!s.found) note = linked ? L"linked  \x00B7  not found: Build stops" : L"not found: Build stops", warn = true;
        else {
            note = (linked ? L"linked  \x00B7  changed " : L"changed ") + s.changed;
            if (s.afterBuild) after = L"  after the last build";
        }
        const draw::Rect r{x0 - scale(6), y, colRight, y + rowH};
        scriptRows_.push_back(r);
        const bool on = s.selected;
        listRowFace(c, r, on, hot(r));
        // The name is the row's click (selects the script); a script's tip gives its file.
        const std::wstring tip = script ? (linked ? std::wstring(L"A linked file. StarDraft 2 does not copy or change it: ") : std::wstring()) + s.file
                                        : std::wstring();
        hits_.push_back({r, [this, name] { host_.selectScript(name); }, tip, draw::Cursor::Hand});
        // The grip (six dots, shown under the mouse) moves the row by a drag; a generated row does not move.
        const bool dragged = drag_.active && drag_.from == int(i);
        if (dragged) c.fill(r, kSel, 160);
        const bool can = !generated;
        if (can && (hot(r) || dragged)) paintGrip(c, r.left + scale(5), (r.top + r.bottom) / 2 - scale(6), dragged);
        if (can) {
            hits_.push_back({{r.left, r.top, r.left + scale(18), r.bottom}, nullptr, L"Drag to move", draw::Cursor::SizeAll, int(i)});
        }
        // The row's cover (as the old row actions): an arrow over the whole row, the grip included; its clicks pass on.
        hits_.push_back({r, nullptr, std::wstring(), draw::Cursor::Arrow});
        // The name in the mono font, the note after it in the UI font.
        const draw::Font ui = c.setFont(c.font({draw::Face::Mono, 0}));
        const draw::Rect t{x0 + scale(16), r.top, r.right - scale(380), r.bottom};
        c.text(t, widen(name), on ? kAccent : kText, draw::kPathEllipsis | draw::kMiddle);
        const int nameRight = t.left + std::min<int>(t.width(), c.measure(widen(name)));
        c.setFont(ui);
        const draw::Rect n{nameRight + scale(16), r.top, r.right - scale(140), r.bottom};
        c.text(n, note, warn ? kWarn : kDim, draw::kEllipsis | draw::kMiddle);
        if (!after.empty()) {
            draw::Rect na = n;
            na.left = std::min<int>(n.right, n.left + c.measure(note));
            c.text(na, after, kAccent, draw::kEllipsis | draw::kMiddle);
        }
        // The row's buttons: a script opens (in the user's editor, in its folder); each row moves and leaves the build.
        std::vector<RowAct> acts;
        if (script) {
            acts.push_back({UiIcon::OpenExternal, L"Open in your editor", [this, name] { host_.openScript(name); }});
            acts.push_back({UiIcon::Folder, L"Show in folder", [this, name] { host_.showScript(name); }});
        }
        const bool movable = !generated;
        acts.push_back({UiIcon::MoveUp, L"Earlier", [this, name] { host_.moveScript(name, -1); }, false, movable && i > 0 && !list[i - 1].generated});
        acts.push_back({UiIcon::MoveDown, L"Later", [this, name] { host_.moveScript(name, 1); }, false, movable && i + 1 < list.size()});
        acts.push_back({UiIcon::Close,
                        generated ? L"Comes from the Data workspace"
                        : linked  ? L"Remove the link. StarDraft 2 does not delete the file."
                                  : L"Remove from the build (the file stays in the project folder)",
                        [this, name] { host_.removeScript(name); }, true, !generated});
        paintRowActions(c, r, acts, on);
        y += rowH;
    }
    // The drop line: where the dragged row goes.
    if (drag_.active && !scriptRows_.empty()) {
        const int ly = drag_.to < int(scriptRows_.size()) ? scriptRows_[size_t(drag_.to)].top : scriptRows_.back().bottom;
        c.fill({scriptRows_.front().left, ly - scale(1), scriptRows_.front().right, ly + scale(1)}, kAccent);
    }
    y += scale(4);
    const int ax = smallButton(c, x0, y, rowH, L"New script", [this] { host_.newScript(); });
    smallButton(c, ax + scale(8), y, rowH, L"Link a file\x2026", [this] { host_.linkScript(); },
                L"Add an .eps or .py file from anywhere on disk to the build. StarDraft 2 does not copy or change it.");
    return y + rowH + scale(24);
}

int ProjectPageView::paintTrace(draw::Canvas& c, int y, int x0, int labelW, int rowH, int colRight) {
    const ProjectHost::Trace t = host_.trace();
    if (!t.shown) return y;
    const int headH = scale(stardraft::kit::kRowH);
    // A section heading: its name and a line to the column's edge.
    c.text({x0, y, colRight, y + headH}, L"epTrace", kText, draw::kMiddle);
    c.fill({x0, y + headH - 1, colRight, y + headH}, kBorder);
    y += headH + scale(6);
    auto label = [&](const wchar_t* text) { c.text({x0, y, x0 + labelW, y + rowH}, text, kDim, draw::kMiddle | draw::kEllipsis); };
    label(L"Trace file");
    {
        const draw::Rect r{x0 + labelW, y, colRight - scale(170), y + rowH};
        if (t.built) c.text(r, t.traceFile, kDim, draw::kMiddle | draw::kEllipsis);
        else c.text(r, t.traceBuild ? L"None yet: Build writes it" : L"None: Trace build is off", kDim, draw::kMiddle | draw::kEllipsis);
        const int bx = colRight - c.measure(L"Start epTrace") - scale(20);
        if (t.open)
            c.text({bx, y, colRight, y + rowH}, L"epTrace is open", kAccent, draw::kMiddle | draw::kEllipsis | draw::kRight);
        else
            smallButton(c, bx, y, rowH, L"Start epTrace", [this] { host_.startEpTrace(); },
                        L"Start the map in StarCraft first. epTrace asks for administrator rights, shows the lines that run, and writes its "
                        L"report when you close its window",
                        t.built);
    }
    y += rowH + scale(4);
    label(L"Report");
    {
        const draw::Rect r{x0 + labelW, y, colRight - scale(170), y + rowH};
        if (!t.hasReport)
            c.text(r, L"None yet: close epTrace's window to write it", kDim, draw::kMiddle | draw::kEllipsis);
        else if (t.total == 0)
            c.text(r, L"No samples: no script line ran each trigger cycle while epTrace looked", kDim, draw::kMiddle | draw::kEllipsis);
        else
            c.text(r, std::to_wstring(t.total) + L" samples on " + std::to_wstring(t.lineCount) + L" lines  \x00B7  " + t.reportTime, kDim,
                   draw::kMiddle | draw::kEllipsis);
        if (t.hasReport)
            smallButton(c, colRight - c.measure(L"Open the report") - scale(20), y, rowH, L"Open the report", [this] { host_.openReport(); },
                        L"Open the report in Notepad, which shows how often epTrace found each script line running");
    }
    y += rowH + scale(4);
    if (!t.hasReport || t.lineCount == 0) return y + scale(20);
    // The lines with the most samples: the samples (and a bar), where the line is, and its code.
    const auto& lines = t.lines;
    const int64_t most = lines.front().samples;
    const int countW = scale(70), barW = scale(60), whereW = std::min(scale(380), (colRight - x0) / 2);
    for (size_t i = 0; i < lines.size(); ++i) {
        const ProjectHost::TraceLine& ln = lines[i];
        const draw::Rect r{x0 - scale(6), y, colRight, y + rowH};
        listRowFace(c, r, false, hot(r));
        int x = r.left + scale(6);
        c.text({x, r.top, x + countW, r.bottom}, std::to_wstring(ln.samples), kText, draw::kMiddle | draw::kEllipsis | draw::kRight);
        x += countW + scale(10);
        const draw::Rect bar{x, r.top + rowH / 2 - scale(3), x + std::max<int>(scale(2), int(barW * ln.samples / std::max<int64_t>(1, most))),
                             r.top + rowH / 2 + scale(3)};
        c.roundBox(bar, kAccent, draw::kNone, float(scale(3)));
        x += barW + scale(12);
        c.text({x, r.top, x + whereW, r.bottom}, ln.where, kText, draw::kMiddle | draw::kEllipsis);
        x += whereW + scale(12);
        const draw::Font ui = c.setFont(c.font({draw::Face::Mono, 0}));
        c.text({x, r.top, r.right - scale(8), r.bottom}, ln.code, kDim, draw::kMiddle | draw::kEllipsis);
        c.setFont(ui);
        hits_.push_back({r, [this, i] { host_.openTraceLine(i); }, ln.tip, draw::Cursor::Hand});
        y += rowH;
    }
    return y + scale(24);
}

void ProjectPageView::listRowFace(draw::Canvas& c, draw::Rect r, bool selected, bool over) {
    if (selected) {
        c.fill(r, kSel);
        c.fill({r.left, r.top, r.left + scale(2), r.bottom}, kAccent);
    } else if (over) {
        c.fill(r, kRowHot);
    }
}

void ProjectPageView::paintGrip(draw::Canvas& c, int x, int y, bool on) {
    for (int gx = 0; gx < 2; ++gx)
        for (int gy = 0; gy < 3; ++gy) {
            const int dx = x + gx * scale(5), dy = y + gy * scale(5);
            c.fill({dx, dy, dx + scale(2), dy + scale(2)}, on ? kAccent : kGripDot);
        }
}

void ProjectPageView::paintRowActions(draw::Canvas& c, draw::Rect row, const std::vector<RowAct>& acts, bool show) {
    if (!show && !hot(row)) return;
    const int w = scale(stardraft::kit::kIconBox), h = std::min(row.height(), scale(stardraft::kit::kIconBox));
    int x = row.right - scale(4) - w * int(acts.size());
    const int y = (row.top + row.bottom - h) / 2;
    for (const RowAct& a : acts) {
        const draw::Rect r = squareOf({x, y, x + w, y + h});
        const bool over = a.enabled && hot(r);
        if (over) c.roundBox(r, kCtlHot, draw::kNone, float(scale(stardraft::kit::kRadius)));
        c.icon(r, int(a.icon), !a.enabled ? kFaint : over ? (a.danger ? kBad : kText) : kDim, scale(16));
        hits_.push_back({r, a.enabled ? a.click : nullptr, a.tip, a.enabled ? draw::Cursor::Hand : draw::Cursor::Arrow});
        x += w;
    }
}

int ProjectPageView::smallButton(draw::Canvas& c, int x, int y, int h, const std::wstring& text, std::function<void()> click,
                                 const std::wstring& tip, bool enabled) {
    const int bh = std::min(h, scale(24)), w = c.measure(text) + scale(20);
    const draw::Rect r{x, y + (h - bh) / 2, x + w, y + (h - bh) / 2 + bh};
    if (enabled) {
        drawButton(c, r, text, hot(r));
    } else {  // a button that cannot be pressed now: its ground and its text are faint
        c.roundBox(r, kBg, kBorder, float(scale(stardraft::kit::kRadius)));
        c.text(r, text, kFaint, draw::kMiddle | draw::kCenter | draw::kEllipsis);
    }
    hits_.push_back({r, enabled ? std::move(click) : nullptr, tip, enabled ? draw::Cursor::Hand : draw::Cursor::Arrow});
    return r.right;
}

void ProjectPageView::startDrag(int from, draw::Point pt) {
    drag_ = {true, from, from};
    if (host()) host()->capture(*this, true);
    dragTo(pt);
    invalidate();
}

void ProjectPageView::dragTo(draw::Point pt) {
    int to = int(scriptRows_.size());
    for (size_t i = 0; i < scriptRows_.size(); ++i)
        if (pt.y < (scriptRows_[i].top + scriptRows_[i].bottom) / 2) {
            to = int(i);
            break;
        }
    if (to != drag_.to) drag_.to = to, invalidate();
}

void ProjectPageView::endDrag() {
    const Drag d = drag_;
    drag_ = {};
    if (host()) host()->capture(*this, false);
    invalidate();
    if (d.from < 0 || d.to == d.from || d.to == d.from + 1) return;
    if (size_t(d.from) >= scriptNames_.size()) return;
    const int to = d.to > d.from ? d.to - 1 : d.to;
    const std::string moved = scriptNames_[size_t(d.from)];  // the same script for each step
    for (int k = d.from; k < to; ++k) host_.moveScript(moved, 1);
    for (int k = d.from; k > to; --k) host_.moveScript(moved, -1);
}

void ProjectPageView::paintCheck(draw::Canvas& c, draw::Rect r, const std::wstring& label, bool on, const std::wstring& tip,
                                 std::function<void()> click) {
    const int s = scale(14);
    const int top = (r.top + r.bottom - s) / 2;
    const draw::Rect b{r.left, top, r.left + s, top + s};
    const bool over = hot(r);
    // The box: accent when on, its border lit under the mouse.
    c.roundBox(b, on ? kAccentFill : kCtl, on ? kAccent : over ? kBorderHot : kBorderStrong, float(scale(3)));
    if (on) {
        const int w = b.width(), h = b.height();
        const draw::Point mark[3] = {
            {b.left + w * 22 / 100, b.top + h * 52 / 100}, {b.left + w * 42 / 100, b.top + h * 72 / 100}, {b.left + w * 78 / 100, b.top + h * 30 / 100}};
        c.polyline(mark, 3, kOnText, std::max(1, scale(2)));
    }
    c.text({b.right + scale(8), r.top, r.right, r.bottom}, label, on || over ? kText : kDim, draw::kMiddle | draw::kEllipsis);
    hits_.push_back({r, std::move(click), tip, draw::Cursor::Hand});
}

bool ProjectPageView::mouse(const draw::MouseEvent& e) {
    switch (e.kind) {
    case draw::MouseKind::Move:
    case draw::MouseKind::Leave: {  // a part under the mouse is painted again
        if (drag_.active) {  // a drag: the row goes where the mouse is
            if (e.kind == draw::MouseKind::Move) dragTo(e.pt);
            return true;
        }
        const int hv = e.kind == draw::MouseKind::Leave ? -1 : hitAt(e.pt);
        if (hv != hover_) hover_ = hv, invalidate();
        return false;
    }
    case draw::MouseKind::Down:
        if (e.button != 0) return false;
        focus();  // the press takes the keyboard focus (the old page was a window that took it)
        if (const int hi = clickAt(e.pt); hi >= 0 && hits_[size_t(hi)].grip >= 0) {
            startDrag(hits_[size_t(hi)].grip, e.pt);
            return true;
        }
        if (const int hi = clickAt(e.pt); hi >= 0 && hits_[size_t(hi)].click) {
            const std::function<void()> run = hits_[size_t(hi)].click;  // a copy: the click may repaint and clear hits_
            run();
            return true;
        }
        return false;
    case draw::MouseKind::Up:
        if (drag_.active && e.button == 0) {
            endDrag();
            return true;
        }
        return false;
    case draw::MouseKind::DoubleClick:
        host_.openSelectedScript();
        return true;
    case draw::MouseKind::Wheel:
        scrollTo(scroll_ - int(e.wheel * float(scale(32) * 3)));
        return true;
    default: return false;
    }
}

bool ProjectPageView::vscroll(int code, int pos, int track, int) {
    const int page = height();
    switch (code) {
    case 0: scrollTo(scroll_ - scale(32)); break;  // SB_LINEUP
    case 1: scrollTo(scroll_ + scale(32)); break;  // SB_LINEDOWN
    case 2: scrollTo(scroll_ - page); break;       // SB_PAGEUP
    case 3: scrollTo(scroll_ + page); break;       // SB_PAGEDOWN
    case 4: scrollTo(pos); break;                  // SB_THUMBPOSITION
    case 5: scrollTo(track); break;                // SB_THUMBTRACK
    case 6: scrollTo(0); break;                    // SB_TOP
    case 7: scrollTo(contentH_); break;            // SB_BOTTOM
    default: return false;
    }
    return true;
}

draw::Cursor ProjectPageView::cursor(draw::Point pt) {
    const int hi = hitAt(pt);
    return hi >= 0 ? hits_[size_t(hi)].cursor : draw::Cursor::Arrow;
}

std::wstring ProjectPageView::tip(draw::Point pt) {
    const int hi = tipAt(pt);
    return hi >= 0 ? hits_[size_t(hi)].tip : std::wstring();
}

// The project page's helpers that need no window (moved from project_view.inc).
// Sections the page sets up elsewhere (Build options) or Build writes (keys and mouse, chat commands): not scripts.
bool projectManagedSection(const std::string& name) {
    for (const char* n : {"eudTurbo", "unlimiter", "noAirCollision", "bgmplayer", "dataDumper", "MSQC", "chatEvent"})
        if (name == n) return true;
    return false;
}

int projectPreviewWidth(int pageW) { return gDock.scriptPreview ? std::min(pageW / 2, std::max(scale(420), pageW * 2 / 5)) : 0; }

std::string sectionValue(const EudProject& pr, const std::string& section, const std::string& key) {
    for (const auto& l : pr.sectionLines(section)) {
        const size_t c = l.find(':');
        if (c == std::string::npos) continue;
        std::string k = l.substr(0, c), v = l.substr(c + 1);
        auto trim = [](std::string& s) {
            s.erase(0, s.find_first_not_of(" \t"));
            s.erase(s.find_last_not_of(" \t") + 1);
        };
        trim(k), trim(v);
        if (k == key) return v;
    }
    return {};
}

// The lines of a project's generated/triggers.eps now, without their indent.
std::vector<std::string> epTraceTriggerCode(const fs::path& folder) {
    std::vector<std::string> out;
    std::ifstream f(folder / "generated" / "triggers.eps");
    for (std::string s; std::getline(f, s);) {
        if (!s.empty() && s.back() == '\r') s.pop_back();
        s.erase(0, s.find_first_not_of(" \t"));
        out.push_back(s);
    }
    return out;
}
