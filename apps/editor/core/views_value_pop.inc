// The value pop-out (portable, core.inc): a value is typed in a small window by the value, never in place. Enter or OK
// applies, Esc or Cancel leaves the value as it was. What it shows adapts to the value: its range, what a number means
// in the units players think in (frames as seconds, hit points as stored), and for numbers the variables that fit
// (double-click one to put it in). A text with values is the game text editor (core/views_game_text.inc) with each
// value a chip; a text the game shows has its colors (Preview and Raw).
//
// The model (the spec, the hint, the suggestions, the values to put in, a value's words) is the Windows pop-out's too
// (main.cpp: showValuePopup, its RichEdit and list box); ValuePopView is the pop-out as core views, which the SDL
// program opens through the seam (shellEditValue, shellEditValueBelow). The Windows program keeps its native one for now.

struct ValuePopSpec {
    std::wstring title, help;
    bool inRepeat = false, inPlayerLoop = false;  // what the block sits in: counter and current player exist there
    std::string type;                      // the value's type
    const TriggerProperty* prop = nullptr;  // when the value is a property's
    long long min = 0, max = -1;
    bool constantOnly = false;   // a number known when the map builds (a variable's size or starting value)
    bool constantsNamed = false;  // ... which may be a constant, by name (a list's size)
    bool* wantsGame = nullptr;    // set: a "From the game..." button, which closes the pop-out and sets it
    bool insertValues = false;    // a text with values: an "Insert value" button
    bool colorsOnly = false;      // the game's text with its colors, as rich text (no values; braces are braces)
    bool code = false;            // epScript: a box of several lines in the code font (Enter: a new line; OK applies)
    std::string initial;
    // A property block's .dat value: shown in a picture with the value it would have (set, add or subtract).
    std::string previewDat, previewField;
    int previewEntry = -1, previewMod = 0;  // 0 set to, 1 add, 2 subtract
};
// A row of the suggestions: a heading, a note, or something to put in (its name, kind, what it is, what goes in).
struct ValueSuggestion {
    enum class Kind { Heading, Note, Item } kind = Kind::Item;
    std::wstring name, tag, about;
    std::wstring insert;
    int selectFrom = -1, selectLen = 0;  // what to select after putting it in (an array's index)
    bool dim = false;                    // fits the kind but not here (a constant out of range)
};

// Whether the value is typed as a number (an expression with the variables); the rest are typed as text.
inline bool valuePopIsNumber(const ValuePopSpec& spec) {
    const TriggerType* t = triggerType(spec.type);
    return spec.type != "DecimalSeconds" && (!t || t->picker == TriggerPicker::Number || t->picker == TriggerPicker::Unclear || triggerPlayerType(spec.type));
}

// What the typed text means (`hint`, `bad` when it is a problem); false when it cannot be used as it is.
inline bool valuePopHint(const ValuePopSpec& spec, bool number, const std::string& text, std::wstring& hint, bool& bad) {
    bad = false;
    if (spec.type == "DecimalSeconds") {  // seconds, with decimals (kept as milliseconds)
        char* end = nullptr;
        const double v = std::strtod(text.c_str(), &end);
        if (text.empty() || end == text.c_str() || *end || v < 0 || v > 2000000) return hint = L"Type a number of seconds, for example 2.5", bad = true, false;
        hint = std::to_wstring(std::lround(v * 1000)) + L" milliseconds";
        return true;
    }
    if (!number) {
        hint = spec.type == "Code"    ? L"Build puts this code into generated\\triggers.eps unchanged."
               : spec.type == "Sound" ? (text.empty() ? L"The path of a sound file in the map (sound\\\x2026\\name.wav)" : L"Build makes sure that this file is in the map")
                                      : L"";  // (no letter count: a name or a text needs none)
        return true;
    }
    std::map<std::string, const TriggerNode*> names = gTE.set.variables();
    if (spec.constantOnly)
        std::erase_if(names, [&](const auto& kv) { return !spec.constantsNamed || kv.second->variable.kind != TriggerVariable::Kind::Constant; });
    const TriggerExprResult r = triggerExpression(text.empty() ? "0" : text, names, 8);
    if (!r.ok) return hint = widen(r.error), bad = true, false;
    if (spec.constantOnly && !r.constant) return hint = spec.constantsNamed ? L"a number or a constant here" : L"a plain number here", bad = true, false;
    if (!r.constant) {
        hint = L"The game calculates this value while it runs";
        return true;
    }
    if (spec.max >= spec.min && (r.value < spec.min || r.value > spec.max))
        return hint = L"Use a value from " + std::to_wstring(spec.min) + L" to " + std::to_wstring(spec.max), bad = true, false;
    auto seconds = [](double s) {
        wchar_t b[48];
        if (s >= 60) std::swprintf(b, 48, L"%d:%02d", int(s) / 60, int(s) % 60);
        else std::swprintf(b, 48, L"%.2g seconds", s);
        return std::wstring(b);
    };
    const std::string label = spec.prop ? spec.prop->label : "";
    std::wstring h;
    if (label.find("frames") != std::string::npos) h = L"\x2248 " + seconds(double(r.value) / 24.0) + L" at Fastest (24 frames a second)";
    else if (spec.type == "Seconds") h = L"= " + seconds(double(r.value)) + L" of game time";
    else if (spec.type == "Milliseconds") h = L"= " + seconds(double(r.value) / 1000.0);
    else if (spec.type == "Count" && r.value == 0) h = L"0 means all that match the other values";
    else if (spec.type == "Percent") h = std::to_wstring(r.value) + L"%";
    if (spec.prop && spec.prop->scale > 1)
        h += (h.empty() ? L"" : L"; ") + std::wstring(L"the game keeps it as ") + std::to_wstring(r.value * spec.prop->scale) + L" (\x00D7" +
             std::to_wstring(spec.prop->scale) + L")";
    hint = h;
    return true;
}

// Where the word at the caret starts (letters, digits and _ before it).
inline size_t valuePopWordStart(const std::wstring& text, size_t caret) {
    size_t start = std::min(caret, text.size());
    while (start > 0 && (iswalnum(text[start - 1]) || text[start - 1] == L'_')) --start;
    return start;
}
// What fits, for the word at the caret: the project's variables, and what the loops around the block give.
inline std::vector<ValueSuggestion> valuePopSuggestions(const ValuePopSpec& spec, const std::wstring& text, size_t caret) {
    const size_t start = valuePopWordStart(text, caret);
    const std::wstring word = text.substr(start, std::min(caret, text.size()) - start);
    auto fits = [&](const std::wstring& name) { return name.rfind(word, 0) == 0; };
    std::vector<ValueSuggestion> rows, loop, vars;
    if (spec.inRepeat && fits(L"counter")) loop.push_back({ValueSuggestion::Kind::Item, L"counter", L"0, 1, 2 \x2026", L"", L"counter"});
    if (spec.inPlayerLoop && fits(L"current")) loop.push_back({ValueSuggestion::Kind::Item, L"current player", L"0\x2013" L"7", L"", L"current player"});
    if (!loop.empty()) {
        rows.push_back({ValueSuggestion::Kind::Heading, L"Loop"});
        rows.insert(rows.end(), loop.begin(), loop.end());
    }
    for (const auto& [name, node] : gTE.set.variables()) {
        const TriggerVariable& v = node->variable;
        const std::wstring n = widen(name);
        if (v.kind == TriggerVariable::Kind::Unit || !fits(n)) continue;
        if (spec.constantOnly && v.kind != TriggerVariable::Kind::Constant) continue;
        ValueSuggestion s;
        s.name = n, s.insert = n;
        switch (v.kind) {
        case TriggerVariable::Kind::Constant: {
            const long long value = std::atoll(v.initial.c_str());
            s.tag = L"constant", s.about = L"= " + std::to_wstring(value);
            if (spec.max >= spec.min && (value < spec.min || value > spec.max)) s.about += L"  \x00B7  out of range", s.dim = true;
            break;
        }
        case TriggerVariable::Kind::Number: s.tag = L"number"; break;
        case TriggerVariable::Kind::Flag: s.tag = L"flag"; break;
        case TriggerVariable::Kind::PerPlayer: s.tag = L"player array"; break;
        case TriggerVariable::Kind::Array:
            s.tag = L"list of " + std::to_wstring(v.size);
            s.about.clear();
            s.insert = n + L"[0]", s.selectFrom = int(n.size()) + 1, s.selectLen = 1;
            break;
        default: break;
        }
        if (!node->comment.empty()) s.about = widen(node->comment) + (s.about.empty() ? L"" : L"  \x00B7  " + s.about);
        vars.push_back(s);
    }
    if (!vars.empty()) {
        rows.push_back({ValueSuggestion::Kind::Heading, spec.constantOnly ? L"Constants" : L"Variables"});
        rows.insert(rows.end(), vars.begin(), vars.end());
    }
    if (rows.empty()) rows.push_back({ValueSuggestion::Kind::Note, L"", L"", word.empty() ? L"No variables" : L"No match"});
    return rows;
}

// A plain string's braces as a text with values has them ("{" is "{{"), and back.
inline std::string valuePopBracesToWords(const std::string& s) {
    std::string e;
    for (char ch : s) e += ch == '{' ? "{{" : ch == '}' ? "}}" : std::string(1, ch);
    return e;
}
inline std::string valuePopWordsToBraces(const std::string& s) {
    std::string plain;
    for (size_t i = 0; i < s.size(); ++i) {
        plain += s[i];
        if ((s[i] == '{' || s[i] == '}') && i + 1 < s.size() && s[i + 1] == s[i]) ++i;
    }
    return plain;
}

// What fits in a text (Insert value): the loop's values, the variables, a player's name, a text of the game, the
// values from the game and a calculation. The values: "\x01tbl" a text of the game, "\x01v:<id>" a value from the game,
// "\x01calc" a calculation, else the expression.
inline std::vector<ListPickItem> valuePopInsertItems(const ValuePopSpec& spec) {
    std::vector<ListPickItem> items;
    if (spec.inRepeat) items.push_back({L"counter", L"This loop", "counter"});
    if (spec.inPlayerLoop) items.push_back({L"current player", L"This loop", "current player"});
    for (const auto& [name, node] : gTE.set.variables()) {
        const auto k = node->variable.kind;
        if (k == TriggerVariable::Kind::Unit) continue;
        const std::string v = k == TriggerVariable::Kind::PerPlayer ? name + "[current player]"
                              : k == TriggerVariable::Kind::Array   ? name + "[0]"
                              : k == TriggerVariable::Kind::Text    ? name + ":s"
                                                                     : name;
        items.push_back({widen(k == TriggerVariable::Kind::Text ? name : v), k == TriggerVariable::Kind::Text ? L"Texts" : L"Variables", v});
    }
    items.push_back({L"the current player's name", L"Players", "name(current player):s"});
    items.push_back({L"A text of the game\x2026", L"Texts", "\x01" "tbl"});
    for (const auto& cat : TriggerDef::valueCategories())
        for (const auto& d : TriggerDef::all())
            if (d.value && d.category == cat && d.tier != "hidden") items.push_back({widen(d.title), widen(cat), "\x01v:" + d.id});
    items.push_back({L"A calculation\x2026", L"Calculations", "\x01" "calc"});
    return items;
}

// The values from the game, for a picker: "title   -> its sentence", by category.
inline std::vector<ListPickItem> valueBlockPickItems() {
    std::vector<ListPickItem> items;
    for (const auto& cat : TriggerDef::valueCategories())
        for (const auto& d : TriggerDef::all())
            if (d.value && d.category == cat && d.tier != "hidden") {
                std::wstring sentence;
                for (size_t i = 0; i < d.sentence.size();) {
                    if (d.sentence[i] == '{') {
                        const size_t end = d.sentence.find('}', i);
                        const int pi = d.param(d.sentence.substr(i + 1, end - i - 1));
                        sentence += L"(" + (pi >= 0 ? trigTypeNote(d.params[size_t(pi)]) : L"?") + L")";
                        i = end + 1;
                        continue;
                    }
                    sentence += wchar_t(static_cast<unsigned char>(d.sentence[i++]));
                }
                items.push_back({widen(d.title) + L"   \x2192 " + sentence, widen(cat), d.id});
            }
    return items;
}

// A part of a value from the game in its words: where it is, and which of the value's values it is.
struct RichSeg {
    long from = 0, len = 0;
    int param = -1;
    bool block = false;  // a value from the game in it (its menu: another value, or a number)
};
// A value block as words: "kills of Zerg Zergling by Player 1"; `segs`: where its values are in them.
inline std::wstring trigPlainSentence(const TriggerCall& c, std::vector<RichSeg>* segs = nullptr) {
    const TriggerDef* d = TriggerDef::find(c.def);
    if (!d) return widen(c.def);
    std::wstring out;
    const std::string& s = d->sentence;
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '{') {
            const size_t end = s.find('}', i);
            const int p = end == std::string::npos ? -1 : d->param(s.substr(i + 1, end - i - 1));
            if (p >= 0) {
                const long at = long(out.size());
                const bool block = p < int(c.argBlocks.size()) && !c.argBlocks[size_t(p)].empty();
                if (block) out += L"(" + trigPlainSentence(c.argBlocks[size_t(p)][0]) + L")";
                else if (d->params[size_t(p)].type == "ButtonIndex") out += trigButtonIndexText(*d, c, p < int(c.args.size()) ? c.args[size_t(p)] : "", nullptr);
                else if (d->params[size_t(p)].type == "GameString") out += trigGameStringText(p < int(c.args.size()) ? c.args[size_t(p)] : "");
                else out += trigValueText(triggerParamType(*d, c.args, p), p < int(c.args.size()) ? c.args[size_t(p)] : "");
                if (segs) segs->push_back({at, long(out.size()) - at, p, block});
                i = end + 1;
                continue;
            }
        }
        out += wchar_t(static_cast<unsigned char>(s[i++]));
    }
    return out;
}

// --- The pop-out as core views -------------------------------------------------------------------------------------

// The layout is the Windows pop-out's (showValuePopup), in pixels at 100 %.
namespace valuePopLayout {
constexpr int kPad = 12, kTitleH = 26, kHelpH = 36, kHintH = 22, kBoxH = 28, kButtonW = 96, kSideButtonW = 150;
constexpr int kRowH = 26, kTwoLineRowH = 44, kListRowH = 28, kListMaxH = 220;
constexpr int kToolsH = 24;  // the game text editor's tool row (core/views_game_text.inc: one row at this width)
constexpr int kPreviewH = 22;  // the .dat value's line ("Marine   40  ->  45")
inline int width(const ValuePopSpec& spec, bool number) { return spec.insertValues || spec.colorsOnly ? 680 : number ? 460 : 520; }
}  // namespace valuePopLayout

class ValuePopView final : public draw::View {
public:
    explicit ValuePopView(const ValuePopSpec& spec) : spec_(spec), number_(valuePopIsNumber(spec)), rich_(spec.insertValues || spec.colorsOnly) {
        using namespace valuePopLayout;
        if (rich_) {
            GameTextSpec gs;
            gs.palette = TextPalette::Game, gs.multiline = true, gs.align = true;
            gs.values = spec.insertValues;  // (a plain string with colors: its braces are words)
            text_ = std::make_unique<GameTextView>(gs);
            text_->setColors(draw::rgb(184, 184, 232), draw::rgb(0x11, 0x11, 0x11));
            if (gs.values) {
                if (gTE.textValuesFor) values_ = *gTE.textValuesFor;
                text_->chipLabel = [this](const TriggerTextPart& part) { return partLabel(part); };
                text_->onValueClick = [this](size_t index, const std::string& token, draw::Rect box) { chipClicked(index, token, box); };
                text_->pickPlayerColor = [this](unsigned char& code) { return pickPlayer(code); };
            }
            text_->setBytes(spec.initial);
            text_->onChange = [this] { refresh(); };
            add(*text_, {0, 0, 0, 0});
        } else {
            field_.setMultiline(spec.code);
            if (spec.code) field_.setFont({draw::Face::Mono, 0});
            field_.setText(widen(spec.initial));
            field_.model().selectAll();
            field_.onChange = [this] { refresh(); };
            if (!spec.code) field_.onEnter = [this] { accept(); };
            field_.onEscape = [this] { finish(false); };
            add(field_, {0, 0, 0, 0});
        }
        ok_.setText(tr(L"OK")), ok_.setTone(stardraft::kit::Tone::Primary), ok_.onClick = [this] { accept(); };
        cancel_.setText(tr(L"Cancel")), cancel_.onClick = [this] { finish(false); };
        add(ok_, {0, 0, 0, 0});
        add(cancel_, {0, 0, 0, 0});
        if (spec.wantsGame && number_) {
            game_.setText(tr(L"From the game\x2026"));
            game_.onClick = [this] {
                *spec_.wantsGame = true;
                finish(false);
            };
            add(game_, {0, 0, 0, 0});
        }
        if (spec.insertValues) {
            insert_.setText(tr(L"Insert value"));  // (and a chevron, paintOver: not every font has the Windows label's U+25BE)
            insert_.onClick = [this] { insertValue(); };
            add(insert_, {0, 0, 0, 0});
        }
        // The list is as tall as what it can hold (headings, the variables, the loop's values), up to a limit.
        if (number_ && (!spec.constantOnly || spec.constantsNamed)) {
            int rows = 2 + int(spec.inRepeat) + int(spec.inPlayerLoop);
            for (const auto& [name, node] : gTE.set.variables()) rows += node->variable.kind != TriggerVariable::Kind::Unit;
            listH_ = std::clamp(rows * scale(kListRowH), scale(kListRowH), scale(kListMaxH));
            bar_.onScroll = [this](int pos) {
                top_ = pos;
                invalidate();
            };
            add(bar_, {0, 0, 0, 0});
        }
        refresh();
    }

    // The window's outer size (its border is the host's).
    draw::Point outerSize() const {
        using namespace valuePopLayout;
        const int boxRows = spec_.code ? 8 : 1;
        int h = scale(kPad) + scale(kTitleH) + (spec_.help.empty() ? 0 : scale(kHelpH));
        if (rich_) h += scale(kBoxH) * 6 + scale(4) + scale(kToolsH) + scale(6);
        else h += scale(kBoxH) * boxRows + scale(6) + scale(kHintH);
        if (listH_) h += scale(4) + listH_;
        if (spec_.previewEntry >= 0) h += scale(kPad) + scale(kPreviewH);
        h += scale(kPad) + scale(stardraft::kit::kButtonH) + scale(kPad) + 2;
        return {scale(valuePopLayout::width(spec_, number_)), h};
    }
    // Where the window is (a point in the program's window; the pickers it opens go below their place in it).
    void setPlace(draw::Point at) { place_ = at; }

    bool accepted() const { return accepted_; }
    bool done() const { return done_; }
    const std::string& result() const { return result_; }
    // The value from the game's values of a text with values, as the pop-out left them.
    const std::vector<TriggerCall>& values() const { return values_; }
    // Tab (+1) or Shift+Tab (-1) applied the value and asks for the next one; 0 none.
    int advance() const { return advance_; }
    // The view with the keyboard when the window opens.
    draw::View& focusView() { return rich_ ? static_cast<draw::View&>(text_->preview()) : field_; }
    // The text box (the self-tests type into it); the game text editor of a text with colors or values.
    stardraft::kit::TextField& field() { return field_; }
    GameTextView* textView() { return text_.get(); }
    // Runs when the pop-out closes itself (OK, Cancel, Esc, From the game): the host closes its window.
    std::function<void()> onDone;
    // The texts of the game for "A text of the game..." (the Strings tab's entries); empty: the item is left out.
    std::function<std::vector<ListPickItem>()> gameTexts;

    // OK: the value, when it can be used as it is.
    void accept() {
        if (!check()) return invalidate();
        if (rich_) {
            result_ = text_->bytes();
            if (spec_.insertValues) gTE.textValuesOut = values_;
        } else {
            result_ = narrow(field_.text());
            if (spec_.code) std::erase(result_, '\r');  // the lines as \n
        }
        accepted_ = true;
        finish(true);
    }
    void finish(bool ok) {
        if (done_) return;
        done_ = true, accepted_ = ok && accepted_;
        if (onDone) onDone();
    }

    bool key(const draw::KeyEvent& e) override {
        if (!e.down) return false;
        if (e.key == draw::Key::Escape) return finish(false), true;
        if (e.key == draw::Key::Tab && !rich_ && !spec_.code) {  // Tab applies and asks for the next value (Shift+Tab: the one before)
            accept();
            if (accepted_) advance_ = (e.mods & draw::kShift) ? -1 : 1;
            return true;
        }
        if (e.key == draw::Key::Enter && !rich_ && !spec_.code) return accept(), true;
        if (listH_ && (e.key == draw::Key::Down || e.key == draw::Key::Up)) {  // the suggestions: the next row to put in
            const int step = e.key == draw::Key::Down ? 1 : -1;
            for (int i = sel_ + step; i >= 0 && i < int(rows_.size()); i += step)
                if (rows_[size_t(i)].kind == ValueSuggestion::Kind::Item) return select(i), true;
            return true;
        }
        return false;
    }

    void resized() override { layout(); }

    void paint(draw::Canvas& c) override {
        using namespace valuePopLayout;
        c.fill({0, 0, width(), height()}, kBg);
        if (spec_.insertValues) insert_.setEnabled(!text_->isRaw());  // Insert value makes a chip: Preview only
        // The title (bold) and the help under it
        const draw::Font of = c.setFont(c.font({draw::Face::Ui, 0, true}));
        c.text(titleRect_, spec_.title, kText, draw::kMiddle | draw::kEllipsis);
        c.setFont(of);
        if (!spec_.help.empty())
            c.text({titleRect_.left, titleRect_.bottom, titleRect_.right, titleRect_.bottom + scale(kHelpH)}, spec_.help, kDim, draw::kWrap | draw::kEllipsis);
        // The box's line (the kit field draws its own; the game text editor too)
        if (!rich_) c.text(hintRect_, hint_, hintBad_ ? kBad : kGood, draw::kMiddle | draw::kEllipsis);
        if (listH_) paintList(c);
        if (spec_.previewEntry >= 0) paintPreview(c);
    }

    void paintOver(draw::Canvas& c) override {
        if (!spec_.insertValues) return;
        const draw::Rect b = insert_.frame();
        const int is = scale(12), x = b.right - scale(10) - is, y = (b.top + b.bottom - is) / 2;
        c.icon({x, y, x + is, y + is}, int(UiIcon::ChevronDown), text_ && text_->isRaw() ? kDim : kText, is);
    }

    bool mouse(const draw::MouseEvent& e) override {
        using draw::MouseKind;
        if (!listH_ || !list_.contains(e.pt)) return false;
        if (e.kind == MouseKind::Wheel) return scrollTo(top_ - int(e.wheel * scale(kListWheelStep))), true;
        const int i = rowAt(e.pt);
        if (e.kind == MouseKind::Down && e.button == 0) {
            if (i >= 0 && rows_[size_t(i)].kind == ValueSuggestion::Kind::Item) select(i);
            return true;
        }
        if (e.kind == MouseKind::DoubleClick && e.button == 0) {  // a double click puts it in
            if (i >= 0 && rows_[size_t(i)].kind == ValueSuggestion::Kind::Item) select(i), putIn();
            return true;
        }
        return e.kind != MouseKind::Move;
    }

    // The suggestion row `i` is put in at the caret, in place of the word there (the self-tests).
    void putIn(int i) {
        select(i);
        putIn();
    }
    const std::vector<ValueSuggestion>& suggestions() const { return rows_; }
    const std::wstring& hint() const { return hint_; }

private:
    static constexpr int kListWheelStep = 3 * valuePopLayout::kRowH;  // (3 rows a notch, as the list box)

    void layout() {
        using namespace valuePopLayout;
        const int w = width(), pad = scale(kPad), eh = scale(kBoxH), bw = scale(kButtonW), bh = scale(stardraft::kit::kButtonH);
        int y = pad;
        titleRect_ = {pad, y, w - pad, y + scale(kTitleH)};
        y += scale(kTitleH) + (spec_.help.empty() ? 0 : scale(kHelpH));
        if (rich_) {
            const int th = eh * 6 + scale(4) + scale(kToolsH);
            text_->setFrame({pad, y, w - pad, y + th});
            y += th + scale(6);
        } else {
            const int rows = spec_.code ? 8 : 1;
            field_.setFrame({pad, y, w - pad - 2, y + eh * rows});
            y += eh * rows + scale(6);
            hintRect_ = {pad, y, w - pad, y + scale(kHintH)};
            y += scale(kHintH);
        }
        if (listH_) {
            y += scale(4);
            list_ = {pad, y, w - pad - 2, y + listH_};
            y += listH_;
            syncBar();
        }
        if (spec_.previewEntry >= 0) y += pad, previewRect_ = {pad, y, w - pad - 2, y + scale(kPreviewH)}, y += scale(kPreviewH);
        y += pad;
        ok_.setFrame({w - pad - 2 * bw - scale(8) - 2, y, w - pad - bw - scale(8) - 2, y + bh});
        cancel_.setFrame({w - pad - bw - 2, y, w - pad - 2, y + bh});
        game_.setFrame({pad, y, pad + scale(kSideButtonW), y + bh});
        insert_.setFrame({pad, y, pad + scale(kSideButtonW), y + bh});
    }

    bool check() {
        const std::string text = rich_ ? std::string() : narrow(field_.text());
        const bool ok = rich_ ? true : valuePopHint(spec_, number_, text, hint_, hintBad_);
        if (rich_) hint_.clear(), hintBad_ = false;
        return ok;
    }
    void refresh() {
        check();
        if (listH_) {
            rows_ = valuePopSuggestions(spec_, field_.text(), field_.model().selectionStart());  // (as the Windows list: the selection's start)
            sel_ = -1, top_ = 0;
            syncBar();
        }
        invalidate();
    }

    // --- The suggestions ----------------------------------------------------------------------------------------
    int rowHeight(size_t i) const {
        const bool twoLines = rows_[i].kind == ValueSuggestion::Kind::Item && !rows_[i].about.empty();
        return scale(twoLines ? valuePopLayout::kTwoLineRowH : valuePopLayout::kRowH);
    }
    int contentH() const {
        int h = 0;
        for (size_t i = 0; i < rows_.size(); ++i) h += rowHeight(i);
        return h;
    }
    draw::Rect rowRect(size_t i) const {
        int y = list_.top - top_;
        for (size_t k = 0; k < i; ++k) y += rowHeight(k);
        const int right = barShown() ? list_.right - scale(stardraft::kit::kScrollBarW) : list_.right;
        return {list_.left, y, right, y + rowHeight(i)};
    }
    int rowAt(draw::Point p) const {
        if (barShown() && p.x >= list_.right - scale(stardraft::kit::kScrollBarW)) return -1;
        for (size_t i = 0; i < rows_.size(); ++i)
            if (rowRect(i).contains(p)) return int(i);
        return -1;
    }
    bool barShown() const { return contentH() > list_.height(); }
    void syncBar() {
        bar_.setVisible(barShown());
        bar_.setFrame({list_.right - scale(stardraft::kit::kScrollBarW), list_.top, list_.right, list_.bottom});
        bar_.setRange(contentH(), list_.height());
        bar_.setPos(top_);
    }
    void scrollTo(int top) {
        top = std::clamp(top, 0, std::max(0, contentH() - list_.height()));
        if (top == top_) return;
        top_ = top;
        bar_.setPos(top_);
        invalidate();
    }
    void select(int i) {
        sel_ = i;
        const draw::Rect r = rowRect(size_t(i));
        if (r.top < list_.top) scrollTo(top_ - (list_.top - r.top));
        else if (r.bottom > list_.bottom) scrollTo(top_ + (r.bottom - list_.bottom));
        invalidate();
    }
    // The selected suggestion in place of the word at the caret (an array's index selected after it).
    void putIn() {
        if (sel_ < 0 || sel_ >= int(rows_.size()) || rows_[size_t(sel_)].kind != ValueSuggestion::Kind::Item) return;
        const ValueSuggestion row = rows_[size_t(sel_)];
        auto& m = field_.model();
        const size_t caret = m.caret(), start = valuePopWordStart(field_.text(), caret);
        m.setSelection(start, caret);
        m.insert(row.insert);
        if (row.selectFrom >= 0) m.setSelection(start + size_t(row.selectFrom), start + size_t(row.selectFrom + row.selectLen));
        field_.focus();
        field_.invalidate();
        refresh();
    }
    // A row: on the window's ground, the selected row a rounded pill (as the pickers); a heading small and dim; an item
    // its name (as it is typed) and its kind, and below your description (or a constant's value) when there is one.
    void paintList(draw::Canvas& c) {
        c.pushClip(list_);
        for (size_t i = 0; i < rows_.size(); ++i) {
            const draw::Rect rr = rowRect(i);
            if (rr.bottom < list_.top || rr.top > list_.bottom) continue;
            const ValueSuggestion& row = rows_[i];
            if (int(i) == sel_ && row.kind == ValueSuggestion::Kind::Item)
                c.roundBox({rr.left + scale(2), rr.top + scale(1), rr.right - scale(2), rr.bottom - scale(1)}, kSel, kSel, float(scale(6)));
            const draw::Rect r{rr.left + scale(10), rr.top, rr.right - scale(10), rr.bottom};
            if (row.kind == ValueSuggestion::Kind::Heading) {
                const draw::Font of = c.setFont(c.font({draw::Face::Ui, -(shellUiFontLfHeight() * 5 / 6)}));  // (ui::smallFont)
                c.text({r.left, r.top + scale(6), r.right, r.bottom}, row.name, kDim, draw::kMiddle);
                c.setFont(of);
                continue;
            }
            if (row.kind == ValueSuggestion::Kind::Note) {
                c.text(r, row.about, kDim, draw::kMiddle | draw::kEllipsis);
                continue;
            }
            const draw::Rect top = row.about.empty() ? r : draw::Rect{r.left, r.top + scale(4), r.right, r.top + scale(22)};
            c.text(top, row.name, row.dim ? kDim : kAccent, draw::kMiddle);
            c.text(top, row.tag, kDim, draw::kMiddle | draw::kRight);
            if (!row.about.empty()) c.text({r.left, r.top + scale(22), r.right, r.bottom - scale(2)}, row.about, kDim, draw::kEllipsis);
        }
        c.popClip();
    }
    // The .dat value the block changes: the entry, its value now and the value it would have.
    void paintPreview(draw::Canvas& c) {
        const int scaleBy = spec_.prop ? std::max(1, spec_.prop->scale) : 1;
        const long long now = datValue(spec_.previewDat, spec_.previewField, spec_.previewEntry);
        const std::wstring text = field_.text();
        wchar_t* end = nullptr;
        const long long typed = std::wcstoll(text.c_str(), &end, 0);
        const bool known = !text.empty() && end && *end == 0;
        long long after = now;
        if (known) after = spec_.previewMod == 1 ? now + typed * scaleBy : spec_.previewMod == 2 ? now - typed * scaleBy : typed * scaleBy;
        if (const DatField* f = datFieldNamed(spec_.previewDat, spec_.previewField)) after = std::clamp<long long>(after, 0, f->maxValue());
        std::wstring caption = std::wstring(entryName(spec_.previewDat, spec_.previewEntry)) + L"   " + std::to_wstring(now / scaleBy);
        caption += known ? L"  \x2192  " + std::to_wstring(after / scaleBy) : L"  (set while the game runs)";
        c.text(previewRect_, caption, kDim, draw::kMiddle | draw::kEllipsis);
    }

    // --- A text with values -------------------------------------------------------------------------------------
    // A chip's words: a value from the game in its words, the others as the trigger pane shows them.
    std::wstring partLabel(const TriggerTextPart& part) {
        if (part.kind == TriggerTextPart::Kind::Expression) return textChipLabel(part.text, part.format);
        if (part.kind == TriggerTextPart::Kind::GameValue && part.value >= 0 && part.value < int(values_.size()))
            return trigPlainSentence(values_[size_t(part.value)]) + textFormatSuffix(part.format);
        return {};
    }
    // A list picker under `box` (this view's coordinates): the picked value, or "" when none.
    std::string pick(const std::wstring& title, std::vector<ListPickItem> items, const std::string& current, draw::Rect box) {
        ListPickView v(title, std::move(items), current);
        const draw::Point o = origin();
        if (!runListPick(v, {place_.x + o.x + box.left, place_.y + o.y + box.bottom + scale(2)})) return {};
        return v.result();
    }
    // Insert value: a value at the caret, as a chip.
    void insertValue() {
        const draw::Rect box = insert_.frame();
        std::vector<ListPickItem> items = valuePopInsertItems(spec_);
        std::vector<ListPickItem> texts = gameTexts ? gameTexts() : std::vector<ListPickItem>{};
        if (texts.empty()) std::erase_if(items, [](const ListPickItem& it) { return it.value == "\x01" "tbl"; });
        const std::string picked = pick(tr(L"Insert value"), std::move(items), "", box);
        std::string token;
        std::wstring label;
        if (picked.empty()) return;
        if (picked == "\x01" "calc") {  // typed, with the variables offered as it is typed
            ValuePopSpec spec;
            spec.type = "Number", spec.title = L"A calculation   for example gold * 2 + 1";
            spec.inRepeat = spec_.inRepeat, spec.inPlayerLoop = spec_.inPlayerLoop;
            std::string out;
            if (!nested(spec, box, out) || out.empty()) return;
            token = "{" + out + "}", label = widen(out);
        } else if (picked == "\x01" "tbl") {  // a text of the game (stat_txt.tbl), as the Strings tab lists them
            const std::string n = pick(tr(L"Search: a text of the game"), std::move(texts), "", box);
            if (!trigIsNumberText(n) || std::atoi(n.c_str()) <= 0) return;
            token = "{tbl(" + n + "):s}", label = textChipLabel("tbl(" + n + ")", "s");
        } else if (picked.rfind("\x01v:", 0) == 0) {  // a value from the game
            const TriggerDef* d = TriggerDef::find(picked.substr(3));
            if (!d || !d->value) return;
            values_.push_back(TriggerCall::make(*d));
            token = "{#" + std::to_string(values_.size()) + "}", label = trigPlainSentence(values_.back());
        } else {
            const auto parts = triggerTextParts("{" + picked + "}");
            token = "{" + picked + "}", label = parts.empty() ? widen(picked) : textChipLabel(parts[0].text, parts[0].format);
        }
        text_->insertValue(label, token);
    }
    // A value pop-out from this one (a calculation, a value in the text), under `box`.
    bool nested(const ValuePopSpec& spec, draw::Rect box, std::string& out);
    // A click on a chip: a code can only go; a value its menu (its value, how it shows, Remove).
    void chipClicked(size_t index, const std::string& token, draw::Rect box) {
        const auto parts = triggerTextParts(token);
        if (parts.empty() || parts[0].kind == TriggerTextPart::Kind::Words) {
            if (pick(tr(L"Code"), {{L"Remove", L"", "remove"}}, "", box) == "remove") text_->removeValue(index);
            return;
        }
        TriggerTextPart part = parts[0];
        const bool game = part.kind == TriggerTextPart::Kind::GameValue;
        std::vector<ListPickItem> items{{game ? L"Another value from the game\x2026" : L"Change the value\x2026", L"", "value"}};
        for (const auto& [code, name] : triggerTextFormats())
            if (code != "c") items.push_back({L"Show as " + widen(name), L"Show as", "f:" + code});  // (a player's color: the color row)
        items.push_back({L"Remove", L"", "remove"});
        const std::string choice = pick(tr(L"Value"), std::move(items), "f:" + part.format, box);
        if (choice == "remove") return text_->removeValue(index);
        if (choice.rfind("f:", 0) == 0) part.format = choice.substr(2);
        else if (choice == "value" && game) {
            if (part.value < 0 || part.value >= int(values_.size())) return;
            const std::string id = pick(tr(L"Search values from the game"), valueBlockPickItems(), values_[size_t(part.value)].def, box);
            const TriggerDef* nd = id.empty() ? nullptr : TriggerDef::find(id);
            if (!nd || !nd->value) return;
            values_[size_t(part.value)] = TriggerCall::make(*nd);
        } else if (choice == "value") {
            ValuePopSpec spec;
            spec.type = "Number", spec.title = L"A value in the text", spec.initial = part.text;
            spec.inRepeat = spec_.inRepeat, spec.inPlayerLoop = spec_.inPlayerLoop;
            std::string out;
            if (!nested(spec, box, out) || out.empty()) return;
            part.text = out;
        } else {
            return;
        }
        text_->replaceValue(index, partLabel(part), triggerTextJoin({part}));
    }
    // The player's color swatch: whose color.
    bool pickPlayer(unsigned char& code) {
        std::vector<ListPickItem> items{{L"the current player", L"", "current player"}};
        for (int p = 1; p <= 8; ++p) items.push_back({L"Player " + std::to_wstring(p), L"", "P" + std::to_string(p)});
        const draw::Rect caret = text_->caretBox();
        const draw::Rect f = text_->frame();
        const std::string picked = pick(tr(L"Whose color"), std::move(items), "", {f.left + caret.left, f.top + caret.top, f.left + caret.right, f.top + caret.bottom});
        code = gameTextPlayerCode(picked);
        return code != 0;
    }

    ValuePopSpec spec_;
    bool number_ = false, rich_ = false;
    std::unique_ptr<GameTextView> text_;
    stardraft::kit::TextField field_;
    stardraft::kit::ScrollBar bar_;
    stardraft::kit::Button ok_, cancel_, game_, insert_;
    std::vector<TriggerCall> values_;  // the text's values from the game ({#n}), changed here until OK
    std::vector<ValueSuggestion> rows_;
    std::wstring hint_;
    bool hintBad_ = false, done_ = false, accepted_ = false;
    int advance_ = 0, listH_ = 0, sel_ = -1, top_ = 0;
    draw::Rect titleRect_, hintRect_, list_, previewRect_;
    draw::Point place_{0, 0};
    std::string result_;
};

// Runs the pop-out at `at` (a point on the screen; the SDL program: in its main window) until it is applied or closed.
// True when applied (`out` holds the value). `shown` is the pop-out while it is open (the self-tests reach it there);
// `gameTexts` gives "A text of the game..." its entries.
inline bool runValuePop(const ValuePopSpec& spec, draw::Point at, std::string& out, ValuePopView** shown = nullptr,
                        std::function<std::vector<ListPickItem>()> gameTexts = {}) {
    ValuePopView v(spec);
    v.setPlace(at);
    v.gameTexts = std::move(gameTexts);
    v.onDone = [&v] { shellClosePopout(v); };
    if (shown) *shown = &v;
    const draw::Point size = v.outerSize();
    shellRunPopout(v, at.x, at.y, size.x, size.y, &v.focusView(), nullptr);
    if (shown) *shown = nullptr;
    if (!v.accepted()) return false;
    out = v.result();
    return true;
}
inline bool ValuePopView::nested(const ValuePopSpec& spec, draw::Rect box, std::string& out) {
    const draw::Point o = origin();
    return runValuePop(spec, {place_.x + o.x + box.left, place_.y + o.y + box.bottom + scale(2)}, out, nullptr, gameTexts);
}
