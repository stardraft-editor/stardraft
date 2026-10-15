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
};

// The background music (bgmplayer) and files in memory (dataDumper) are off for now: not shown. A project that has them
// keeps its sections, and Build uses them.
constexpr bool kProjectMusicAndFiles = false;

// The Project page: the project's files, its build options and its scripts. A view in a view window: it scrolls, and it
// takes the clicks of its own parts. The sections after Files are the program's code for now (shellPaintProjectRest,
// shell.h), which draws and clicks them on the same canvas.
class ProjectPageView final : public draw::View {
public:
    explicit ProjectPageView(ProjectHost& host) : host_(host) {}

    // The scroll position (the page's height is the one the last paint found).
    void scrollTo(int s) {
        const int s2 = std::clamp(s, 0, std::max(0, contentH_ - height()));
        if (s2 != scroll_) scroll_ = s2, invalidate();
    }

    void paint(draw::Canvas& c) override;
    bool mouse(const draw::MouseEvent& e) override;
    bool vscroll(int code, int pos, int track, int) override;
    bool windowScrollBar(int& range, int& page, int& pos) override {
        range = std::max(0, contentH_ - 1), page = height(), pos = scroll_;
        return true;
    }
    void resized() override { host_.pageResized(); }
    bool hoverTip() const override { return true; }
    draw::Cursor cursor(draw::Point pt) override;
    std::wstring tip(draw::Point pt) override;
    // Report feedback names the part under the mouse by its tip; elsewhere on the page, the page.
    std::wstring feedbackName(draw::Point pt) override {
        const int hi = hitAt(pt);
        if (hi >= 0 && !hits_[size_t(hi)].tip.empty()) return hits_[size_t(hi)].tip;
        const std::wstring t = shellProjectRestTip(pt);
        return t.empty() ? std::wstring(L"the Project page") : t;
    }

private:
    struct Hit {
        draw::Rect r;
        std::function<void()> click;
        std::wstring tip;
        draw::Cursor cursor = draw::Cursor::Arrow;
    };
    struct Button {
        std::wstring text, tip;
        std::function<void()> click;
    };
    int hitAt(draw::Point pt) const {
        for (size_t i = hits_.size(); i-- > 0;)
            if (hits_[i].r.contains(pt)) return int(i);
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

    ProjectHost& host_;
    std::vector<Hit> hits_;  // what the last paint put under the mouse
    int hover_ = -1;         // the hit under the mouse at the last move
    int scroll_ = 0, contentH_ = 0;
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
        y = shellPaintProjectRest(c, y, pageW, h);
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
        const int hv = e.kind == draw::MouseKind::Leave ? -1 : hitAt(e.pt);
        if (hv != hover_) hover_ = hv, invalidate();
        return shellProjectRestMouse(e);
    }
    case draw::MouseKind::Down:
        if (e.button != 0) return false;
        if (const int hi = hitAt(e.pt); hi >= 0 && hits_[size_t(hi)].click) {
            const std::function<void()> run = hits_[size_t(hi)].click;  // a copy: the click may repaint and clear hits_
            run();
            return true;
        }
        return shellProjectRestMouse(e);
    case draw::MouseKind::Up:
    case draw::MouseKind::DoubleClick: return shellProjectRestMouse(e);
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
    return hi >= 0 ? hits_[size_t(hi)].cursor : shellProjectRestCursor(pt);
}

std::wstring ProjectPageView::tip(draw::Point pt) {
    const int hi = hitAt(pt);
    return hi >= 0 ? hits_[size_t(hi)].tip : shellProjectRestTip(pt);
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
