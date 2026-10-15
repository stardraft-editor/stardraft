// The game text editor as kit widgets (portable, core.inc): the box at the top and the tool row under it. Preview is a
// kit RichField: the text in its codes' colors (core/game_text_model.inc), its lines aligned (codes 13 and 12); a
// swatch colors the selected text, or the text typed next; an align button aligns the selected lines. Raw is a kit
// TextField: the text with each code as <XX> (a "<" that could read as a code is "<<"). A text that is not changed
// reads back as its own bytes. The rules are the Windows editor's (game_text.inc: a RichEdit box); the Data workspace's
// Strings tab uses this one (its host is data_extras.inc).
//
// The Preview keeps the codes itself: one code for each character of the field's text (0: none, the text's own color)
// and an alignment for each paragraph; a change in the field (typing, paste, cut, undo) is compared with the text
// before it, and the new characters take the code at the caret, as the RichEdit box gave them its format there.

constexpr int kGameTextSwatch = 16, kGameTextGap = 4, kGameTextRowH = 24;

// How a code's text looks in the box: its color (`normal` for the text's own); hidden text crossed out; a code that is
// not a color of the palette (kept as it is) with a dotted line under it.
struct GameTextLook {
    draw::Color color;
    bool hidden = false, unknown = false;
};
inline GameTextLook gameTextLook(TextPalette p, unsigned char code, draw::Color normal) {
    if (code <= 2) return {normal};  // (none, 01 normal, 02 pale blue: the box's normal color)
    if (p == TextPalette::Game) {
        if (code == 0x1A) return {draw::rgb(64, 64, 110)};  // almost black in the game: dark, but readable here
        const draw::Color c = gameTextColor(code, normal);
        return c != 0xFFFFFFFF ? GameTextLook{c} : GameTextLook{normal, false, true};
    }
    const draw::Color c = briefingTextColor(code, normal);
    if (c == kInvisibleText) return {draw::rgb(112, 112, 122), true};
    return c != 0xFFFFFFFF ? GameTextLook{c} : GameTextLook{normal, false, true};
}
inline draw::Color gameTextLookColor(TextPalette p, unsigned char code, draw::Color normal) { return gameTextLook(p, code, normal).color; }

// A text with values (GameTextSpec::values): a player's color is a code of the editor's own, kGameTextPlayerCode + 1 to
// + 8 for Player 1 to 8, + 9 for the current player ({P2:c}, {current player:c}). It shows in that player's color with
// dots under it (the current player: the normal color); the swatch kGameTextPlayerSwatch picks one.
constexpr unsigned char kGameTextPlayerCode = 0xE0, kGameTextPlayerSwatch = 0xFF;
inline std::string gameTextPlayerName(unsigned char code) {
    return code == kGameTextPlayerCode + 9 ? std::string("current player") : "P" + std::to_string(code - kGameTextPlayerCode);
}
// The code of a player's color by its name ("P1" to "P8", "current player"); 0 when it is none of them.
inline unsigned char gameTextPlayerCode(const std::string& name) {
    if (name == "current player") return kGameTextPlayerCode + 9;
    if (name.size() == 2 && name[0] == 'P' && name[1] >= '1' && name[1] <= '8') return static_cast<unsigned char>(kGameTextPlayerCode + (name[1] - '0'));
    return 0;
}
inline draw::Color gameTextPlayerColor(unsigned char code, draw::Color normal) {
    static const unsigned char codes[8] = {8, 0x0E, 0x0F, 0x10, 0x11, 0x15, 0x16, 0x17};
    const int p = code - kGameTextPlayerCode;
    return p >= 1 && p <= 8 ? gameTextColor(codes[p - 1], normal) : normal;
}
// The swatches the editor shows: the palette's, and in a text with values a player's color at the end.
inline std::vector<TextSwatch> gameTextSwatches(const GameTextSpec& spec) {
    std::vector<TextSwatch> s = textSwatches(spec.palette);
    if (spec.values) s.push_back({kGameTextPlayerSwatch, L"A player's color (as the game shows that player's color)"});
    return s;
}

// The tool row: the swatches from the left (as many rows as they need), the align buttons and Preview | Raw at the
// right of the first row. `top`: the row's top; the boxes are in the editor's coordinates.
struct GameTextToolLayout {
    std::vector<std::pair<draw::Rect, unsigned char>> swatches;
    draw::Rect align[3], seg[2];
    int height = 0;
};
inline GameTextToolLayout gameTextToolLayout(draw::Canvas& c, const GameTextSpec& spec, int top, int width) {
    GameTextToolLayout t;
    const int rowH = scale(kGameTextRowH), s = scale(kGameTextSwatch), gap = scale(kGameTextGap);
    // Right: Preview | Raw (and the align buttons before it)
    int right = width;
    const wchar_t* names[2] = {L"Preview", L"Raw"};
    int segW[2];
    for (int k = 0; k < 2; ++k) segW[k] = c.measure(names[k]) + scale(18);
    const int segH = scale(20), segY = top + (rowH - segH) / 2;
    int x = right - segW[0] - segW[1];
    t.seg[0] = {x, segY, x + segW[0], segY + segH}, t.seg[1] = {x + segW[0], segY, right, segY + segH};
    right = x - scale(10);
    if (spec.align && spec.multiline) {
        const int b = scale(22);
        for (int k = 2; k >= 0; --k) t.align[k] = {right - b, top + (rowH - b) / 2, right, top + (rowH - b) / 2 + b}, right -= b;
        right -= scale(10);
    }
    // Left: the swatches, row by row
    x = 0;
    int y = top, rowRight = right;
    for (const TextSwatch& sw : gameTextSwatches(spec)) {
        if (x > 0 && x + s > rowRight) x = 0, y += rowH, rowRight = width;
        const int sy = y + (rowH - s) / 2;
        t.swatches.push_back({{x, sy, x + s, sy + s}, sw.code});
        x += s + gap;
    }
    t.height = y + rowH - top;
    return t;
}
// The height the editor needs for a box of `boxH` and the tool row at `width` (the canvas has the UI font).
inline int gameTextHeight(draw::Canvas& c, const GameTextSpec& spec, int width, int boxH) {
    return boxH + scale(4) + gameTextToolLayout(c, spec, 0, width).height;
}

// A kit field that tells its owner when it gets or loses the focus (the editor's frame shows the focus).
template <class Field>
class GameTextFocusField final : public Field {
public:
    std::function<void(bool has)> told;
    void focusChanged(bool has) override {
        Field::focusChanged(has);
        if (told) told(has);
    }
};

class GameTextView final : public draw::View {
public:
    explicit GameTextView(const GameTextSpec& spec) : spec_(spec) {
        rich_.setPlain(true);
        rich_.setMultiline(spec.multiline);
        rich_.setWrap(true);
        rich_.setPlaceholder(spec.cue);
        rich_.onChange = [this] { richChanged(); };
        rich_.told = [this](bool has) { focusMoved(has); };
        rich_.onChipClick = [this](size_t index, draw::Rect r) {
            const draw::Rect f = rich_.frame();
            if (onValueClick) onValueClick(index, valueToken(index), {r.left + f.left, r.top + f.top, r.right + f.left, r.bottom + f.top});
        };
        raw_.setPlain(true);
        raw_.setMultiline(spec.multiline);
        raw_.setWrap(true);
        raw_.setFont({draw::Face::Mono, 0});
        raw_.setPlaceholder(spec.cue);
        raw_.onChange = [this] { changed(); };
        raw_.told = [this](bool has) { focusMoved(has); };
        add(rich_, {0, 0, 0, 0});
        add(raw_, {0, 0, 0, 0});
        raw_.setVisible(false);
        setBytes("");
    }

    // The text's look: the normal color and the box's ground (the theme's; the game's colors read on a dark ground).
    void setColors(draw::Color normal, draw::Color back) {
        if (normal == normal_ && back == back_) return;
        normal_ = normal, back_ = back;
        restyle();
        invalidate();
    }
    // The page's value differs from the game's: the frame in the changed color.
    void setChanged(bool on) {
        if (on != changedLook_) changedLook_ = on, invalidate();
    }

    // The text as the game's bytes: unchanged, the bytes that were put in.
    std::string bytes() const {
        const std::string now = rawMode_ ? gameTextFromRaw(raw_.text(), crlf_) : read();
        return now == baseRead_ ? base_ : now;
    }
    // What the box reads as now (the codes as the editor writes them), without the unchanged-text rule.
    std::string readNow() const { return rawMode_ ? gameTextFromRaw(raw_.text(), crlf_) : read(); }
    void setBytes(const std::string& bytes) {
        crlf_ = spec_.crlf || bytes.find("\r\n") != std::string::npos;
        setting_ = true;
        if (rawMode_) raw_.setText(gameTextToRaw(bytes, spec_.multiline));
        else if (spec_.values) fillValues(bytes);
        else fill(bytes);
        setting_ = false;
        base_ = bytes;
        baseRead_ = readNow();
        invalidate();
    }

    // Preview or Raw: the text goes over to the other box (unchanged, it stays the bytes it was).
    bool isRaw() const { return rawMode_; }
    void setRaw(bool raw) {
        if (raw == rawMode_) return;
        const std::string now = bytes();
        const bool focused = hasFocusInside();
        rawMode_ = raw;
        setBytes(now);
        rich_.setVisible(!raw), raw_.setVisible(raw);
        if (focused) (raw ? static_cast<draw::View&>(raw_) : rich_).focus();
        invalidate();
    }
    // A swatch: the selected text (or the text typed next) takes the code. In Raw: the code's tag at the caret.
    void applyCode(unsigned char code) {
        if (code == kGameTextPlayerSwatch && spec_.values) {  // a player's color: whose (the owner asks), then as a code
            if (rawMode_ || !pickPlayerColor) return;
            unsigned char player = 0;  // (the field keeps its selection while the picker is open)
            if (!pickPlayerColor(player) || player <= kGameTextPlayerCode) return rich_.focus(), void();
            code = player;
        }
        if (rawMode_) return rawInsert(code, false);
        const unsigned char c = code == 1 ? 0 : code;
        const size_t a = std::min(rich_.caret(), rich_.anchor()), b = std::max(rich_.caret(), rich_.anchor());
        if (a < b) {
            for (size_t i = a; i < b && i < codes_.size(); ++i) codes_[i] = c;
        } else {
            pending_ = c, pendingAt_ = a;
            if (text_.empty()) startCode_ = c;
        }
        restyle();
        rich_.focus();
        changed();
    }
    // An align button: the selected lines (the caret's line) aligned (0 left, 1 center, 2 right). In Raw: the code's
    // tag at the start of the caret's line.
    void align(int how) {
        if (rawMode_) {
            if (how) rawInsert(how == 1 ? 0x13 : 0x12, true);
            return;
        }
        const size_t a = std::min(rich_.caret(), rich_.anchor()), b = std::max(rich_.caret(), rich_.anchor());
        const size_t pa = size_t(std::count(text_.begin(), text_.begin() + std::min(a, text_.size()), L'\n'));
        const size_t pb = size_t(std::count(text_.begin(), text_.begin() + std::min(b, text_.size()), L'\n'));
        for (size_t p = pa; p <= pb && p < aligns_.size(); ++p) aligns_[p] = uint8_t(how);
        rich_.setLineAligns(aligns_);
        rich_.focus();
        changed();
    }
    bool hasFocusInside() const {
        const draw::Host* h = host();
        return h && (h->hasFocus(rich_) || h->hasFocus(raw_));
    }
    void focusText() { (rawMode_ ? static_cast<draw::View&>(raw_) : rich_).focus(); }

    // The text changed (typed, a swatch, an alignment); the focus left the editor.
    std::function<void()> onChange;
    std::function<void()> onFocusLost;

    // A text with values (GameTextSpec::values). chipLabel: a value's chip's words (empty: the value is left out, as a
    // value from the game the block does not have). onValueClick: a click on a chip (its index in reading order, its
    // token, its box in this view's coordinates). pickPlayerColor: the player's color swatch asks whose
    // (kGameTextPlayerCode + n); false: none.
    std::function<std::wstring(const TriggerTextPart& part)> chipLabel;
    std::function<void(size_t index, const std::string& token, draw::Rect box)> onValueClick;
    std::function<bool(unsigned char& code)> pickPlayerColor;
    // A value put in at the caret (in place of the selection), as a chip with these words.
    void insertValue(const std::wstring& label, const std::string& token) {
        if (rawMode_) {  // Raw: the token as typed
            raw_.model().insert(widen(token));
            raw_.focus();
            return changed();
        }
        tokens_.push_back(token);
        rich_.insertChip({label, 0, uint64_t(tokens_.size())});
        if (posText() != text_) richChanged();
        rich_.focus();
    }
    // The chip `index` (reading order) with new words and token; Remove: the chip goes.
    void replaceValue(size_t index, const std::wstring& label, const std::string& token) {
        tokens_.push_back(token);
        rich_.replaceChip(index, {label, 0, uint64_t(tokens_.size())});
        changed();
    }
    void removeValue(size_t index) {
        size_t at = 0, k = 0;
        for (; at < text_.size(); ++at)
            if (text_[at] == kChipMark && k++ == index) break;
        if (at >= text_.size()) return;
        std::vector<stardraft::kit::RichPart> parts = rich_.parts(), kept;
        size_t n = 0;
        for (auto& p : parts) {
            if (p.chip && n++ == index) continue;
            if (!p.chip && !kept.empty() && !kept.back().chip) kept.back().text += p.text;
            else kept.push_back(std::move(p));
        }
        text_.erase(text_.begin() + long(at));
        if (at < codes_.size()) codes_.erase(codes_.begin() + long(at));
        rich_.setParts(std::move(kept));
        rich_.setLineAligns(aligns_);
        restyle();
        rich_.focus();
        changed();
    }
    std::string valueToken(size_t index) const {
        const stardraft::kit::RichChip ch = rich_.chip(index);
        return ch.id >= 1 && ch.id <= tokens_.size() ? tokens_[size_t(ch.id - 1)] : std::string();
    }
    std::wstring valueLabel(size_t index) const { return rich_.chip(index).label; }
    // The caret's place in this view (a picker opens under it).
    draw::Rect caretBox() const {
        const draw::Rect cr = rawMode_ ? raw_.caretRect() : rich_.caretRect();
        const draw::Rect f = rawMode_ ? raw_.frame() : rich_.frame();
        return {cr.left + f.left, cr.top + f.top, cr.right + f.left, cr.bottom + f.top};
    }

    // For the self-test: the boxes.
    stardraft::kit::RichField& preview() { return rich_; }
    stardraft::kit::TextField& rawBox() { return raw_; }

    void resized() override { place(); }

    void paint(draw::Canvas& c) override {
        // Preview | Raw's widths are the canvas's: the boxes are placed again when they change.
        const int w0 = c.measure(L"Preview"), w1 = c.measure(L"Raw");
        if (w0 != measured_[0] || w1 != measured_[1]) measured_[0] = w0, measured_[1] = w1, place();
        const bool focused = hasFocusInside();
        c.roundBox(frame_, rawMode_ ? kCtl : back_, focused ? kAccent : changedLook_ ? kGoodLine : kBorder, float(scale(stardraft::kit::kRadius)));
        // The inside of the line is the text box's ground to its corners (the native boxes covered the line's round
        // inner corners).
        c.fill({frame_.left + 1, frame_.top + 1, frame_.right - 1, frame_.bottom - 1}, rawMode_ ? kCtl : back_);
        if (rawMode_) {  // the Raw box's own round line inside (as the native edit box's theme drew it)
            const draw::Rect in{frame_.left + 1, frame_.top + 1, frame_.right - 1, frame_.bottom - 1};
            c.fill(in, kBg);
            c.roundBox(in, kCtl, kBorder, float(scale(4)));
        }
        tools_ = gameTextToolLayout(c, spec_, frame_.bottom + scale(4), width());
        tips_.clear();
        // The swatches (Preview: a ring on the caret's color)
        const unsigned char at = rawMode_ ? 0xFF : caretCode();
        for (const auto& [r, code] : tools_.swatches) {
            const bool over = hot(r), on = code == at;
            if (on || over) {
                const draw::Rect ring{r.left - scale(2), r.top - scale(2), r.right + scale(2), r.bottom + scale(2)};
                c.roundBox(ring, draw::kNone, on ? kAccent : kBorderHot, float(scale(4)));
            }
            const unsigned char look = code;
            if (look == kGameTextPlayerSwatch && spec_.values) {  // a player's color: the first players' colors side by side
                static const draw::Color stripes[] = {draw::rgb(244, 4, 4), draw::rgb(12, 72, 204), draw::rgb(44, 180, 148), draw::rgb(136, 64, 156)};
                for (int k = 0; k < 4; ++k) c.fill({r.left + k * r.width() / 4, r.top, r.left + (k + 1) * r.width() / 4, r.bottom}, stripes[k]);
                c.roundBox(r, draw::kNone, draw::rgb(70, 70, 76), float(scale(3)));
            } else if (look == 1) {  // normal: the normal color with a slash through it (as the trigger text pop-out has it)
                c.roundBox(r, normal_, draw::rgb(150, 150, 156), float(scale(3)));
                c.line(r.left + scale(3), r.bottom - scale(3), r.right - scale(3), r.top + scale(3), draw::rgb(30, 30, 32), scale(1));
            } else if (spec_.palette == TextPalette::Scenario && briefingTextColor(look, normal_) == kInvisibleText) {  // hidden: an empty box, crossed
                c.roundBox(r, back_, kDim, float(scale(3)));
                c.fill({r.left + scale(3), (r.top + r.bottom) / 2, r.right - scale(3), (r.top + r.bottom) / 2 + 1}, kDim);
            } else {
                c.roundBox(r, gameTextLookColor(spec_.palette, look, normal_), kBorder, float(scale(3)));  // (a line round it: white on a light theme)
            }
            std::wstring name;
            for (const TextSwatch& s : gameTextSwatches(spec_))
                if (s.code == code) name = s.name;
            if (code == kGameTextPlayerSwatch && spec_.values) name = tr(name.c_str());
            else name += tr(rawMode_ ?L". Puts its code in at the cursor." : L". Colors the selected text, or the text you type next.");
            tips_.push_back({r, name});
        }
        // The align buttons (Preview: the caret's line's alignment on)
        if (spec_.align && spec_.multiline) {
            const int now = rawMode_ ? -1 : caretAlign();
            static const wchar_t* glyphs[3] = {L"\xE8E4", L"\xE8E3", L"\xE8E2"};
            static const wchar_t* tips[3] = {L"Align left", L"Center the line (code 13)", L"Align right (code 12)"};
            for (int k = 0; k < 3; ++k) {
                const bool over = hot(tools_.align[k]), on = k == now;
                if (on) c.roundBox(tools_.align[k], kSel, kAccentLine, float(scale(4)));
                else if (over) c.roundBox(tools_.align[k], kCtlHot, draw::kNone, float(scale(4)));
                kitpaint::drawGlyph(c, tools_.align[k], glyphs[k], on ? kAccent : over ? kText : kDim, 12);
                tips_.push_back({tools_.align[k], tips[k]});
            }
        }
        // Preview | Raw
        {
            const draw::Rect whole{tools_.seg[0].left, tools_.seg[0].top, tools_.seg[1].right, tools_.seg[1].bottom};
            c.roundBox(whole, kCtl, kBorder, float(scale(stardraft::kit::kRadius)));
            const wchar_t* names[2] = {L"Preview", L"Raw"};
            const wchar_t* tips[2] = {L"The text as the game shows it", L"The text with its codes as <XX> (\"<<\" is a \"<\")"};
            for (int k = 0; k < 2; ++k) {
                const bool on = (k == 1) == rawMode_;
                if (on) c.roundBox(tools_.seg[k], kSel, kAccentLine, float(scale(stardraft::kit::kRadius)));
                else if (hot(tools_.seg[k])) c.roundBox(tools_.seg[k], kCtlHot, draw::kNone, float(scale(stardraft::kit::kRadius)));
                c.text(tools_.seg[k], names[k], on ? kAccent : kText, draw::kMiddle | draw::kCenter);
                tips_.push_back({tools_.seg[k], tips[k]});
            }
        }
    }

    bool mouse(const draw::MouseEvent& e) override {
        if (e.kind == draw::MouseKind::Move || e.kind == draw::MouseKind::Leave) {
            invalidate();  // (the part under the mouse)
            return false;
        }
        if (e.kind != draw::MouseKind::Up || e.button != 0) return e.kind == draw::MouseKind::Down && e.button == 0 && onTool(e.pt);
        for (const auto& [r, code] : tools_.swatches)
            if (r.contains(e.pt)) return applyCode(code), true;
        if (spec_.align && spec_.multiline)
            for (int k = 0; k < 3; ++k)
                if (tools_.align[k].contains(e.pt)) return align(k), true;
        for (int k = 0; k < 2; ++k)
            if (tools_.seg[k].contains(e.pt)) return setRaw(k == 1), true;
        return false;
    }
    draw::Cursor cursor(draw::Point pt) override { return onTool(pt) ? draw::Cursor::Hand : draw::Cursor::Arrow; }
    std::wstring tip(draw::Point pt) override {
        for (const auto& [r, t] : tips_)
            if (r.contains(pt)) return t;
        return {};
    }
    bool hoverTip() const override { return true; }

private:
    bool hot(draw::Rect r) const { return host() && r.contains(host()->mouse(*this)); }
    bool onTool(draw::Point pt) const {
        for (const auto& [r, t] : tips_)
            if (r.contains(pt)) return true;
        return false;
    }

    // The box at the top (the window's height less the tool row), the text boxes inside its line: the Preview's text
    // 9 px from the frame's left and 6 px from its top (the RichEdit box's place and its inner margins); Raw's 7 px from
    // the left (the edit box's margin).
    void place() {
        const int toolsH = toolsHeight();
        frame_ = {0, 0, width(), std::max(scale(stardraft::kit::kFieldH), height() - toolsH - scale(4))};
        const draw::Rect in{frame_.left + 1, frame_.top + 1, frame_.right - 1, frame_.bottom - 1};
        rich_.setFrame({in.left + scale(2), in.top + scale(2), in.right - scale(2), in.bottom - scale(2)});
        raw_.setFrame({in.left + scale(4), in.top, in.right - scale(4), in.bottom - 1});  // (inside the Raw box's own line)
    }
    // The tool row's height for the width (as gameTextToolLayout measures it, with the last measured text widths).
    int toolsHeight() const {
        const int rowH = scale(kGameTextRowH), s = scale(kGameTextSwatch), gap = scale(kGameTextGap);
        int right = width() - (measured_[0] + scale(18)) - (measured_[1] + scale(18)) - scale(10);
        if (spec_.align && spec_.multiline) right -= 3 * scale(22) + scale(10);
        int x = 0, rows = 1, rowRight = right;
        for (size_t k = 0; k < gameTextSwatches(spec_).size(); ++k) {
            if (x > 0 && x + s > rowRight) x = 0, ++rows, rowRight = width();
            x += s + gap;
        }
        return rows * rowH;
    }

    // Bytes into the Preview: the runs' codes, the lines' alignments.
    void fill(const std::string& bytes) {
        const bool utf8 = gameTextIsUtf8(bytes);
        std::wstring text;
        std::vector<uint8_t> codes, aligns;
        unsigned char code = 0;
        std::string piece;
        auto flush = [&] {
            const std::wstring w = gameTextDecode(piece, utf8);
            text += w, codes.insert(codes.end(), w.size(), code), piece.clear();
        };
        size_t i = 0;
        while (i <= bytes.size()) {
            size_t end = bytes.find('\n', i);
            if (end == std::string::npos) end = bytes.size();
            size_t lineEnd = end;
            if (lineEnd > i && bytes[lineEnd - 1] == '\r') --lineEnd;
            int align = 0;
            flush();
            for (size_t k = i; k < lineEnd; ++k) {
                const unsigned char c = static_cast<unsigned char>(bytes[k]);
                if (c == 0x12 || c == 0x13) align = c == 0x13 ? 1 : 2;  // the line's alignment (the last one counts)
                else if (c == '\r' || c == 0) continue;
                else if (c < 0x20 && c != '\t') flush(), code = c;
                else piece += char(c);
            }
            flush();
            aligns.push_back(uint8_t(align));
            if (end >= bytes.size()) break;
            text += L'\n', codes.push_back(code);
            i = end + 1;
        }
        startCode_ = text.empty() ? code : codes.empty() ? 0 : codes[0];  // typing at the start: the first run's code
        text_ = text, codes_ = std::move(codes), aligns_ = std::move(aligns);
        pending_ = 0xFF;
        rich_.setText(text);
        rich_.setLineAligns(aligns_);
        restyle();
    }
    // A text with values into the Preview: the words with their codes, each value a chip (its token kept in tokens_), a
    // code that is no color (hidden text 0B and 14, others) a chip too, a player's color as its code (as the Windows
    // pop-out's rich box, main.cpp: richSetText).
    void fillValues(const std::string& raw) {
        const bool utf8 = gameTextIsUtf8(raw);
        std::vector<stardraft::kit::RichPart> parts;
        std::wstring text, piece;
        std::vector<uint8_t> codes, aligns{0};
        unsigned char code = 0;
        auto flushText = [&] {
            if (piece.empty()) return;
            if (!parts.empty() && !parts.back().chip) parts.back().text += piece;
            else parts.push_back(stardraft::kit::RichPart::plain(piece));
            piece.clear();
        };
        auto addChars = [&](const std::wstring& w) { piece += w, text += w, codes.insert(codes.end(), w.size(), code); };
        auto addChip = [&](const std::wstring& label, const std::string& token) {
            flushText();
            tokens_.push_back(token);
            parts.push_back(stardraft::kit::RichPart::of({label, 0, uint64_t(tokens_.size())}));
            text += kChipMark, codes.push_back(code);
        };
        for (const TriggerTextPart& part : triggerTextParts(raw)) {
            if (part.kind == TriggerTextPart::Kind::Words) {
                std::string run;
                auto flushRun = [&] {
                    if (!run.empty()) addChars(gameTextDecode(run, utf8)), run.clear();
                };
                for (const char ch : part.text) {
                    const unsigned char u = static_cast<unsigned char>(ch);
                    if (u == '\r' || u == 0) continue;
                    if (u == '\n') {
                        flushRun(), addChars(L"\n"), aligns.push_back(0);
                        continue;
                    }
                    if (u == 0x12 || u == 0x13) {  // the line's alignment (the last one counts)
                        aligns.back() = u == 0x13 ? 1 : 2;
                        continue;
                    }
                    if (u < 0x20 && u != '\t') {
                        flushRun();
                        if (u == 1 || u == 2 || gameTextColor(u, 0xFFFFFFFF) != 0xFFFFFFFF) {  // a color: what follows has it
                            code = u;
                            continue;
                        }
                        wchar_t name[16];
                        std::swprintf(name, 16, L"code %02X", unsigned(u));
                        addChip(u == 0x0B ? std::wstring(L"hidden text") : u == 0x14 ? std::wstring(L"hidden text (14)") : std::wstring(name), std::string(1, ch));
                        continue;
                    }
                    run += ch;
                }
                flushRun();
                continue;
            }
            if (part.kind == TriggerTextPart::Kind::Expression && part.format == "c")
                if (const unsigned char pc = gameTextPlayerCode(part.text)) {  // a player's color: what follows has it
                    code = pc;
                    continue;
                }
            const std::wstring label = chipLabel ? chipLabel(part) : std::wstring();
            if (!label.empty()) addChip(label, triggerTextJoin({part}));
        }
        flushText();
        startCode_ = text.empty() ? code : codes.empty() ? 0 : codes[0];
        text_ = text, codes_ = std::move(codes), aligns_ = std::move(aligns);
        pending_ = 0xFF;
        rich_.setParts(std::move(parts));
        rich_.setLineAligns(aligns_);
        restyle();
    }
    // The Preview of a text with values as its text: the words (braces doubled), each run's code where it changes (a
    // player's color as {P2:c}), each value's token (in the color before it), each aligned line's code at its start.
    std::string readValues() const {
        std::string out;
        unsigned char cur = 0;
        bool lineStart = true;
        size_t para = 0, chip = 0;
        std::wstring words;
        auto flush = [&] { out += narrow(words), words.clear(); };
        auto style = [&](unsigned char want) {
            const bool normal = want == 0;
            if (normal ? cur <= 1 : want == cur) return;
            flush();
            if (want > kGameTextPlayerCode) out += "{" + gameTextPlayerName(want) + ":c}";
            else out += char(normal ? 1 : want);
            cur = normal ? 1 : want;
        };
        for (size_t i = 0; i < text_.size(); ++i) {
            const wchar_t ch = text_[i];
            if (ch == L'\n') {
                flush(), out += crlf_ ? "\r\n" : "\n", lineStart = true, ++para;
                continue;
            }
            if (lineStart) {
                lineStart = false;
                const uint8_t a = para < aligns_.size() ? aligns_[para] : 0;
                if (a == 1 || a == 2) flush(), out += char(a == 1 ? 0x13 : 0x12);
            }
            const unsigned char want = i < codes_.size() ? codes_[i] : 0;
            if (ch == kChipMark) {
                const std::string token = valueToken(chip++);
                if (token.size() > 1) style(want);  // a value takes the color before it (a code chip is its own byte)
                flush(), out += token;
                continue;
            }
            style(want);
            if (ch == L'{') words += L"{{";
            else if (ch == L'}') words += L"}}";
            else words += ch;
        }
        flush();
        return out;
    }
    // The field's content with each chip as one kChipMark (the field's positions: a chip is one).
    std::wstring posText() const {
        std::wstring t;
        for (const auto& p : rich_.parts()) {
            if (p.chip) t += kChipMark;
            else t += p.text;
        }
        return t;
    }
    static constexpr wchar_t kChipMark = L'\xFFFC';
    std::vector<std::string> tokens_;  // a chip's token by its id - 1 (a text with values)

    // The Preview as bytes: each run's code where it changes, each aligned line's code at its start.
    std::string read() const {
        if (spec_.values) return readValues();
        std::string out;
        unsigned char cur = 0;  // the code in force (0: none yet)
        bool lineStart = true;
        size_t para = 0;
        std::wstring words;
        auto flush = [&] { out += narrow(words), words.clear(); };
        for (size_t i = 0; i < text_.size(); ++i) {
            const wchar_t ch = text_[i];
            if (ch == L'\n') {
                flush(), out += crlf_ ? "\r\n" : "\n", lineStart = true, ++para;
                continue;
            }
            if (lineStart) {  // the line's alignment
                lineStart = false;
                const uint8_t a = para < aligns_.size() ? aligns_[para] : 0;
                if (a == 1 || a == 2) flush(), out += char(a == 1 ? 0x13 : 0x12);
            }
            // The run's code, before its first character
            const unsigned char want = i < codes_.size() ? codes_[i] : 0;
            const bool normal = want == 0;
            if (normal ? cur > 1 : want != cur) {
                flush();
                out += char(normal ? 1 : want);
                cur = normal ? 1 : want;
            }
            words += ch;
        }
        flush();
        return out;
    }

    // The code new text at `p` takes: a swatch's code waiting there, else the character's before it (the first one's
    // at the start).
    unsigned char typingCode(size_t p) const {
        if (pending_ != 0xFF && pendingAt_ == p) return pending_;
        if (p > 0 && p - 1 < codes_.size()) return codes_[p - 1];
        return codes_.empty() ? startCode_ : codes_[0];
    }
    // The field's text changed: the codes and the alignments follow it.
    void richChanged() {
        const std::wstring now = spec_.values ? posText() : rich_.plainText();
        const std::wstring& old = text_;
        size_t p = 0;
        while (p < old.size() && p < now.size() && old[p] == now[p]) ++p;
        size_t s = 0;
        while (s < old.size() - p && s < now.size() - p && old[old.size() - 1 - s] == now[now.size() - 1 - s]) ++s;
        const size_t oldEnd = old.size() - s, newEnd = now.size() - s;
        // The new characters: the first replaced one's code (a selection typed over), else the code at the caret.
        const unsigned char code = oldEnd > p && p < codes_.size() ? codes_[p] : typingCode(p);
        const size_t para = size_t(std::count(old.begin(), old.begin() + p, L'\n'));
        const size_t gone = size_t(std::count(old.begin() + p, old.begin() + oldEnd, L'\n'));
        const size_t added = size_t(std::count(now.begin() + p, now.begin() + newEnd, L'\n'));
        codes_.erase(codes_.begin() + std::min(p, codes_.size()), codes_.begin() + std::min(oldEnd, codes_.size()));
        codes_.insert(codes_.begin() + std::min(p, codes_.size()), newEnd - p, code);
        if (aligns_.size() < para + 1) aligns_.resize(para + 1, 0);
        const uint8_t a = aligns_[para];
        aligns_.erase(aligns_.begin() + std::min(para + 1, aligns_.size()), aligns_.begin() + std::min(para + 1 + gone, aligns_.size()));
        aligns_.insert(aligns_.begin() + std::min(para + 1, aligns_.size()), added, a);
        text_ = now;
        if (text_.empty()) startCode_ = code;
        pending_ = 0xFF;
        rich_.setLineAligns(aligns_);
        restyle();
        changed();
    }
    // The spans of the codes' colors.
    void restyle() {
        std::vector<stardraft::kit::Span> spans;
        for (size_t i = 0; i < codes_.size();) {
            size_t j = i + 1;
            while (j < codes_.size() && codes_[j] == codes_[i]) ++j;
            if (spec_.values && codes_[i] > kGameTextPlayerCode) {  // a player's color: dots under it
                spans.push_back({i, j - i, gameTextPlayerColor(codes_[i], normal_), false, false, true});
                i = j;
                continue;
            }
            const GameTextLook look = gameTextLook(spec_.palette, codes_[i], normal_);
            spans.push_back({i, j - i, look.color, false, look.hidden, look.unknown});
            i = j;
        }
        rich_.setSpans(std::move(spans));
    }
    // The code at the caret (the swatch with a ring): the selection's (0xFF: several), else the text typed next's.
    unsigned char caretCode() const {
        const size_t a = std::min(rich_.caret(), rich_.anchor()), b = std::max(rich_.caret(), rich_.anchor());
        unsigned char c;
        if (a < b && a < codes_.size()) {
            c = codes_[a];
            for (size_t i = a; i < b && i < codes_.size(); ++i)
                if (codes_[i] != c) return 0xFF;
        } else {
            c = typingCode(a);
        }
        if (spec_.values && c > kGameTextPlayerCode) return kGameTextPlayerSwatch;
        return c ? c : 1;
    }
    int caretAlign() const {
        const size_t p = rich_.caretParagraph();
        return p < aligns_.size() ? aligns_[p] : 0;
    }

    // Raw: a code in at the selection (or at the start of the caret's line), through the bytes, so that the text around
    // it keeps its meaning (a "<" before it stays a "<").
    void rawInsert(unsigned char code, bool lineStart) {
        const std::wstring v = raw_.text();
        auto& m = raw_.model();
        size_t from = m.selectionStart(), to = m.selectionEnd();
        if (lineStart) from = to = m.lineStart(from);
        from = std::min(from, v.size()), to = std::clamp(to, from, v.size());
        const std::string before = gameTextFromRaw(v.substr(0, from), crlf_) + char(code), after = gameTextFromRaw(v.substr(to), crlf_);
        const std::wstring now = gameTextToRaw(before + after, spec_.multiline);
        const size_t caret = gameTextToRaw(before, spec_.multiline).size();  // (the tag's text does not depend on what follows it)
        raw_.setText(now);
        m.setSelection(caret, caret);
        raw_.focus();
        changed();
    }

    void changed() {
        invalidate();
        if (!setting_ && onChange) onChange();
    }
    void focusMoved(bool has) {
        invalidate();  // (the frame)
        if (!has && !hasFocusInside() && onFocusLost) onFocusLost();
    }

    GameTextSpec spec_;
    GameTextFocusField<stardraft::kit::RichField> rich_;
    GameTextFocusField<stardraft::kit::TextField> raw_;
    draw::Color normal_ = draw::rgb(184, 184, 232), back_ = draw::rgb(0x11, 0x11, 0x11);
    bool rawMode_ = false, crlf_ = false, setting_ = false, changedLook_ = false;
    std::string base_, baseRead_;  // what was put in, and how it read back then (unchanged: the bytes that were put in)
    std::wstring text_;            // the Preview's text as the codes know it
    std::vector<uint8_t> codes_;   // a code for each character of text_ (0: none)
    std::vector<uint8_t> aligns_;  // each paragraph's alignment (0 left, 1 center, 2 right)
    unsigned char startCode_ = 0;  // typing into an empty text (or at the start)
    unsigned char pending_ = 0xFF; // a swatch picked with no selection: the next text typed at pendingAt_ takes it
    size_t pendingAt_ = 0;
    draw::Rect frame_;
    int measured_[2] = {0, 0};
    GameTextToolLayout tools_;
    std::vector<std::pair<draw::Rect, std::wstring>> tips_;
};
