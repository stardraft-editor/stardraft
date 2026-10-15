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
