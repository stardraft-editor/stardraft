#include <cstdlib>
#include <sstream>

// The Data workspace's Buttons tab model (portable, core.inc): a button set's places on the command card, the names
// the fields and the list show, a button's label and cost, the edits of a set (add, move, reorder, remove, drag to a
// place) and a whole button's text (the trigger's Replace a button). The Buttons tab's paint and clicks, and the button
// pop-out's window, stay the program's (data_extras.inc); they take the results from here, so every program edits a
// command card the same way.

// A new button's condition and action: the game's defaults for a button made in the editor.
constexpr uint32_t kButtonNewCondition = 0x4282D0;
constexpr uint32_t kButtonNewAction = 0x4233F0;

// A place on the command card (1-9): its name as the pop-out's Place field shows it.
const wchar_t* buttonPlaceName(int pos) {
    static const wchar_t* const kPlaces[9] = {L"Top left", L"Top center", L"Top right", L"Left", L"Center", L"Right", L"Bottom left", L"Bottom center", L"Bottom right"};
    return pos >= 1 && pos <= 9 ? kPlaces[pos - 1] : L"?";
}

// The place as the Position field and its list show it: "Row 2, column 3".
std::wstring buttonRowColumn(int pos) { return L"Row " + std::to_wstring((pos - 1) / 3 + 1) + L", column " + std::to_wstring((pos - 1) % 3 + 1); }

// The place as the list's column shows it: "R2 C3".
std::wstring buttonCardCell(int pos) { return L"R" + std::to_wstring((pos - 1) / 3 + 1) + L" C" + std::to_wstring((pos - 1) % 3 + 1); }

// The card's cell under a point, in cells (0-2 across and down): its place (1-9), or 0 outside the card.
uint16_t buttonPlaceAt(int cx, int cy) { return cx < 0 || cy < 0 || cx > 2 || cy > 2 ? uint16_t(0) : uint16_t(cy * 3 + cx + 1); }

// A button's label: its tooltip's first line (`tooltip`: the string the button shows, "" when none), or what it does.
// With its icon, that is enough to know it.
std::wstring buttonRowTextFrom(const Button& b, const std::wstring& tooltip) {
    std::wstring text = tooltip;
    if (const size_t nl = text.find(L'\n'); nl != std::wstring::npos) text.resize(nl);
    if (text.empty())
        if (const ButtonFunctionDef* a = ButtonSets::action(b.action)) text = widen(a->name);
    return text.empty() ? L"(no text)" : text;
}

// What a button costs, as the tooltip of its type shows it ("Make a unit", "Upgrade", "Use a tech", "Research").
// `value` reads a .dat's field of an entry (the program's datValue).
std::wstring buttonCostText(const Button& b, int type, const std::function<int(const std::string&, const std::string&, int)>& value) {
    auto join = [](std::initializer_list<std::pair<int, const wchar_t*>> parts) {
        std::wstring o;
        for (const auto& [n, what] : parts)
            if (n > 0) o += (o.empty() ? L"" : L"    ") + std::to_wstring(n) + L" " + what;
        return o;
    };
    const int v = b.actionArg;
    switch (type) {
    case 1:
    case 5: {
        const int supply = value("units", "Supply Required", v);
        return join({{value("units", "Mineral Cost", v), L"minerals"}, {value("units", "Vespene Cost", v), L"gas"}, {(supply + 1) / 2, L"supply"}});
    }
    case 2: return join({{value("upgrades", "Mineral Cost Base", v), L"minerals"}, {value("upgrades", "Vespene Cost Base", v), L"gas"}});
    case 3: return join({{value("techdata", "Energy Required", v), L"energy"}});
    case 4: return join({{value("techdata", "Mineral Cost", v), L"minerals"}, {value("techdata", "Vespene Cost", v), L"gas"}});
    }
    return L"";
}

// "place,icon,condition,action,conditionValue,actionValue,tooltip,tooltipNotUsable", and back (a trigger's button).
bool buttonFromText(const std::string& v, Button& b) {
    std::vector<uint32_t> f;
    std::stringstream ss(v);
    for (std::string x; std::getline(ss, x, ',');) f.push_back(uint32_t(std::strtoul(x.c_str(), nullptr, 0)));
    if (f.size() != 8) return false;
    b = {uint16_t(f[0]), uint16_t(f[1]), f[2], f[3], uint16_t(f[4]), uint16_t(f[5]), uint16_t(f[6]), uint16_t(f[7])};
    return true;
}
std::string buttonToText(const Button& b) {
    return std::to_string(b.pos) + "," + std::to_string(b.icon) + "," + std::to_string(b.condition) + "," + std::to_string(b.action) + "," +
           std::to_string(b.conditionArg) + "," + std::to_string(b.actionArg) + "," + std::to_string(b.enabledString) + "," + std::to_string(b.disabledString);
}

// The edits of a set. `bs` is the set's buttons, in the order the game checks them; an index is a button's place in it.

// A new button's place: the first free one of the nine (the ninth when all are taken).
uint16_t buttonFreePlace(const std::vector<Button>& bs) {
    uint16_t p = 1;
    for (; p <= 9; ++p)
        if (std::none_of(bs.begin(), bs.end(), [p](const Button& x) { return x.pos == p; })) break;
    return std::min<uint16_t>(p, 9);
}

// A new button at a place, with the game's defaults.
Button buttonNew(uint16_t pos) {
    Button nb;
    nb.pos = pos, nb.icon = 0, nb.condition = kButtonNewCondition, nb.action = kButtonNewAction;
    return nb;
}

// The buttons in one place, in list order.
std::vector<int> buttonsAtPlace(const std::vector<Button>& bs, int pos) {
    std::vector<int> here;
    for (size_t i = 0; i < bs.size(); ++i)
        if (bs[i].pos == pos) here.push_back(int(i));
    return here;
}

// The button a click on a place selects next: the first of them, or the one after `sel` (the last wraps to the first).
int buttonNextInPlace(const std::vector<int>& here, int sel) {
    auto it = std::find(here.begin(), here.end(), sel);
    return it == here.end() || it + 1 == here.end() ? here[0] : *(it + 1);
}

// Moves button `i` to place `pos`; false when there is no such button or it is in that place already.
bool buttonMoveTo(std::vector<Button>& bs, int i, uint16_t pos) {
    if (i < 0 || i >= int(bs.size()) || bs[size_t(i)].pos == pos) return false;
    bs[size_t(i)].pos = pos;
    return true;
}

// Swaps button `i` with the one `by` places away (earlier or later in the list); returns the new index of the button.
int buttonSwapWith(std::vector<Button>& bs, int i, int by) {
    std::swap(bs[size_t(i)], bs[size_t(i + by)]);
    return i + by;
}

// Moves button `from` to index `to` (a drag in the list); returns `to`.
int buttonReorder(std::vector<Button>& bs, int from, int to) {
    const Button moved = bs[size_t(from)];
    bs.erase(bs.begin() + from);
    bs.insert(bs.begin() + to, moved);
    return to;
}

// Removes button `i` when there is one; returns the selection after it (the one before, at least 0).
int buttonRemoveAt(std::vector<Button>& bs, int i) {
    if (size_t(i) < bs.size()) bs.erase(bs.begin() + long(i));
    return std::max(0, i - 1);
}
