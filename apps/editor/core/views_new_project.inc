// File > New EUD project (portable, core.inc): the project's name, its location, the output file and what it starts
// from, with the folder sketch under them (what the project's folder holds). Kit text fields, a kit dropdown and kit
// buttons in a kit Dialog; Create and Cancel are the dialog's buttons. The Browse buttons pick through the seam
// (shellPickFolder, shellPickOutputFile). The host (main.cpp on Windows) gives the defaults and makes the project.

// The dialog's results (what its buttons return).
constexpr int kNewProjectCreate = 1, kNewProjectCancel = 2;

// The layout is the native window's, in pixels at 100 %, from the content's top left corner (the Dialog's insets put
// the content 16 px from the window's edges, ui::kDialogPad): a label column 96 px wide (the label 88 px), the boxes
// 560 px wide, one row each 32 px (ui::kFormRowH), the fields 24 px high (ui::kFieldH), the Browse buttons 88 px wide
// (ui::kButtonMinW), the folder sketch 190 px high under the rows, then 16 px to Create and Cancel.
namespace newProjectLayout {
constexpr int kLabelW = 96, kBoxW = 560, kRowStep = 32, kFieldH = 24, kBrowseW = 88, kGap = 8, kInfoH = 190;
constexpr int kComboH = 23;  // the native drop-down list was 23 px high (its height follows the font), the fields 24
constexpr int kContentW = kLabelW + kBoxW, kContentH = 4 * kRowStep + kInfoH;  // 656 x 318
constexpr int kPad = 16;
constexpr int kDialogW = kPad + kContentW + kPad, kDialogH = kPad + kContentH + kPad + 28 + kPad;  // 688 x 394 (buttons 28 px high)
}  // namespace newProjectLayout

// What the dialog starts with: the name and the folder, and the open map's title (empty: no map is open, so the
// project starts from a new blank map).
struct NewProjectStart {
    std::wstring name, location, openMap;
};
// What Create gives: the name, the folder it goes in, the output file (empty: <name>_EUD.scx in the project's folder)
// and whether the project starts from the open map.
struct NewProjectChoice {
    std::wstring name, location, output;
    bool fromOpenMap = false;
};

// The folder sketch under the fields: what the project's folder holds, with the name typed (or <name>). `output`:
// an output file is typed.
// The project's folder: its files, each with what it is (the dialog's list, under the fields).
constexpr int kSketchRows = 5;
inline std::array<std::pair<std::wstring, std::wstring>, kSketchRows> newProjectFiles(const std::wstring& name, bool output) {
    const std::wstring n = name.empty() ? L"<name>" : name;
    return {{{L"project.stardraft", L"The project file"},
             {n + L".scx", L"The map you edit here (Brood War format)"},
             {n + L".eds", L"euddraft's build file: Build writes it"},
             {n + L"_EUD.scx", output ? L"The built map: Build (Ctrl+B) writes it to the Output file" : L"The built map: Build (Ctrl+B) writes it"},
             {L"scripts\\main.eps", L"Your code (epScript). To add more, use File > Project > Add a script."}}};
}

// The dialog's content: the labels, the fields, the Browse buttons, the start dropdown and the sketch.
class NewProjectView final : public draw::View {
public:
    explicit NewProjectView(const NewProjectStart& start) : haveMap_(!start.openMap.empty()) {
        using stardraft::kit::LabelTone;
        const wchar_t* names[4] = {L"Name", L"Location", L"Output file", L"Start from"};
        for (int i = 0; i < 4; ++i) {
            labels_[i].setText(tr(names[i]));
            labels_[i].setTone(LabelTone::Dim);  // (the native labels' text was the dim color: ui::fieldLabel)
            add(labels_[i], {0, 0, 0, 0});
        }
        name_.setText(start.name);
        location_.setText(start.location);
        output_.setPlaceholder(tr(L"<name>_EUD.scx in the project folder"));
        for (stardraft::kit::TextField* f : {&name_, &location_, &output_}) f->onChange = [this] { refreshSketch(); };
        browse_.setText(tr(L"Browse\x2026"));
        browse_.onClick = [this] {
            const std::wstring dir = shellPickFolder(tr(L"Select the folder for the project"));
            if (!dir.empty()) location_.setText(dir), refreshSketch();
        };
        browseOut_.setText(tr(L"Browse\x2026"));
        browseOut_.onClick = [this] {
            const std::wstring file = shellPickOutputFile(name_.text() + L"_EUD.scx");
            if (!file.empty()) output_.setText(file), refreshSketch();
        };
        // Tab goes through the views in the order they are added: the screen order (as the old window's).
        add(name_, {0, 0, 0, 0}), add(location_, {0, 0, 0, 0}), add(browse_, {0, 0, 0, 0});
        add(output_, {0, 0, 0, 0}), add(browseOut_, {0, 0, 0, 0});
        std::vector<stardraft::kit::DropdownItem> items;
        if (haveMap_) items.push_back({tr(L"The open map (") + start.openMap + L")"});
        items.push_back({tr(L"A new blank map (128 x 128 Badlands)")});
        start_.setItems(std::move(items));
        start_.setValue(0);
        start_.enterOpens = false;  // (Enter in a closed list is Create, as in a dialog's combo box)
        add(start_, {0, 0, 0, 0});
        // The folder: a line, its files in two columns (the name, then what it is), then the note on euddraft.
        intro_.setTone(LabelTone::Dim);
        add(intro_, {0, 0, 0, 0});
        for (int i = 0; i < kSketchRows; ++i) files_[i].setTone(LabelTone::Dim), add(fileNames_[i], {0, 0, 0, 0}), add(files_[i], {0, 0, 0, 0});
        note_.setTone(LabelTone::Dim), note_.setWrap(true);
        note_.setText(L"The first build downloads euddraft from GitHub into its own folder. Other copies of euddraft on this computer do "
                      L"not change. A map that you open stays a regular map. Only a project can be built.");
        add(note_, {0, 0, 0, 0});
        refreshSketch();
    }

    // The dialog this view is in (the self-test's picture closes it with Cancel).
    void setDialog(stardraft::kit::Dialog* d) { dlg_ = d; }
    void cancel() {
        if (dlg_) dlg_->close(kNewProjectCancel);
    }

    // What the fields hold now (a call after Create).
    NewProjectChoice choice() const {
        NewProjectChoice c;
        c.name = name_.text(), c.location = location_.text(), c.output = output_.text();
        c.fromOpenMap = haveMap_ && start_.value() == 0;
        return c;
    }
    // Fills the fields as typing would (the self-test): the name, the location and the start's index.
    void fill(const std::wstring& name, const std::wstring& location, int start) {
        name_.setText(name), location_.setText(location), start_.setValue(start);
        refreshSketch();
    }
    // Closes the dialog as Create does (the self-test).
    void create() {
        if (dlg_) dlg_->close(kNewProjectCreate);
    }

    void paint(draw::Canvas& c) override { c.fill({0, 0, width(), height()}, kBg); }

    void resized() override {
        using namespace newProjectLayout;
        auto r = [](int x, int y, int w, int h) { return draw::Rect{scale(x), scale(y), scale(x + w), scale(y + h)}; };
        for (int i = 0; i < 4; ++i) labels_[i].setFrame(r(0, i * kRowStep, kLabelW - kGap, kFieldH));
        name_.setFrame(r(kLabelW, 0, kBoxW, kFieldH));
        location_.setFrame(r(kLabelW, kRowStep, kBoxW - kBrowseW - kGap, kFieldH));
        browse_.setFrame(r(kLabelW + kBoxW - kBrowseW, kRowStep, kBrowseW, kFieldH));
        output_.setFrame(r(kLabelW, 2 * kRowStep, kBoxW - kBrowseW - kGap, kFieldH));
        browseOut_.setFrame(r(kLabelW + kBoxW - kBrowseW, 2 * kRowStep, kBrowseW, kFieldH));
        start_.setFrame(r(kLabelW, 3 * kRowStep, kBoxW, kComboH));
        const int y0 = 4 * kRowStep + 4, rowH = 20, nameW = 216;  // (the names' column: a project name of about 24 letters)
        intro_.setFrame(r(0, y0, kContentW, rowH));
        for (int i = 0; i < kSketchRows; ++i) {
            fileNames_[i].setFrame(r(16, y0 + 26 + i * rowH, nameW - 24, rowH));
            files_[i].setFrame(r(nameW, y0 + 26 + i * rowH, kContentW - nameW, rowH));
        }
        note_.setFrame(r(0, y0 + 26 + kSketchRows * rowH + 10, kContentW, kInfoH - (26 + kSketchRows * rowH + 14)));
    }

private:
    void refreshSketch() {
        const std::wstring n = name_.text().empty() ? L"<name>" : name_.text();
        intro_.setText(L"The project goes into the new folder " + n + L"\\ in the location above:");
        const auto rows = newProjectFiles(name_.text(), !output_.text().empty());
        for (int i = 0; i < kSketchRows; ++i) fileNames_[i].setText(rows[size_t(i)].first), files_[i].setText(rows[size_t(i)].second);
    }

    stardraft::kit::Dialog* dlg_ = nullptr;
    bool haveMap_ = false;
    stardraft::kit::Label labels_[4];
    stardraft::kit::TextField name_, location_, output_;
    stardraft::kit::Button browse_, browseOut_;
    stardraft::kit::Dropdown start_;
    stardraft::kit::Label intro_, fileNames_[kSketchRows], files_[kSketchRows], note_;
};

// Runs the dialog from `from` (the program's anchor view). True when Create closed it (`out` holds the choice).
// `shown` is the content while the dialog is open, so the host can find its window.
inline bool runNewProjectDialog(draw::View& from, const NewProjectStart& start, NewProjectChoice& out, NewProjectView*& shown) {
    using namespace newProjectLayout;
    NewProjectView view(start);
    stardraft::kit::Dialog dlg;
    dlg.setContent(view);
    view.setDialog(&dlg);
    std::vector<stardraft::kit::DialogButton> buttons;
    buttons.push_back({tr(L"Create"), kNewProjectCreate, true, false, false, 0});
    buttons.push_back({tr(L"Cancel"), kNewProjectCancel, false, true, false, 0});
    dlg.setButtons(std::move(buttons));
    dlg.setInsets({kPad, kPad, kPad, kPad, kPad, 8});  // the rows from the edges, the sketch 16 px above the buttons
    shown = &view;
    const int r = dlg.run(from, tr(L"New EUD project"), draw::Point{scale(kDialogW), scale(kDialogH)});
    shown = nullptr;
    if (r != kNewProjectCreate) return false;
    out = view.choice();
    return true;
}
