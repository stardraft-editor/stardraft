// The game's text (portable, core.inc): its colors and its codes, and the text drawn as the game colors it on a Canvas.
// The map's title and description, mission briefings, the game's strings (Data > Strings), classic trigger texts and
// the unit names hold the game's control codes: a color for the text after it, align right (12) and center (13) for
// its line, hidden (0B, 14: briefings).
// The game has two sets of text colors: the in-game one (messages, unit names: the long list with the players'
// colors) and the one of mission briefings and the scenario's title and description (pale blue, green, light green,
// gray, white, red). The editor of these texts is game_text.inc (Windows) and core/views_game_text.inc (the kit's).

enum class TextPalette { Game, Scenario };

// The game's text colors (as StarCraft: Remastered draws them; codes 01 and 02: the text's own color, pale blue).
// 0xFFFFFFFF: not a color.
inline draw::Color gameTextColor(unsigned char c, draw::Color normal) {
    switch (c) {
    case 1: return normal;                       // use default
    case 2: return draw::rgb(184, 184, 232);     // pale blue
    case 3: return draw::rgb(220, 220, 60);      // yellow
    case 4: return draw::rgb(255, 255, 255);     // white
    case 5: return draw::rgb(132, 116, 116);     // grey
    case 6: return draw::rgb(200, 24, 24);       // red
    case 7: return draw::rgb(16, 252, 24);       // green
    case 8: return draw::rgb(244, 4, 4);         // red (player 1)
    case 0x0E: return draw::rgb(12, 72, 204);    // blue (player 2)
    case 0x0F: return draw::rgb(44, 180, 148);   // teal (player 3)
    case 0x10: return draw::rgb(136, 64, 156);   // purple (player 4)
    case 0x11: return draw::rgb(248, 140, 20);   // orange (player 5)
    case 0x15: return draw::rgb(112, 48, 20);    // brown (player 6)
    case 0x16: return draw::rgb(204, 224, 208);  // white (player 7)
    case 0x17: return draw::rgb(252, 252, 56);   // yellow (player 8)
    case 0x18: return draw::rgb(8, 128, 8);      // green (player 9)
    case 0x19: return draw::rgb(252, 252, 124);  // brighter yellow (player 10)
    case 0x1A: return draw::rgb(0, 0, 24);       // "cyan" (the game draws it almost black)
    case 0x1B: return draw::rgb(236, 196, 176);  // pinkish (player 11)
    case 0x1C: return draw::rgb(64, 104, 212);   // dark cyan (player 12)
    case 0x1D: return draw::rgb(116, 164, 124);  // greygreen
    case 0x1E: return draw::rgb(144, 144, 184);  // bluegrey
    case 0x1F: return draw::rgb(0, 228, 252);    // turquoise
    }
    return 0xFFFFFFFF;
}
// Mission briefings color text in their own way (the same codes, other colors): 01 the text's own color, 00 02 1A
// pale blue, 03 green, 04 light green, 05 gray, 06 white, 07 red; 0B and 14 make the text after them invisible.
constexpr draw::Color kInvisibleText = 0xFFFFFFFE;
inline draw::Color briefingTextColor(unsigned char c, draw::Color normal) {
    switch (c) {
    case 1: return normal;
    case 0: case 2: case 0x1A: return draw::rgb(184, 184, 232);  // pale blue
    case 3: return draw::rgb(16, 216, 24);                       // green
    case 4: return draw::rgb(160, 236, 160);                     // light green
    case 5: return draw::rgb(132, 132, 132);                     // gray
    case 6: return draw::rgb(255, 255, 255);                     // white
    case 7: return draw::rgb(220, 32, 32);                       // red
    case 0x0B: case 0x14: return kInvisibleText;
    }
    return 0xFFFFFFFF;
}

// The palette's swatches, in their order (code 1: the normal color).
struct TextSwatch {
    unsigned char code;
    const wchar_t* name;
};
inline const std::vector<TextSwatch>& textSwatches(TextPalette p) {
    static const std::vector<TextSwatch> game = {
        {1, L"Normal"}, {3, L"Yellow"}, {4, L"White"}, {5, L"Gray"}, {6, L"Red"}, {7, L"Green"}, {8, L"Red (Player 1)"}, {0x0E, L"Blue (Player 2)"},
        {0x0F, L"Teal (Player 3)"}, {0x10, L"Purple (Player 4)"}, {0x11, L"Orange (Player 5)"}, {0x15, L"Brown (Player 6)"}, {0x16, L"White (Player 7)"},
        {0x17, L"Yellow (Player 8)"}, {0x18, L"Green (Player 9)"}, {0x19, L"Bright yellow (Player 10)"}, {0x1B, L"Pink (Player 11)"},
        {0x1C, L"Dark cyan (Player 12)"}, {0x1D, L"Gray-green"}, {0x1E, L"Blue-gray"}, {0x1F, L"Turquoise"}};
    static const std::vector<TextSwatch> scenario = {{1, L"Normal"}, {2, L"Pale blue"}, {3, L"Green"}, {4, L"Light green"}, {5, L"Gray"},
                                                     {6, L"White"}, {7, L"Red"}, {0x0B, L"Hidden (the game does not show this text)"}};
    return p == TextPalette::Game ? game : scenario;
}

// Draws `raw` (UTF-8 with the game's control bytes) in `r`, line by line, in the canvas's font; returns the height used.
// `briefing`: the colors of mission briefings. A line wraps at the box's width between words; codes 12 and 13 align it.
inline int drawGameText(draw::Canvas& c, draw::Rect r, const std::string& raw, draw::Color normal, int lineHeight = 0, bool briefing = false) {
    const int lh = lineHeight ? lineHeight : c.metrics().height + scale(2), width = r.right - r.left;
    int y = r.top;
    draw::Color color = normal;
    // A line of colored pieces, wrapped at the box's width between words.
    struct Piece {
        std::wstring text;
        draw::Color color;
        int w;
    };
    auto drawLine = [&](const std::vector<Piece>& pieces, int align) {
        int w = 0;
        for (const auto& p : pieces) w += p.w;
        int x = align == 1 ? r.right - w : align == 2 ? r.left + (width - w) / 2 : r.left;
        for (const auto& p : pieces) {
            if (p.color != kInvisibleText) c.textAt({x, y}, p.text, p.color);
            x += p.w;
        }
        y += lh;
    };
    size_t i = buttonPrefix(raw) ? 2 : 0;
    while (i <= raw.size()) {
        size_t end = raw.find('\n', i);
        if (end == std::string::npos) end = raw.size();
        // Words (a word keeps the spaces after it), each in its colors.
        std::vector<std::vector<Piece>> words;
        std::vector<Piece> word;
        int align = 0;
        std::string run;
        bool space = false;
        auto flushRun = [&] {
            if (!run.empty()) word.push_back({widen(run), color, 0}), run.clear();
        };
        auto flushWord = [&] {
            flushRun();
            if (!word.empty()) words.push_back(std::move(word)), word.clear();
        };
        for (size_t k = i; k < end; ++k) {
            const unsigned char ch = static_cast<unsigned char>(raw[k]);
            if (ch == 0x12) align = 1;
            else if (ch == 0x13) align = 2;
            else if (ch < 0x20) {
                if (const draw::Color nc = briefing ? briefingTextColor(ch, normal) : gameTextColor(ch, normal); nc != 0xFFFFFFFF) flushRun(), color = nc;
            } else {
                if (ch != ' ' && space) flushWord(), space = false;
                run += char(ch);
                if (ch == ' ') space = true;
            }
        }
        flushWord();
        std::vector<Piece> lineNow;
        int x = 0;
        for (auto& wd : words) {
            int ww = 0;
            for (auto& p : wd) p.w = c.measureAt(p.text), ww += p.w;  // (the width as textAt draws it)
            if (x > 0 && x + ww > width) drawLine(lineNow, align), lineNow.clear(), x = 0;
            for (auto& p : wd) lineNow.push_back(p);
            x += ww;
        }
        drawLine(lineNow, align);
        i = end + 1;
    }
    return y - r.top;
}

// --- Raw: the bytes with each code as <XX> ------------------------------------------------------------------------

// "<XX>" at v[i]: two hex digits for a code 01..1F.
inline int gameTextTagAt(const std::wstring& v, size_t i) {
    if (i + 3 >= v.size() || v[i] != L'<' || v[i + 3] != L'>' || !iswxdigit(v[i + 1]) || !iswxdigit(v[i + 2])) return -1;
    const int c = int(std::wcstoul(v.substr(i + 1, 2).c_str(), nullptr, 16));
    return c >= 1 && c < 0x20 ? c : -1;
}
// Bytes as text (UTF-8, else Windows-1252 for the whole text, as widen does).
inline std::wstring gameTextDecode(const std::string& s, bool utf8) {
    std::wstring w;
    if (s.empty()) return w;
    if (utf8 && stardraft::text::decodeUtf8(s, w)) return w;
    w.clear();
    for (const char ch : s) stardraft::text::appendWide(w, stardraft::text::cp1252(static_cast<unsigned char>(ch)));
    return w;
}
inline bool gameTextIsUtf8(const std::string& s) {
    std::wstring w;
    return s.empty() || stardraft::text::decodeUtf8(s, w);
}

inline std::wstring gameTextToRaw(const std::string& bytes, bool multiline) {
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
inline std::string gameTextFromRaw(const std::wstring& v, bool crlf) {
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

// --- The editor's settings ------------------------------------------------------------------------------------------

struct GameTextSpec {
    TextPalette palette = TextPalette::Game;
    bool multiline = true;
    bool align = true;   // the align buttons (multi-line texts)
    bool crlf = false;   // lines end in \r\n (map texts); else as the text had them (\n for a new text)
    std::wstring cue;    // shown in an empty box
    // A text with values (the trigger text pop-out, core/views_value_pop.inc): a value ({gold}, {#1}) is a chip, braces in
    // words are doubled ("{{"), a player's color ({P2:c}) colors what follows, and a swatch gives a player's color.
    bool values = false;
};
