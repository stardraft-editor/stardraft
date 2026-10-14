// --- Game text: one editor for the texts that the game colors ----------------------------------------------------
// The map's title and description, mission briefings, the game's strings (Data > Strings), classic trigger texts and
// the unit names hold the game's control codes: a color for the text after it, align right (12) and center (13) for
// its line, hidden (0B, 14: briefings). The editor shows them as the game does: Preview, the text in its colors and
// its lines aligned (a swatch colors the selected text, or the text typed next; an align button aligns the selected
// lines); Raw, the text with each code as <XX> (a "<" that could read as a code is "<<"). A text that is not changed
// reads back as its own bytes.
// The game has two sets of text colors: the in-game one (messages, unit names: the long list with the players'
// colors) and the one of mission briefings and the scenario's title and description (pale blue, green, light green,
// gray, white, red).

enum class TextPalette { Game, Scenario };

extern COLORREF kRichDefault, kRichBack;
std::wstring richText(HWND re);

// The game's text colors (as StarCraft: Remastered draws them; codes 01 and 02: the text's own color, pale blue).
COLORREF gameTextColor(unsigned char c, COLORREF normal) {
    switch (c) {
    case 1: return normal;                 // use default
    case 2: return RGB(184, 184, 232);     // pale blue
    case 3: return RGB(220, 220, 60);      // yellow
    case 4: return RGB(255, 255, 255);     // white
    case 5: return RGB(132, 116, 116);     // grey
    case 6: return RGB(200, 24, 24);       // red
    case 7: return RGB(16, 252, 24);       // green
    case 8: return RGB(244, 4, 4);         // red (player 1)
    case 0x0E: return RGB(12, 72, 204);    // blue (player 2)
    case 0x0F: return RGB(44, 180, 148);   // teal (player 3)
    case 0x10: return RGB(136, 64, 156);   // purple (player 4)
    case 0x11: return RGB(248, 140, 20);   // orange (player 5)
    case 0x15: return RGB(112, 48, 20);    // brown (player 6)
    case 0x16: return RGB(204, 224, 208);  // white (player 7)
    case 0x17: return RGB(252, 252, 56);   // yellow (player 8)
    case 0x18: return RGB(8, 128, 8);      // green (player 9)
    case 0x19: return RGB(252, 252, 124);  // brighter yellow (player 10)
    case 0x1A: return RGB(0, 0, 24);       // "cyan" (the game draws it almost black)
    case 0x1B: return RGB(236, 196, 176);  // pinkish (player 11)
    case 0x1C: return RGB(64, 104, 212);   // dark cyan (player 12)
    case 0x1D: return RGB(116, 164, 124);  // greygreen
    case 0x1E: return RGB(144, 144, 184);  // bluegrey
    case 0x1F: return RGB(0, 228, 252);    // turquoise
    }
    return 0xFFFFFFFF;
}
// Mission briefings color text in their own way (the same codes, other colors): 01 the text's own color, 00 02 1A
// pale blue, 03 green, 04 light green, 05 gray, 06 white, 07 red; 0B and 14 make the text after them invisible.
constexpr COLORREF kInvisibleText = 0xFFFFFFFE;
COLORREF briefingTextColor(unsigned char c, COLORREF normal) {
    switch (c) {
    case 1: return normal;
    case 0: case 2: case 0x1A: return RGB(184, 184, 232);  // pale blue
    case 3: return RGB(16, 216, 24);                       // green
    case 4: return RGB(160, 236, 160);                     // light green
    case 5: return RGB(132, 132, 132);                     // gray
    case 6: return RGB(255, 255, 255);                     // white
    case 7: return RGB(220, 32, 32);                       // red
    case 0x0B: case 0x14: return kInvisibleText;
    }
    return 0xFFFFFFFF;
}

// The palette's swatches, in their order (code 1: the normal color).
struct TextSwatch {
    unsigned char code;
    const wchar_t* name;
};
const std::vector<TextSwatch>& textSwatches(TextPalette p) {
    static const std::vector<TextSwatch> game = {
        {1, L"Normal"}, {3, L"Yellow"}, {4, L"White"}, {5, L"Gray"}, {6, L"Red"}, {7, L"Green"}, {8, L"Red (Player 1)"}, {0x0E, L"Blue (Player 2)"},
        {0x0F, L"Teal (Player 3)"}, {0x10, L"Purple (Player 4)"}, {0x11, L"Orange (Player 5)"}, {0x15, L"Brown (Player 6)"}, {0x16, L"White (Player 7)"},
        {0x17, L"Yellow (Player 8)"}, {0x18, L"Green (Player 9)"}, {0x19, L"Bright yellow (Player 10)"}, {0x1B, L"Pink (Player 11)"},
        {0x1C, L"Dark cyan (Player 12)"}, {0x1D, L"Gray-green"}, {0x1E, L"Blue-gray"}, {0x1F, L"Turquoise"}};
    static const std::vector<TextSwatch> scenario = {{1, L"Normal"}, {2, L"Pale blue"}, {3, L"Green"}, {4, L"Light green"}, {5, L"Gray"},
                                                     {6, L"White"}, {7, L"Red"}, {0x0B, L"Hidden (the game does not show this text)"}};
    return p == TextPalette::Game ? game : scenario;
}

// How a code's text looks in the box: its color; hidden text crossed out; a code that is not a color of the palette
// (kept as it is) underlined with dots.
struct TextLook {
    COLORREF color;
    bool hidden = false, unknown = false;
};
TextLook textCodeLook(TextPalette p, unsigned char code) {
    if (code <= 2) return {kRichDefault};  // (none, 01 normal, 02 pale blue: the box's normal color)
    if (p == TextPalette::Game) {
        if (code == 0x1A) return {RGB(64, 64, 110)};  // almost black in the game: dark, but readable here
        const COLORREF c = gameTextColor(code, kRichDefault);
        return c != 0xFFFFFFFF ? TextLook{c} : TextLook{kRichDefault, false, true};
    }
    const COLORREF c = briefingTextColor(code, kRichDefault);
    if (c == kInvisibleText) return {RGB(112, 112, 122), true};
    return c != 0xFFFFFFFF ? TextLook{c} : TextLook{kRichDefault, false, true};
}

// A run's code is in its format (in the kerning threshold, which changes nothing at these sizes): the text keeps its
// own code (02 and 01 look the same, and stay apart).
constexpr int kTextCodeKern = 10000;
CHARFORMAT2W textCodeFormat(TextPalette p, unsigned char code) {
    CHARFORMAT2W f{};
    f.cbSize = sizeof f;
    f.dwMask = CFM_COLOR | CFM_KERNING | CFM_STRIKEOUT | CFM_UNDERLINE | CFM_UNDERLINETYPE | CFM_BOLD | CFM_ITALIC;
    const TextLook look = textCodeLook(p, code);
    f.crTextColor = look.color;
    f.wKerning = WORD(code ? kTextCodeKern + code : 0);
    f.dwEffects = (look.hidden ? CFE_STRIKEOUT : 0) | (look.unknown ? CFE_UNDERLINE : 0);
    f.bUnderlineType = look.unknown ? CFU_UNDERLINEDOTTED : CFU_UNDERLINENONE;
    return f;
}
// The code of a run in the box (0: the normal color). Text from elsewhere (no code in its format): the palette's code
// of its color.
unsigned char textRunCode(ITextFont* font, TextPalette p) {
    float kern = 0;
    long fore = long(kRichDefault);
    font->GetKerning(&kern), font->GetForeColor(&fore);
    const int k = int(std::lround(kern * 20)) - kTextCodeKern;  // (points, as twips)
    if (k > 0 && k < 0x20) return (unsigned char)k;
    const COLORREF c = fore == tomAutoColor ? kRichDefault : COLORREF(fore);
    if (c == kRichDefault) return 0;
    for (const TextSwatch& s : textSwatches(p))
        if (s.code > 2 && textCodeLook(p, s.code).color == c) return s.code;
    return 0;
}

// --- Raw: the bytes with each code as <XX> ------------------------------------------------------------------------

// "<XX>" at v[i]: two hex digits for a code 01..1F.
int gameTextTagAt(const std::wstring& v, size_t i) {
    if (i + 3 >= v.size() || v[i] != L'<' || v[i + 3] != L'>' || !iswxdigit(v[i + 1]) || !iswxdigit(v[i + 2])) return -1;
    const int c = int(std::wcstoul(v.substr(i + 1, 2).c_str(), nullptr, 16));
    return c >= 1 && c < 0x20 ? c : -1;
}
// Bytes as text (UTF-8, else Windows-1252 for the whole text, as widen does).
std::wstring gameTextDecode(const std::string& s, bool utf8) {
    if (s.empty()) return {};
    const UINT cp = utf8 ? CP_UTF8 : 1252;
    const int n = MultiByteToWideChar(cp, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(std::max(0, n)), L'\0');
    if (n > 0) MultiByteToWideChar(cp, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}
bool gameTextIsUtf8(const std::string& s) { return s.empty() || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0) > 0; }

std::wstring gameTextToRaw(const std::string& bytes, bool multiline) {
    const bool utf8 = gameTextIsUtf8(bytes);
    // The text with each code as a place holder (a NUL, its code beside), then the tags; a "<" that could read as a
    // tag, or that is followed by "<", is "<<".
    std::wstring text;
    std::map<size_t, unsigned char> codes;
    std::string run;
    auto flush = [&] { text += gameTextDecode(run, utf8), run.clear(); };
    for (size_t i = 0; i < bytes.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(bytes[i]);
        if (c == '\r' && i + 1 < bytes.size() && bytes[i + 1] == '\n') continue;  // (the line end is the box's)
        if (c == '\n' && multiline) {
            flush(), text += L'\n';
            continue;
        }
        if (c < 0x20) {
            flush(), codes[text.size()] = c, text += L'\0';
            continue;
        }
        run += char(c);
    }
    flush();
    std::wstring out;
    for (size_t i = 0; i < text.size(); ++i) {
        const wchar_t ch = text[i];
        if (const auto it = codes.find(i); it != codes.end()) {
            wchar_t tag[8];
            std::swprintf(tag, 8, L"<%02X>", unsigned(it->second));
            out += tag;
        } else if (ch == L'\n') {
            out += L"\r\n";
        } else if (ch == L'<' && ((i + 1 < text.size() && text[i + 1] == L'<') || codes.count(i + 1) || gameTextTagAt(text, i) > 0)) {
            out += L"<<";
        } else {
            out += ch;
        }
    }
    return out;
}
std::string gameTextFromRaw(const std::wstring& v, bool crlf) {
    std::string out;
    std::wstring run;
    auto flush = [&] { out += narrow(run), run.clear(); };
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] == L'\r') continue;
        if (v[i] == L'\n') {
            flush(), out += crlf ? "\r\n" : "\n";
            continue;
        }
        if (v[i] == L'<' && i + 1 < v.size() && v[i + 1] == L'<') {  // "<<": a "<"
            run += L'<', ++i;
            continue;
        }
        if (const int c = gameTextTagAt(v, i); c > 0) {
            flush(), out += char(c), i += 3;
            continue;
        }
        run += v[i];
    }
    flush();
    return out;
}

// --- The editor ---------------------------------------------------------------------------------------------------

struct GameTextSpec {
    TextPalette palette = TextPalette::Game;
    bool multiline = true;
    bool align = true;   // the align buttons (multi-line texts)
    bool crlf = false;   // lines end in \r\n (map texts); else as the text had them (\n for a new text)
    std::wstring cue;    // shown in an empty box
};
struct GameTextBox {
    HWND wnd = nullptr, rich = nullptr, raw = nullptr;
    int id = 0;
    GameTextSpec spec;
    bool rawMode = false, crlf = false, setting = false;
    bool changed = false;  // the page's value differs from the game's (the frame in the changed color)
    std::string base, baseRead;  // what was put in, and how it read back then (unchanged: the bytes that were put in)
    ITextDocument* doc = nullptr;
    ui::Surface surface;
    RECT frame{};
};
std::map<HWND, std::unique_ptr<GameTextBox>> gGameTexts;
GameTextBox* gameTextOf(HWND w) {
    const auto it = gGameTexts.find(w);
    return it == gGameTexts.end() ? nullptr : it->second.get();
}
GameTextBox* gameTextOfChild(HWND child) {
    for (auto& [w, g] : gGameTexts)
        if (g->rich == child || g->raw == child) return g.get();
    return nullptr;
}

ITextDocument* gameTextDoc(GameTextBox& g) {
    if (!g.doc) {
        IRichEditOle* ole = nullptr;
        static const GUID iidTextDocument = {0x8CC497C0, 0xA1DF, 0x11CE, {0x80, 0x98, 0x00, 0xAA, 0x00, 0x47, 0xBE, 0x5D}};
        if (SendMessageW(g.rich, EM_GETOLEINTERFACE, 0, LPARAM(&ole)) && ole) ole->QueryInterface(iidTextDocument, reinterpret_cast<void**>(&g.doc)), ole->Release();
    }
    return g.doc;
}

// Bytes into the Preview box: the runs in their codes' looks, the lines aligned.
void gameTextFill(GameTextBox& g, const std::string& bytes) {
    const bool utf8 = gameTextIsUtf8(bytes);
    struct Run {
        unsigned char code;
        LONG from, to;
    };
    std::vector<Run> runs;
    std::vector<std::pair<LONG, int>> aligned;  // a line's start and its alignment (1 center, 2 right)
    std::wstring text;
    unsigned char code = 0;
    LONG runFrom = 0;
    std::string piece;
    auto flush = [&] {
        text += gameTextDecode(piece, utf8), piece.clear();
    };
    auto cut = [&](unsigned char next) {  // a new code: the run so far ends
        flush();
        if (LONG(text.size()) > runFrom) runs.push_back({code, runFrom, LONG(text.size())});
        runFrom = LONG(text.size()), code = next;
    };
    size_t i = 0;
    while (i <= bytes.size()) {
        size_t end = bytes.find('\n', i);
        if (end == std::string::npos) end = bytes.size();
        size_t lineEnd = end;
        if (lineEnd > i && bytes[lineEnd - 1] == '\r') --lineEnd;
        int align = 0;
        flush();
        const LONG lineStart = LONG(text.size());
        for (size_t k = i; k < lineEnd; ++k) {
            const unsigned char c = static_cast<unsigned char>(bytes[k]);
            if (c == 0x12 || c == 0x13) align = c == 0x13 ? 1 : 2;  // the line's alignment (the last one counts)
            else if (c == '\r' || c == 0) continue;
            else if (c < 0x20 && c != '\t') cut(c);
            else piece += char(c);
        }
        flush();
        if (align) aligned.push_back({lineStart, align});
        if (end >= bytes.size()) break;
        text += L'\r';
        i = end + 1;
    }
    const unsigned char trailing = code;  // the code in force at the end (a text that is only a code: what is typed has it)
    cut(0);
    g.setting = true;
    SendMessageW(g.rich, WM_SETREDRAW, FALSE, 0);
    SetWindowTextW(g.rich, L"");
    CHARRANGE all{0, -1};
    SendMessageW(g.rich, EM_EXSETSEL, 0, LPARAM(&all));
    CHARFORMAT2W normal = textCodeFormat(g.spec.palette, 0);
    SendMessageW(g.rich, EM_SETCHARFORMAT, SCF_ALL, LPARAM(&normal));
    SendMessageW(g.rich, EM_REPLACESEL, FALSE, LPARAM(text.c_str()));
    for (const Run& r : runs) {
        CHARRANGE cr{r.from, r.to};
        SendMessageW(g.rich, EM_EXSETSEL, 0, LPARAM(&cr));
        CHARFORMAT2W f = textCodeFormat(g.spec.palette, r.code);
        SendMessageW(g.rich, EM_SETCHARFORMAT, SCF_SELECTION, LPARAM(&f));
    }
    for (const auto& [at, align] : aligned) {
        CHARRANGE cr{at, at};
        SendMessageW(g.rich, EM_EXSETSEL, 0, LPARAM(&cr));
        PARAFORMAT2 pf{};
        pf.cbSize = sizeof pf, pf.dwMask = PFM_ALIGNMENT, pf.wAlignment = WORD(align == 1 ? PFA_CENTER : PFA_RIGHT);
        SendMessageW(g.rich, EM_SETPARAFORMAT, 0, LPARAM(&pf));
    }
    CHARRANGE start{0, 0};
    SendMessageW(g.rich, EM_EXSETSEL, 0, LPARAM(&start));
    // Typing at the start: the first run's look (else the normal one)
    CHARFORMAT2W first = textCodeFormat(g.spec.palette, text.empty() ? trailing : runs.empty() || runs[0].from > 0 ? 0 : runs[0].code);
    SendMessageW(g.rich, EM_SETCHARFORMAT, SCF_SELECTION, LPARAM(&first));
    SendMessageW(g.rich, EM_EMPTYUNDOBUFFER, 0, 0);
    SendMessageW(g.rich, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g.rich, nullptr, TRUE);
    g.setting = false;
}

// The Preview box as bytes: each run's code where it changes, each aligned line's code at its start.
std::string gameTextRead(GameTextBox& g) {
    const std::wstring text = richText(g.rich);  // (a line end is one \r, as the box counts)
    const LONG n = LONG(text.size());
    std::string out;
    ITextDocument* doc = gameTextDoc(g);
    if (!doc) return narrow(text);
    unsigned char cur = 0;  // the code in force (0: none yet)
    bool lineStart = true;
    std::wstring words;
    auto flush = [&] { out += narrow(words), words.clear(); };
    ITextRange* r = nullptr;
    if (FAILED(doc->Range(0, 0, &r)) || !r) return narrow(text);
    for (long start = 0; start < n;) {  // run by run (a run has one format)
        long moved = 0, end = 0;
        r->SetRange(start, start);
        if (FAILED(r->MoveEnd(tomCharFormat, 1, &moved)) || !moved) break;
        r->GetEnd(&end);
        end = std::min<long>(end, n);
        if (end <= start) break;
        unsigned char want = 0;
        ITextFont* font = nullptr;
        if (SUCCEEDED(r->GetFont(&font)) && font) want = textRunCode(font, g.spec.palette), font->Release();
        for (long i = start; i < end; ++i) {
            const wchar_t ch = text[size_t(i)];
            if (ch == L'\r' || ch == L'\v') {  // (\v: Shift+Enter's line break, not code 0B)
                flush(), out += g.crlf ? "\r\n" : "\n", lineStart = true;
                continue;
            }
            if (lineStart) {  // the line's alignment
                lineStart = false;
                ITextRange* lr = nullptr;
                if (SUCCEEDED(doc->Range(i, i, &lr)) && lr) {
                    ITextPara* para = nullptr;
                    long a = tomAlignLeft;
                    if (SUCCEEDED(lr->GetPara(&para)) && para) para->GetAlignment(&a), para->Release();
                    lr->Release();
                    if (a == tomAlignCenter || a == tomAlignRight) flush(), out += char(a == tomAlignCenter ? 0x13 : 0x12);
                }
            }
            // The run's code, before its first character
            const bool normal = want == 0;
            if (normal ? cur > 1 : want != cur) {
                flush();
                out += char(normal ? 1 : want);
                cur = normal ? 1 : want;
            }
            words += ch;
        }
        start = end;
    }
    r->Release();
    flush();
    return out;
}

std::string gameTextGet(HWND w) {
    GameTextBox* g = gameTextOf(w);
    if (!g) return {};
    const std::string now = g->rawMode ? gameTextFromRaw(windowText(g->raw), g->crlf) : gameTextRead(*g);
    return now == g->baseRead ? g->base : now;
}
void gameTextSet(HWND w, const std::string& bytes) {
    GameTextBox* g = gameTextOf(w);
    if (!g) return;
    g->crlf = g->spec.crlf || bytes.find("\r\n") != std::string::npos;
    g->setting = true;
    if (g->rawMode) SetWindowTextW(g->raw, gameTextToRaw(bytes, g->spec.multiline).c_str());
    else gameTextFill(*g, bytes);
    g->setting = false;
    g->base = bytes;
    g->baseRead = g->rawMode ? gameTextFromRaw(windowText(g->raw), g->crlf) : gameTextRead(*g);
    InvalidateRect(g->wnd, nullptr, FALSE);
}
HWND gameTextFocusBox(HWND w) {
    GameTextBox* g = gameTextOf(w);
    return g ? (g->rawMode ? g->raw : g->rich) : nullptr;
}
bool gameTextHasFocus(HWND w) {
    GameTextBox* g = gameTextOf(w);
    return g && (GetFocus() == g->rich || GetFocus() == g->raw);
}

void gameTextNotify(GameTextBox& g, WORD code) {
    if (!g.setting) SendMessageW(GetParent(g.wnd), WM_COMMAND, MAKEWPARAM(g.id, code), LPARAM(g.wnd));
}

// Preview or Raw: the text goes over to the other box (unchanged, it stays the bytes it was).
void gameTextSetRaw(GameTextBox& g, bool raw) {
    if (raw == g.rawMode) return;
    const std::string bytes = gameTextGet(g.wnd);
    const bool focused = GetFocus() == g.rich || GetFocus() == g.raw;
    g.rawMode = raw;
    gameTextSet(g.wnd, bytes);
    ShowWindow(raw ? g.rich : g.raw, SW_HIDE), ShowWindow(raw ? g.raw : g.rich, SW_SHOW);
    if (focused && !gBackground) SetFocus(raw ? g.raw : g.rich);
    InvalidateRect(g.wnd, nullptr, FALSE);
}

// The code at the caret (the swatch with a ring): the selection's first character, else the character before it.
unsigned char gameTextCaretCode(GameTextBox& g) {
    CHARFORMAT2W f{};
    f.cbSize = sizeof f;
    SendMessageW(g.rich, EM_GETCHARFORMAT, SCF_SELECTION, LPARAM(&f));
    const int k = (f.dwMask & CFM_KERNING) ? int(f.wKerning) - kTextCodeKern : -1;
    if (k > 0 && k < 0x20) return (unsigned char)k;
    if (!(f.dwMask & CFM_COLOR)) return 0xFF;  // (several)
    if (f.crTextColor == kRichDefault) return 1;
    for (const TextSwatch& s : textSwatches(g.spec.palette))
        if (s.code > 2 && textCodeLook(g.spec.palette, s.code).color == f.crTextColor) return s.code;
    return 1;
}
int gameTextCaretAlign(GameTextBox& g) {
    PARAFORMAT2 pf{};
    pf.cbSize = sizeof pf;
    SendMessageW(g.rich, EM_GETPARAFORMAT, 0, LPARAM(&pf));
    if (!(pf.dwMask & PFM_ALIGNMENT)) return -1;
    return pf.wAlignment == PFA_CENTER ? 1 : pf.wAlignment == PFA_RIGHT ? 2 : 0;
}

// A swatch: the selected text (or the text typed next) takes the code. In Raw: the code's tag at the caret.
// Raw: a code in at the selection (or at the start of the caret's line), through the bytes, so that the text around
// it keeps its meaning (a "<" before it stays a "<").
void gameTextRawInsert(GameTextBox& g, unsigned char code, bool lineStart) {
    const std::wstring v = windowText(g.raw);
    DWORD from = 0, to = 0;
    SendMessageW(g.raw, EM_GETSEL, WPARAM(&from), LPARAM(&to));
    if (lineStart) from = to = DWORD(SendMessageW(g.raw, EM_LINEINDEX, SendMessageW(g.raw, EM_LINEFROMCHAR, from, 0), 0));
    from = std::min<DWORD>(from, DWORD(v.size())), to = std::clamp<DWORD>(to, from, DWORD(v.size()));
    const std::string before = gameTextFromRaw(v.substr(0, from), g.crlf) + char(code), after = gameTextFromRaw(v.substr(to), g.crlf);
    const std::wstring now = gameTextToRaw(before + after, g.spec.multiline);
    const DWORD caret = DWORD(gameTextToRaw(before, g.spec.multiline).size());  // (the tag's text does not depend on what follows it)
    SetWindowTextW(g.raw, now.c_str());
    SendMessageW(g.raw, EM_SETSEL, caret, caret);
    if (!gBackground) SetFocus(g.raw);
    gameTextNotify(g, EN_CHANGE);
}
void gameTextApply(GameTextBox& g, unsigned char code) {
    if (g.rawMode) return gameTextRawInsert(g, code, false);
    CHARFORMAT2W f = textCodeFormat(g.spec.palette, code == 1 ? 0 : code);
    SendMessageW(g.rich, EM_SETCHARFORMAT, SCF_SELECTION, LPARAM(&f));
    if (!gBackground) SetFocus(g.rich);
    gameTextNotify(g, EN_CHANGE);  // (a format sends no EN_CHANGE)
    InvalidateRect(g.wnd, nullptr, FALSE);
}
// An align button: the selected lines (the caret's line) aligned (0 left, 1 center, 2 right). In Raw: the code's tag
// at the start of the caret's line.
void gameTextAlign(GameTextBox& g, int align) {
    if (g.rawMode) {
        if (align) gameTextRawInsert(g, align == 1 ? 0x13 : 0x12, true);
        return;
    }
    PARAFORMAT2 pf{};
    pf.cbSize = sizeof pf, pf.dwMask = PFM_ALIGNMENT, pf.wAlignment = WORD(align == 1 ? PFA_CENTER : align == 2 ? PFA_RIGHT : PFA_LEFT);
    SendMessageW(g.rich, EM_SETPARAFORMAT, 0, LPARAM(&pf));
    if (!gBackground) SetFocus(g.rich);
    gameTextNotify(g, EN_CHANGE);
    InvalidateRect(g.wnd, nullptr, FALSE);
}

// The tool row's height for a width (the swatches go on as many rows as they need).
constexpr int kGameTextSwatch = 16, kGameTextGap = 4, kGameTextRowH = 24;
int gameTextToolsHeight(GameTextBox& g, int width, bool layoutOnly = true);
struct GameTextTools {
    std::vector<std::pair<RECT, unsigned char>> swatches;
    RECT align[3]{}, seg[2]{};
    int height = 0;
};
GameTextTools gameTextLayout(GameTextBox& g, HDC dc, int top, int width) {
    GameTextTools t;
    const int rowH = scale(kGameTextRowH), s = scale(kGameTextSwatch), gap = scale(kGameTextGap);
    // Right: Preview | Raw (and the align buttons before it)
    int right = width;
    const wchar_t* names[2] = {L"Preview", L"Raw"};
    int segW[2];
    for (int k = 0; k < 2; ++k) segW[k] = ui::textWidth(dc, names[k]) + scale(18);
    const int segH = scale(20), segY = top + (rowH - segH) / 2;
    int x = right - segW[0] - segW[1];
    t.seg[0] = {x, segY, x + segW[0], segY + segH}, t.seg[1] = {x + segW[0], segY, right, segY + segH};
    right = x - scale(10);
    if (g.spec.align && g.spec.multiline) {
        const int b = scale(22);
        for (int k = 2; k >= 0; --k) t.align[k] = {right - b, top + (rowH - b) / 2, right, top + (rowH - b) / 2 + b}, right -= b;
        right -= scale(10);
    }
    // Left: the swatches, row by row
    x = 0;
    int y = top, rowRight = right;
    for (const TextSwatch& sw : textSwatches(g.spec.palette)) {
        if (x > 0 && x + s > rowRight) x = 0, y += rowH, rowRight = width;
        const int sy = y + (rowH - s) / 2;
        t.swatches.push_back({{x, sy, x + s, sy + s}, sw.code});
        x += s + gap;
    }
    t.height = y + rowH - top;
    return t;
}
int gameTextToolsHeight(GameTextBox& g, int width, bool) {
    HDC dc = GetDC(nullptr);
    HGDIOBJ of = SelectObject(dc, app.font);
    const int h = gameTextLayout(g, dc, 0, width).height;
    SelectObject(dc, of);
    ReleaseDC(nullptr, dc);
    return h;
}

// The boxes inside the frame (the frame is the window's top; the tool row is under it).
void gameTextPlace(GameTextBox& g) {
    RECT rc;
    GetClientRect(g.wnd, &rc);
    const int tools = gameTextToolsHeight(g, rc.right);
    g.frame = {0, 0, rc.right, std::max<LONG>(scale(ui::kFieldH), rc.bottom - tools - scale(4))};
    const RECT in{g.frame.left + 1, g.frame.top + 1, g.frame.right - 1, g.frame.bottom - 1};
    for (HWND b : {g.rich, g.raw}) MoveWindow(b, in.left, in.top, in.right - in.left, in.bottom - in.top, TRUE);
    if (g.spec.multiline) {  // room inside the box
        RECT fr;
        GetClientRect(g.rich, &fr);
        InflateRect(&fr, -scale(8), -scale(5));
        SendMessageW(g.rich, EM_SETRECT, 0, LPARAM(&fr));
        SendMessageW(g.raw, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(scale(6), scale(6)));
    } else {
        SendMessageW(g.raw, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(scale(4), scale(4)));
    }
}

void gameTextPaint(GameTextBox& g) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(g.wnd, &ps);
    RECT rc;
    GetClientRect(g.wnd, &rc);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP back = CreateCompatibleBitmap(dc, std::max(1, int(rc.right)), std::max(1, int(rc.bottom)));
    HGDIOBJ ob = SelectObject(mem, back), of = SelectObject(mem, app.font);
    FillRect(mem, &rc, app.bgBrush);
    SetBkMode(mem, TRANSPARENT);
    g.surface.begin(g.wnd);
    const bool focused = GetFocus() == g.rich || GetFocus() == g.raw;
    ui::box(mem, g.frame, g.rawMode ? kCtl : kRichBack, focused ? kAccent : g.changed ? kGoodLine : kBorder);
    const GameTextTools t = gameTextLayout(g, mem, g.frame.bottom + scale(4), rc.right);
    // The swatches (Preview: a ring on the caret's color)
    const unsigned char at = g.rawMode ? 0xFF : gameTextCaretCode(g);
    for (const auto& [r, code] : t.swatches) {
        const bool over = ui::hot(r), on = code == at;
        if (on || over) {
            RECT ring = r;
            InflateRect(&ring, scale(2), scale(2));
            ui::box(mem, ring, CLR_NONE, on ? kAccent : kBorderHot, scale(4));
        }
        const TextLook look = textCodeLook(g.spec.palette, code);
        if (code == 1) {  // normal: the normal color with a slash through it (as the trigger text pop-out has it)
            ui::box(mem, r, kRichDefault, RGB(150, 150, 156), scale(3));
            HPEN pen = CreatePen(PS_SOLID, scale(1), RGB(30, 30, 32));
            HGDIOBJ op = SelectObject(mem, pen);
            MoveToEx(mem, r.left + scale(3), r.bottom - scale(3), nullptr), LineTo(mem, r.right - scale(3), r.top + scale(3));
            SelectObject(mem, op), DeleteObject(pen);
        } else if (look.hidden) {  // hidden: an empty box, crossed
            ui::box(mem, r, kRichBack, kDim, scale(3));
            fillAlpha(mem, {r.left + scale(3), (r.top + r.bottom) / 2, r.right - scale(3), (r.top + r.bottom) / 2 + 1}, kDim, 255);
        } else {
            ui::box(mem, r, look.color, kBorder, scale(3));  // (a line round it: white on a light theme)
        }
        std::wstring name;
        for (const TextSwatch& s : textSwatches(g.spec.palette))
            if (s.code == code) name = s.name;
        name += g.rawMode ? std::wstring(L". Puts its code in at the cursor.") : std::wstring(L". Colors the selected text, or the text you type next.");
        GameTextBox* gp = &g;
        const unsigned char c = code;
        hitsOut().push_back({r, nullptr, [gp, c] { gameTextApply(*gp, c); }, name, IDC_HAND, {}});
    }
    // The align buttons (Preview: the caret's line's alignment on)
    if (g.spec.align && g.spec.multiline) {
        const int now = g.rawMode ? -1 : gameTextCaretAlign(g);
        static const wchar_t* glyphs[3] = {L"\xE8E4", L"\xE8E3", L"\xE8E2"};
        static const wchar_t* tips[3] = {L"Align left", L"Center the line (code 13)", L"Align right (code 12)"};
        for (int k = 0; k < 3; ++k) {
            const bool over = ui::hot(t.align[k]), on = k == now;
            if (on) ui::box(mem, t.align[k], kSel, kAccentLine, scale(4));
            else if (over) ui::box(mem, t.align[k], kCtlHot, CLR_NONE, scale(4));
            ui::glyph(mem, t.align[k], ui::Ico(glyphs[k]), on ? kAccent : over ? kText : kDim, 12);
            GameTextBox* gp = &g;
            hitsOut().push_back({t.align[k], nullptr, [gp, k] { gameTextAlign(*gp, k); }, tips[k], IDC_HAND, {}});
        }
    }
    // Preview | Raw
    {
        const RECT whole{t.seg[0].left, t.seg[0].top, t.seg[1].right, t.seg[1].bottom};
        ui::box(mem, whole, kCtl, kBorder);
        const wchar_t* names[2] = {L"Preview", L"Raw"};
        const wchar_t* tips[2] = {L"The text as the game shows it", L"The text with its codes as <XX> (\"<<\" is a \"<\")"};
        for (int k = 0; k < 2; ++k) {
            const bool on = (k == 1) == g.rawMode;
            if (on) ui::box(mem, t.seg[k], kSel, kAccentLine);
            else if (ui::hot(t.seg[k])) ui::box(mem, t.seg[k], kCtlHot, CLR_NONE);
            SetTextColor(mem, on ? kAccent : kText);
            RECT tr2 = t.seg[k];
            DrawTextW(mem, names[k], -1, &tr2, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
            GameTextBox* gp = &g;
            const bool raw = k == 1;
            hitsOut().push_back({t.seg[k], nullptr, [gp, raw] { gameTextSetRaw(*gp, raw); }, tips[k], IDC_HAND, {}});
        }
    }
    g.surface.end(g.wnd);
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, of), SelectObject(mem, ob);
    DeleteObject(back);
    DeleteDC(mem);
    EndPaint(g.wnd, &ps);
}

// The boxes: a paste is plain text (it takes the look at the caret); focus leaving the editor tells the page.
LRESULT CALLBACK gameTextBoxProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref) {
    GameTextBox* g = reinterpret_cast<GameTextBox*>(ref);
    const bool ctrl = GetKeyState(VK_CONTROL) < 0 && GetKeyState(VK_MENU) >= 0, shift = GetKeyState(VK_SHIFT) < 0;  // (AltGr is Ctrl+Alt)
    if (hwnd == g->rich && (msg == WM_PASTE || (msg == WM_KEYDOWN && ((ctrl && wp == 'V') || (shift && wp == VK_INSERT))))) {
        std::wstring text;
        bool got = false;
        if (OpenClipboard(hwnd)) {
            if (HANDLE h = GetClipboardData(CF_UNICODETEXT))
                if (const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(h))) text = p, got = true, GlobalUnlock(h);
            CloseClipboard();
        }
        if (!got) return 0;  // no text (a picture): nothing
        if (!g->spec.multiline) std::replace_if(text.begin(), text.end(), [](wchar_t c) { return c == L'\r' || c == L'\n'; }, L' ');
        SendMessageW(hwnd, EM_REPLACESEL, TRUE, LPARAM(text.c_str()));
        return 0;
    }
    if (msg == WM_CHAR && wp == 1 && hwnd == g->raw) return SendMessageW(hwnd, EM_SETSEL, 0, -1), 0;  // Ctrl+A in the plain box
    if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS) InvalidateRect(g->wnd, nullptr, FALSE);  // the frame
    const bool inside = HWND(wp) == g->rich || HWND(wp) == g->raw || HWND(wp) == g->wnd;
    if (msg == WM_KILLFOCUS && !inside) gameTextNotify(*g, EN_KILLFOCUS);
    if (msg == WM_SETFOCUS && !inside) gameTextNotify(*g, EN_SETFOCUS);
    return DefSubclassProc(hwnd, msg, wp, lp);
}

LRESULT CALLBACK gameTextProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    GameTextBox* g = gameTextOf(hwnd);
    if (!g) return DefWindowProcW(hwnd, msg, wp, lp);
    LRESULT res = 0;
    if (msg != WM_PAINT && g->surface.message(hwnd, msg, wp, lp, res)) return res;
    switch (msg) {
    case WM_PAINT: gameTextPaint(*g); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_SETFOCUS: SetFocus(g->rawMode ? g->raw : g->rich); return 0;  // (a click on the tool row: the text keeps the focus)
    case WM_SIZE: gameTextPlace(*g); InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_COMMAND:
        if (HIWORD(wp) == EN_CHANGE && !g->setting) gameTextNotify(*g, EN_CHANGE), InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_NOTIFY:
        if (reinterpret_cast<NMHDR*>(lp)->code == EN_SELCHANGE) InvalidateRect(hwnd, nullptr, FALSE);  // the caret's color and alignment
        return 0;
    case WM_CTLCOLOREDIT: {
        static HBRUSH back = nullptr;
        static COLORREF backColor = CLR_NONE;
        if (backColor != kCtl) {
            if (back) DeleteObject(back);
            back = CreateSolidBrush(kCtl), backColor = kCtl;
        }
        SetTextColor(HDC(wp), kText);
        SetBkColor(HDC(wp), kCtl);
        return LRESULT(back);
    }
    case WM_NCDESTROY: {
        if (g->doc) g->doc->Release(), g->doc = nullptr;
        gGameTexts.erase(hwnd);
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// Makes the editor in `parent` at `r` (the box at the top, the tool row under it); WM_COMMAND to the parent with `id`:
// EN_CHANGE, EN_SETFOCUS, EN_KILLFOCUS (the focus left the editor).
HWND gameTextCreate(HWND parent, int id, RECT r, const GameTextSpec& spec) {
    static bool registered = false;
    if (!registered) {
        LoadLibraryW(L"Msftedit.dll");
        WNDCLASSEXW wc{sizeof wc};
        wc.lpfnWndProc = gameTextProc, wc.hInstance = app.inst, wc.hCursor = LoadCursor(nullptr, IDC_ARROW), wc.lpszClassName = L"StarDraftGameText";
        RegisterClassExW(&wc);
        registered = true;
    }
    auto box = std::make_unique<GameTextBox>();
    GameTextBox& g = *box;
    g.id = id, g.spec = spec;
    HWND w = CreateWindowExW(WS_EX_CONTROLPARENT, L"StarDraftGameText", L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, r.left, r.top, r.right - r.left, r.bottom - r.top,
                             parent, HMENU(INT_PTR(id)), app.inst, nullptr);
    g.wnd = w;
    gGameTexts[w] = std::move(box);
    const DWORD lines = spec.multiline ? ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | WS_VSCROLL : ES_AUTOHSCROLL;
    g.rich = CreateWindowExW(0, MSFTEDIT_CLASS, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NOOLEDRAGDROP | lines, 0, 0, 10, 10, w, nullptr, app.inst, nullptr);
    SendMessageW(g.rich, WM_SETFONT, WPARAM(app.font), TRUE);
    SendMessageW(g.rich, EM_SETBKGNDCOLOR, 0, LPARAM(kRichBack));
    SendMessageW(g.rich, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE);
    SendMessageW(g.rich, EM_SETTEXTMODE, TM_RICHTEXT | TM_MULTILEVELUNDO, 0);
    darkControl(g.rich, L"DarkMode_Explorer");
    g.raw = CreateWindowExW(0, WC_EDITW, L"", WS_CHILD | WS_TABSTOP | lines, 0, 0, 10, 10, w, nullptr, app.inst, nullptr);
    SendMessageW(g.raw, WM_SETFONT, WPARAM(monoFont()), TRUE);
    darkControl(g.raw, L"DarkMode_CFD");
    if (!spec.cue.empty()) SendMessageW(g.raw, EM_SETCUEBANNER, TRUE, LPARAM(spec.cue.c_str()));
    SetWindowSubclass(g.rich, gameTextBoxProc, 7, DWORD_PTR(&g));
    SetWindowSubclass(g.raw, gameTextBoxProc, 7, DWORD_PTR(&g));
    gameTextPlace(g);
    gameTextSet(w, "");
    return w;
}
// The height the editor needs for a box of `boxH` and the tool row at `width`.
int gameTextHeight(const GameTextSpec& spec, int width, int boxH) {
    GameTextBox probe;
    probe.spec = spec;
    return boxH + scale(4) + gameTextToolsHeight(probe, width);
}
// For pages and tests: a swatch, an align button, Preview or Raw, as a click on them does.
void gameTextColorSelection(HWND w, unsigned char code) {
    if (GameTextBox* g = gameTextOf(w)) gameTextApply(*g, code);
}
void gameTextAlignSelection(HWND w, int align) {
    if (GameTextBox* g = gameTextOf(w)) gameTextAlign(*g, align);
}
void gameTextShowRaw(HWND w, bool raw) {
    if (GameTextBox* g = gameTextOf(w)) gameTextSetRaw(*g, raw);
}
void gameTextSetChanged(HWND w, bool changed) {
    if (GameTextBox* g = gameTextOf(w); g && g->changed != changed) g->changed = changed, InvalidateRect(w, nullptr, FALSE);
}
bool gameTextIsRaw(HWND w) {
    GameTextBox* g = gameTextOf(w);
    return g && g->rawMode;
}
// A game text's bytes without its codes, its lines joined by a space (in the text's own encoding: a list's line).
std::string gameTextStripped(const std::string& bytes) {
    std::string t;
    for (const char ch : bytes)
        if (static_cast<unsigned char>(ch) >= 0x20 || ch == '\t') t += ch;
        else if (ch == '\n') t += ' ';
    return t;
}
// A game text in one line without its codes (a block's sentence, a list): the line ends as a mark.
std::wstring gameTextPlain(const std::string& bytes) {
    std::string t;
    for (const char ch : bytes)
        if (static_cast<unsigned char>(ch) >= 0x20 || ch == '\t') t += ch;
        else if (ch == '\n') t += '\x01';
    std::wstring w = widen(t), out;
    for (const wchar_t c : w) out += c == 1 ? std::wstring(L"  \x21B5  ") : std::wstring(1, c);
    return out;
}
