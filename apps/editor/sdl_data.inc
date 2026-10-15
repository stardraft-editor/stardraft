// The Data workspace on this system (the model is core/data_list.inc, core/views_data.inc, core/views_data_form.inc and
// core/views_data_refs.inc; the .dat tables are the stock files of the game, read once when their tab is first shown).
// Here: the tab strip, the entry list, the entry's fields (the game's values; the editing, the choices' and the flags'
// clicks, the undo and the project's edits are not on this system yet), the References panel (empty for now: its
// entries come from the Strings and Buttons tabs' data, which is data_extras.inc's and not on this system yet), and the
// Strings and Buttons tabs, which show "Not on this system yet". Included by main_sdl.cpp after sdl_briefing.inc;
// registerWorkspaceView(Workspace::Data, gSdlData) puts it in the window, and --data <tab>:<entry> opens an entry for a
// snapshot.

// --- The .dat tables -------------------------------------------------------------------------------------------------

int sdlDatCount() { return int(DatSchema::all().size()); }
int sdlStringsTab() { return sdlDatCount(); }
int sdlButtonsTab() { return sdlDatCount() + 1; }
int sdlDataTabCount() { return sdlDatCount() + 2; }
bool sdlExtraTab(int tab) { return tab >= sdlDatCount(); }

// A .dat's stock table, read on first use (nullptr: the game data is not open, or the .dat does not read).
DatTable* sdlDatTable(int tab) {
    static std::vector<std::unique_ptr<DatTable>> tables;
    static std::vector<bool> tried;
    if (tab < 0 || tab >= sdlDatCount() || !gDataOk) return nullptr;
    if (tables.size() != size_t(sdlDatCount())) tables.resize(size_t(sdlDatCount())), tried.assign(size_t(sdlDatCount()), false);
    const size_t i = size_t(tab);
    if (!tried[i]) {
        tried[i] = true;
        auto t = std::make_unique<DatTable>();
        std::string err;
        if (t->load(gState.data, DatSchema::all()[i], err)) tables[i] = std::move(t);
    }
    return tables[i].get();
}

// A tab's title: the .dat's (core/views_data.inc), or Strings and Buttons.
std::wstring sdlTabTitle(int tab) {
    if (tab == sdlStringsTab()) return L"Strings";
    if (tab == sdlButtonsTab()) return L"Buttons";
    return tab >= 0 && tab < sdlDatCount() ? datTitle(DatSchema::all()[size_t(tab)].name) : L"";
}

// The list's model host (core/data_list.inc): the stock tables, the names and the changed flags.
class SdlDataListHost final : public DataListHost {
public:
    bool hasTable(int tab) override { return sdlDatTable(tab) != nullptr; }
    int entries(int tab) override { return sdlDatTable(tab) ? DatSchema::all()[size_t(tab)].entries : 0; }
    int firstEntry(int) override { return 0; }
    bool changed(int tab, int e) override {
        const DatTable* t = sdlDatTable(tab);
        return t && t->entryChanged(e);
    }
    std::wstring name(int tab, int e) override { return sdlDatTable(tab) ? entryName(DatSchema::all()[size_t(tab)].name, e) : L""; }
};
SdlDataListHost gSdlDataListHost;

// The References panel's program parts: on this system the references are not computed yet (shown: false, so the panel
// has nothing to list).
class SdlDataRefsHost final : public DataRefsHost {
public:
    std::vector<DataRefRow> refs(bool& shown) override {
        shown = false;
        return {};
    }
    void goTo(const std::string&, int) override {}
    bool travel(int) override { return false; }
};
SdlDataRefsHost gSdlDataRefsHost;
DataRefsPage gSdlDataRefs(gSdlDataRefsHost);

// --- The workspace view ----------------------------------------------------------------------------------------------

// The Data workspace's place: which tab and entry show, and each tab's own entry and the scrolls.
struct SdlDataState {
    int tab = 0, entry = 0;
    std::vector<int> tabEntry;  // the entry each tab last had open
    int listScroll = 0, formScroll = 0;
};

struct SdlDataView final : draw::View {
    SdlDataState st;
    std::vector<draw::Rect> tabBoxes;  // each tab's box, from the last paint (a click finds its tab here)
    int formHeight = 0;                // the form's content height, from the last paint

    SdlDataView() { add(gSdlDataRefs, draw::Rect(0, 0, 0, 0)); }

    int tabsH() const { return scale(34); }
    int listW() const { return scale(240); }
    int refsW() const { return scale(260); }
    int rowH() const { return scale(26); }
    int formLeft() const { return listW(); }
    int formRight() const { return std::max(formLeft(), width() - refsW()); }
    draw::Rect refsRect() const { return {width() - refsW(), 0, width(), height()}; }

    // The tab, by name: "units", "images", "strings" or "buttons", and the entry (the snapshot's --data).
    void select(const std::string& tabName, int entry) {
        int tab = -1;
        if (tabName == "strings") tab = sdlStringsTab();
        else if (tabName == "buttons") tab = sdlButtonsTab();
        else
            for (int i = 0; i < sdlDatCount(); ++i)
                if (DatSchema::all()[size_t(i)].name == tabName) tab = i;
        if (tab < 0) return;
        if (st.tabEntry.size() < size_t(sdlDataTabCount())) st.tabEntry.resize(size_t(sdlDataTabCount()), 0);
        st.tab = tab, st.entry = entry, st.listScroll = st.formScroll = 0;
        st.tabEntry[size_t(tab)] = entry;
        scrollToEntry();
    }

    // The list scrolls to the entry it shows (the middle of the list, or its top when the view has no height yet).
    void scrollToEntry() {
        const DataListRows r = rows();
        for (size_t i = 0; i < r.shown.size(); ++i)
            if (r.shown[i] == st.entry) {
                st.listScroll = std::max(0, int(i) * rowH() - std::max(0, (height() - tabsH()) / 2));
                return;
            }
    }

    // Another tab: the one it leaves keeps its entry, the one it goes to comes back on the entry it last showed (core).
    void switchTab(int next) {
        if (next == st.tab || next < 0 || next >= sdlDataTabCount()) return;
        if (st.tabEntry.size() < size_t(sdlDataTabCount())) st.tabEntry.resize(size_t(sdlDataTabCount()), 0);
        dataSwitchTab(st.tab, st.entry, st.tabEntry, st.listScroll, st.formScroll, next);
        invalidate();
    }

    void ensureTabs() {
        if (st.tabEntry.size() < size_t(sdlDataTabCount())) st.tabEntry.resize(size_t(sdlDataTabCount()), 0);
    }

    DataListRows rows() {
        if (sdlExtraTab(st.tab)) return {};
        return dataListRows(gSdlDataListHost, st.tab, L"", false, {});
    }

    void paint(draw::Canvas& c) override {
        ensureTabs();
        if (refsRect().left != gSdlDataRefs.frame().left || refsRect().width() != gSdlDataRefs.width() || height() != gSdlDataRefs.height())
            gSdlDataRefs.setFrame(refsRect());
        c.fill({0, 0, width(), height()}, kBg);
        paintTabs(c);
        paintList(c);
        paintForm(c);
    }

    void paintTabs(draw::Canvas& c) {
        c.fill({0, 0, width(), tabsH()}, kPanel);
        c.line(0, tabsH() - 1, width(), tabsH() - 1, kBorder);
        const int n = sdlDataTabCount();
        std::vector<DataTabText> texts;
        std::vector<std::wstring> counts;
        for (int i = 0; i < n; ++i) {
            const std::wstring count = sdlExtraTab(i) ? L"" : std::to_wstring(DatSchema::all()[size_t(i)].entries);
            counts.push_back(count);
            texts.push_back({c.measure(sdlTabTitle(i)), count.empty() ? 0 : c.measure(count)});
        }
        const DataTabMetrics m{scale(8), scale(64), 0, 0, scale(8), scale(2)};
        const DataTabLayout lay = dataTabLayout(texts, width(), m);
        tabBoxes.clear();
        for (int i = 0; i < n; ++i) {
            const draw::Rect box{lay.boxes[size_t(i)].first, 0, lay.boxes[size_t(i)].second, tabsH()};
            tabBoxes.push_back(box);
            const bool on = i == st.tab;
            if (on) c.fill(box, kSel);
            std::wstring label = sdlTabTitle(i);
            if (lay.showCounts && !counts[size_t(i)].empty()) label += L"  " + counts[size_t(i)];
            c.text(box, label, on ? kText : kDim, draw::kCenter | draw::kMiddle | draw::kEllipsis);
        }
    }

    void paintList(draw::Canvas& c) {
        const draw::Rect area{0, tabsH(), listW(), height()};
        c.fill(area, kPanel);
        c.fill({listW() - 1, tabsH(), listW(), height()}, kBorder);
        if (sdlExtraTab(st.tab)) return;
        const DataListRows r = rows();
        dataListClampScroll(st.listScroll, r.shown.size(), rowH(), height() - tabsH());
        for (size_t i = 0; i < r.shown.size(); ++i) {
            const int e = r.shown[i];
            const int top = tabsH() + int(i) * rowH() - st.listScroll;
            if (top >= height()) break;
            if (top + rowH() <= tabsH()) continue;
            const draw::Rect row{0, top, listW() - 1, top + rowH()};
            const bool on = e == st.entry;
            if (on) c.fill(row, kSel);
            c.text({scale(8), top, scale(52), top + rowH()}, std::to_wstring(e), kDim, draw::kMiddle | draw::kRight);
            const bool changed = gSdlDataListHost.changed(st.tab, e);
            c.text({scale(62), top, listW() - scale(22), top + rowH()}, entryName(DatSchema::all()[size_t(st.tab)].name, e),
                   on ? kText : kDim, draw::kMiddle | draw::kEllipsis);
            if (changed) c.fill({listW() - scale(14), top + rowH() / 2 - scale(3), listW() - scale(8), top + rowH() / 2 + scale(3)}, kGood);
        }
    }

    void paintForm(draw::Canvas& c) {
        const int l = formLeft(), r = formRight();
        c.fill({l, tabsH(), r, height()}, kBg);
        const draw::Rect all{l, tabsH(), r, height()};
        if (sdlExtraTab(st.tab)) return c.text(all, tr(L"Not on this system yet"), kDim, draw::kCenter | draw::kMiddle | draw::kWrap);
        const DatTable* t = sdlDatTable(st.tab);
        if (!t || st.entry < 0 || st.entry >= DatSchema::all()[size_t(st.tab)].entries)
            return c.text(all, tr(L"Pick an entry in the list"), kDim, draw::kCenter | draw::kMiddle | draw::kWrap);
        const DatSchema& sc = t->schema();
        const int pad = scale(12), labelW = scale(190), valW = scale(110), colRight = r - pad;
        const DataFormScale fs{scale(2), scale(8), scale(4), scale(7), scale(110), scale(320), scale(190), scale(22), scale(14)};
        int y = tabsH() + scale(10) - st.formScroll;
        const std::wstring title = entryName(sc.name, st.entry);
        c.text({l + pad, y, r - pad, y + scale(26)}, sdlTabTitle(st.tab) + L"  #" + std::to_wstring(st.entry) + (title.empty() ? L"" : L"  " + title), kText,
               draw::kMiddle | draw::kEllipsis);
        y += scale(32);
        c.text({l + pad, y, r - pad, y + scale(20)}, tr(L"The game's values. Editing is not on this system yet."), kDim, draw::kMiddle | draw::kEllipsis);
        y += scale(28);
        for (size_t s = 0; s < sc.sections.size(); ++s) {
            c.text({l + pad, y, r - pad, y + scale(22)}, widen(sc.sections[s]), kDim, draw::kMiddle | draw::kEllipsis);
            y += scale(24);
            std::vector<const DatField*> fields;
            for (const DatField& f : sc.fields)
                if (f.section == int(s) && f.covers(st.entry)) fields.push_back(&f);
            std::stable_sort(fields.begin(), fields.end(), [](const DatField* a, const DatField* b) { return a->order < b->order; });
            for (const DatField* f : fields) {
                const DataFieldRow row = dataFieldRow(l + pad, labelW, valW, y, rowH(), colRight, fs);
                const uint32_t v = t->value(*f, st.entry);
                c.text(row.label, widen(f->name), kDim, draw::kMiddle | draw::kEllipsis);
                if (fieldIsBool(sc.name, *f)) {
                    c.frame(row.boolCheck, kBorder);
                    if (v) c.fill({row.boolCheck.left + 3, row.boolCheck.top + 3, row.boolCheck.right - 3, row.boolCheck.bottom - 3}, kAccent);
                    c.text(row.boolText, v ? tr(L"Yes") : tr(L"No"), kText, draw::kMiddle);
                } else {
                    c.fill(row.box, kCtl);
                    c.text(dataValueText(row.box, fs), fieldValueText(*f, v), kText, draw::kMiddle | draw::kEllipsis);
                    std::wstring note;
                    if (!f->valueNames.empty()) note = v < f->valueNames.size() ? widen(f->valueNames[v]) : L"";
                    else if (!fieldRefDat(*f).empty()) note = entryName(fieldRefDat(*f), int(v));
                    if (!note.empty()) c.text(row.note, note, kDim, draw::kMiddle | draw::kEllipsis);
                }
                y += rowH();
            }
            y += scale(8);
        }
        formHeight = y + st.formScroll - tabsH() - scale(10);  // the content's height (the wheel stops at its end)
    }

    bool mouse(const draw::MouseEvent& e) override {
        using draw::MouseKind;
        if (e.kind == MouseKind::Wheel) {
            if (e.horizontal) return false;
            if (e.pt.x < listW()) {
                st.listScroll = std::max(0, st.listScroll - int(std::lround(e.wheel * rowH() * 3)));
                invalidate();
                return true;
            }
            const int most = std::max(0, formHeight - (height() - tabsH()));
            st.formScroll = std::clamp(st.formScroll - int(std::lround(e.wheel * rowH() * 3)), 0, most);
            invalidate();
            return true;
        }
        if (e.kind != MouseKind::Down || e.button != 0) return false;
        const draw::Point p = e.pt;
        ensureTabs();
        if (p.y < tabsH()) {
            for (size_t i = 0; i < tabBoxes.size(); ++i)
                if (tabBoxes[i].contains(p)) switchTab(int(i));
            return true;
        }
        if (p.x < listW()) {
            const DataListRows r = rows();
            const int i = (p.y - tabsH() + st.listScroll) / rowH();
            if (i >= 0 && i < int(r.shown.size())) {
                st.entry = r.shown[size_t(i)];
                st.tabEntry[size_t(st.tab)] = st.entry;
                st.formScroll = 0;
                invalidate();
            }
            return true;
        }
        return true;
    }

    bool focusable() const override { return false; }
};

SdlDataView gSdlData;

// --- The snapshot's entry: --data <tab>:<entry> (the Data workspace opens the entry as it is shown) -----------------------

void sdlDataSelect(const std::string& spec) {
    const size_t colon = spec.find(':');
    if (colon == std::string::npos) return;
    gSdlData.select(spec.substr(0, colon), std::atoi(spec.c_str() + colon + 1));
}
