// The Data workspace's list model (portable, core.inc): the entries a tab lists, in the order the list shows them. The
// search filter (the entry's number and name, fuzzy: fuzzyScore), the Changed filter, the pinned entries first (in the
// order they were pinned), the pins' settings text, and the cursor of a tab switch (the tab that comes back on the
// entry it last showed). The program (main.cpp: DataListProgram) gives the tables, the entries' names and changed flags
// through DataListHost; the list's paint, its scroll bar, its tooltips and the search box stay the program's.

#include <sstream>

// The program's tables, as the list needs them.
class DataListHost {
public:
    virtual ~DataListHost() = default;
    virtual bool hasTable(int tab) = 0;            // the tab has entries to list (a .dat that reads, or an extra tab)
    virtual int entries(int tab) = 0;              // the numbers the tab runs to (the first entry to this, not counting)
    virtual int firstEntry(int tab) = 0;           // the first entry the list shows (strings count from 1)
    virtual bool changed(int tab, int e) = 0;      // the entry differs from the game's (the Changed filter)
    virtual std::wstring name(int tab, int e) = 0;  // the entry's name; empty when it has none
};

// Lower case (the search and the names compare in lower case).
std::wstring dataLower(std::wstring t) {
    std::transform(t.begin(), t.end(), t.begin(), [](wchar_t c) { return wchar_t(::towlower(c)); });
    return t;
}

// The entry's name in the list and in messages: its name, or "#n" when it has none.
std::wstring dataEntryName(DataListHost& host, int tab, int e) {
    std::wstring n = host.name(tab, e);
    return n.empty() ? L"#" + std::to_wstring(e) : n;
}

// The list's rows of a tab: `shown` in order, and how many of its first rows are pinned (`pinnedShown`). `pins`: the
// tab's pinned entries in the order they were pinned; the pinned ones the list shows come first.
struct DataListRows {
    std::vector<int> shown;
    int pinnedShown = 0;
};
DataListRows dataListRows(DataListHost& host, int tab, const std::wstring& search, bool changedOnly, const std::vector<int>& pins) {
    DataListRows out;
    if (!host.hasTable(tab)) return out;
    const std::wstring filter = dataLower(search);
    for (int e = host.firstEntry(tab); e < host.entries(tab); ++e) {
        if (changedOnly && !host.changed(tab, e)) continue;
        if (!filter.empty()) {
            const std::wstring hay = dataLower(std::to_wstring(e) + L" " + host.name(tab, e));
            if (fuzzyScore(hay, filter) < 0) continue;
        }
        out.shown.push_back(e);
    }
    // The pinned entries that the list shows: first, in the order they were pinned.
    std::vector<int> head;
    for (int p : pins)
        if (std::find(out.shown.begin(), out.shown.end(), p) != out.shown.end() && std::find(head.begin(), head.end(), p) == head.end())
            head.push_back(p);
    if (!head.empty()) {
        std::erase_if(out.shown, [&](int e) { return std::find(head.begin(), head.end(), e) != head.end(); });
        out.shown.insert(out.shown.begin(), head.begin(), head.end());
        out.pinnedShown = int(head.size());
    }
    return out;
}

// A shorter list: the scroll goes back inside it (else the list shows empty rows below its end).
void dataListClampScroll(int& scroll, size_t count, int rowH, int height) {
    const int most = std::max(0, int(count) * rowH - height);
    scroll = std::clamp(scroll, 0, most);
}

// The list's filters: the search box's text (as typed; the rows compare it in lower case) and the Changed toggle (only
// the changed entries). The Data workspace's search box and button set them (core/views_data_workspace.inc).
struct DataListFilter {
    std::wstring search;
    bool changedOnly = false;
};

// The status line when the Changed toggle changes: "Showing changed units only", or "Showing all units".
std::wstring dataChangedMessage(const std::wstring& tabTitle, bool on) {
    const std::wstring what = dataLower(tabTitle);
    return on ? L"Showing changed " + what + L" only" : L"Showing all " + what;
}

// The settings' DataPins/<table> text: the entries, comma-separated (in the order they were pinned).
std::vector<int> dataPinsParse(const std::wstring& text) {
    std::vector<int> v;
    std::wistringstream in(text);
    for (std::wstring part; std::getline(in, part, L',');)
        if (!part.empty()) v.push_back(int(std::wcstol(part.c_str(), nullptr, 10)));
    return v;
}
std::wstring dataPinsText(const std::vector<int>& pins) {
    std::wstring list;
    for (int x : pins) list += (list.empty() ? L"" : L",") + std::to_wstring(x);
    return list;
}

// Pins entry `e` (the new pin goes last) or unpins it. True when it is pinned now.
bool dataPinToggle(std::vector<int>& pins, int e) {
    const auto it = std::find(pins.begin(), pins.end(), e);
    if (it == pins.end()) {
        pins.push_back(e);
        return true;
    }
    pins.erase(it);
    return false;
}

// The search box's cue banner for a tab.
std::wstring dataSearchCue(const std::wstring& tabTitle) { return L"Search " + dataLower(tabTitle); }

// The cursor of a tab switch to `next`: the tab it leaves keeps its entry, the tab it goes to comes back on the entry it
// last showed (`tabEntry`), and the list and the form go to the top. The caller records the place (back and forward)
// and clears the search box first.
void dataSwitchTab(int& tab, int& entry, std::vector<int>& tabEntry, int& listScroll, int& formScroll, int next) {
    tabEntry[size_t(tab)] = entry;
    tab = next;
    entry = tabEntry[size_t(next)];
    listScroll = formScroll = 0;
}
