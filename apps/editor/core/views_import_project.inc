// File > Import a project (portable, core.inc; stardraft/project_import.h): a dialog of four steps that makes a new EUD
// project from an EUD Editor 3 project (.e3s), an EUD Editor 2 project (.e2s) or a euddraft project (.eds).
//
// The steps are in a rail at the left (a done step has a check mark), the step's title and one line under it at the top,
// its controls in cards under them:
//
//   1 Project  the file (a card with its editor and version), the new project's name and folder
//   2 Map      the map (found, or the user selects it) and where Build writes the built map; the old editor's build folder
//              when a part of the file needs it (found beside the file, or the user selects it)
//   3 Parts    one card per part of the file: a check box (import it or leave it out), what it holds, and a chip that
//              says how it comes over (as StarDraft's own data, from the build folder, or not); for EUD Editor 3 with a
//              build folder, where the scripts come from
//   4 Summary  the counts, then what is imported, what is not, and what to do, for the choices made
//
// The steps change with the file and the choices: the build folder's card shows only when a part needs it, the scripts'
// choice only when there is a build, and the summary lists what the choices give. Back, Next (Import on the last step)
// and Cancel are the dialog's buttons; Next is off until the step is complete (a file that reads, a name, a map).
// The kit's widgets in a kit Dialog; files and folders come through the seam (shellPickFile, shellPickFolder).

constexpr int kImportNext = 1, kImportBack = 2, kImportCancel = 3;

namespace importLayout {
constexpr int kContentW = 848, kContentH = 508;  // the content; the dialog adds 16 px edges and the buttons' row
constexpr int kPad = 16, kButtonRow = 28 + 16;
constexpr int kDialogW = kPad + kContentW + kPad, kDialogH = kPad + kContentH + 16 + kButtonRow;  // 880 x 584
constexpr int kRailW = 196, kMainX = kRailW + 28;  // the steps' rail, and where the step's own part starts
constexpr int kStepH = 60;                         // a step in the rail
constexpr int kHeadH = 76;                         // the title and the line under it
constexpr int kLabelW = 104, kFieldH = 24, kBrowseW = 96, kGap = 8, kCardPad = 16;
constexpr int kPartH = 58;                         // a part's card on the Parts step
}  // namespace importLayout

// A text with texts put in for its %s (a translated pattern).
inline std::wstring importFmt(const wchar_t* pattern, std::initializer_list<std::wstring> texts) {
    std::wstring out;
    auto t = texts.begin();
    for (const wchar_t* p = pattern; *p; ++p) {
        if (p[0] == L'%' && p[1] == L's' && t != texts.end()) out += *t++, ++p;
        else out += *p;
    }
    return out;
}
inline std::wstring importNum(int n) { return std::to_wstring(n); }
// A note of the import library ("the tileset edits: ..."), translated, as an item of a list: its first letter a capital.
inline std::wstring importNote(const std::string& note) {
    std::wstring t = tr(widen(note));
    if (!t.empty()) t[0] = wchar_t(std::towupper(t[0]));
    return t;
}
inline std::wstring importPath(const fs::path& p) { return p.empty() ? std::wstring() : fs::path(p).make_preferred().wstring(); }

class ImportWizardView final : public draw::View {
public:
    explicit ImportWizardView(std::wstring location) {
        using stardraft::kit::LabelTone;
        // The cards first: they are under the controls that sit on them.
        for (auto* card : {&railCard_, &fileCard_, &newCard_, &mapCard_, &buildCard_, &tileCard_[0], &tileCard_[1], &tileCard_[2]}) add(*card, {0, 0, 0, 0});
        stepper_.setSteps({{tr(L"Project"), tr(L"The file to import")},
                           {tr(L"Map"), tr(L"Map and build folder")},
                           {tr(L"Parts"), tr(L"What to import")},
                           {tr(L"Summary"), tr(L"Check, then import")}});
        stepper_.onSelect = [this](int i) { go(i); };  // (a done step: back to it)
        add(stepper_, {0, 0, 0, 0});
        title_.setFont({draw::Face::Ui, scale(20), true});
        add(title_, {0, 0, 0, 0});
        subtitle_.setWrap(true), subtitle_.setTone(LabelTone::Dim);
        add(subtitle_, {0, 0, 0, 0});

        // 1 Project
        fileName_.setFont({draw::Face::Ui, 0, true});
        fileWhat_.setWrap(false);
        filePath_.setTone(LabelTone::Faint);
        for (auto* l : {&fileName_, &fileWhat_, &filePath_}) add(*l, {0, 0, 0, 0});
        fileBrowse_.onClick = [this] {
            ShellFilePick d;
            d.title = tr(L"Select the project to import");
            d.filters = {{tr(L"EUD Editor and euddraft projects (*.e3s, *.e2s, *.eds)"), L"*.e3s;*.e2s;*.e2p;*.eds;*.edd"}};
            if (const fs::path f = shellPickFile(d); !f.empty()) setFile(f);
        };
        add(fileBrowse_, {0, 0, 0, 0});
        newHead_.setText(tr(L"New project")), newHead_.setFont({draw::Face::Ui, 0, true});
        add(newHead_, {0, 0, 0, 0});
        for (auto* l : {&nameL_, &locL_, &outL_}) l->setTone(LabelTone::Dim), add(*l, {0, 0, 0, 0});
        nameL_.setText(tr(L"Name")), locL_.setText(tr(L"Location"));
        name_.onChange = [this] { refresh(); };
        add(name_, {0, 0, 0, 0});
        location_.setText(std::move(location));
        location_.onChange = [this] { refresh(); };
        add(location_, {0, 0, 0, 0});
        locBrowse_.setText(tr(L"Browse\x2026"));
        locBrowse_.onClick = [this] {
            if (const std::wstring d = shellPickFolder(tr(L"Select the folder for the project")); !d.empty()) location_.setText(d), refresh();
        };
        add(locBrowse_, {0, 0, 0, 0});

        // 2 Map
        mapHead_.setFont({draw::Face::Ui, 0, true}), buildHead_.setFont({draw::Face::Ui, 0, true});
        mapHead_.setText(tr(L"Map"));
        add(mapHead_, {0, 0, 0, 0}), add(buildHead_, {0, 0, 0, 0});
        mapName_.setIcon(int(UiIcon::Map));
        add(mapName_, {0, 0, 0, 0});
        mapPathL_.setTone(LabelTone::Faint);
        add(mapPathL_, {0, 0, 0, 0});
        mapBrowse_.setText(tr(L"Browse\x2026"));
        mapBrowse_.onClick = [this] {
            ShellFilePick d;
            d.title = tr(L"Select the map");
            d.filters = {{tr(L"StarCraft maps (*.scx, *.scm)"), L"*.scx;*.scm"}};
            if (const fs::path f = shellPickFile(d); !f.empty()) choices_.map = f, refresh();
        };
        add(mapBrowse_, {0, 0, 0, 0});
        outL_.setText(tr(L"Output map"));
        outGroup_.onChange = [this](stardraft::kit::RadioButton& b) { choices_.keepOutput = &b == &outKeep_, refresh(); };
        add(outDefault_, {0, 0, 0, 0}), add(outKeep_, {0, 0, 0, 0});
        buildText_.setWrap(true), buildText_.setTone(LabelTone::Dim);
        add(buildText_, {0, 0, 0, 0});
        buildPath_.setTone(LabelTone::Faint);
        add(buildPath_, {0, 0, 0, 0});
        buildBrowse_.setText(tr(L"Browse\x2026"));
        buildBrowse_.onClick = [this] {
            if (const std::wstring d = shellPickFolder(tr(L"Select the build folder of the old editor")); !d.empty()) setBuildFolder(d);
        };
        add(buildBrowse_, {0, 0, 0, 0});
        for (auto* ch : {&mapChip_, &buildChip_}) ch->setAlignRight(true), add(*ch, {0, 0, 0, 0});

        // 3 Parts
        partsView_.setContent(&partsPanel_);
        add(partsView_, {0, 0, 0, 0});
        scriptsL_.setText(tr(L"Scripts from"));
        scriptsL_.setTone(LabelTone::Dim);
        fromText_.setLabel(tr(L"The project file (as written)"));
        fromBuild_.setLabel(tr(L"The build folder (as built)"));
        scriptsGroup_.onChange = [this](stardraft::kit::RadioButton& b) { choices_.scriptsFromBuild = &b == &fromBuild_, refresh(); };

        // 4 Summary
        for (int i = 0; i < 3; ++i) {
            tileNum_[i].setFont({draw::Face::Ui, scale(26), true});
            tileCap_[i].setTone(LabelTone::Dim);
            add(tileNum_[i], {0, 0, 0, 0}), add(tileCap_[i], {0, 0, 0, 0});
        }
        tileNum_[0].setTone(LabelTone::Good), tileNum_[1].setTone(LabelTone::Normal), tileNum_[2].setTone(LabelTone::Warn);
        
        summaryView_.setContent(&summaryPanel_);
        add(summaryView_, {0, 0, 0, 0});
        toBuild_.setText(tr(L"Select the build folder\x2026"));
        toBuild_.onClick = [this] { go(1); };
        add(toBuild_, {0, 0, 0, 0});
        go(0);
    }

    void setDialog(stardraft::kit::Dialog* d) {
        dlg_ = d;
        refresh();
    }
    int step() const { return step_; }
    // The dialog's Back and Next (Next on the last step is Import: it closes the dialog).
    void stepped(int result) {
        if (result == kImportBack && step_ > 0) go(step_ - 1);
        else if (result == kImportNext && step_ < 3 && stepComplete()) go(step_ + 1);
        else if (result == kImportNext && step_ == 3 && stepComplete() && dlg_) dlg_->close(kImportNext);
    }
    void go(int s) {
        step_ = std::clamp(s, 0, 3);
        if (step_ == 3) buildSummary();
        refresh();
        focusStep();
    }

    // Reads the file (the scan) and shows what it is. False when it does not read.
    bool setFile(const fs::path& f) {
        std::string err;
        stardraft::ImportScan s;
        file_ = f;
        scanned_ = stardraft::scanImport(f, shellState().data, s, err);
        error_ = scanned_ ? std::wstring() : widen(err);
        if (scanned_) {
            scan_ = s;
            choices_ = stardraft::defaultChoices(scan_);
            std::wstring n = f.stem().wstring();
            std::error_code ec;
            for (int k = 2; fs::exists(fs::path(location_.text()) / n, ec); ++k) n = f.stem().wstring() + L" " + std::to_wstring(k);
            name_.setText(n);
            buildParts();
        }
        refresh();
        return scanned_;
    }
    void setBuildFolder(const fs::path& d) {
        if (!scanned_) return;
        std::string err;
        stardraft::ImportScan s;
        if (!stardraft::scanImport(scan_.file, shellState().data, s, err, d)) return;
        const fs::path keepMap = choices_.map;
        const bool keepOut = choices_.keepOutput;
        const std::set<std::string> skip = choices_.skip;
        scan_ = s;
        choices_ = stardraft::defaultChoices(scan_);
        choices_.map = keepMap, choices_.keepOutput = keepOut, choices_.skip = skip;
        choices_.buildFolder = scan_.buildFolder;
        buildSelected_ = scan_.buildFolder.empty();  // (selected, but it holds no build: the chip says so)
        buildParts();
        refresh();
    }

    // What the steps hold (the host imports with them).
    const stardraft::ImportScan& scan() const { return scan_; }
    const stardraft::ImportChoices& choices() const { return choices_; }
    std::wstring name() const { return name_.text(); }
    std::wstring location() const { return location_.text(); }
    // The summary's lists, one item a line (a test reads them): 0 imported, 1 not imported, 2 to do.
    std::wstring summaryText(int list) const {
        std::wstring s;
        for (const auto& item : items_)
            if (item.list == list && !item.heading) s += (s.empty() ? L"" : L"\n") + item.label->text();
        return s;
    }

    void paint(draw::Canvas& c) override {
        c.fill({0, 0, width(), height()}, stardraft::kit::theme().bg);
        measure(c);
    }

    void resized() override { place(); }

private:
    // A part's card on the Parts step, made again for each scan.
    struct PartRow {
        std::string id;
        stardraft::kit::Card card;
        stardraft::kit::Label icon;
        stardraft::kit::CheckBox box;
        stardraft::kit::Label detail;
        stardraft::kit::Badge chip;
    };
    // A line of the summary: an icon and a text.
    struct Item {
        int list = 0;
        bool heading = false;
        std::unique_ptr<stardraft::kit::Label> label;
        int h = 22;
    };

    static const wchar_t* partTitle(const std::string& id, const std::string& kind) {
        if (id == "dat") return L".dat values";
        if (id == "strings") return L"Texts (stat_txt.tbl)";
        if (id == "buttons") return L"Button sets";
        if (id == "requirements") return L"Requirements";
        if (id == "looks") return L"Status panels and wireframes";
        if (id == "scripts") return L"Scripts";
        if (id == "generated") return kind == "e2s" ? L"Code that only EUD Editor 2 makes" : L"Code that only EUD Editor 3 makes";
        if (id == "files") return L"Other files";
        if (id == "plugins") return L"euddraft plugins";
        if (id == "labels") return L"Notes for entries";
        if (id == "settings") return L"Build settings";
        return L"Parts that StarDraft does not edit";
    }
    static UiIcon partIcon(const std::string& id) {
        if (id == "dat") return UiIcon::DatUnits;
        if (id == "strings") return UiIcon::DatStrings;
        if (id == "buttons") return UiIcon::DatButtons;
        if (id == "requirements") return UiIcon::DatTech;
        if (id == "looks") return UiIcon::DatPortraits;
        if (id == "scripts" || id == "generated") return UiIcon::Code;
        if (id == "files") return UiIcon::Folder;
        if (id == "plugins") return UiIcon::Build;
        if (id == "labels") return UiIcon::Tag;
        if (id == "settings") return UiIcon::Settings;
        return UiIcon::Info;
    }
    // What a part holds, in words; `brief`: without the lists of files (the summary).
    std::wstring partDetail(const stardraft::ImportPart& p, bool brief = false) const {
        auto joined = [&](size_t most) {
            std::wstring s;
            for (size_t k = 0; k < p.details.size() && k < most; ++k) s += (k ? L", " : L"") + tr(widen(p.details[k]));
            if (p.details.size() > most) s += L", \x2026";
            return s;
        };
        if (p.id == "dat") return p.count == 1 ? importFmt(tr(L"1 value: %s"), {joined(10)}) : importFmt(tr(L"%s values: %s"), {importNum(p.count), joined(10)});
        if (p.id == "strings") return importFmt(tr(p.count == 1 ? L"1 text" : L"%s texts"), {importNum(p.count)});
        if (p.id == "buttons") {
            std::wstring s = importFmt(tr(p.count == 1 ? L"1 set" : L"%s sets"), {importNum(p.count)});
            if (!p.details.empty()) s += L"; " + importFmt(tr(p.details.size() == 1 ? L"1 unit uses another unit's set" : L"%s units use another unit's set"), {importNum(int(p.details.size()))});
            return s;
        }
        if (p.id == "requirements") return importFmt(tr(p.count == 1 ? L"1 list" : L"%s lists"), {importNum(p.count)});
        if (p.id == "looks") return importFmt(tr(p.count == 1 ? L"1 unit" : L"%s units"), {importNum(p.count)});
        if (p.id == "scripts" && brief) return importFmt(tr(p.count == 1 ? L"1 file" : L"%s files"), {importNum(p.count)});
        if (p.id == "scripts")
            return p.count == 1 ? importFmt(tr(L"1 file: %s"), {joined(4)}) : importFmt(tr(L"%s files, in their folders: %s"), {importNum(p.count), joined(4)});
        if (p.id == "plugins") return joined(8);
        if (p.id == "labels") return importFmt(tr(p.count == 1 ? L"1 note, kept in notes\\EUD Editor 3 notes.txt" : L"%s notes, kept in notes\\EUD Editor 3 notes.txt"), {importNum(p.count)});
        if (p.id == "generated" || p.id == "settings" || p.id == "unsupported") {  // (sentences of the library: each on its own)
            std::wstring s;
            for (size_t k = 0; k < p.details.size() && k < 3; ++k) s += k ? L", " + tr(widen(p.details[k])) : importNote(p.details[k]);
            return p.details.size() > 3 ? s + L", \x2026" : s;
        }
        return joined(6);
    }
    void buildParts() {
        for (auto& r : rows_)
            for (draw::View* v : {(draw::View*)&r->card, (draw::View*)&r->icon, (draw::View*)&r->box, (draw::View*)&r->detail, (draw::View*)&r->chip}) partsPanel_.remove(*v);
        rows_.clear();
        for (const stardraft::ImportPart& p : scan_.parts) {
            auto r = std::make_unique<PartRow>();
            r->id = p.id;
            r->icon.setIcon(int(partIcon(p.id)));
            r->box.setLabel(tr(partTitle(p.id, scan_.kind)));
            r->box.setPlainLabel(true);
            r->box.setEnabled(p.way != stardraft::ImportPart::Way::None);
            r->box.onChange = [this, id = p.id](bool on) {
                if (on) choices_.skip.erase(id);
                else choices_.skip.insert(id);
                refresh();
            };
            r->detail.setText(partDetail(p));
            r->detail.setTone(stardraft::kit::LabelTone::Dim);
            r->box.setTip(r->detail.text());
            r->chip.setAlignRight(true);
            partsPanel_.add(r->card, {0, 0, 0, 0});
            partsPanel_.add(r->icon, {0, 0, 0, 0}), partsPanel_.add(r->box, {0, 0, 0, 0}), partsPanel_.add(r->detail, {0, 0, 0, 0});
            partsPanel_.add(r->chip, {0, 0, 0, 0});
            rows_.push_back(std::move(r));
        }
        if (scriptsL_.parent()) partsPanel_.remove(scriptsL_), partsPanel_.remove(fromText_), partsPanel_.remove(fromBuild_);
        if (showScriptsChoice()) partsPanel_.add(scriptsL_, {0, 0, 0, 0}), partsPanel_.add(fromText_, {0, 0, 0, 0}), partsPanel_.add(fromBuild_, {0, 0, 0, 0});
    }
    bool showScriptsChoice() const { return scanned_ && scan_.kind == "e3s" && scan_.part("scripts") && !scan_.buildFolder.empty(); }
    bool buildHelps() const { return scanned_ && scan_.part("generated"); }
    fs::path mapPath() const { return choices_.map.empty() ? scan_.map : choices_.map; }
    bool mapThere() const {
        std::error_code ec;
        const fs::path m = mapPath();
        return !m.empty() && fs::is_regular_file(m, ec);
    }
    bool nameOk() const {
        const std::wstring n = name_.text();
        std::error_code ec;
        return !n.empty() && n.find_first_of(L"\\/:*?\"<>|") == std::wstring::npos && !location_.text().empty() &&
               !(fs::exists(fs::path(location_.text()) / n, ec) && !fs::is_empty(fs::path(location_.text()) / n, ec));
    }
    bool stepComplete() const {
        if (step_ == 0) return scanned_ && nameOk();
        if (step_ == 1) return mapThere();
        return scanned_ && nameOk() && mapThere();  // (Import: the steps before are still complete)
    }
    // How a part comes over now, as its chip says it.
    std::pair<std::wstring, stardraft::kit::BadgeTone> partState(const stardraft::ImportPart& p) const {
        if (p.way == stardraft::ImportPart::Way::None) return {tr(L"Not supported"), stardraft::kit::BadgeTone::Neutral};
        if (choices_.skip.count(p.id)) return {tr(L"Left out"), stardraft::kit::BadgeTone::Neutral};
        if (p.way == stardraft::ImportPart::Way::Build || (p.id == "scripts" && choices_.scriptsFromBuild))
            return p.needsBuild ? std::pair{tr(L"Not imported: no build folder"), stardraft::kit::BadgeTone::Warn} : std::pair{tr(L"From the build folder"), stardraft::kit::BadgeTone::Good};
        return {tr(L"Included"), stardraft::kit::BadgeTone::Good};
    }

    void buildSummary() {
        for (auto& it : items_) summaryPanel_.remove(*it.label);
        items_.clear();
        needBuild_ = false;
        std::vector<std::wstring> lists[3];
        auto add = [&](int list, const std::wstring& text) { lists[list].push_back(text); };
        int counts[3] = {0, 0, 0};
        add(0, importFmt(tr(L"New project %s, in %s"), {name_.text(), location_.text()}));
        add(0, importFmt(tr(L"The map %s, copied into the project"), {mapPath().filename().wstring()}));
        if (choices_.keepOutput && !scan_.output.empty()) add(0, importFmt(tr(L"Build writes the map to %s"), {importPath(scan_.output)}));
        for (const stardraft::ImportPart& p : scan_.parts) {
            const std::wstring title = tr(partTitle(p.id, scan_.kind));
            if (p.way == stardraft::ImportPart::Way::None) {
                for (const std::string& d : p.details) add(1, importNote(d)), ++counts[1];
                continue;
            }
            if (choices_.skip.count(p.id)) {
                add(1, importFmt(tr(L"%s: left out"), {title})), ++counts[1];
                continue;
            }
            const bool fromBuild = p.way == stardraft::ImportPart::Way::Build || (p.id == "scripts" && choices_.scriptsFromBuild);
            if (fromBuild && p.needsBuild) {
                for (const std::string& d : p.details) add(1, importFmt(tr(L"%s: needs the build folder"), {importNote(d)})), ++counts[1];
                if (!needBuild_)
                    add(2, scan_.kind == "e2s" ? tr(L"Select EUD Editor 2's build folder (Data\\eudplibdata) in step 2, or build the map once in EUD Editor 2 and import again")
                                               : tr(L"Select EUD Editor 3's build folder (BuildData_<name>) in step 2, or build the map once in EUD Editor 3 and import again")),
                        ++counts[2];
                needBuild_ = true;
                continue;
            }
            add(0, importFmt(L"%s: %s", {title, partDetail(p, true)}) + (fromBuild ? std::wstring(L" ") + tr(L"(from the build folder)") : std::wstring()));
            ++counts[0];
        }
        auto todo = [&](const wchar_t* text) { add(2, tr(text)), ++counts[2]; };
        if (const stardraft::ImportPart* g = scan_.part("generated"); g && scan_.kind == "e3s" && !choices_.scriptsFromBuild && !g->needsBuild)
            todo(L"The build folder is not used: the scripts with macros, block scripts and classic triggers come in as text that euddraft cannot build");
        if (scan_.kind == "e2s" && scan_.part("generated") && !scan_.part("generated")->needsBuild)
            todo(L"TriggerEditor.eps is EUD Editor 2's code for an older euddraft: build, then correct the errors that the log shows");
        if (!scan_.buildFolder.empty() && !scan_.buildMatches) todo(L"The build folder is of another map: select the build folder of this project");
        todo(L"Build the project (Ctrl+B) and test the map");
        // The lists, each under its heading.
        static const wchar_t* const heads[3] = {L"To import", L"Not imported", L"To do"};
        for (int list = 0; list < 3; ++list) {
            if (lists[list].empty()) continue;
            Item h;
            h.list = list, h.heading = true;
            h.label = std::make_unique<stardraft::kit::Label>(tr(heads[list]));
            h.label->setFont({draw::Face::Ui, 0, true});
            summaryPanel_.add(*h.label, {0, 0, 0, 0});
            items_.push_back(std::move(h));
            for (const std::wstring& text : lists[list]) {
                Item it;
                it.list = list;
                it.label = std::make_unique<stardraft::kit::Label>(text);
                it.label->setWrap(true);
                it.label->setIcon(int(list == 0 ? UiIcon::Check : list == 1 ? UiIcon::Remove : UiIcon::Warning));
                it.label->setTone(list == 2 ? stardraft::kit::LabelTone::Warn : stardraft::kit::LabelTone::Normal);
                summaryPanel_.add(*it.label, {0, 0, 0, 0});
                items_.push_back(std::move(it));
            }
        }
        // The tiles count parts (and things to do); the map is not counted.
        for (int i = 0; i < 3; ++i) tileNum_[i].setText(std::to_wstring(counts[i]));
        tileCap_[0].setText(tr(counts[0] == 1 ? L"part to import" : L"parts to import"));
        tileCap_[1].setText(tr(counts[1] == 1 ? L"part not imported" : L"parts not imported"));
        tileCap_[2].setText(tr(counts[2] == 1 ? L"thing to do" : L"things to do"));
    }

    void refresh() {
        using stardraft::kit::LabelTone;
        static const wchar_t* const titles[4] = {L"Select the project", L"Map and build folder", L"Select what to import", L"Summary"};
        title_.setText(tr(titles[step_]));
        subtitle_.setText(step_ == 0 ? tr(L"An EUD Editor 3 (.e3s), EUD Editor 2 (.e2s) or euddraft (.eds) project. StarDraft makes a new project from it. "
                                          L"The old project does not change.")
                          : step_ == 1 ? tr(L"The new project gets a copy of the map.")
                          : step_ == 2 ? tr(L"Clear a part to leave it out.")
                                       : tr(L"Import makes the project and opens it."));
        // 1
        if (file_.empty()) {
            fileName_.setText(tr(L"No project selected")), fileName_.setIcon(int(UiIcon::Open)), fileName_.setTone(LabelTone::Dim);
            fileWhat_.setText({}), filePath_.setText({});
            fileBrowse_.setText(tr(L"Select\x2026"));
        } else {
            fileName_.setText(file_.filename().wstring()), fileName_.setIcon(int(UiIcon::Project)), fileName_.setTone(LabelTone::Normal);
            filePath_.setText(importPath(file_.parent_path()));
            fileBrowse_.setText(tr(L"Change\x2026"));
            if (!scanned_) fileWhat_.setText(importFmt(tr(L"Could not read the file. %s"), {error_})), fileWhat_.setTone(LabelTone::Bad);
            else {
                const std::wstring v = widen(scan_.version);
                fileWhat_.setText(scan_.kind == "e3s"   ? importFmt(tr(L"EUD Editor 3 project, saved by version %s"), {v})
                                  : scan_.kind == "e2s" ? importFmt(tr(L"EUD Editor 2 project, saved by version %s"), {v})
                                                        : tr(L"euddraft project"));
                fileWhat_.setTone(LabelTone::Good);
            }
        }
        // 2
        mapName_.setText(mapPath().empty() ? tr(L"None") : mapPath().filename().wstring());
        if (mapThere()) mapPathL_.setText(importPath(mapPath().parent_path())), mapPathL_.setTone(LabelTone::Faint);
        else mapPathL_.setText(tr(L"The map is not on this computer. Select it to go on.")), mapPathL_.setTone(LabelTone::Warn);
        setBadge(mapChip_, mapThere() ? tr(L"Found") : tr(L"Not found"), mapThere() ? stardraft::kit::BadgeTone::Good : stardraft::kit::BadgeTone::Warn);
        mapBrowse_.setText(mapThere() ? tr(L"Change\x2026") : tr(L"Browse\x2026"));
        outDefault_.setLabel(importFmt(tr(L"In the project folder (%s_EUD.scx)"), {name_.text().empty() ? std::wstring(L"<name>") : name_.text()}));
        outKeep_.setLabel(scan_.output.empty() ? tr(L"Where the old project writes it (none)") : importFmt(tr(L"Where the old project writes it (%s)"), {importPath(scan_.output)}));
        outKeep_.setEnabled(!scan_.output.empty());
        outDefault_.setOn(!choices_.keepOutput), outKeep_.setOn(choices_.keepOutput);
        buildHead_.setText(scan_.kind == "e2s" ? tr(L"EUD Editor 2's build folder") : tr(L"EUD Editor 3's build folder"));
        buildPath_.setText(scan_.buildFolder.empty() ? tr(L"None") : importPath(scan_.buildFolder));
        if (!scan_.buildFolder.empty())
            setBadge(buildChip_, scan_.buildMatches ? tr(L"Found") : tr(L"Another map's build"), scan_.buildMatches ? stardraft::kit::BadgeTone::Good : stardraft::kit::BadgeTone::Warn);
        else
            setBadge(buildChip_, buildSelected_ ? tr(L"No build in this folder") : tr(L"Not found"), stardraft::kit::BadgeTone::Warn);
        buildBrowse_.setText(scan_.buildFolder.empty() ? tr(L"Browse\x2026") : tr(L"Change\x2026"));
        buildText_.setText(scan_.kind == "e2s"
                               ? tr(L"EUD Editor 2 writes the code of its trigger editor and of some plugins only when it builds. Its build folder is "
                                    L"Data\\eudplibdata in the folder of EUD Editor 2.")
                               : tr(L"EUD Editor 3 writes the code of macros, block scripts, classic triggers, the BGM player and dot images only "
                                    L"when it builds. Its build folder is BuildData_<name>, beside the map, the project file, or in Data\\temp in "
                                    L"the folder of EUD Editor 3."));
        // 3
        for (auto& r : rows_)
            if (const stardraft::ImportPart* p = scan_.part(r->id)) {
                const auto [text, tone] = partState(*p);
                setBadge(r->chip, text, tone);
                r->box.setOn(p->way != stardraft::ImportPart::Way::None && !choices_.skip.count(r->id));
            }
        fromText_.setOn(!choices_.scriptsFromBuild), fromBuild_.setOn(choices_.scriptsFromBuild);
        if (dlg_) {
            dlg_->setEnabledFor(kImportBack, step_ > 0);
            dlg_->setEnabledFor(kImportNext, stepComplete());
            dlg_->setTextFor(kImportNext, step_ == 3 ? tr(L"Import") : tr(L"Next"));
        }
        place();
        invalidate();
    }

    void place() {
        using namespace importLayout;
        auto r = [](int x, int y, int w, int h) { return draw::Rect{scale(x), scale(y), scale(x + w), scale(y + h)}; };
        const int W = width() > 0 ? int(width() * 100 / std::max(1, scale(100))) : kContentW;
        const int H = height() > 0 ? int(height() * 100 / std::max(1, scale(100))) : kContentH;
        const int X = kMainX, MW = W - kMainX;  // the step's own part
        railCard_.setFrame(r(0, 0, kRailW, H));
        stepper_.setFrame(r(16, 20, kRailW - 28, 4 * stardraft::kit::kStepH));
        stepper_.setCurrent(step_);
        fileCard_.setVisible(step_ == 0), newCard_.setVisible(step_ == 0), mapCard_.setVisible(step_ == 1);
        buildCard_.setVisible(step_ == 1 && buildHelps());
        for (auto& t : tileCard_) t.setVisible(step_ == 3);
        title_.setFrame(r(X, 4, MW, 30));
        subtitle_.setFrame(r(X, 38, MW, subtitleH_));
        const int top = std::max(kHeadH, 38 + subtitleH_ + 18);
        auto show = [](std::initializer_list<draw::View*> vs, bool on) {
            for (draw::View* v : vs) v->setVisible(on);
        };
        show({&fileName_, &fileWhat_, &filePath_, &fileBrowse_, &newHead_, &nameL_, &name_, &locL_, &location_, &locBrowse_}, step_ == 0);
        show({&mapHead_, &mapName_, &mapPathL_, &mapChip_, &mapBrowse_, &outL_, &outDefault_, &outKeep_}, step_ == 1);
        show({&buildHead_, &buildText_, &buildPath_, &buildChip_, &buildBrowse_}, step_ == 1 && buildHelps());
        partsView_.setVisible(step_ == 2);
        summaryView_.setVisible(step_ == 3);
        for (int i = 0; i < 3; ++i) tileNum_[i].setVisible(step_ == 3), tileCap_[i].setVisible(step_ == 3);
        const int P = kCardPad, inner = MW - 2 * P;
        if (step_ == 0) {
            // The file's card, then the new project's card.
            fileCard_.setFrame(r(X, top, MW, 92));
            fileName_.setFrame(r(X + P, top + 14, inner - kBrowseW - kGap, 22));
            fileWhat_.setFrame(r(X + P + 24, top + 38, inner - kBrowseW - kGap - 24, 20));
            filePath_.setFrame(r(X + P + 24, top + 60, inner - kBrowseW - kGap - 24, 18));
            fileBrowse_.setFrame(r(X + MW - P - kBrowseW, top + 14, kBrowseW, 28));
            const int t2 = top + 92 + 16;
            newCard_.setFrame(r(X, t2, MW, 116));
            newHead_.setFrame(r(X + P, t2 + 12, inner, 22));
            nameL_.setFrame(r(X + P, t2 + 44, kLabelW, kFieldH)), name_.setFrame(r(X + P + kLabelW, t2 + 44, inner - kLabelW, kFieldH));
            locL_.setFrame(r(X + P, t2 + 76, kLabelW, kFieldH)), location_.setFrame(r(X + P + kLabelW, t2 + 76, inner - kLabelW - kBrowseW - kGap, kFieldH));
            locBrowse_.setFrame(r(X + MW - P - kBrowseW, t2 + 76, kBrowseW, kFieldH));
        }
        if (step_ == 1) {
            mapCard_.setFrame(r(X, top, MW, 160));
            mapHead_.setFrame(r(X + P, top + 12, inner, 22));
            mapChip_.setFrame(r(X + MW - P - 160, top + 12, 160, 22));
            mapName_.setFrame(r(X + P, top + 44, inner - kBrowseW - kGap, 22));
            mapPathL_.setFrame(r(X + P + 24, top + 66, inner - kBrowseW - kGap - 24, 18));
            mapBrowse_.setFrame(r(X + MW - P - kBrowseW, top + 44, kBrowseW, 28));
            outL_.setFrame(r(X + P, top + 98, kLabelW, kFieldH));
            outDefault_.setFrame(r(X + P + kLabelW, top + 98, inner - kLabelW, kFieldH));
            outKeep_.setFrame(r(X + P + kLabelW, top + 124, inner - kLabelW, kFieldH));
            if (buildHelps()) {
                const int t2 = top + 160 + 16, h = 98 + buildTextH_;
                buildCard_.setFrame(r(X, t2, MW, h));
                buildHead_.setFrame(r(X + P, t2 + 12, inner - 170, 22));
                buildChip_.setFrame(r(X + MW - P - 200, t2 + 12, 200, 22));
                buildText_.setFrame(r(X + P, t2 + 40, inner, buildTextH_));
                buildPath_.setFrame(r(X + P, t2 + 50 + buildTextH_, inner - kBrowseW - kGap, 22));
                buildBrowse_.setFrame(r(X + MW - P - kBrowseW, t2 + 46 + buildTextH_, kBrowseW, 28));
            }
        }
        // 3: a card per part, in the scrolled list.
        partsView_.setFrame(r(X, top, MW, H - top));
        int y = 0;
        const int chipW = 196, rowW = MW - 14;
        for (auto& row : rows_) {
            const bool choice = row->id == "scripts" && showScriptsChoice();
            const int h = kPartH + (choice ? 2 * kFieldH + 8 : 0);
            row->card.setFrame(r(0, y, rowW, h - 8));
            row->icon.setFrame(r(14, y + 12, 20, 20));
            row->box.setFrame(r(42, y + 9, rowW - 42 - chipW - 12, kFieldH));
            row->chip.setFrame(r(rowW - 12 - chipW, y + 10, chipW, 22));
            row->detail.setFrame(r(68, y + 30, rowW - 68 - 12, 18));
            if (choice) {
                scriptsL_.setFrame(r(68, y + 54, 110, kFieldH));
                fromText_.setFrame(r(180, y + 54, rowW - 180 - 12, kFieldH)), fromBuild_.setFrame(r(180, y + 54 + kFieldH, rowW - 180 - 12, kFieldH));
            }
            y += h;
        }
        partsPanel_.setFrame(draw::Rect{0, 0, scale(MW) - (partsView_.hasVerticalBar() ? scale(12) : 0), scale(y)});
        partsView_.refresh();
        // 4: the tiles, then the lists.
        const int tileW = (MW - 2 * 12) / 3;
        for (int i = 0; i < 3; ++i) {
            const int tx = X + i * (tileW + 12);
            tileCard_[i].setFrame(r(tx, top, tileW, 72));
            tileNum_[i].setFrame(r(tx + P, top + 8, tileW - 2 * P, 34));
            tileCap_[i].setFrame(r(tx + P, top + 44, tileW - 2 * P, 20));
        }
        toBuild_.setVisible(step_ == 3 && needBuild_);
        toBuild_.setFrame(r(X, top + 72 + 12, 220, 28));
        const int lt = top + 72 + 16 + (needBuild_ ? 40 : 0);
        summaryView_.setFrame(r(X, lt, MW, H - lt));
        int sy = 0, last = -1;
        for (auto& it : items_) {
            if (it.list != last && last != -1) sy += 12;  // (a gap between the lists)
            last = it.list;
            if (it.heading) it.h = 22;
            it.label->setFrame(r(0, sy, MW - 16, it.h));
            sy += it.h + 4;
        }
        summaryPanel_.setFrame(draw::Rect{0, 0, scale(MW) - (summaryView_.hasVerticalBar() ? scale(12) : 0), scale(sy)});
        summaryView_.refresh();
    }

    // The heights (at 100 %) that the wrapped texts need at their widths, measured in paint; a change places them again.
    void measure(draw::Canvas& c) {
        const int s = std::max(1, scale(100));
        bool changed = false;
        auto need = [&](stardraft::kit::Label& l, int& h, int min) {
            if (l.width() <= 0) return;
            const int v = std::max(min, l.preferredHeight(c, l.width()) * 100 / s + 2);
            if (v != h) h = v, changed = true;
        };
        need(subtitle_, subtitleH_, 20);
        need(buildText_, buildTextH_, 20);
        for (auto& it : items_)
            if (!it.heading) need(*it.label, it.h, 20);
        if (changed) place(), invalidate();
    }

    // The focus on the step that shows: a hidden field must not take the keys.
    void focusStep() {
        if (step_ == 0) name_.focus();
        else if (step_ == 1) mapBrowse_.focus();
        else if (step_ == 2 && !rows_.empty()) rows_.front()->box.focus();
        else if (step_ == 3 && needBuild_) toBuild_.focus();
        else if (dlg_) dlg_->focus();
    }
    static void setBadge(stardraft::kit::Badge& b, const std::wstring& text, stardraft::kit::BadgeTone tone) { b.setText(text), b.setTone(tone); }

    stardraft::kit::Dialog* dlg_ = nullptr;
    int step_ = 0;
    bool scanned_ = false, buildSelected_ = false, needBuild_ = false;
    int subtitleH_ = 20, buildTextH_ = 40;
    fs::path file_;
    std::wstring error_;
    stardraft::ImportScan scan_;
    stardraft::ImportChoices choices_;
    stardraft::kit::Card railCard_, fileCard_, newCard_, mapCard_, buildCard_, tileCard_[3];
    stardraft::kit::Stepper stepper_;
    stardraft::kit::Label title_, subtitle_;
    stardraft::kit::Label fileName_, fileWhat_, filePath_, newHead_, nameL_, locL_;
    stardraft::kit::Label mapHead_, mapName_, mapPathL_, outL_, buildHead_, buildText_, buildPath_;
    stardraft::kit::TextField name_, location_;
    stardraft::kit::Button fileBrowse_, locBrowse_, mapBrowse_, buildBrowse_, toBuild_;
    stardraft::kit::Badge mapChip_, buildChip_;
    stardraft::kit::RadioGroup outGroup_, scriptsGroup_;
    stardraft::kit::RadioButton outDefault_{L"", outGroup_, true}, outKeep_{L"", outGroup_};
    stardraft::kit::ScrollView partsView_, summaryView_;
    draw::View partsPanel_;
    draw::View summaryPanel_;
    std::vector<std::unique_ptr<PartRow>> rows_;
    stardraft::kit::Label scriptsL_;
    stardraft::kit::RadioButton fromText_{L"", scriptsGroup_, true}, fromBuild_{L"", scriptsGroup_};
    stardraft::kit::Label tileNum_[3], tileCap_[3];
    std::vector<Item> items_;
};

// The dialog's buttons and size (the snapshot uses them too).
inline draw::Point importWizardSetup(stardraft::kit::Dialog& dlg, ImportWizardView& view) {
    using namespace importLayout;
    dlg.setContent(view);
    std::vector<stardraft::kit::DialogButton> buttons;
    buttons.push_back({tr(L"Back"), kImportBack, false, false, true, 0, true});
    buttons.push_back({tr(L"Next"), kImportNext, true, false, false, 96, true});
    buttons.push_back({tr(L"Cancel"), kImportCancel, false, true, false, 0});
    dlg.setButtons(std::move(buttons));
    dlg.stepped = [&view](int r) { view.stepped(r); };
    dlg.setInsets({kPad, kPad, kPad, kPad, 16, 8});
    view.setDialog(&dlg);
    return draw::Point{scale(kDialogW), scale(kDialogH)};
}

// Makes the project from the steps' choices and opens it, with notes\Import report.txt (what came in and what did not).
// False: it could not (the error box said why).
inline bool importFromWizard(const ImportWizardView& view) {
    stardraft::EudProject pr;
    stardraft::ImportReport rep;
    std::string err;
    if (!stardraft::importProject(view.scan(), view.choices(), fs::path(view.location()), narrow(view.name()), shellState().data, pr, rep, err)) {
        shellErrorBox(importFmt(tr(L"Could not import the project. %s"), {widen(err)}), tr(L"Import a project"));
        return false;
    }
    std::string text = "Import of " + narrow(view.scan().file.filename().wstring()) + "\n\nImported:\n";
    for (const std::string& l : rep.imported) text += "  - " + l + "\n";
    text += "\nNot imported:\n";
    for (const std::string& l : rep.notImported) text += "  - " + l + "\n";
    if (rep.notImported.empty()) text += "  (nothing)\n";
    std::error_code ec;
    fs::create_directories(pr.folder() / "notes", ec);
    std::ofstream(pr.folder() / "notes" / "Import report.txt", std::ios::binary) << text;
    openProject(pr.folder());
    setMessage(rep.notImported.empty() ? importFmt(tr(L"Imported the project %s"), {view.name()})
                                       : importFmt(tr(L"Imported the project %s. What did not come over is in notes\\Import report.txt"), {view.name()}));
    return true;
}

// File > Import a project: the steps, then the project from their choices.
inline void projectImportWizard(draw::View& from) {
    ImportWizardView view(defaultProjectsFolder().make_preferred().wstring());
    stardraft::kit::Dialog dlg;
    const draw::Point size = importWizardSetup(dlg, view);
    if (dlg.run(from, tr(L"Import a project"), size) == kImportNext) importFromWizard(view);
}
