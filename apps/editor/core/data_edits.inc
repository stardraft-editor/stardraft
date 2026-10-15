// The Data workspace's value edits and their undo (portable, core.inc): a field's value set on a table, and the edit
// history (each saved change is a step: the table, or the extra's text, before and after it). The Windows program
// (main.cpp: dataSaved, dataUndo; data_extras.inc: extrasSaved) saves the files and repaints; the steps are kept here.

// One change: the .dat table's bytes (extra 0) or an extra's edits text (1 strings, 2 buttons, 3 requirements) before
// and after it, with the tab and the entry that were open.
struct DataEditStep {
    int tab = 0, entry = 0;
    Bytes before, after;
    int extra = 0;
};

class DataEditHistory {
public:
    static constexpr size_t kMost = 200;  // the oldest step goes when there are more

    std::vector<DataEditStep> undo, redo;
    std::vector<Bytes> saved;  // each table as last saved (one per DatSchema::all(), in that order)

    void clear() { undo.clear(), redo.clear(), saved.clear(); }
    // A new start: no steps, and each table as it is now is the saved one (no table: empty).
    void reset(const std::vector<std::unique_ptr<DatTable>>& tables) {
        clear();
        for (const auto& t : tables) saved.push_back(t ? t->bytes() : Bytes{});
    }
    bool canUndo(bool isRedo) const { return !(isRedo ? redo : undo).empty(); }
    // A table's change: when `now` differs from the table's saved bytes, a step (the saved bytes before, `now` after),
    // and `now` is the saved bytes. False: no change.
    bool recordTable(int tab, int entry, const Bytes& now) {
        if (tab < 0 || size_t(tab) >= saved.size() || saved[size_t(tab)] == now) return false;
        push({tab, entry, saved[size_t(tab)], now, 0});
        saved[size_t(tab)] = now;
        return true;
    }
    // An extra's change (strings, buttons, requirements: extra 1 to 3): its text before and after.
    void recordExtra(int tab, int entry, int extra, Bytes before, Bytes after) { push({tab, entry, std::move(before), std::move(after), extra}); }
    // The step to undo (redo: to redo), taken from its list. False: there is none.
    bool take(bool isRedo, DataEditStep& s) {
        auto& from = isRedo ? redo : undo;
        if (from.empty()) return false;
        s = from.back();
        from.pop_back();
        return true;
    }
    // A taken step that was undone (redo: redone) goes to the other list.
    void done(bool isRedo, DataEditStep s) { (isRedo ? undo : redo).push_back(std::move(s)); }
    // After an undo or redo of a table's step: the table's bytes as they are now are the saved ones.
    void tableSaved(int tab, const Bytes& now) {
        if (tab >= 0 && size_t(tab) < saved.size()) saved[size_t(tab)] = now;
    }

private:
    void push(DataEditStep s) {
        undo.push_back(std::move(s));
        if (undo.size() > kMost) undo.erase(undo.begin());
        redo.clear();
    }
};

// A field's value on an entry of a table, kept inside what the field can hold.
inline void dataApplyValue(DatTable& t, const DatField& f, int entry, uint32_t v) { t.set(f, entry, std::min(v, f.maxValue())); }
