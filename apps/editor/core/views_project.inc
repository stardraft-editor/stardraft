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
};

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
        y = shellPaintProjectRest(c, y, pageW, h);
    }
    contentH_ = y + scroll_;
    if (const int most = std::max(0, contentH_ - h); scroll_ > most) scroll_ = most, invalidate();  // a taller window: no scroll is left
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
