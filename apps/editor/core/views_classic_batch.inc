// Classic triggers' block dialog (Add a block, Edit the block; portable, core.inc): the variable's name, the kind and the
// values. The values are a list with a row each; Add values picks them from the kind's list (a multi-pick: the checked
// ones stay in their order, new ones go last) and Remove the value takes the selected one out. The kind is locked while
// the block has values. The OK button needs one value; Remove the block (editing only) takes the block out; Cancel keeps
// it as it was. The dialog is a kit Dialog (Host::runModal); the host (classic_triggers.inc on Windows) gives the names
// of the values and runs the multi-pick.

// The dialog's results (what its buttons return).
constexpr int kBatchOk = 1, kBatchCancel = 2, kBatchRemove = 3;

// The program's part of the dialog: the values' names, the kind's values for the picker, and the picker itself.
class BatchHost {
public:
    virtual ~BatchHost() = default;
    // The text of a value of the kind (a unit's name, a player, a location).
    virtual std::wstring valueText(ClassicBatchKind kind, uint32_t value) = 0;
    // The values the kind offers for Add values, each with its name, in the list's order.
    virtual std::vector<std::pair<uint32_t, std::wstring>> offeredValues(ClassicBatchKind kind) = 0;
    // The multi-pick over `items`, with `checked` checked: true when OK, and `checked` then holds the new checks. `at`:
    // the place under the Add values button, in the dialog's window.
    virtual bool pickValues(const std::vector<std::pair<uint32_t, std::wstring>>& items, std::set<uint32_t>& checked, draw::Point at) = 0;
};

// The name a new batch's variable gets: the kind's name (classicBlockKinds).
inline std::string batchDefaultName(ClassicBatchKind k) { return classicBlockKind(k).name; }

// The point of a view's top left corner in its root's coordinates (the window's client area).
inline draw::Point batchOriginOf(const draw::View& v) {
    draw::Point p{0, 0};
    for (const draw::View* c = &v; c; c = c->parent()) p.x += c->frame().left, p.y += c->frame().top;
    return p;
}

// The layout is the native dialog's (its template, 240 x 206 dialog units at Segoe UI 9 pt: 1.5 px across and 2 px
// down at 100 %), in pixels at 100 %, from the content's top left corner. The content sits 12 px from the left edge,
// 8 px from the top, 9 px from the right, and ends where the buttons start (the Dialog's insets, runBatchDialog). The
// kit's fields are kFieldH (24 px) high, where the native edit box was 28 px and the native combo 21 px; the values are
// a kit list (rows of kRowH, the native rows were 17 px) with the kit's scroll bar.
namespace batchLayout {
constexpr int kLabelX = 0, kLabelW = 126;     // "Variable name", "Kind", "Values": the label column
constexpr int kFieldX = 132, kFieldW = 204;   // the fields and the list start 132 px in (abs 144)
constexpr int kIntroY = 8, kIntroH = 44;      // the text at the top (two lines)
constexpr int kNameLabelY = 66, kNameY = 58;  // label text top; the name field's top (24 px, centered on the native box)
constexpr int kKindLabelY = 106, kKindY = 99;
constexpr int kValuesLabelY = 146, kValuesY = 140, kValuesH = 160;
constexpr int kButtonsY = 308, kButtonH = 28;  // Add values and Remove the value: 99 px and 102 px wide
constexpr int kAddW = 99, kRemoveX = 237, kRemoveW = 102;
constexpr int kContentW = 339;                 // 360 - 12 - 9
}  // namespace batchLayout

// The dialog's content: the text, the form's rows and the values' buttons. The dialog's own buttons are the Dialog's.
class BatchView final : public draw::View {
public:
    BatchView(BatchHost& host, const ClassicBatch& batch, bool editing) : host_(host), editing_(editing), shown_(batch.kind) {
        intro_.setWrap(true), intro_.setTone(stardraft::kit::LabelTone::Normal);  // (the native text was the window's text color)
        intro_.setText(tr(L"A block repeats the actions in it for each value. The variable is the value of each copy."));
        name_.setText(widen(batch.name));
        kind_.setItems(kindItems());
        kind_.enterOpens = false;  // Enter in the closed list is the dialog's default button
        kind_.setValue(kindIndex(batch.kind));
        kind_.onChange = [this](int i) { changeKind(i); };
        values_.setMode(stardraft::kit::SelectMode::Single);
        values_.label = [this](int i) { return i >= 0 && i < int(vals_.size()) ? host_.valueText(shown_, vals_[size_t(i)]) : std::wstring(); };
        values_.onSelect = [this] { removeValue_.setEnabled(values_.selection().size() == 1); };
        addValues_.setText(tr(L"Add values…")), addValues_.onClick = [this] { addValues(); };
        removeValue_.setText(tr(L"Remove the value")), removeValue_.onClick = [this] { removeSelected(); };
        removeValue_.setEnabled(false);
        add(intro_, {0, 0, 0, 0}), add(name_, {0, 0, 0, 0}), add(kind_, {0, 0, 0, 0}), add(values_, {0, 0, 0, 0});
        add(addValues_, {0, 0, 0, 0}), add(removeValue_, {0, 0, 0, 0});
        fill(batch.values);
    }

    // The dialog this view is in (the buttons' enabled state and the OK gate are set on it).
    void setDialog(stardraft::kit::Dialog* d) { dlg_ = d; }

    // The answer: the kind, the name and the values (a call after OK).
    void collect(ClassicBatch& out) const {
        out.kind = shown_;
        out.values = vals_;
        out.name = classicBatchName(narrow(name_.text()), shown_);
    }
    bool canOk() const { return !vals_.empty(); }
    // Closes the dialog as Cancel does (the self-test's picture closes it).
    void cancel() {
        if (dlg_) dlg_->close(kBatchCancel);
    }

    // The text field (the self-test types into it) and the list (for tests and the screenshot).
    stardraft::kit::TextField& nameField() { return name_; }
    const std::vector<uint32_t>& values() const { return vals_; }
    bool editing() const { return editing_; }

    void paint(draw::Canvas& c) override {
        using namespace batchLayout;
        if (!focused_ && host()) focused_ = true, name_.focus();  // the name has the keyboard when the dialog opens
        c.fill({0, 0, width(), height()}, kBg);
        // The labels' text sits at the top of their boxes, as the native static text did.
        const draw::Color text = stardraft::kit::labelColor(stardraft::kit::LabelTone::Normal);
        c.text({scale(kLabelX), scale(kNameLabelY), scale(kLabelX + kLabelW), scale(kNameLabelY + 20)}, tr(L"Variable name"), text, draw::kLeft);
        c.text({scale(kLabelX), scale(kKindLabelY), scale(kLabelX + kLabelW), scale(kKindLabelY + 20)}, tr(L"Kind"), text, draw::kLeft);
        c.text({scale(kLabelX), scale(kValuesLabelY), scale(kLabelX + kLabelW), scale(kValuesLabelY + 20)}, tr(L"Values"), text, draw::kLeft);
    }
    void resized() override {
        using namespace batchLayout;
        intro_.setFrame({scale(kLabelX), scale(kIntroY), scale(kLabelX + kFieldX + kFieldW), scale(kIntroY + kIntroH)});
        name_.setFrame({scale(kFieldX), scale(kNameY), scale(kFieldX + kFieldW), scale(kNameY + 24)});
        kind_.setFrame({scale(kFieldX), scale(kKindY), scale(kFieldX + kFieldW), scale(kKindY + 24)});
        values_.setFrame({scale(kFieldX), scale(kValuesY), scale(kFieldX + kFieldW), scale(kValuesY + kValuesH)});
        addValues_.setFrame({scale(kFieldX), scale(kButtonsY), scale(kFieldX + kAddW), scale(kButtonsY + kButtonH)});
        removeValue_.setFrame({scale(kRemoveX), scale(kButtonsY), scale(kRemoveX + kRemoveW), scale(kButtonsY + kButtonH)});
    }

private:
    static std::vector<stardraft::kit::DropdownItem> kindItems() {
        std::vector<stardraft::kit::DropdownItem> out;
        for (const ClassicBlockKind& k : classicBlockKinds()) {
            stardraft::kit::DropdownItem it;
            it.label = widen(k.label);
            out.push_back(std::move(it));
        }
        return out;
    }
    int kindIndex(ClassicBatchKind k) const {
        const auto& kinds = classicBlockKinds();
        for (size_t i = 0; i < kinds.size(); ++i)
            if (kinds[i].kind == k) return int(i);
        return 0;
    }
    ClassicBatchKind kindAt(int i) const { return classicBlockKinds()[size_t(std::clamp(i, 0, int(classicBlockKinds().size()) - 1))].kind; }

    // The default name follows the kind (a name that is the old kind's default); the values start empty.
    void changeKind(int i) {
        const ClassicBatchKind chosen = kindAt(i);
        if (narrow(name_.text()) == batchDefaultName(shown_)) name_.setText(widen(batchDefaultName(chosen)));
        shown_ = chosen;
        fill({});
    }

    // The list's rows are the given values; the buttons and OK follow them (the kind is locked while there are values).
    void fill(std::vector<uint32_t> chosen) {
        vals_ = std::move(chosen);
        values_.clearSelection();
        values_.setCount(int(vals_.size()));
        removeValue_.setEnabled(false);
        kind_.setEnabled(vals_.empty());
        if (dlg_) dlg_->setEnabledFor(kBatchOk, !vals_.empty());
        invalidate();
    }

    void addValues() {
        const auto items = host_.offeredValues(shown_);
        std::set<uint32_t> checked(vals_.begin(), vals_.end());
        std::vector<std::pair<uint32_t, std::wstring>> offered;
        for (const auto& it : items)
            if (!(shown_ == ClassicBatchKind::Unit && it.first > 227)) offered.push_back(it);  // (the groups of units are not one unit type)
        const draw::Point at{batchOriginOf(addValues_).x, batchOriginOf(addValues_).y + addValues_.height()};
        if (!host_.pickValues(offered, checked, at)) return;
        std::vector<uint32_t> next;
        for (uint32_t v : vals_)
            if (checked.count(v)) next.push_back(v);  // the values that stay keep their order
        for (const auto& it : offered)
            if (checked.count(it.first) && std::find(vals_.begin(), vals_.end(), it.first) == vals_.end()) next.push_back(it.first);
        fill(std::move(next));
    }

    // The selected value goes; the others keep their order, and the next one is selected.
    void removeSelected() {
        const auto sel = values_.selection();
        if (sel.size() != 1) return;
        const int cur = sel[0];
        vals_.erase(vals_.begin() + cur);
        const int n = int(vals_.size());
        values_.setCount(n);
        if (n > 0) values_.selectOnly(std::min(cur, n - 1));
        removeValue_.setEnabled(n > 0);
        kind_.setEnabled(n == 0);
        if (dlg_) dlg_->setEnabledFor(kBatchOk, n > 0);
        invalidate();
    }

    BatchHost& host_;
    bool editing_ = false;
    ClassicBatchKind shown_ = ClassicBatchKind::Unit;  // the kind the list shows now (the default name follows it)
    std::vector<uint32_t> vals_;                       // the block's values, a row each
    stardraft::kit::Dialog* dlg_ = nullptr;
    stardraft::kit::Label intro_;
    stardraft::kit::TextField name_;
    stardraft::kit::Dropdown kind_;
    stardraft::kit::ListView values_;
    stardraft::kit::Button addValues_, removeValue_;
    bool focused_ = false;
};

// Runs the dialog from `from` (the program's anchor view; the host shows it over its owner window). Returns the button
// that closed it: kBatchOk (the batch holds the answer), kBatchRemove or kBatchCancel. `view` is the content, so the
// host can find the window (the self-test's screenshot) while it is open.
inline int runBatchDialog(draw::View& from, BatchHost& host, ClassicBatch& batch, bool editing, BatchView*& shown) {
    BatchView view(host, batch, editing);
    stardraft::kit::Dialog dlg;
    dlg.setContent(view);
    view.setDialog(&dlg);
    std::vector<stardraft::kit::DialogButton> buttons;
    if (editing) buttons.push_back({tr(L"Remove the block"), kBatchRemove, false, false, true, 165});
    buttons.push_back({tr(editing ? L"Save" : L"Add the block"), kBatchOk, true, false, false, 75});
    buttons.push_back({tr(L"Cancel"), kBatchCancel, false, true, false, 75});
    dlg.setButtons(std::move(buttons));
    dlg.setInsets({12, 8, 9, 12, 0, 6});  // the native template's margins: 12 px left, 8 top, 9 right, 12 bottom; buttons 6 apart
    dlg.setCenterOnWorkArea(true);        // DS_CENTER
    dlg.setAllow([&view](int r) { return r != kBatchOk || view.canOk(); });
    dlg.setEnabledFor(kBatchOk, view.canOk());
    shown = &view;
    const int r = dlg.run(from, tr(editing ? L"Edit the block" : L"Add a block"), draw::Point{scale(360), scale(412)});  // 240 x 206 dialog units
    shown = nullptr;
    if (r == kBatchOk) view.collect(batch);
    return r;
}
