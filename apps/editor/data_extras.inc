// The Data workspace's tables that are not .dat files (included into main.cpp, before paintDataForm):
//  - a Strings tab: stat_txt.tbl, each string's text in an edit box with a colored preview; for button texts, the
//    hotkey and the button type as their own controls;
//  - a Buttons tab: each button set as the command card it makes (3 x 3), the selected button's fields beside it;
//  - a Requirements section on the Units, Upgrades, Tech Data and Orders forms;
//  - a Display section on the Units form: the button set, the status panel, the wireframes.
// Edits are saved as dat/strings.json, dat/buttons.json, dat/requirements.json and dat/unit_display.json; Build writes
// their code.

bool showListPicker(const std::wstring& prompt, std::vector<ListPickItem> items, const std::string& current, POINT at, std::string& out);

HWND hitsWnd() { return gHitsWnd ? gHitsWnd : gData.form; }

void (*gButtonPopHook)(HWND) = nullptr;  // self-test
struct DataExtras {
    StatTxt strings;
    bool stringsLoaded = false;
    Requirements reqs;
    ButtonSets buttons;
    UnitLooks looks;
    std::string saved[4];   // the edits text as last saved: strings, buttons, requirements, unit display
    bool loadFailed[4] = {};  // the project's file did not load: it is not saved over or removed
    int button = 0;         // the selected button of the open set
    HWND textEdit = nullptr;
    int textFor = -1;       // the string the edit box shows
    bool textDirty = false;
    bool rawText = false;   // the edit box shows the whole string: a button's hotkey and type characters too
} gExtras;
enum { kExtraStrings, kExtraButtons, kExtraReqs, kExtraLooks, kExtraKinds };

int datTabCount() { return int(DatSchema::all().size()); }
int stringsTab() { return datTabCount(); }
int buttonsTab() { return datTabCount() + 1; }
int dataTabCount() { return datTabCount() + 2; }
bool extraTab(int tab) { return tab >= datTabCount(); }

fs::path extrasFile(const EudProject& pr, int k) {
    static const char* names[] = {"strings.json", "buttons.json", "requirements.json", "unit_display.json"};
    return pr.folder() / "dat" / names[k];
}
std::string extrasText(int k) {
    return k == kExtraStrings ? gExtras.strings.editsText()
           : k == kExtraButtons ? gExtras.buttons.editsText()
           : k == kExtraLooks   ? gExtras.looks.editsText()
                                : gExtras.reqs.editsText();
}
bool setExtrasText(int k, const std::string& text, std::string& err) {
    return k == kExtraStrings ? gExtras.strings.setEditsText(text, err)
           : k == kExtraButtons ? gExtras.buttons.setEditsText(text, err)
           : k == kExtraLooks   ? gExtras.looks.setEditsText(text, err)
                                : gExtras.reqs.setEditsText(text, err);
}
bool extrasSaveFile(int k, const fs::path& f, std::string& err) {
    if (gExtras.loadFailed[k]) return err = f.string() + " did not load: not saved, so nothing in it is lost", false;
    return k == kExtraStrings ? gExtras.strings.saveEdits(f, err)
           : k == kExtraButtons ? gExtras.buttons.saveEdits(f, err)
           : k == kExtraLooks   ? gExtras.looks.saveEdits(f, err)
                                : gExtras.reqs.saveEdits(f, err);
}

// The project's strings, button sets and requirements: the game's, plus the project's edits.
void extrasLoad(const EudProject* pr) {
    std::string err;
    if (!gExtras.stringsLoaded) gExtras.stringsLoaded = gExtras.strings.load(app.data, err);
    gExtras.strings.setEdits({});
    gExtras.reqs = Requirements();
    gExtras.buttons = ButtonSets();
    gExtras.looks = UnitLooks();
    gExtras.button = 0, gExtras.textFor = -1;
    for (int k = 0; k < kExtraKinds; ++k) {
        gExtras.saved[k].clear();
        gExtras.loadFailed[k] = false;
        if (!pr) continue;
        const fs::path f = extrasFile(*pr, k);
        const bool ok = k == kExtraStrings ? gExtras.strings.loadEdits(f, err)
                        : k == kExtraButtons ? gExtras.buttons.loadEdits(f, err)
                        : k == kExtraLooks   ? gExtras.looks.loadEdits(f, err)
                                             : gExtras.reqs.loadEdits(f, err);
        gExtras.loadFailed[k] = !ok;
        if (!ok) themedMessageBox(app.main, widen(err).c_str(), (L"Could not read " + f.filename().wstring()).c_str(), MB_ICONERROR), err.clear();
        gExtras.saved[k] = extrasText(k);
    }
}

// Before a build: the edits saved, and their code written (or taken out when there are none).
bool writeExtrasEdits(EudProject& pr, std::string& err) {
    StatTxt strings;
    Requirements reqs;
    ButtonSets buttons;
    UnitLooks looks;
    // Another project than the one the Data workspace shows, or a file of it did not load (it may be repaired now):
    // from disk.
    const bool open = gData.project == &pr && !extrasLoadFailed();
    if (!open) {
        if (!strings.load(app.data, err) || !strings.loadEdits(extrasFile(pr, kExtraStrings), err) || !reqs.loadEdits(extrasFile(pr, kExtraReqs), err) ||
            !buttons.loadEdits(extrasFile(pr, kExtraButtons), err) || !looks.loadEdits(extrasFile(pr, kExtraLooks), err))
            return false;
    }
    const StatTxt& s = open ? gExtras.strings : strings;
    const Requirements& r = open ? gExtras.reqs : reqs;
    const ButtonSets& b = open ? gExtras.buttons : buttons;
    const UnitLooks& lk = open ? gExtras.looks : looks;
    if (!s.saveEdits(extrasFile(pr, kExtraStrings), err) || !r.saveEdits(extrasFile(pr, kExtraReqs), err) || !b.saveEdits(extrasFile(pr, kExtraButtons), err) ||
        !lk.saveEdits(extrasFile(pr, kExtraLooks), err))
        return false;
    const std::string sCode = s.editsScript(err);
    if (!err.empty()) return false;
    const std::string rCode = r.editsScript(err);
    if (!err.empty()) return false;
    return pr.writeGeneratedScript("strings.eps", sCode, err) && pr.writeGeneratedScript("requirements.eps", rCode, err) &&
           pr.writeGeneratedScript("buttons.eps", b.editsScript(), err) && pr.writeGeneratedScript("unit_display.eps", lk.editsScript(), err);
}

// --- What the tabs list ----------------------------------------------------------------------------------------

std::wstring tabTitle(int tab) {
    if (tab == stringsTab()) return L"Strings";
    if (tab == buttonsTab()) return L"Buttons";
    return tab >= 0 && tab < datTabCount() ? datTitle(DatSchema::all()[size_t(tab)].name) : L"";
}
// A tab's icon (the Data group of the UI icons).
UiIcon datIcon(const std::string& dat) {
    static const std::map<std::string, UiIcon> icons = {
        {"units", UiIcon::DatUnits},     {"weapons", UiIcon::DatWeapons}, {"flingy", UiIcon::DatFlingy},   {"sprites", UiIcon::DatSprites},
        {"images", UiIcon::DatImages},   {"upgrades", UiIcon::DatUpgrades}, {"techdata", UiIcon::DatTech}, {"orders", UiIcon::DatOrders},
        {"sfxdata", UiIcon::DatSfx},     {"portdata", UiIcon::DatPortraits}, {"strings", UiIcon::DatStrings}, {"buttons", UiIcon::DatButtons}};
    const auto it = icons.find(dat);
    return it != icons.end() ? it->second : UiIcon::Data;
}
UiIcon tabIcon(int tab) {
    if (tab == stringsTab()) return UiIcon::DatStrings;
    if (tab == buttonsTab()) return UiIcon::DatButtons;
    return tab >= 0 && tab < datTabCount() ? datIcon(DatSchema::all()[size_t(tab)].name) : UiIcon::Data;
}
// Entries of a tab. Strings count from 1, as the game's references to them do (0 is none).
int tabEntries(int tab) {
    if (tab == stringsTab()) return gExtras.strings.count() + 1;
    if (tab == buttonsTab()) return ButtonSets::kSets;
    return tab >= 0 && tab < datTabCount() ? DatSchema::all()[size_t(tab)].entries : 0;
}
int tabFirstEntry(int tab) { return tab == stringsTab() ? 1 : 0; }
int tabChangedCount(int tab) {
    if (tab == stringsTab()) return gExtras.strings.changedCount();
    if (tab == buttonsTab()) return gExtras.buttons.changedCount();
    const DatTable* t = datTable(tab);
    return t ? t->changedCount() : 0;
}

// A string's text without its button prefix (a hotkey and a type byte) and with its control bytes left out: a line
// for lists.
bool buttonPrefix(const std::string& s) { return s.size() >= 2 && static_cast<unsigned char>(s[1]) <= 5 && static_cast<unsigned char>(s[0]) >= 0x20; }
std::wstring stringLine(const std::string& raw) {
    std::string o;
    for (size_t i = buttonPrefix(raw) ? 2 : 0; i < raw.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(raw[i]);
        if (c == '\n' || c == '\r') o += ' ';
        else if (c >= 0x20) o += char(c);
    }
    return widen(o);
}
std::wstring statString(int id) {  // stat_txt.tbl string `id` (1-based), as the project has it
    return id >= 1 && id <= gExtras.strings.count() ? stringLine(gExtras.strings.text(id - 1)) : L"";
}
std::wstring projectUnitName(int u) {  // a unit's name string (u + 1) when the project changed it, else ""
    return u >= 0 && u < gExtras.strings.count() && gExtras.strings.changed(u) ? statString(u + 1) : L"";
}

// Sets 0-227 are the units'. The others are named by the button that opens them ("Terran SCV: Build Structure").
std::wstring buttonSetName(int s) {
    if (s < 228) {
        const std::wstring n = entryName("units", s);
        return n.empty() ? L"Unit " + std::to_wstring(s) : n;
    }
    for (int o = 0; o < 228; ++o)
        for (const Button& b : gExtras.buttons.set(o))
            if (b.actionArg == s && ButtonSets::action(b.action) && ButtonSets::action(b.action)->arg && std::string(ButtonSets::action(b.action)->arg) == "buttons") {
                const std::wstring unit = entryName("units", o), text = statString(b.enabledString);
                return (unit.empty() ? L"Unit " + std::to_wstring(o) : unit) + L": " + (text.empty() ? L"button set" : text);
            }
    return L"Button set " + std::to_wstring(s);
}

bool tabEntryChanged(int tab, int e) {
    if (tab == stringsTab()) return e >= 1 && gExtras.strings.changed(e - 1);
    if (tab == buttonsTab()) return e >= 0 && e < ButtonSets::kSets && gExtras.buttons.changed(e);
    const DatTable* t = datTable(tab);
    bool req = false;
    if (t) {
        const std::string& dat = t->schema().name;
        for (int k = 0; k < Requirements::kKinds; ++k)
            if (Requirements::kind(k).dat == dat && e < Requirements::kind(k).entries) req |= gExtras.reqs.changed(k, e);
    }
    return (t && t->entryChanged(e)) || req;
}
std::wstring tabEntryName(int tab, int e) {
    if (tab == stringsTab()) return statString(e);
    if (tab == buttonsTab()) return buttonSetName(e);
    const DatTable* t = datTable(tab);
    return t ? entryName(t->schema().name, e) : L"";
}
const UnitPalette::Bitmap* tabEntryIcon(int tab, int e) {
    if (tab == stringsTab()) return nullptr;
    if (tab == buttonsTab()) return e < 228 ? entryIcon(datTabOf("units"), e) : nullptr;
    return entryIcon(tab, e);
}

// --- Game text, drawn as the game colors it ------------------------------------------------------------------

// (The game's text colors, gameTextColor and briefingTextColor: game_text.inc.)
// Draws `raw` (UTF-8 with the game's control bytes) in `r`, line by line; returns the height used. `briefing`: the
// colors of mission briefings.
int drawGameText(HDC dc, RECT r, const std::string& raw, COLORREF normal, int lineHeight = 0, bool briefing = false) {
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    const int lh = lineHeight ? lineHeight : tm.tmHeight + scale(2), width = r.right - r.left;
    int y = r.top;
    COLORREF color = normal;
    // A line of colored pieces, wrapped at the box's width between words.
    struct Piece {
        std::wstring text;
        COLORREF color;
        int w;
    };
    auto drawLine = [&](const std::vector<Piece>& pieces, int align) {
        int w = 0;
        for (const auto& p : pieces) w += p.w;
        int x = align == 1 ? r.right - w : align == 2 ? r.left + (width - w) / 2 : r.left;
        for (const auto& p : pieces) {
            if (p.color != kInvisibleText) SetTextColor(dc, p.color), TextOutW(dc, x, y, p.text.c_str(), int(p.text.size()));
            x += p.w;
        }
        y += lh;
    };
    auto measure = [&](const std::wstring& t) {
        SIZE sz{};
        GetTextExtentPoint32W(dc, t.c_str(), int(t.size()), &sz);
        return int(sz.cx);
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
            const unsigned char c = static_cast<unsigned char>(raw[k]);
            if (c == 0x12) align = 1;
            else if (c == 0x13) align = 2;
            else if (c < 0x20) {
                if (const COLORREF nc = briefing ? briefingTextColor(c, normal) : gameTextColor(c, normal); nc != 0xFFFFFFFF) flushRun(), color = nc;
            } else {
                if (c != ' ' && space) flushWord(), space = false;
                run += char(c);
                if (c == ' ') space = true;
            }
        }
        flushWord();
        std::vector<Piece> lineNow;
        int x = 0;
        for (auto& wd : words) {
            int ww = 0;
            for (auto& p : wd) p.w = measure(p.text), ww += p.w;
            if (x > 0 && x + ww > width) drawLine(lineNow, align), lineNow.clear(), x = 0;
            for (auto& p : wd) lineNow.push_back(p);
            x += ww;
        }
        drawLine(lineNow, align);
        i = end + 1;
    }
    return y - r.top;
}

// --- Common parts ----------------------------------------------------------------------------------------------

bool extrasLoadFailed() {
    for (int k = 0; k < kExtraKinds; ++k)
        if (gExtras.loadFailed[k]) return true;
    return false;
}

// Saves and records an undo step for whatever changed (the .dat tables do theirs in dataSaved). False when a save
// failed (its error is the status message).
bool extrasSaved() {
    bool ok = true;
    std::string err;
    for (int k = 0; k < kExtraKinds; ++k) {
        const std::string now = extrasText(k);
        if (now == gExtras.saved[k]) continue;
        const int tab = k == kExtraStrings ? stringsTab() : k == kExtraButtons ? buttonsTab() : gData.datTab;
        gData.undo.push_back({tab, gData.entry, Bytes(gExtras.saved[k].begin(), gExtras.saved[k].end()), Bytes(now.begin(), now.end()), k + 1});
        if (gData.undo.size() > 200) gData.undo.erase(gData.undo.begin());
        gData.redo.clear();
        gExtras.saved[k] = now;
        if (gData.project) {
            const fs::path f = extrasFile(*gData.project, k);
            if (!extrasSaveFile(k, f, err)) setMessage(widen(err)), ok = false;
        }
    }
    return ok;
}
void extrasChanged() {
    dataSaved();
    InvalidateRect(gData.form, nullptr, FALSE);
    InvalidateRect(gData.list, nullptr, FALSE);
}

// The title block: icon, name, and a line under it ("stat_txt.tbl string 12  ·  changed  ·  Reset").
int formTitle(HDC dc, const FormLayout& l, int y, const UnitPalette::Bitmap* icon, const std::wstring& title, const std::wstring& sub, bool changed,
              std::function<void()> reset, std::optional<UiIcon> kind = std::nullopt) {
    return ui::title(dc, l.x0, y, l.colRight + l.picW, icon, title, sub, changed, std::move(reset), kind);
}
// The entries of a .dat (or "buttons", "icons", "strings") for a list picker.
std::vector<ListPickItem> entryItems(const std::string& kind, bool none = false) {
    std::vector<ListPickItem> items;
    if (none) items.push_back({L"None", L"", "0"});
    if (kind == "buttons") {
        for (int s = 0; s < ButtonSets::kSets; ++s) items.push_back({buttonSetName(s) + L"   #" + std::to_wstring(s), L"", std::to_string(s), s < 228 ? "units" : "", s < 228 ? s : -1});
    } else if (kind == "icons") {
        const auto& names = DatSchema::entryNames("icons");
        for (size_t i = 0; i < names.size(); ++i) items.push_back({widen(names[i]) + L"   #" + std::to_wstring(i), L"", std::to_string(i), "icons", int(i)});
    } else if (kind == "strings") {
        for (int i = 1; i <= gExtras.strings.count(); ++i) items.push_back({statString(i) + L"   #" + std::to_wstring(i), L"", std::to_string(i)});
    } else if (const DatSchema* sc = DatSchema::find(kind)) {
        const bool icons = datHasIcons(kind);
        for (int i = 0; i < sc->entries; ++i) {
            const std::wstring n = entryName(kind, i);
            items.push_back({(n.empty() ? L"(unnamed)" : n) + L"   #" + std::to_wstring(i), L"", std::to_string(i), icons ? kind : "", icons ? i : -1});
        }
    }
    return items;
}
std::wstring argName(const char* kind, int v) {
    if (!kind) return L"";
    const std::string k = kind;
    if (k == "buttons") return buttonSetName(v);
    const std::wstring n = entryName(k, v);
    return n.empty() ? widen(k) + L" #" + std::to_wstring(v) : n;
}
const UnitPalette::Bitmap* argIcon(const char* kind, int v) {
    if (!kind) return nullptr;
    const std::string k = kind;
    if (k == "buttons") return v < 228 ? datRefIcon("units", uint32_t(v)) : nullptr;
    return datHasIcons(k) ? datRefIcon(k, uint32_t(v)) : nullptr;
}
// Opens entry `v` of `kind` (a .dat, "buttons" or "strings").
std::function<void()> openEntry(const char* kind, int v) {
    if (!kind) return nullptr;
    const std::string k = kind;
    return [k, v] { dataGoTo(k, v); };
}
// A thin bar, how much of the game's room is used, with the numbers beside it.
int capacityBar(HDC dc, const FormLayout& l, int y, size_t used, size_t room, const std::wstring& what) {
    const bool over = used > room;
    wchar_t num[64];
    std::swprintf(num, 64, L"%zu / %zu bytes", used, room);
    SIZE sz{};
    GetTextExtentPoint32W(dc, num, int(wcslen(num)), &sz);
    RECT r{l.x0, y + l.rowH / 2 - scale(2), l.colRight - sz.cx - scale(12), y + l.rowH / 2 + scale(2)};
    fillAlpha(dc, r, kBorder, 255);
    RECT f = r;
    f.right = r.left + int((r.right - r.left) * std::min<double>(1.0, double(used) / double(std::max<size_t>(1, room))));
    fillAlpha(dc, f, over ? kBad : kAccent, 255);
    RECT t{r.right + scale(12), y, l.colRight, y + l.rowH};
    SetTextColor(dc, over ? kBad : kDim);
    DrawTextW(dc, num, -1, &t, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
    hitsOut().push_back({{l.x0, y, l.colRight, y + l.rowH}, nullptr, nullptr, (what.ends_with(L".") ? what : what + L".") + (over ? L" The total is more than the game has room for. Build stops." : L""), IDC_ARROW, {}});
    return y + l.rowH;
}

// --- Requirements section (Units, Upgrades, Tech Data, Orders) ------------------------------------------------

std::wstring opName(const RequirementOp& r) {
    if (r.op < 0x100) return L"Must have";
    if (r.op == 0xFFFF) return L"End of the level's list";
    const RequirementOpDef* d = Requirements::op(r.op);
    return d ? widen(d->name) : L"Step 0x" + std::to_wstring(r.op);
}
const char* opArgKind(const RequirementOp& r) {
    if (r.op < 0x100) return "units";
    const RequirementOpDef* d = Requirements::op(r.op);
    return d ? d->arg : nullptr;
}

// Picks a step: the opcodes, then "End of the level's list". `then` gets the new step.
void pickRequirementOp(RECT box, uint16_t current, std::function<void(RequirementOp)> then) {
    std::vector<ListPickItem> items;
    for (const auto& d : Requirements::ops()) items.push_back({widen(d.name), L"", std::to_string(d.op)});
    items.push_back({L"End of the level's list", L"", std::to_string(0xFFFF)});
    std::string picked;
    if (!showListPicker(L"Step", items, std::to_string(current < 0x100 ? 0xFF03 : current), belowBox(box), picked)) return;
    then({uint16_t(std::stoul(picked)), 0});
}

int paintRequirementList(HDC dc, const FormLayout& l, int k, int entry, int y) {
    const RequirementList& list = gExtras.reqs.list(k, entry);
    const bool changed = gExtras.reqs.changed(k, entry);
    static const wchar_t* titles[] = {L"Requirements to make", L"Requirements to research", L"Requirements to research",
                                      L"Requirements to use", L"Requirements"};
    y = sectionHeader(dc, l, y, titles[k], changed);
    auto setList = [k, entry](RequirementList n) {
        gExtras.reqs.set(k, entry, std::move(n));
        extrasChanged();
    };
    // The four ways, as EUD Editor has them (ReqModule: default, not used, always allow, custom):
    //   Default: the game's list. Always: an empty list (nothing is needed). Disabled: no list (the offset is 0).
    //   Custom: the steps below.
    enum { kDefault, kAlways, kDisabled, kCustom };
    const int mode = !changed ? kDefault : !list.used ? kDisabled : list.ops.empty() ? kAlways : kCustom;
    {
        fieldLabel(dc, l, y, L"Use", changed);
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
            const int s = scale(14), tw = ui::textWidth(dc, ways[w].name);
            const RECT r{x, y, x + s + scale(8) + tw + scale(4), y + l.rowH};
            const RECT b{r.left, (r.top + r.bottom - s) / 2, r.left + s, (r.top + r.bottom - s) / 2 + s};
            ui::radioMark(dc, b, mode == w, ui::hot(r));
            RECT t{b.right + scale(8), r.top, r.right, r.bottom};
            SetTextColor(dc, mode == w ? (w == kDefault ? kText : kGood) : ui::hot(r) ? kText : kDim);
            DrawTextW(dc, ways[w].name, -1, &t, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            hitsOut().push_back({r, nullptr,
                                 [k, entry, w, mode, setList] {
                                     const RequirementList& st = gExtras.reqs.stock(k, entry);
                                     RequirementList n;
                                     if (w == kDefault) n = st;
                                     else if (w == kAlways) n.used = true;
                                     else if (w == kDisabled) n.used = false;
                                     else {  // Custom: from the game's list (an empty one when it has none)
                                         if (mode == kCustom) return;
                                         n = st.used && !st.ops.empty() ? st : RequirementList{true, {}};
                                     }
                                     setList(n);
                                 },
                                 ways[w].tip, IDC_HAND, {}});
            x = r.right + scale(18);
        }
        y += l.rowH;
    }
    if (list.used) {  // the steps (Default and Always show them too: a change makes it Custom)
        int level = 0;
        for (size_t i = 0; i < list.ops.size(); ++i) {
            const RequirementOp& r = list.ops[i];
            if (r.op == 0xFFFF) level = 0;
            const int indent = scale(level ? 22 : 0);
            const bool end = r.op == 0xFFFF;
            RECT opBox{l.x0 + indent, y + scale(2), l.x0 + l.labelW - scale(12), y + l.rowH - scale(2)};
            pickBox(dc, opBox, opName(r), false, [k, entry, i](RECT b) {
                pickRequirementOp(b, gExtras.reqs.list(k, entry).ops[i].op, [k, entry, i](RequirementOp n) {
                    RequirementList l2 = gExtras.reqs.list(k, entry);
                    const RequirementOp old = l2.ops[i];
                    if (opArgKind(n) && opArgKind(old) && std::string(opArgKind(n)) == opArgKind(old)) n.arg = old.op < 0x100 ? old.op : old.arg;
                    l2.ops[i] = n;
                    gExtras.reqs.set(k, entry, l2);
                    extrasChanged();
                });
            });
            if (end) {  // dimmed: it only closes a level's list
                RECT e{opBox.left + scale(8), opBox.top, opBox.right - scale(22), opBox.bottom};
                fillAlpha(dc, opBox, kBg, 120);
                (void)e;
            }
            if (const char* ak = opArgKind(r)) {
                const int v = r.op < 0x100 ? r.op : r.arg;
                RECT argBox{l.x0 + l.labelW, y + scale(2), l.x0 + l.labelW + scale(320), y + l.rowH - scale(2)};
                pickBox(dc, argBox, argName(ak, v), false,
                        [k, entry, i, ak](RECT b) {
                            std::string picked;
                            const RequirementOp cur = gExtras.reqs.list(k, entry).ops[i];
                            if (!showListPicker(L"Value", entryItems(ak), std::to_string(cur.op < 0x100 ? cur.op : cur.arg), belowBox(b), picked)) return;
                            RequirementList l2 = gExtras.reqs.list(k, entry);
                            RequirementOp& n = l2.ops[i];
                            if (n.op < 0x100) n.op = 0xFF03;  // "must have" with its value
                            n.arg = uint16_t(std::stoul(picked));
                            gExtras.reqs.set(k, entry, l2);
                            extrasChanged();
                        },
                        L"", argIcon(ak, v), v, openEntry(ak, v));
            }
            removeCross(dc, {l.x0, y, l.colRight, y + l.rowH}, {l.colRight - scale(24), y, l.colRight, y + l.rowH}, [k, entry, i] {
                RequirementList l2 = gExtras.reqs.list(k, entry);
                l2.ops.erase(l2.ops.begin() + long(i));
                gExtras.reqs.set(k, entry, l2);
                extrasChanged();
            }, L"Remove the step");
            if (r.op == 0xFF1F || r.op == 0xFF20 || r.op == 0xFF21) level = 1;  // a level's list follows (level 3 and more: to the end)
            y += l.rowH;
        }
        const int ay = y;
        const int ax = linkButton(dc, l.x0, y, l.rowH, L"Add a step", [k, entry, ay] {
            pickRequirementOp({scale(18), ay, scale(218), ay + scale(26)}, 0xFF03, [k, entry](RequirementOp n) {
                RequirementList l2 = gExtras.reqs.list(k, entry);
                l2.ops.push_back(n);
                gExtras.reqs.set(k, entry, l2);
                extrasChanged();
            });
        });
        (void)ax;
    }
    y += l.rowH;
    y = capacityBar(dc, l, y, gExtras.reqs.used(), Requirements::capacity(), L"Memory all requirement lists use together");
    return y + scale(14);
}

int gWireframeTop = -1;  // where the last painted Display section's wireframe rows start (form content; the self-test)

// The wireframes of the art View shows (SD, HD2 or the Carbot skin), loaded when first shown; nullptr when the
// game's files do not have them.
Wireframes* wireframeSet(WireKind kind) {
    static std::map<std::pair<int, int>, std::unique_ptr<Wireframes>> sets;  // (kind, art)
    const int art = app.carbot ? 2 : app.quality == GraphicsQuality::SD ? 0 : 1;
    auto& s = sets[{int(kind), art}];
    if (!s) {
        s = std::make_unique<Wireframes>();
        std::string err;
        if (!s->load(app.data, kind, art != 0, art == 2, err)) *s = {};
    }
    return s->count() ? s.get() : nullptr;
}

// The three wireframes a unit shows (`frames`: single, group, transport; -1 for none), as the status panel shows
// them at full health. At (x, top), to the right of the rows top..bottom; under them (from `bottom`) when the form
// is too narrow. Returns the bottom of what it drew, at least `bottom`.
int paintWireframePreviews(HDC dc, const FormLayout& l, int u, const int (&frames)[3], int x, int top, int bottom) {
    static const WireKind kinds[3] = {WireKind::Single, WireKind::Group, WireKind::Transport};
    static const wchar_t* captions[3] = {L"Wireframe", L"Group", L"Transport"};
    const int cap = scale(18), gap = scale(8);
    const int pic = std::max(scale(64), bottom - top);
    const bool under = x + 3 * pic + 2 * gap > l.colRight;
    if (under) x = l.x0 + l.labelW, top = bottom + scale(4);
    const bool shields = datValue("units", "Shield Enable", u) != 0;
    HGDIOBJ of = SelectObject(dc, ui::smallFont());
    for (int k = 0; k < 3; ++k) {
        const RECT r{x + k * (pic + gap), top, x + k * (pic + gap) + pic, top + pic};
        fillAlpha(dc, r, RGB(0, 0, 0), 255);  // the status panel's black, in both themes
        outline(dc, r, kBorder);
        Wireframes* set = frames[k] >= 0 ? wireframeSet(kinds[k]) : nullptr;
        const Sprite* s = set ? set->picture(frames[k], shields) : nullptr;
        if (s) {
            // One scale for the three, so that a group picture is smaller than a single one, as in the game: a single
            // wireframe's box (64 SD pixels, 128 HD2 pixels) fills the box. SD art in whole screen pixels a pixel.
            const float fit = float(pic - scale(6)) / float(set->hd() ? 128 : 64);
            const float zoom = std::min(set->hd() || fit < 1 ? fit : std::floor(fit), float(pic - scale(6)) / float(std::max(s->width, s->height)));
            blitSprite(dc, *s, (r.left + r.right) / 2, (r.top + r.bottom) / 2, zoom);
        } else {
            SetTextColor(dc, kDim);
            RECT t = r;
            DrawTextW(dc, frames[k] < 0 ? L"\x2014" : L"Not found", -1, &t, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        }
        RECT c{r.left, r.bottom, r.right, r.bottom + cap};
        SetTextColor(dc, kDim);
        DrawTextW(dc, captions[k], -1, &c, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_END_ELLIPSIS);
        std::wstring tip = k == 0 ? L"The wireframe" : k == 1 ? L"The group wireframe" : L"The transport wireframe";
        tip += frames[k] < 0 ? L": the game has none for this unit" : L" at full health" + std::wstring(shields ? L" and full shields" : L"") +
                                                                          L", as View shows the art. Frame " + std::to_wstring(frames[k]) + L" of " +
                                                                          widen(Wireframes::fileName(kinds[k])) + (set && set->hd() ? L".dds.grp" : L".grp");
        gWidgetHits.push_back({{r.left, r.top, r.right, c.bottom}, nullptr, nullptr, tip, IDC_ARROW, {}});
    }
    SelectObject(dc, of);
    return std::max(bottom, top + pic + cap);
}

// A unit's Display section: the button set it uses, its status panel, its wireframes (single, group, transport).
int paintUnitDisplay(HDC dc, int u, int y) {
    if (u < 0 || u >= UnitLooks::kUnits) return y;
    RECT rc;
    GetClientRect(gData.form, &rc);
    const FormLayout l = formLayout(rc);
    const UnitLooks::Unit& v = gExtras.looks.unit(u);
    const int set = gExtras.buttons.unitSet(u);
    y = sectionHeader(dc, l, y, L"Display", gExtras.looks.changed(u) || set != u);
    auto box = [&](int row) { return RECT{l.x0 + l.labelW, row + scale(2), l.x0 + l.labelW + scale(320), row + l.rowH - scale(2)}; };
    auto changeLooks = [u](const std::function<void(UnitLooks::Unit&)>& f) {
        UnitLooks::Unit n = gExtras.looks.unit(u);
        f(n);
        gExtras.looks.set(u, n);
        extrasChanged();
    };
    // The button set (its own: the unit's number).
    fieldLabel(dc, l, y, L"Button set", set != u);
    pickBox(dc, box(y), buttonSetName(set), set != u,
            [u, set](RECT b) {
                std::string picked;
                if (!showListPicker(L"Button set", entryItems("buttons"), std::to_string(set), belowBox(b), picked)) return;
                gExtras.buttons.setUnitSet(u, std::stoi(picked));
                extrasChanged();
            },
            L"The command card that the unit shows. The default value is the set with the unit's number.", argIcon("buttons", set), set, openEntry("buttons", set));
    y += l.rowH;
    // The status panel: one of the kinds (two functions of the game's code).
    const uint32_t st = gExtras.looks.status(u), di = gExtras.looks.display(u);
    const UnitPanelKind* kind = UnitLooks::kindOf(st, di);
    wchar_t custom[64];
    std::swprintf(custom, 64, L"Other (0x%06X, 0x%06X)", unsigned(st), unsigned(di));
    const bool panelChanged = v.status || v.display;
    fieldLabel(dc, l, y, L"Status panel", panelChanged);
    pickBox(dc, box(y), kind ? widen(kind->name) : custom, panelChanged,
            [u, changeLooks](RECT b) {
                std::vector<ListPickItem> items;
                const auto kinds = UnitLooks::kinds();
                for (size_t k = 0; k < kinds.size(); ++k) items.push_back({widen(kinds[k].name), L"", std::to_string(k)});
                std::string picked;
                if (!showListPicker(L"Status panel", items, "", belowBox(b), picked)) return;
                const UnitPanelKind& k = kinds[size_t(std::stoi(picked))];
                changeLooks([&](UnitLooks::Unit& n) {  // the game's own pair: no change
                    const bool own = k.status == gExtras.looks.stockStatus(u) && k.display == gExtras.looks.stockDisplay(u);
                    n.status = own ? 0 : k.status, n.display = own ? 0 : k.display;
                });
            },
            L"What the panel at the bottom shows when the unit is selected, for example a building's, a unit's or a transport's (its cargo)");
    y += l.rowH;
    // Wireframes: the unit whose picture shows (its own by default); group and transport have fewer.
    struct Wire {
        const wchar_t* name;
        int UnitLooks::Unit::*field;
        int count;
        const wchar_t* tip;
    };
    static const Wire wires[] = {
        {L"Wireframe", &UnitLooks::Unit::wireframe, UnitLooks::kWireframes, L"The picture in the selection panel when the unit is selected alone"},
        {L"Group wireframe", &UnitLooks::Unit::groupWireframe, UnitLooks::kGroupWireframes, L"The small picture when units are selected together"},
        {L"Transport wireframe", &UnitLooks::Unit::transportWireframe, UnitLooks::kTransportWireframes, L"The picture inside a transport"},
    };
    const int wireTop = y;
    gWireframeTop = wireTop + gData.formScroll;
    int shownFrame[3] = {-1, -1, -1};
    for (const Wire& w : wires) {
        const int now = v.*(w.field);
        const bool changed = now >= 0;
        fieldLabel(dc, l, y, w.name, changed);
        if (u >= w.count) {  // the game has no such picture for this unit
            RECT t = box(y);
            t.left += scale(8);
            SetTextColor(dc, kDim);
            DrawTextW(dc, L"\x2014", -1, &t, DT_SINGLELINE | DT_VCENTER);
            y += l.rowH;
            continue;
        }
        const int shown = changed ? now : u;
        shownFrame[&w - wires] = shown;
        pickBox(dc, box(y), entryName("units", shown), changed,
                [u, w, changeLooks](RECT b) {
                    std::vector<ListPickItem> items = entryItems("units");
                    items.resize(std::min(items.size(), size_t(w.count)));
                    std::string picked;
                    if (!showListPicker(w.name, items, "", belowBox(b), picked)) return;
                    const int p = std::stoi(picked);
                    changeLooks([&](UnitLooks::Unit& n) { n.*(w.field) = p == u ? -1 : p; });
                },
                w.tip, datRefIcon("units", uint32_t(shown)), shown);
        y += l.rowH;
    }
    y = paintWireframePreviews(dc, l, u, shownFrame, box(wireTop).right + scale(16), wireTop, y);
    return y + scale(14);
}

// The requirement lists of the open entry, for the .dat forms that have them.
int paintRequirements(HDC dc, const std::string& dat, int entry, int, int y, int) {
    RECT rc;
    GetClientRect(gData.form, &rc);
    const FormLayout l = formLayout(rc);
    for (int k = 0; k < Requirements::kKinds; ++k)
        if (Requirements::kind(k).dat == dat && entry < Requirements::kind(k).entries) y = paintRequirementList(dc, l, k, entry, y);
    return y;
}

// --- Buttons tab -----------------------------------------------------------------------------------------------

// A button's label: its tooltip's first line (or what it does). With its icon, that is enough to know it.
std::wstring buttonRowText(const Button& b) {
    std::wstring text = b.enabledString ? statString(b.enabledString) : L"";
    if (const size_t nl = text.find(L'\n'); nl != std::wstring::npos) text.resize(nl);
    if (text.empty())
        if (const ButtonFunctionDef* a = ButtonSets::action(b.action)) text = widen(a->name);
    return text.empty() ? L"(no text)" : text;
}

// The game's text with its colors, in the rich text pop-out (main.cpp); false when it was not changed.
bool editGameText(const std::wstring& title, std::string& text, POINT at);

// What a button costs, as the tooltip of its type shows it ("Make a unit", "Upgrade", "Use a tech", "Research").
std::wstring buttonCost(const Button& b, int type) {
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
        const int supply = datValue("units", "Supply Required", v);
        return join({{datValue("units", "Mineral Cost", v), L"minerals"}, {datValue("units", "Vespene Cost", v), L"gas"}, {(supply + 1) / 2, L"supply"}});
    }
    case 2: return join({{datValue("upgrades", "Mineral Cost Base", v), L"minerals"}, {datValue("upgrades", "Vespene Cost Base", v), L"gas"}});
    case 3: return join({{datValue("techdata", "Energy Required", v), L"energy"}});
    case 4: return join({{datValue("techdata", "Mineral Cost", v), L"minerals"}, {datValue("techdata", "Vespene Cost", v), L"gas"}});
    }
    return L"";
}

// The tooltip as the game shows it: the text, and the cost line its type adds.
int paintButtonTooltip(HDC dc, RECT box, const Button& b) {
    if (!b.enabledString) return 0;
    const std::string& raw = gExtras.strings.text(b.enabledString - 1);
    const int type = buttonPrefix(raw) ? raw[1] : 0;
    const std::wstring cost = buttonCost(b, type);
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    const int lines = 1 + int(std::count(raw.begin(), raw.end(), '\n')) + (cost.empty() ? 0 : 1);
    box.bottom = box.top + lines * (tm.tmHeight + scale(2)) + scale(16);
    fillAlpha(dc, box, RGB(8, 8, 10), 255);
    roundBox(dc, box, scale(4), RGB(60, 64, 70));
    RECT tr{box.left + scale(10), box.top + scale(8), box.right - scale(10), box.bottom};
    const int h = drawGameText(dc, tr, raw, RGB(208, 208, 208));
    if (!cost.empty()) {
        RECT cr{tr.left, tr.top + h, tr.right, tr.top + h + tm.tmHeight + scale(2)};
        SetTextColor(dc, RGB(150, 190, 220));
        DrawTextW(dc, cost.c_str(), -1, &cr, DT_SINGLELINE | DT_VCENTER);
    }
    return box.bottom - box.top;
}

// The command card: 3 x 3 cells, each with its button's icon. Click selects (again: the next button in that
// place); drag to another cell moves it; an empty cell adds a button there.
int paintCommandCard(HDC dc, int set, int x, int y, int cell) {
    const auto& bs = gExtras.buttons.set(set);
    const int gap = scale(4);
    RECT card{x, y, x + 3 * cell + 4 * gap, y + 3 * cell + 4 * gap};
    fillAlpha(dc, card, RGB(14, 14, 16), 255);
    roundBox(dc, card, scale(6), kBorder);
    for (int p = 1; p <= 9; ++p) {
        RECT c{x + gap + (p - 1) % 3 * (cell + gap), y + gap + (p - 1) / 3 * (cell + gap), 0, 0};
        c.right = c.left + cell, c.bottom = c.top + cell;
        std::vector<int> here;
        for (size_t i = 0; i < bs.size(); ++i)
            if (bs[i].pos == p) here.push_back(int(i));
        const bool sel = std::find(here.begin(), here.end(), gExtras.button) != here.end();
        const bool hot = mouseIn(c);
        fillAlpha(dc, c, sel ? kSel : hot ? RGB(36, 39, 44) : RGB(26, 28, 32), 255);
        roundBox(dc, c, scale(4), sel ? kAccent : RGB(48, 52, 58), sel ? 2 : 1);
        if (!here.empty()) {
            const int shown = sel ? gExtras.button : here[0];
            if (const auto* b = datRefIcon("icons", bs[size_t(shown)].icon); b && b->bmp) drawFitted(dc, *b, {c.left + scale(5), c.top + scale(5), c.right - scale(5), c.bottom - scale(5)});
            if (here.size() > 1) {  // more than one button here: the game shows the first whose condition holds; a dot each, in order
                const int d = scale(5), dg = scale(3), w = int(here.size()) * (d + dg) - dg;
                for (size_t j = 0; j < here.size(); ++j) {
                    const int dx = (c.left + c.right - w) / 2 + int(j) * (d + dg), dy = c.bottom - scale(9);
                    RECT dot{dx, dy, dx + d, dy + d};
                    fillAlpha(dc, dot, here[j] == shown ? kAccent : RGB(110, 114, 120), 255);
                }
            }
        } else if (hot) {
            SetTextColor(dc, kDim);
            DrawTextW(dc, L"+", 1, &c, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        }
        std::wstring tip;
        if (!here.empty()) {
            const Button& b = bs[size_t(here[0])];
            tip = statString(b.enabledString);
            if (here.size() > 1) tip += L"\n" + std::to_wstring(here.size()) + L" buttons in this place. The game shows the first whose condition holds. Click again for the next one.";
        } else {
            tip = L"Add a button here";
        }
        const int p0 = p;
        hitsOut().push_back({c,
                               [set, x, y, cell, gap](POINT pt) {  // drag: the selected button to the cell under the mouse
                                   const int cx = (pt.x - x - gap) / (cell + gap), cy = (pt.y - y - gap) / (cell + gap);
                                   if (cx < 0 || cy < 0 || cx > 2 || cy > 2) return;
                                   auto bs2 = gExtras.buttons.set(set);
                                   if (gExtras.button < 0 || gExtras.button >= int(bs2.size())) return;
                                   const uint16_t np = uint16_t(cy * 3 + cx + 1);
                                   if (bs2[size_t(gExtras.button)].pos == np) return;
                                   bs2[size_t(gExtras.button)].pos = np;
                                   gExtras.buttons.set(set, bs2);
                                   InvalidateRect(gData.form, nullptr, FALSE);
                               },
                               [set, here, p0] {
                                   if (here.empty()) {  // a new button
                                       auto bs2 = gExtras.buttons.set(set);
                                       Button nb;
                                       nb.pos = uint16_t(p0), nb.icon = 0, nb.condition = 0x4282D0, nb.action = 0x4233F0;
                                       bs2.push_back(nb);
                                       gExtras.buttons.set(set, bs2);
                                       gExtras.button = int(bs2.size()) - 1;
                                       extrasChanged();
                                       return;
                                   }
                                   auto it = std::find(here.begin(), here.end(), gExtras.button);
                                   gExtras.button = it == here.end() || it + 1 == here.end() ? here[0] : *(it + 1);
                                   InvalidateRect(gData.form, nullptr, FALSE);
                               },
                               tip, here.empty() ? IDC_HAND : IDC_SIZEALL, {}});
    }
    return card.bottom - y;
}

void paintButtonsForm(HDC dc, RECT rc, int& y) {
    const FormLayout l = formLayout(rc);
    const int set = std::clamp(gData.entry, 0, ButtonSets::kSets - 1);
    const auto& bs = gExtras.buttons.set(set);
    if (gExtras.button >= int(bs.size())) gExtras.button = bs.empty() ? 0 : int(bs.size()) - 1;
    y = formTitle(dc, l, y, tabEntryIcon(buttonsTab(), set), buttonSetName(set), L"Button set " + std::to_wstring(set) + L"  \x00B7  " + std::to_wstring(bs.size()) + L" buttons",
                  gExtras.buttons.changed(set), [set] {
                      gExtras.buttons.reset(set);
                      extrasChanged();
                  });
    // The card and the tooltip: pictures, in the right column (above the fields when the form is narrow).
    const int cell = scale(64), top = y;
    int picBottom = y + paintCommandCard(dc, set, l.picX, y, cell);
    // The buttons, in the order the game checks them: in one place, the first whose condition holds shows.
    y = sectionHeader(dc, l, y, L"Buttons", gExtras.buttons.changed(set));
    for (size_t i = 0; i < bs.size(); ++i) {
        const Button& b = bs[i];
        RECT r{l.x0 - scale(6), y, l.colRight, y + l.rowH};
        const bool on = int(i) == gExtras.button, hot = mouseIn(r);
        ui::listRow(dc, r, on, hot);
        int sharing = 0;  // others in the same place
        for (const Button& o : bs) sharing += o.pos == b.pos;
        const int gx = l.x0 + scale(14);  // after the grip
        SetTextColor(dc, kDim);
        RECT nr{gx, y, gx + scale(26), y + l.rowH};
        DrawTextW(dc, std::to_wstring(i).c_str(), -1, &nr, DT_SINGLELINE | DT_VCENTER);
        if (const auto* ic = datRefIcon("icons", b.icon); ic && ic->bmp) drawFitted(dc, *ic, {gx + scale(28), y + scale(3), gx + scale(48), y + l.rowH - scale(3)});
        RECT pr{gx + scale(54), y, gx + scale(120), y + l.rowH};
        SetTextColor(dc, sharing > 1 ? kWarn : kDim);
        DrawTextW(dc, (L"R" + std::to_wstring((b.pos - 1) / 3 + 1) + L" C" + std::to_wstring((b.pos - 1) % 3 + 1)).c_str(), -1, &pr, DT_SINGLELINE | DT_VCENTER);
        RECT tr{gx + scale(120), y, l.colRight - scale(80), y + l.rowH};
        SetTextColor(dc, on ? kAccent : kText);
        DrawTextW(dc, buttonRowText(b).c_str(), -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        const int bi = int(i);
        hitsOut().push_back({r, nullptr, [bi] {
                                 gExtras.button = bi;
                                 InvalidateRect(gData.form, nullptr, FALSE);
                             },
                             sharing > 1 ? L"Shares its place with another button: the game shows the first whose condition holds (the higher one in this list)" : L"",
                             IDC_HAND, {}});
        ui::reorderRow(dc, "buttons", r, bi, [set](int from, int to) {
            auto bs2 = gExtras.buttons.set(set);
            const Button moved = bs2[size_t(from)];
            bs2.erase(bs2.begin() + from);
            bs2.insert(bs2.begin() + to, moved);
            gExtras.buttons.set(set, bs2);
            gExtras.button = to;
            extrasChanged();
        });
        // On the row: earlier, later, remove.
        auto move = [set, bi](int by) {
            return [set, bi, by] {
                auto bs2 = gExtras.buttons.set(set);
                std::swap(bs2[size_t(bi)], bs2[size_t(bi + by)]);
                gExtras.buttons.set(set, bs2);
                gExtras.button = bi + by;
                extrasChanged();
            };
        };
        ui::rowActions(dc, r,
                       {{UiIcon::MoveUp, L"Earlier: checked before the one above", move(-1), ui::Tone::Normal, false, i > 0},
                        {UiIcon::MoveDown, L"Later", move(1), ui::Tone::Normal, false, i + 1 < bs.size()},
                        {UiIcon::Close, L"Remove the button",
                         [set, bi] {
                             auto bs2 = gExtras.buttons.set(set);
                             if (size_t(bi) < bs2.size()) bs2.erase(bs2.begin() + long(bi));
                             gExtras.buttons.set(set, bs2);
                             gExtras.button = std::max(0, bi - 1);
                             extrasChanged();
                         },
                         ui::Tone::Danger}},
                       false);
        y += l.rowH;
    }
    ui::reorderDropLine(dc, "buttons");
    {
        const int ay = y;
        linkButton(dc, l.x0, y, l.rowH, L"Add a button", [set, ay] {
            auto bs2 = gExtras.buttons.set(set);
            uint16_t p = 1;
            for (; p <= 9; ++p)
                if (std::none_of(bs2.begin(), bs2.end(), [p](const Button& x) { return x.pos == p; })) break;
            Button nb;
            nb.pos = std::min<uint16_t>(p, 9), nb.condition = 0x4282D0, nb.action = 0x4233F0;
            bs2.push_back(nb);
            gExtras.buttons.set(set, bs2);
            gExtras.button = int(bs2.size()) - 1;
            extrasChanged();
            (void)ay;
        });
        y += l.rowH + scale(8);
    }
    y = sectionHeader(dc, l, y, bs.empty() ? L"Button" : L"Button " + std::to_wstring(gExtras.button), gExtras.buttons.changed(set));
    if (!bs.empty()) picBottom += scale(12) + paintButtonTooltip(dc, {l.picX, picBottom + scale(12), l.picX + 3 * cell + scale(16), 0}, bs[size_t(gExtras.button)]);
    if (!l.twoCol) y = picBottom + scale(16);
    if (bs.empty()) {
        fieldLabel(dc, l, y, L"No buttons", false);
        y += l.rowH;
    } else {
        const int bi = gExtras.button;
        const Button& b = bs[size_t(bi)];
        const auto& stock = gExtras.buttons.stock(set);
        const Button* sb = size_t(bi) < stock.size() ? &stock[size_t(bi)] : nullptr;
        auto edit = [set, bi](std::function<void(Button&)> f) {
            auto bs2 = gExtras.buttons.set(set);
            if (size_t(bi) >= bs2.size()) return;
            f(bs2[size_t(bi)]);
            gExtras.buttons.set(set, bs2);
            extrasChanged();
        };
        auto row = [&](const wchar_t* name, bool changed, const std::wstring& text, std::function<void(RECT)> click, const UnitPalette::Bitmap* icon, int number,
                       std::function<void()> open, const std::wstring& tip = L"") {
            fieldLabel(dc, l, y, name, changed);
            pickBox(dc, {l.x0 + l.labelW, y + scale(2), l.x0 + l.labelW + scale(320), y + l.rowH - scale(2)}, text, changed, std::move(click), tip, icon, number,
                    std::move(open));
            y += l.rowH;
        };
        row(L"Position", sb && sb->pos != b.pos, L"Row " + std::to_wstring((b.pos - 1) / 3 + 1) + L", column " + std::to_wstring((b.pos - 1) % 3 + 1),
            [edit, b](RECT r) {
                std::vector<ListPickItem> items;
                for (int p = 1; p <= 9; ++p) items.push_back({L"Row " + std::to_wstring((p - 1) / 3 + 1) + L", column " + std::to_wstring((p - 1) % 3 + 1), L"", std::to_string(p)});
                std::string picked;
                if (showListPicker(L"Position", items, std::to_string(b.pos), belowBox(r), picked)) edit([&](Button& x) { x.pos = uint16_t(std::stoul(picked)); });
            },
            nullptr, b.pos, nullptr);
        const auto& iconNames = DatSchema::entryNames("icons");
        row(L"Icon", sb && sb->icon != b.icon, b.icon < iconNames.size() ? widen(iconNames[b.icon]) : L"Icon",
            [edit, b](RECT r) {
                std::string picked;
                if (showListPicker(L"Icon", entryItems("icons"), std::to_string(b.icon), belowBox(r), picked)) edit([&](Button& x) { x.icon = uint16_t(std::stoul(picked)); });
            },
            datRefIcon("icons", b.icon), b.icon, nullptr);
        // Condition and action: their functions, and a value when the function takes one.
        for (int which = 0; which < 2; ++which) {
            const bool cond = which == 0;
            const uint32_t fn = cond ? b.condition : b.action;
            const ButtonFunctionDef* d = cond ? ButtonSets::condition(fn) : ButtonSets::action(fn);
            wchar_t hex[24];
            std::swprintf(hex, 24, L"Function 0x%06X", unsigned(fn));
            row(cond ? L"Shown when" : L"Action", sb && (cond ? sb->condition != fn : sb->action != fn), d ? widen(d->name) : std::wstring(hex),
                [edit, cond, fn](RECT r) {
                    std::vector<ListPickItem> items;
                    for (const auto& f : cond ? ButtonSets::conditions() : ButtonSets::actions()) items.push_back({widen(f.name), L"", std::to_string(f.address)});
                    std::string picked;
                    if (showListPicker(cond ? L"Shown when" : L"Action", items, std::to_string(fn), belowBox(r), picked))
                        edit([&](Button& x) { (cond ? x.condition : x.action) = uint32_t(std::stoul(picked)); });
                },
                nullptr, -1, nullptr, cond ? L"The game's condition for showing the button" : L"Select the action that the button performs");
            if (d && d->arg) {
                const uint16_t v = cond ? b.conditionArg : b.actionArg;
                const char* kind = d->arg;
                row(cond ? L"Shown when: value" : L"Action: value", sb && (cond ? sb->conditionArg != v : sb->actionArg != v), argName(kind, v),
                    [edit, cond, v, kind](RECT r) {
                        std::string picked;
                        if (showListPicker(L"Value", entryItems(kind), std::to_string(v), belowBox(r), picked))
                            edit([&](Button& x) { (cond ? x.conditionArg : x.actionArg) = uint16_t(std::stoul(picked)); });
                    },
                    argIcon(kind, v), v, openEntry(kind, v));
            }
        }
        // Texts: the tooltip itself, edited here (the string in stat_txt.tbl: every button that uses it changes);
        // the arrow picks another string. Under each: a link to the string in the Strings tab, and its raw value when
        // "Display raw value" is on (the same setting as the Strings tab's).
        ui::check(dc, {l.x0 + l.labelW, y, l.x0 + l.labelW + scale(300), y + l.rowH}, L"Display raw value", gExtras.rawText, [] {
            gExtras.rawText = !gExtras.rawText;
            gExtras.textFor = -1;
            InvalidateRect(gData.form, nullptr, FALSE);
        }, L"Show each tooltip's string as the game stores it: the hotkey and type characters, and codes as <01>");
        y += l.rowH;
        for (int which = 0; which < 2; ++which) {
            const uint16_t s = which ? b.disabledString : b.enabledString;
            const bool strChanged = s && gExtras.strings.changed(s - 1);
            fieldLabel(dc, l, y, which ? L"Tooltip when grayed" : L"Tooltip", (sb && (which ? sb->disabledString != s : sb->enabledString != s)) || strChanged);
            RECT box{l.x0 + l.labelW, y + scale(2), l.colRight - scale(30), y + l.rowH - scale(2)};
            const bool hot = mouseIn(box);
            // The game's text colors read on a dark ground only: the box stays dark in the light theme.
            fillAlpha(dc, box, gLightTheme ? (hot ? RGB(46, 48, 54) : RGB(34, 36, 40)) : hot ? RGB(36, 39, 44) : kCtl, 255);
            roundBox(dc, box, scale(6), strChanged ? kGoodLine : kBorder);
            RECT tb{box.left + scale(8), box.top, box.right - scale(8), box.bottom};
            if (s) {
                const std::string& raw = gExtras.strings.text(s - 1);
                const size_t nl = raw.find('\n');
                const int saved = SaveDC(dc);
                IntersectClipRect(dc, tb.left, tb.top, tb.right, tb.bottom);
                drawGameText(dc, {tb.left, tb.top + (tb.bottom - tb.top - scale(18)) / 2, tb.right, tb.bottom}, nl == std::string::npos ? raw : raw.substr(0, nl),
                             RGB(184, 184, 232));
                RestoreDC(dc, saved);
            } else {
                SetTextColor(dc, kDim);
                DrawTextW(dc, L"None", -1, &tb, DT_SINGLELINE | DT_VCENTER);
            }
            hitsOut().push_back({box, nullptr, [edit, which, s, box] {
                                     if (!s) return;
                                     const std::string raw = gExtras.strings.text(s - 1);
                                     const size_t lead = buttonPrefix(raw) ? 2 : 0;  // the hotkey and the button type stay
                                     std::string body = raw.substr(lead);
                                     const bool crlf = body.find("\r\n") != std::string::npos;
                                     for (size_t at; (at = body.find("\r\n")) != std::string::npos;) body.erase(at, 1);
                                     POINT pt{box.left, box.bottom + scale(4)};
                                     ClientToScreen(gData.form, &pt);
                                     std::string out = body;
                                     if (!editGameText(which ? L"Tooltip when grayed" : L"Tooltip", out, pt)) return;
                                     if (crlf) {  // the string's own line ends
                                         std::string o;
                                         for (char ch : out) ch == '\n' ? o += "\r\n" : o += ch;
                                         out = o;
                                     }
                                     gExtras.strings.set(s - 1, raw.substr(0, lead) + out);
                                     extrasChanged();
                                 },
                                 s ? L"Edit the text (string " + std::to_wstring(s) + L"; every button that uses it changes)" : L"", IDC_HAND, {}});
            RECT pick{box.right + scale(6), y + scale(2), l.colRight, y + l.rowH - scale(2)};
            ui::glyph(dc, pick, UiIcon::Dropdown, mouseIn(pick) ? kAccent : kDim);  // a list: another string
            hitsOut().push_back({pick, nullptr, [edit, which, s, pick] {
                                     std::string picked;
                                     if (showListPicker(L"Text", entryItems("strings", true), std::to_string(s), belowBox(pick), picked))
                                         edit([&](Button& x) { (which ? x.disabledString : x.enabledString) = uint16_t(std::stoul(picked)); });
                                 },
                                 L"Another string", IDC_HAND, {}});
            y += l.rowH;
            if (s) {  // the string: go to it; its raw value
                const int sid = s;
                const int lx = ui::link(dc, l.x0 + l.labelW, y, scale(22), L"String " + std::to_wstring(s), [sid] { dataGoTo("strings", sid); },
                                        L"Open this string in the Strings tab", ui::Tone::Primary);
                if (gExtras.rawText) {
                    HGDIOBJ of2 = SelectObject(dc, monoFont());
                    SetTextColor(dc, kDim);
                    RECT rt{lx + scale(12), y, l.colRight, y + scale(22)};
                    DrawTextW(dc, widen(StatTxt::escape(gExtras.strings.text(s - 1))).c_str(), -1, &rt,
                              DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
                    SelectObject(dc, of2);
                }
                y += scale(24);
            }
        }
    }
    if (l.twoCol) y = std::max(y, picBottom);
    (void)top;
    y += scale(14);
}

// --- A whole button, in a pop-out (a trigger's Replace a button) ------------------------------------------------

// "place,icon,condition,action,conditionValue,actionValue,tooltip,tooltipNotUsable", and back.
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


struct ButtonPop {
    HWND wnd = nullptr;
    Button b;
    bool done = false, accepted = false, picking = false;
    std::vector<DataWidgetHit> hits;
    RECT ok{}, cancel{};
} * gButtonPop = nullptr;

void buttonPopPaint(HWND hwnd) {
    ButtonPop& p = *gButtonPop;
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    HGDIOBJ ob = SelectObject(mem, bmp), of = SelectObject(mem, app.font);
    FillRect(mem, &rc, app.bgBrush);
    SetBkMode(mem, TRANSPARENT);
    p.hits.clear();
    std::vector<DataWidgetHit>* savedOut = gHitsOut;
    HWND savedWnd = gHitsWnd;
    gHitsOut = &p.hits, gHitsWnd = hwnd;
    gHitsOf[hwnd] = &p.hits;
    FormLayout l{};
    l.x0 = scale(16), l.labelW = scale(150), l.rowH = scale(28), l.colRight = rc.right - scale(16), l.colW = l.colRight - l.x0;
    int y = scale(12);
    // The button as the card shows it, and its tooltip.
    const int cell = scale(64);
    RECT c{l.x0, y, l.x0 + cell, y + cell};
    fillAlpha(mem, c, RGB(26, 28, 32), 255);
    roundBox(mem, c, scale(4), kAccent, 2);
    if (const auto* ic = datRefIcon("icons", p.b.icon); ic && ic->bmp) drawFitted(mem, *ic, {c.left + scale(5), c.top + scale(5), c.right - scale(5), c.bottom - scale(5)});
    const int th = paintButtonTooltip(mem, {c.right + scale(12), y, l.colRight, 0}, p.b);
    y += std::max(cell, th) + scale(10);
    const int ly = y;
    linkButton(mem, l.x0, y, l.rowH, L"Copy a button from a set\x2026", [ly] {
        ButtonPop& q = *gButtonPop;
        std::string set;
        q.picking = true;
        POINT at{scale(16), ly + scale(28)};
        ClientToScreen(q.wnd, &at);
        if (showListPicker(L"Button set", entryItems("buttons"), "", at, set)) {
            const auto& bs = gExtras.buttons.set(std::stoi(set));
            std::vector<ListPickItem> items;
            for (size_t i = 0; i < bs.size(); ++i)
                items.push_back({buttonRowText(bs[i]), L"", std::to_string(i), "icons", int(bs[i].icon)});
            std::string pick;
            if (!items.empty() && showListPicker(L"Button", items, "", at, pick)) q.b = bs[size_t(std::stoi(pick))];
        }
        q.picking = false;
        InvalidateRect(q.wnd, nullptr, FALSE);
    });
    y += l.rowH + scale(6);
    // Its fields, as the Buttons tab has them.
    auto row = [&](const wchar_t* name, const std::wstring& text, std::function<void(RECT)> click, const UnitPalette::Bitmap* icon = nullptr, int number = -1) {
        fieldLabel(mem, l, y, name, false);
        pickBox(mem, {l.x0 + l.labelW, y + scale(2), l.colRight, y + l.rowH - scale(2)}, text, false, [click](RECT r) {
            gButtonPop->picking = true;
            click(r);
            gButtonPop->picking = false;
            InvalidateRect(gButtonPop->wnd, nullptr, FALSE);
        }, L"", icon, number);
        y += l.rowH;
    };
    Button& b = p.b;
    static const wchar_t* const kPlaces[9] = {L"Top left", L"Top center", L"Top right", L"Left", L"Center", L"Right", L"Bottom left", L"Bottom center", L"Bottom right"};
    row(L"Place", b.pos >= 1 && b.pos <= 9 ? kPlaces[b.pos - 1] : L"?", [](RECT r) {
        std::vector<ListPickItem> items;
        for (int k = 1; k <= 9; ++k) items.push_back({kPlaces[k - 1], L"", std::to_string(k)});
        std::string picked;
        if (showListPicker(L"Place", items, std::to_string(gButtonPop->b.pos), belowBox(r), picked)) gButtonPop->b.pos = uint16_t(std::stoul(picked));
    });
    const auto& iconNames = DatSchema::entryNames("icons");
    row(L"Icon", b.icon < iconNames.size() ? widen(iconNames[b.icon]) : L"Icon", [](RECT r) {
        std::string picked;
        if (showListPicker(L"Icon", entryItems("icons"), std::to_string(gButtonPop->b.icon), belowBox(r), picked)) gButtonPop->b.icon = uint16_t(std::stoul(picked));
    }, datRefIcon("icons", b.icon), b.icon);
    for (int which = 0; which < 2; ++which) {
        const bool cond = which == 0;
        const uint32_t fn = cond ? b.condition : b.action;
        const ButtonFunctionDef* d = cond ? ButtonSets::condition(fn) : ButtonSets::action(fn);
        wchar_t hex[24];
        std::swprintf(hex, 24, L"Function 0x%06X", unsigned(fn));
        row(cond ? L"Shown when" : L"Action", d ? widen(d->name) : std::wstring(hex), [cond](RECT r) {
            std::vector<ListPickItem> items;
            for (const auto& f : cond ? ButtonSets::conditions() : ButtonSets::actions()) items.push_back({widen(f.name), L"", std::to_string(f.address)});
            std::string picked;
            Button& x = gButtonPop->b;
            if (showListPicker(cond ? L"Shown when" : L"Action", items, std::to_string(cond ? x.condition : x.action), belowBox(r), picked))
                (cond ? x.condition : x.action) = uint32_t(std::stoul(picked));
        });
        if (d && d->arg) {
            const uint16_t v = cond ? b.conditionArg : b.actionArg;
            const char* kind = d->arg;
            row(cond ? L"Shown when: value" : L"Action: value", argName(kind, v), [cond, kind](RECT r) {
                std::string picked;
                Button& x = gButtonPop->b;
                if (showListPicker(L"Value", entryItems(kind), std::to_string(cond ? x.conditionArg : x.actionArg), belowBox(r), picked))
                    (cond ? x.conditionArg : x.actionArg) = uint16_t(std::stoul(picked));
            }, argIcon(kind, v), v);
        }
    }
    for (int which = 0; which < 2; ++which) {
        const uint16_t s = which ? b.disabledString : b.enabledString;
        row(which ? L"Tooltip when grayed" : L"Tooltip", s ? statString(s) : L"None", [which](RECT r) {
            std::string picked;
            Button& x = gButtonPop->b;
            if (showListPicker(L"Tooltip", entryItems("strings", true), std::to_string(which ? x.disabledString : x.enabledString), belowBox(r), picked))
                (which ? x.disabledString : x.enabledString) = uint16_t(std::stoul(picked));
        }, nullptr, s ? s : -1);
    }
    // OK, Cancel.
    y += scale(10);
    const int bh = scale(ui::kButtonH);
    ui::buttonsRight(mem, l.colRight, y, {{L"OK", ui::Tone::Primary, [] { gButtonPop->accepted = gButtonPop->done = true; }},
                                          {L"Cancel", ui::Tone::Normal, [] { gButtonPop->done = true; }}});
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, of), SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
    gHitsOut = savedOut, gHitsWnd = savedWnd;
    const int want = y + bh + scale(14);
    if (want != rc.bottom) {  // fit the fields (they change with the functions' values)
        RECT wr;
        GetWindowRect(hwnd, &wr);
        SetWindowPos(hwnd, nullptr, 0, 0, wr.right - wr.left, wr.bottom - wr.top - rc.bottom + want, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

LRESULT CALLBACK buttonPopProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    ButtonPop* p = gButtonPop;
    switch (msg) {
    case WM_PAINT:
        if (p) buttonPopPaint(hwnd);
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEMOVE: InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_LBUTTONUP:
        if (p) {
            const POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            for (auto it = p->hits.rbegin(); it != p->hits.rend(); ++it)
                if (PtInRect(&it->r, pt) && it->click) {
                    HWND saved = gHitsWnd;
                    gHitsWnd = hwnd;  // the pickers open below this window's boxes
                    auto click = it->click;
                    click();
                    gHitsWnd = saved;
                    break;
                }
        }
        return 0;
    case WM_KEYDOWN:
        if (p && wp == VK_ESCAPE) p->done = true;
        if (p && wp == VK_RETURN) p->accepted = p->done = true;
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE && p && !p->picking && !gBackground) p->accepted = p->done = true;  // clicking elsewhere keeps it
        return 0;
    case WM_SETCURSOR:
        if (p && LOWORD(lp) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            for (auto it = p->hits.rbegin(); it != p->hits.rend(); ++it)
                if (PtInRect(&it->r, pt)) return SetCursor(LoadCursor(nullptr, it->cursor)), TRUE;
        }
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// The button pop-out: `text` as buttonToText writes it; true when OK.
bool showButtonPopup(std::string& text, POINT at) {
    if (gListWheel) return false;  // the mouse wheel does not open a pop-out
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{sizeof wc};
        wc.lpfnWndProc = buttonPopProc, wc.hInstance = app.inst, wc.hCursor = LoadCursor(nullptr, IDC_ARROW), wc.lpszClassName = L"StarDraftButtonPop";
        RegisterClassExW(&wc);
        registered = true;
    }
    dataLoadForProject();  // the icons and strings come from the project
    ButtonPop p;
    if (!buttonFromText(text, p.b)) p.b = Button{1, 0, 0x4282D0, 0x4233F0, 0, 0, 0, 0};
    ButtonPop* outer = gButtonPop;
    gButtonPop = &p;
    const int w = scale(560), h = scale(470);
    p.wnd = CreateWindowExW(WS_EX_TOOLWINDOW | (gBackground ? WS_EX_NOACTIVATE : 0), L"StarDraftButtonPop", L"", WS_POPUP | WS_BORDER, at.x, at.y, w, h, app.main,
                            nullptr, app.inst, nullptr);
    ui::popupFrame(p.wnd);
    ShowWindow(p.wnd, gBackground ? SW_SHOWNOACTIVATE : SW_SHOW);
    if (gButtonPopHook) gButtonPopHook(p.wnd);
    MSG msg;
    while (!p.done && GetMessageW(&msg, nullptr, 0, 0))
        if (!popoutEatsPress(msg, p.wnd)) TranslateMessage(&msg), DispatchMessageW(&msg);
    gHitsOf.erase(p.wnd);  // its hits go with it
    DestroyWindow(p.wnd);
    gButtonPop = outer;
    if (p.accepted) text = buttonToText(p.b);
    return p.accepted;
}

// --- Strings tab -----------------------------------------------------------------------------------------------

// The editor (game_text.inc) holds the text without a button's hotkey and type characters (the fields show them).
std::string stringEditBody(int id) {
    const std::string& raw = gExtras.strings.text(id - 1);
    return buttonPrefix(raw) ? raw.substr(2) : raw;
}
std::string stringEditPrefix(int id) {
    const std::string& raw = gExtras.strings.text(id - 1);
    return buttonPrefix(raw) ? raw.substr(0, 2) : "";
}
void stringsCommitEdit() {
    if (!gExtras.textEdit || gExtras.textFor < 1 || !gExtras.textDirty) return;
    gExtras.textDirty = false;
    const int id = gExtras.textFor;
    const std::string text = stringEditPrefix(id) + gameTextGet(gExtras.textEdit);
    if (text == gExtras.strings.text(id - 1)) return;
    gExtras.strings.set(id - 1, text);
    extrasChanged();
}
// The game's strings are in-game text: the game's colors (with the players' colors).
GameTextSpec stringsTextSpec() {
    GameTextSpec s;
    s.palette = TextPalette::Game, s.multiline = true, s.align = true;
    return s;
}
void stringsShowEdit(bool show) {
    if (!show) {
        if (gExtras.textEdit && IsWindowVisible(gExtras.textEdit)) stringsCommitEdit(), ShowWindow(gExtras.textEdit, SW_HIDE);
        return;
    }
    if (!gExtras.textEdit) {
        gExtras.textEdit = gameTextCreate(gData.form, 1801, {0, 0, scale(400), scale(160)}, stringsTextSpec());
        ShowWindow(gExtras.textEdit, SW_HIDE);
    }
}

void paintStringsForm(HDC dc, RECT rc, int& y) {
    const FormLayout l = formLayout(rc);
    const int id = std::clamp(gData.entry, 1, gExtras.strings.count());
    const std::string& raw = gExtras.strings.text(id - 1);
    y = formTitle(dc, l, y, nullptr, stringLine(raw), L"stat_txt.tbl string " + std::to_wstring(id), gExtras.strings.changed(id - 1), [id] {
        gExtras.strings.reset(id - 1);
        gExtras.textFor = -1;
        extrasChanged();
    }, UiIcon::DatStrings);
    y = sectionHeader(dc, l, y, L"Text", gExtras.strings.changed(id - 1));
    const int top = y;
    // The preview: a picture, in the right column (above the fields when narrow); its legend is the color codes.
    std::string previewRaw = raw;
    if (gExtras.textDirty && gExtras.textEdit && gExtras.textFor == id) previewRaw = stringEditPrefix(id) + gameTextGet(gExtras.textEdit);
    int picBottom;
    {
        TEXTMETRICW tm;
        GetTextMetricsW(dc, &tm);
        const int lines = 1 + int(std::count(previewRaw.begin(), previewRaw.end(), '\n'));
        RECT pb{l.picX, y, l.picX + std::min(l.picW, scale(420)), y + std::max(scale(60), lines * (int(tm.tmHeight) + scale(2)) + scale(16))};
        fillAlpha(dc, pb, RGB(8, 8, 10), 255);
        roundBox(dc, pb, scale(4), RGB(60, 64, 70));
        drawGameText(dc, {pb.left + scale(10), pb.top + scale(8), pb.right - scale(10), pb.bottom}, previewRaw, RGB(208, 208, 208));
        {  // which box is which: this one is the game's look
            HGDIOBJ of2 = SelectObject(dc, ui::smallFont());
            SetTextColor(dc, RGB(120, 124, 130));
            RECT cap{pb.left + scale(10), pb.bottom - scale(18), pb.right - scale(8), pb.bottom - scale(3)};
            DrawTextW(dc, L"Game preview", -1, &cap, DT_SINGLELINE | DT_RIGHT | DT_VCENTER | DT_NOPREFIX);
            SelectObject(dc, of2);
        }
        hitsOut().push_back({pb, nullptr, nullptr, L"Game preview: the text as the game shows it", IDC_ARROW, {}});
        picBottom = pb.bottom;
    }
    if (!l.twoCol) y = picBottom + scale(12);
    // A button's text (the one shown when it can be used): its hotkey and type as fields of their own.
    bool enabledText = buttonPrefix(raw);
    for (int s = 0; s < ButtonSets::kSets && !enabledText; ++s)
        for (const Button& b : gExtras.buttons.set(s)) enabledText |= b.enabledString == id;
    if (enabledText) {
        const bool has = buttonPrefix(raw);
        const char hot = has ? raw[0] : 0;
        const int type = has ? raw[1] : 0;
        const std::string& stockRaw = gExtras.strings.stock(id - 1);
        const bool hotChanged = buttonPrefix(stockRaw) != has || (has && stockRaw[0] != hot), typeChanged = buttonPrefix(stockRaw) != has || (has && stockRaw[1] != raw[1]);
        static const wchar_t* types[] = {L"Command", L"Make a unit", L"Upgrade", L"Use a technology", L"Research a technology", L"Morph a unit"};
        auto setPrefix = [id](char h, int t) {
            const std::string& r = gExtras.strings.text(id - 1);
            std::string body = buttonPrefix(r) ? r.substr(2) : r;
            gExtras.strings.set(id - 1, std::string(1, h) + char(t) + body);
            gExtras.textFor = -1;
            extrasChanged();
        };
        fieldLabel(dc, l, y, L"Hotkey", hotChanged);
        const std::wstring hotText = !has ? L"None" : hot == 27 ? L"Esc" : hot == ' ' ? L"Space" : std::wstring(1, wchar_t(std::toupper(static_cast<unsigned char>(hot))));
        pickBox(dc, {l.x0 + l.labelW, y + scale(2), l.x0 + l.labelW + scale(320), y + l.rowH - scale(2)}, hotText, hotChanged, [setPrefix, type](RECT r) {
            std::vector<ListPickItem> items;
            for (char c = 'A'; c <= 'Z'; ++c) items.push_back({std::wstring(1, wchar_t(c)), L"", std::string(1, char(std::tolower(c)))});
            for (char c = '0'; c <= '9'; ++c) items.push_back({std::wstring(1, wchar_t(c)), L"", std::string(1, c)});
            items.push_back({L"Esc", L"", std::string(1, char(27))});
            std::string picked;
            if (showListPicker(L"Hotkey", items, "", belowBox(r), picked) && !picked.empty()) setPrefix(picked[0], type);
        }, L"The key that presses the button");
        y += l.rowH;
        fieldLabel(dc, l, y, L"Type", typeChanged);
        pickBox(dc, {l.x0 + l.labelW, y + scale(2), l.x0 + l.labelW + scale(320), y + l.rowH - scale(2)}, has && type < 6 ? types[type] : L"None", typeChanged,
                [setPrefix, hot, has](RECT r) {
                    std::vector<ListPickItem> items;
                    for (int t = 0; t < 6; ++t) items.push_back({types[t], L"", std::to_string(t)});
                    std::string picked;
                    if (showListPicker(L"Type", items, "", belowBox(r), picked)) setPrefix(has ? hot : 'a', std::stoi(picked));
                },
                L"The kind of button. For a unit, an upgrade or a technology, the game's tooltip shows the cost.", nullptr, has ? type : -1);
        y += l.rowH + scale(8);
    }
    // The text: the editor over the field column (Preview: the text in the game's colors; Raw: the codes as <XX>).
    stringsShowEdit(true);
    const RECT er{l.x0, y, l.colRight, y + gameTextHeight(stringsTextSpec(), l.colRight - l.x0, scale(120))};
    if (gExtras.textFor != id) {
        gExtras.textFor = id;
        gameTextSet(gExtras.textEdit, stringEditBody(id));
        gExtras.textDirty = false;
    }
    RECT now;
    GetWindowRect(gExtras.textEdit, &now);
    MapWindowPoints(nullptr, gData.form, reinterpret_cast<POINT*>(&now), 2);
    if (!EqualRect(&now, &er)) SetWindowPos(gExtras.textEdit, nullptr, er.left, er.top, er.right - er.left, er.bottom - er.top, SWP_NOZORDER | SWP_NOACTIVATE);
    gameTextSetChanged(gExtras.textEdit, gExtras.strings.changed(id - 1));
    ShowWindow(gExtras.textEdit, SW_SHOWNA);
    y = er.bottom + scale(10);
    // Room: the game copies the strings over its own.
    y = capacityBar(dc, l, y, gExtras.strings.build().size(), gExtras.strings.capacity(), L"Size of all strings together. The game has room for at most the number after the slash.");
    if (l.twoCol) y = std::max(y, picBottom);
    (void)top;
    y += scale(14);
}
