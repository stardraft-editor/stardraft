// The Requirements section's paint and clicks (portable, core.inc): one list's rows on a Data form (Units, Upgrades,
// Tech Data, Orders), drawn on a draw::Canvas with the arithmetic the Windows form had. The model (the ways, the steps,
// each change to a list) is in views_data_requirements.inc. The program's parts come in through
// RequirementsPaintHost: the Win32 program's implementation is in data_extras.inc (its kit faces: the value box with its
// icon, the link button, the remove cross, the list picker, the click areas). The host must live as long as the click
// areas it makes: their actions run after the paint.

// The list picker's option (a searchable popover): its label, its value, and the icon it shows.
struct ListPickItem {
    std::wstring label, section;
    std::string value;
    std::string iconDat;  // "units", "weapons", ...: drawn with that entry's icon
    int icon = -1;
};

// The section's columns in pixels (scaled by the program): the label's left edge and width, a row's height, and the
// column's right edge.
struct RequirementLayout {
    int x0 = 0, labelW = 0, rowH = 0, colRight = 0;
};

class RequirementsPaintHost {
public:
    virtual ~RequirementsPaintHost() = default;
    virtual draw::Canvas& canvas() = 0;  // what the section draws on
    virtual Requirements& reqs() = 0;    // the game's lists and the project's edits
    virtual void edited() = 0;           // a list changed
    virtual bool hot(draw::Rect r) = 0;  // the mouse is over r
    // A click area: `click` (may be null) on a click, the tip, and the hand cursor (else the arrow).
    virtual void hit(draw::Rect r, std::function<void()> click, const std::wstring& tip, bool hand) = 0;
    // A list picker of `items` under `box`: true with the value picked in `out`.
    virtual bool choose(const std::wstring& prompt, std::vector<ListPickItem> items, const std::string& current, draw::Rect box, std::string& out) = 0;
    // The entries of a value kind ("units", "weapons", ...) for the value picker.
    virtual std::vector<ListPickItem> valueItems(const char* kind) = 0;
    // A dropdown face for a step's name; `click` gets its box.
    virtual void pickBox(draw::Rect r, const std::wstring& text, bool changed, std::function<void(draw::Rect)> click) = 0;
    // A dropdown face for a step's value (its name and icon, the entry's open button); `click` gets its box.
    virtual void valueBox(draw::Rect r, const char* kind, int v, std::function<void(draw::Rect)> click) = 0;
    // The remove cross of a row: shown under the mouse at `at`; the row's click area is `row`.
    virtual void removeRow(draw::Rect row, draw::Rect at, std::function<void()> click, const std::wstring& tip) = 0;
    // A small button in a row ("Add a step"); returns its right edge.
    virtual int linkButton(int x, int y, int h, const std::wstring& text, std::function<void()> click) = 0;
    // A rectangle filled with `c` at `alpha` (the program's alpha fill).
    virtual void fillAlpha(draw::Rect r, draw::Color c, uint8_t alpha) = 0;
};

// The section's heading and a field's label (the kit's sectionHeader and fieldLabel).
int requirementSectionHeader(draw::Canvas& c, const RequirementLayout& l, int y, const std::wstring& name, bool changed) {
    c.text({l.x0, y, l.colRight, y + l.rowH}, name, changed ? kGood : kText, draw::kMiddle);
    c.fill({l.x0, y + l.rowH - 1, l.colRight, y + l.rowH}, kBorder);
    return y + l.rowH + scale(6);
}
void requirementFieldLabel(draw::Canvas& c, const RequirementLayout& l, int y, const std::wstring& name, bool changed) {
    c.text({l.x0, y, l.x0 + l.labelW, y + l.rowH}, name, changed ? kGood : kDim, draw::kMiddle | draw::kEllipsis);
}

// A radio mark (the kit's radioMark).
void requirementRadio(draw::Canvas& c, draw::Rect b, bool on, bool over) {
    c.ellipse(b, kCtl, on ? kAccent : over ? kBorderHot : kBorderStrong);
    if (!on) return;
    const int in = (b.right - b.left) * 3 / 10;
    c.ellipse({b.left + in, b.top + in, b.right - in + 1, b.bottom - in + 1}, kAccent, draw::kNone);
}

// Picks a step: the opcodes, then "End of the level's list". `then` gets the new step.
void requirementPickOp(RequirementsPaintHost& h, draw::Rect box, uint16_t current, std::function<void(RequirementOp)> then) {
    std::vector<ListPickItem> items;
    for (const auto& d : Requirements::ops()) items.push_back({widen(d.name), L"", std::to_string(d.op)});
    items.push_back({L"End of the level's list", L"", std::to_string(0xFFFF)});
    std::string picked;
    if (!h.choose(L"Step", items, std::to_string(requirementPickerCurrent(current)), box, picked)) return;
    then({uint16_t(std::stoul(picked)), 0});
}

// A thin bar, how much of the game's room is used, with the numbers beside it.
int requirementCapacityBar(RequirementsPaintHost& h, const RequirementLayout& l, int y, size_t used, size_t room, const std::wstring& what) {
    draw::Canvas& c = h.canvas();
    const bool over = used > room;
    wchar_t num[64];
    std::swprintf(num, 64, L"%zu / %zu bytes", used, room);
    const int numW = c.measure(num);
    draw::Rect r{l.x0, y + l.rowH / 2 - scale(2), l.colRight - numW - scale(12), y + l.rowH / 2 + scale(2)};
    h.fillAlpha(r, kBorder, 255);
    draw::Rect f = r;
    f.right = r.left + int((r.right - r.left) * std::min<double>(1.0, double(used) / double(std::max<size_t>(1, room))));
    h.fillAlpha(f, over ? kBad : kAccent, 255);
    draw::Rect t{r.right + scale(12), y, l.colRight, y + l.rowH};
    c.text(t, num, over ? kBad : kDim, draw::kMiddle | draw::kRight);
    h.hit({l.x0, y, l.colRight, y + l.rowH}, nullptr,
          (what.ends_with(L".") ? what : what + L".") + (over ? tr(L" The total is more than the game has room for. Build stops.") : L""), false);
    return y + l.rowH;
}

// One list of an entry: its heading, the four ways, the steps, the add link and the memory bar. Returns the next y.
int paintRequirementList(RequirementsPaintHost& h, const RequirementLayout& l, int k, int entry, int y) {
    draw::Canvas& c = h.canvas();
    const RequirementList list = h.reqs().list(k, entry);
    const bool changed = h.reqs().changed(k, entry);
    static const wchar_t* titles[] = {L"Requirements to make", L"Requirements to research", L"Requirements to research",
                                      L"Requirements to use", L"Requirements"};
    y = requirementSectionHeader(c, l, y, titles[k], changed);
    auto setList = [&h, k, entry](RequirementList n) {
        h.reqs().set(k, entry, std::move(n));
        h.edited();
    };
    const int mode = requirementWay(changed, list);  // the four ways
    {
        requirementFieldLabel(c, l, y, L"Use", changed);
        struct Way {
            const wchar_t* name;
            const wchar_t* tip;
        };
        static const Way ways[] = {
            {L"Default", L"The game's list"},
            {L"Always", L"Nothing is needed: an empty list"},
            {L"Disabled", L"No list: the entry's offset is 0, as EUD Editor's \"Do not use\". Not checked in the game yet."},
            {L"Custom", L"Your own steps. Change a step below, or click here to start from the game's list."},
        };
        int x = l.x0 + l.labelW;
        for (int w = 0; w < 4; ++w) {
            const int s = scale(14), tw = c.measure(ways[w].name);
            const draw::Rect r{x, y, x + s + scale(8) + tw + scale(4), y + l.rowH};
            const draw::Rect b{r.left, (r.top + r.bottom - s) / 2, r.left + s, (r.top + r.bottom - s) / 2 + s};
            requirementRadio(c, b, mode == w, h.hot(r));
            const draw::Rect t{b.right + scale(8), r.top, r.right, r.bottom};
            c.text(t, ways[w].name, mode == w ? (w == kReqDefault ? kText : kGood) : h.hot(r) ? kText : kDim, draw::kMiddle);
            h.hit(r,
                  [&h, k, entry, w, mode, setList] {
                      RequirementList n;
                      if (requirementListForWay(w, mode, h.reqs().stock(k, entry), n)) setList(n);
                  },
                  ways[w].tip, true);
            x = r.right + scale(18);
        }
        y += l.rowH;
    }
    if (list.used) {  // the steps (Default and Always show them too: a change makes it Custom)
        const std::vector<int> levels = requirementLevels(list.ops);
        for (size_t i = 0; i < list.ops.size(); ++i) {
            const RequirementOp& r = list.ops[i];
            const int indent = scale(levels[i] ? 22 : 0);
            const bool end = r.op == 0xFFFF;
            const draw::Rect opBox{l.x0 + indent, y + scale(2), l.x0 + l.labelW - scale(12), y + l.rowH - scale(2)};
            h.pickBox(opBox, requirementOpName(r), false, [&h, k, entry, i](draw::Rect b) {
                requirementPickOp(h, b, h.reqs().list(k, entry).ops[i].op, [&h, k, entry, i](RequirementOp n) {
                    h.reqs().set(k, entry, requirementWithOp(h.reqs().list(k, entry), i, n));
                    h.edited();
                });
            });
            if (end) h.fillAlpha(opBox, kBg, 120);  // dimmed: it only closes a level's list
            if (const char* ak = requirementOpArgKind(r)) {
                const int v = requirementOpValue(r);
                const draw::Rect argBox{l.x0 + l.labelW, y + scale(2), l.x0 + l.labelW + scale(320), y + l.rowH - scale(2)};
                h.valueBox(argBox, ak, v, [&h, k, entry, i, ak](draw::Rect b) {
                    std::string picked;
                    const RequirementOp cur = h.reqs().list(k, entry).ops[i];
                    if (!h.choose(L"Value", h.valueItems(ak), std::to_string(requirementOpValue(cur)), b, picked)) return;
                    h.reqs().set(k, entry, requirementWithArg(h.reqs().list(k, entry), i, uint16_t(std::stoul(picked))));
                    h.edited();
                });
            }
            h.removeRow({l.x0, y, l.colRight, y + l.rowH}, {l.colRight - scale(24), y, l.colRight, y + l.rowH},
                        [&h, k, entry, i] {
                            h.reqs().set(k, entry, requirementWithoutStep(h.reqs().list(k, entry), i));
                            h.edited();
                        },
                        L"Remove the step");
            y += l.rowH;
        }
        const int ay = y;
        h.linkButton(l.x0, y, l.rowH, L"Add a step", [&h, k, entry, ay] {
            requirementPickOp(h, {scale(18), ay, scale(218), ay + scale(26)}, 0xFF03, [&h, k, entry](RequirementOp n) {
                h.reqs().set(k, entry, requirementWithNewStep(h.reqs().list(k, entry), n));
                h.edited();
            });
        });
    }
    y += l.rowH;
    y = requirementCapacityBar(h, l, y, h.reqs().used(), Requirements::capacity(), L"Memory all requirement lists use together");
    return y + scale(14);
}
