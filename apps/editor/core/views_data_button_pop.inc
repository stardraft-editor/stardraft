// The button pop-out (portable, core.inc): a whole button, as a trigger's Replace a button gives it, in a window of its
// own. The button as the command card shows it and its tooltip, "Copy a button from a set...", the button's fields as
// the Buttons tab has them (place, icon, condition and action with their values, the two tooltips), then OK and Cancel.
// The view paints them on the Canvas with the Windows pop-out's arithmetic and keeps their click areas; the program's
// parts come in through ButtonPopHost (the Win32 one is in data_extras.inc: the icons, the names, the list pickers). The
// program makes the window and runs it (the window fits the height that onFit asks for); onDone says how it closed.

// The tooltip as the game shows it: `raw` (the string, with the game's codes) and the cost line its type adds, in a dark
// box from `box`'s top (its height is the text's). Returns the box's height.
inline int paintButtonTooltip(draw::Canvas& c, draw::Rect box, const std::string& raw, const std::wstring& cost) {
    const int th = c.metrics().height;
    const int lines = 1 + int(std::count(raw.begin(), raw.end(), '\n')) + (cost.empty() ? 0 : 1);
    box.bottom = box.top + lines * (th + scale(2)) + scale(16);
    c.fill(box, draw::rgb(8, 8, 10));
    c.roundBox(box, draw::kNone, draw::rgb(60, 64, 70), float(scale(4)));
    const draw::Rect tr{box.left + scale(10), box.top + scale(8), box.right - scale(10), box.bottom};
    const int h = drawGameText(c, tr, raw, draw::rgb(208, 208, 208));
    if (!cost.empty()) c.text({tr.left, tr.top + h, tr.right, tr.top + h + th + scale(2)}, cost, draw::rgb(150, 190, 220), draw::kMiddle);
    return box.bottom - box.top;
}

class ButtonPopHost {
public:
    virtual ~ButtonPopHost() = default;
    // The icon of entry `entry` of `dat` ("icons"), and of a function's value of `kind`; empty when there is none.
    virtual draw::Image icon(const char* dat, int entry) = 0;
    virtual draw::Image argIcon(const char* kind, int v) = 0;
    // The name of a function's value of `kind` (a unit, an upgrade, ...).
    virtual std::wstring argName(const char* kind, int v) = 0;
    // stat_txt.tbl string `id` (1-based): its line for a list, and its bytes (the tooltip).
    virtual std::wstring stringLine(int id) = 0;
    virtual std::string stringBytes(int id) = 0;
    // The cost line that a button's tooltip of `type` adds.
    virtual std::wstring cost(const Button& b, int type) = 0;
    // The entries of a kind ("icons", "buttons", "strings") for a picker; `none`: a None entry (value 0) first.
    virtual std::vector<ListPickItem> entryItems(const char* kind, bool none = false) = 0;
    // The buttons of a set, and a button's label in a list.
    virtual std::vector<Button> buttonSet(int set) = 0;
    virtual std::wstring buttonLabel(const Button& b) = 0;
    // A list picker of `items` under `box` (the view's coordinates): true with the value picked in `out`.
    virtual bool choose(const std::wstring& prompt, std::vector<ListPickItem> items, const std::string& current, draw::Rect box, std::string& out) = 0;
};

class ButtonPopView final : public draw::View {
public:
    ButtonPopView(ButtonPopHost& h, const Button& b) : host_(h), button_(b) {}

    const Button& button() const { return button_; }
    // OK, Enter (true); Cancel, Escape (false).
    std::function<void(bool accepted)> onDone;
    // The client height that fits the fields (they change with the functions' values), after each paint.
    std::function<void(int height)> onFit;

    void paint(draw::Canvas& c) override {
        hits_.clear();
        const int right = width();
        // The layout of the Windows pop-out: the Data form's rows, 28 px high, the labels 150 px wide.
        const int x0 = scale(16), labelW = scale(150), rowH = scale(28), colRight = right - scale(16);
        int y = scale(12);
        // The button as the card shows it, and its tooltip.
        const int cell = scale(64);
        const draw::Rect card{x0, y, x0 + cell, y + cell};
        c.fill(card, draw::rgb(26, 28, 32));
        c.roundBox({card.left - 1, card.top - 1, card.right, card.bottom}, draw::kNone, kAccent, float(scale(4)), 2);  // (a 2 px line on the box's edge)
        if (const draw::Image ic = host_.icon("icons", button_.icon)) c.fitted(ic, {card.left + scale(5), card.top + scale(5), card.right - scale(5), card.bottom - scale(5)});
        int th = 0;
        if (button_.enabledString) {
            const std::string raw = host_.stringBytes(button_.enabledString);
            th = paintButtonTooltip(c, {card.right + scale(12), y, colRight, 0}, raw, host_.cost(button_, buttonPrefix(raw) ? raw[1] : 0));
        }
        y += std::max(cell, th) + scale(10);
        // Copy a button from a set: a small button.
        {
            const std::wstring text = L"Copy a button from a set\x2026";
            const draw::Rect r = kitpaint::smallButtonRect(c, x0, y, rowH, text);
            kitpaint::drawButtonFace(c, r, text, kitpaint::Tone::Normal, hot(r) ? kitpaint::ButtonState::Hot : kitpaint::ButtonState::Normal);
            const int ly = y;
            hits_.push_back({r, [this, ly] { copyFromSet(ly); }});
        }
        y += rowH + scale(6);
        // Its fields, as the Buttons tab has them.
        auto row = [&](const wchar_t* name, const std::wstring& text, std::function<void(draw::Rect)> click, draw::Image icon = {}, int number = -1) {
            kitpaint::drawFieldLabel(c, {x0, y, x0 + labelW, y + rowH}, name, false);
            const draw::Rect r{x0 + labelW, y + scale(2), colRight, y + rowH - scale(2)};
            kitpaint::drawPickFace(c, r, text, false, hot(r), icon, number);
            hits_.push_back({r, [this, r, click] {
                                 click(r);
                                 invalidate();
                             }});
            y += rowH;
        };
        Button& b = button_;
        row(L"Place", buttonPlaceName(b.pos), [this](draw::Rect r) {
            std::vector<ListPickItem> items;
            for (int k = 1; k <= 9; ++k) items.push_back({buttonPlaceName(k), L"", std::to_string(k)});
            std::string picked;
            if (host_.choose(L"Place", items, std::to_string(button_.pos), r, picked)) button_.pos = uint16_t(std::stoul(picked));
        });
        static const std::string kIcons = "icons";
        const auto& iconNames = DatSchema::entryNames(kIcons);
        row(L"Icon", b.icon < iconNames.size() ? widen(iconNames[b.icon]) : L"Icon", [this](draw::Rect r) {
            std::string picked;
            if (host_.choose(L"Icon", host_.entryItems("icons"), std::to_string(button_.icon), r, picked)) button_.icon = uint16_t(std::stoul(picked));
        }, host_.icon("icons", b.icon), b.icon);
        for (int which = 0; which < 2; ++which) {
            const bool cond = which == 0;
            const uint32_t fn = cond ? b.condition : b.action;
            const ButtonFunctionDef* d = cond ? ButtonSets::condition(fn) : ButtonSets::action(fn);
            wchar_t hex[24];
            std::swprintf(hex, 24, L"Function 0x%06X", unsigned(fn));
            row(cond ? L"Shown when" : L"Action", d ? widen(d->name) : std::wstring(hex), [this, cond](draw::Rect r) {
                std::vector<ListPickItem> items;
                for (const auto& f : cond ? ButtonSets::conditions() : ButtonSets::actions()) items.push_back({widen(f.name), L"", std::to_string(f.address)});
                std::string picked;
                Button& x = button_;
                if (host_.choose(cond ? L"Shown when" : L"Action", items, std::to_string(cond ? x.condition : x.action), r, picked))
                    (cond ? x.condition : x.action) = uint32_t(std::stoul(picked));
            });
            if (d && d->arg) {
                const uint16_t v = cond ? b.conditionArg : b.actionArg;
                const char* kind = d->arg;
                row(cond ? L"Shown when: value" : L"Action: value", host_.argName(kind, v), [this, cond, kind](draw::Rect r) {
                    std::string picked;
                    Button& x = button_;
                    if (host_.choose(L"Value", host_.entryItems(kind), std::to_string(cond ? x.conditionArg : x.actionArg), r, picked))
                        (cond ? x.conditionArg : x.actionArg) = uint16_t(std::stoul(picked));
                }, host_.argIcon(kind, v), v);
            }
        }
        for (int which = 0; which < 2; ++which) {
            const uint16_t s = which ? b.disabledString : b.enabledString;
            row(which ? L"Tooltip when grayed" : L"Tooltip", s ? host_.stringLine(s) : L"None", [this, which](draw::Rect r) {
                std::string picked;
                Button& x = button_;
                if (host_.choose(L"Tooltip", host_.entryItems("strings", true), std::to_string(which ? x.disabledString : x.enabledString), r, picked))
                    (which ? x.disabledString : x.enabledString) = uint16_t(std::stoul(picked));
            }, {}, s ? s : -1);
        }
        // OK, Cancel: from the right edge (OK the rightmost but one).
        y += scale(10);
        const int bh = scale(stardraft::kit::kButtonH);
        {
            struct Spec {
                const wchar_t* text;
                kitpaint::Tone tone;
                bool ok;
            };
            const Spec specs[2] = {{L"OK", kitpaint::Tone::Primary, true}, {L"Cancel", kitpaint::Tone::Normal, false}};
            int x = colRight;
            for (int k = 1; k >= 0; --k) {
                const int w = kitpaint::buttonWidth(c, specs[k].text);
                const draw::Rect r{x - w, y, x, y + bh};
                kitpaint::drawButtonFace(c, r, specs[k].text, specs[k].tone, hot(r) ? kitpaint::ButtonState::Hot : kitpaint::ButtonState::Normal);
                const bool ok = specs[k].ok;
                hits_.push_back({r, [this, ok] { done(ok); }});
                x -= w + scale(stardraft::kit::kButtonGap);
            }
        }
        const int want = y + bh + scale(14);
        if (want != height() && onFit) onFit(want);
    }

    bool mouse(const draw::MouseEvent& e) override {
        if (e.kind == draw::MouseKind::Move) return invalidate(), true;  // the part under the mouse
        if (e.kind != draw::MouseKind::Up || e.button != 0) return false;
        for (auto it = hits_.rbegin(); it != hits_.rend(); ++it)
            if (it->r.contains(e.pt)) {
                const auto click = it->click;  // (a copy: a repaint during the click makes the hits again)
                click();
                break;
            }
        return true;
    }
    bool key(const draw::KeyEvent& e) override {
        if (!e.down) return false;
        if (e.key == draw::Key::Escape) return done(false), true;
        if (e.key == draw::Key::Enter) return done(true), true;
        return false;
    }
    draw::Cursor cursor(draw::Point pt) override {
        for (auto it = hits_.rbegin(); it != hits_.rend(); ++it)
            if (it->r.contains(pt)) return draw::Cursor::Hand;
        return draw::Cursor::Arrow;
    }

private:
    struct Hit {
        draw::Rect r;
        std::function<void()> click;
    };
    bool hot(draw::Rect r) const { return host() && r.contains(host()->mouse(*this)); }
    void done(bool accepted) {
        if (onDone) onDone(accepted);
    }
    // A button set, then one of its buttons: the pop-out's button becomes a copy of it. The pickers open at the row's
    // left edge, 28 px under the link's row top.
    void copyFromSet(int rowTop) {
        const draw::Rect at{scale(16), rowTop, scale(16), rowTop + scale(28) - scale(2)};  // (a picker opens 2 px under its box)
        std::string set;
        if (host_.choose(L"Button set", host_.entryItems("buttons"), "", at, set)) {
            const std::vector<Button> bs = host_.buttonSet(std::stoi(set));
            std::vector<ListPickItem> items;
            for (size_t i = 0; i < bs.size(); ++i) items.push_back({host_.buttonLabel(bs[i]), L"", std::to_string(i), "icons", int(bs[i].icon)});
            std::string pick;
            if (!items.empty() && host_.choose(L"Button", items, "", at, pick)) button_ = bs[size_t(std::stoi(pick))];
        }
        invalidate();
    }

    ButtonPopHost& host_;
    Button button_;
    std::vector<Hit> hits_;
};
