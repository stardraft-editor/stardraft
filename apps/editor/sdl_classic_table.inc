// Map data's Table view (the SDL program's part; the model is core/classic_table.inc, the view the Data workspace's
// core/views_data_table.inc): the table of the tab that shows, its rows' names and pictures, and its changes into the
// map (one undo step each, as a change in the form).

// A CSV file's place at first: the Documents folder.
static fs::path sdlClassicCsvFolder() { return shellDocumentsFolder(); }

class SdlClassicTableHost final : public DataTableViewHost {
public:
    DatTable* table() override {
        gClassicTableHost = &gSdlClassicHost;
        return gClassic ? classicTable(gClassic->tab) : nullptr;
    }
    const std::vector<int>& rows() override {
        rows_.clear();
        if (gClassic)
            for (int e = 0; e < classicEntryCount(gClassic->tab); ++e) rows_.push_back(e);
        return rows_;
    }
    bool rowsPicked() override { return false; }
    int openEntry() override { return gClassic ? gClassic->entry[gClassic->tab] : -1; }
    draw::Image icon(int e) override {  // (a unit: its picture; an upgrade or a tech: its icon)
        if (!gClassic || !gClassic->def) return {};
        const int tab = gClassic->tab;
        if (tab == kClassicUnits) return sdlDataImage({DataPicture::Unit, e});
        const uint16_t frame = tab == kClassicUpgrades ? gClassic->def->upgradeIcon[size_t(e)] : gClassic->def->techIcon[size_t(e)];
        return frame >= 0xFFFF ? draw::Image{} : sdlDataImage({DataPicture::IconFrame, int(frame)});
    }
    draw::Image refIcon(const std::string&, uint32_t) override { return {}; }
    std::wstring note(const DatField&, uint32_t) override { return L""; }
    std::vector<ListPickItem> dropdownItems(const DatField&) override { return {}; }
    bool choose(const DatField&, uint32_t, draw::Rect, uint32_t&, const std::wstring&) override { return false; }
    void saved(const std::wstring& what) override {
        setMessage(what);
        // (after the table's handler: the map's change can make the page's views again, an undo before it read again)
        // The changes are taken now; an old copy (an undo just before) reads the map first, then they go in.
        const int tab = gClassic ? gClassic->tab : 0;
        shellPost([tab, edits = classicTableEdits(tab)] {
            if (const SdlCopy* c = sdlOpenCopy(); c && gState.ed && c->revision != gState.ed->revision()) sdlMapPageReload();
            if (gClassic && classicTableApply(tab, edits)) gSdlClassicHost.changed();  // (into the map: one undo step)
            if (gSdlPage.classicTable) gSdlPage.classicTable->invalidate();
        });
    }
    void open(int e) override {  // a row's number or name: the entry's form
        if (!gClassic) return;
        const int tab = gClassic->tab;
        sdlClassicSetTable(false);
        if (gSdlPage.classic[tab]) gSdlPage.classic[tab]->select(e);
    }
    void importCsv() override {
        DatTable* t = table();
        if (!t || !gClassic) return;
        const int tab = gClassic->tab;
        sdlTableImportCsv(*t, sdlClassicCsvFolder(), std::wstring(L"the map's ") + kClassicTabNames[tab], [this] { saved(L""); });
        if (gSdlPage.classicTable) gSdlPage.classicTable->invalidate();
    }
    void exportCsv() override {
        const DatTable* t = table();
        if (!t || !gSdlPage.classicTable) return;
        std::wstring stem = widen(t->schema().name);
        std::replace(stem.begin(), stem.end(), L' ', L'_');
        sdlTableExportCsv(*t, gSdlPage.classicTable->columns(), rows(), sdlClassicCsvFolder(), stem);
    }
    void status(const std::wstring& text) override { setMessage(text); }
    bool travel(int) override { return false; }
    bool editGameText(draw::Rect, const std::wstring&, std::string&) override { return false; }  // (names: in the form)
    void menu(draw::Point pt, const MenuModel& items, std::function<void(int)> pick) override {
        if (gSdlPage.classicTable) sdlDataMenu(*gSdlPage.classicTable, pt, items, std::move(pick), L"Map data table");
    }
    bool pickMany(const std::wstring& title, std::vector<ListPickItem> items, std::set<std::string>& checked, draw::Point pt,
                  std::function<void(const std::string&, bool, std::set<std::string>&)> toggle) override {
        if (!gSdlPage.classicTable) return false;
        ListPickView v(title, std::move(items), "");
        if (toggle) v.setLive(checked, shellTr(L"Close"), std::move(toggle));
        else v.setMulti(checked, shellTr(L"OK"));
        const draw::Point o = gSdlPage.classicTable->origin();
        if (!runListPick(v, {o.x + pt.x, o.y + pt.y})) return false;
        checked = v.checked();
        return true;
    }

private:
    std::vector<int> rows_;
};
SdlClassicTableHost gSdlClassicTableHost;
DataTableView& sdlClassicTableView() {
    static DataTableView view(gSdlClassicTableHost);
    return view;
}
