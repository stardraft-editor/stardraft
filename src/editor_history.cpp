// The names of the undo steps (Edit > Undo history, the status bar after Undo and Redo): each step's name comes from
// the map before and after it (the sections that changed and how), so that each edit has a name with no text of its
// own at its call site.

#include "stardraft/editor.h"

#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

namespace stardraft {

namespace {

using Parts = std::vector<std::pair<SectionName, const Bytes*>>;

// The data of the first section of that name (nullptr: none).
const Bytes* partOf(const Parts& parts, SectionName name) {
    for (const auto& [n, data] : parts)
        if (n == name) return data;
    return nullptr;
}

bool sameData(const Bytes* a, const Bytes* b) {
    if (a == b) return true;
    if (!a || !b) return (!a || a->empty()) && (!b || b->empty());
    return *a == *b;
}

// A list of records (units, doodads, sprites) before and after: how many more or fewer, else how many are not the same
// and whether only their places (bytes [at, at + 4): x and y) changed.
struct RecordChange {
    int before = 0, after = 0, changed = 0;
    bool movedOnly = true;
};
RecordChange records(const Bytes* a, const Bytes* b, size_t size, size_t xyAt) {
    RecordChange r;
    r.before = a ? int(a->size() / size) : 0;
    r.after = b ? int(b->size() / size) : 0;
    if (r.before != r.after) return r;
    for (int i = 0; i < r.before; ++i) {
        const uint8_t* p = a->data() + size_t(i) * size;
        const uint8_t* q = b->data() + size_t(i) * size;
        if (std::memcmp(p, q, size) == 0) continue;
        ++r.changed;
        if (std::memcmp(p, q, xyAt) != 0 || std::memcmp(p + xyAt + 4, q + xyAt + 4, size - xyAt - 4) != 0) r.movedOnly = false;
    }
    return r;
}

// The name of a change of records: "Add %d units" (one: "Add 1 unit"), "Delete", "Move" or "Change".
Editor::StepName recordName(const RecordChange& r, const char* one, const char* many) {
    const int n = r.after > r.before ? r.after - r.before : r.before > r.after ? r.before - r.after : r.changed;
    const char* verb = r.after > r.before ? "Add" : r.before > r.after ? "Delete" : r.movedOnly ? "Move" : "Change";
    if (n == 1) return {std::string(verb) + " 1 " + one, -1};
    return {std::string(verb) + " %d " + many, std::max(n, 0)};
}

// Locations (MRGN, 20 bytes each): one is in use when its edges or its name are set.
int locationsUsed(const Bytes* b) {
    int n = 0;
    for (size_t i = 0; b && i + 20 <= b->size(); i += 20)
        if (std::any_of(b->begin() + long(i), b->begin() + long(i + 18), [](uint8_t v) { return v != 0; })) ++n;
    return n;
}

int differentEntries(const Bytes* a, const Bytes* b, size_t size) {
    const size_t na = a ? a->size() / size : 0, nb = b ? b->size() / size : 0;
    int n = int(std::max(na, nb) - std::min(na, nb));
    for (size_t i = 0; i < std::min(na, nb); ++i)
        if (std::memcmp(a->data() + i * size, b->data() + i * size, size) != 0) ++n;
    return n;
}

Editor::StepName nameOf(const Parts& before, const Parts& after, bool files) {
    auto changed = [&](SectionName n) { return !sameData(partOf(before, n), partOf(after, n)); };
    auto anyChanged = [&](std::initializer_list<SectionName> names) {
        return std::any_of(names.begin(), names.end(), [&](SectionName n) { return changed(n); });
    };
    if (changed("DIM ")) return {"Change the map size", -1};
    if (changed("ERA ")) return {"Change the tileset", -1};
    const auto tiles = [&] {
        const Bytes* a = partOf(before, "MTXM");
        const Bytes* b = partOf(after, "MTXM");
        return differentEntries(a, b, 2);
    };
    if (changed("ISOM")) return {"Change the terrain (%d tiles)", tiles()};
    if (changed("DD2 ")) {
        RecordChange r = records(partOf(before, "DD2 "), partOf(after, "DD2 "), 8, 2);
        return recordName(r, "doodad", "doodads");
    }
    if (anyChanged({"MTXM", "TILE"})) return {"Change the terrain (%d tiles)", tiles()};
    if (changed("THG2")) return recordName(records(partOf(before, "THG2"), partOf(after, "THG2"), 10, 2), "sprite", "sprites");
    if (changed("UNIT")) return recordName(records(partOf(before, "UNIT"), partOf(after, "UNIT"), 36, 4), "unit", "units");
    if (changed("MRGN")) {
        const Bytes* a = partOf(before, "MRGN");
        const Bytes* b = partOf(after, "MRGN");
        const int ua = locationsUsed(a), ub = locationsUsed(b);
        RecordChange r{ua, ub, differentEntries(a, b, 20), false};
        return recordName(r, "location", "locations");
    }
    if (changed("TRIG")) return {"Change the triggers", -1};
    if (changed("MBRF")) return {"Change the briefings", -1};
    if (anyChanged({"OWNR", "IOWN", "SIDE", "FORC", "COLR", "CRGB"})) return {"Change the players and forces", -1};
    if (anyChanged({"UNIS", "UNIx", "PUNI"})) return {"Change the unit settings", -1};
    if (anyChanged({"UPGS", "UPGx", "UPGR", "PUPx"})) return {"Change the upgrade settings", -1};
    if (anyChanged({"TECS", "TECx", "PTEC", "PTEx"})) return {"Change the technology settings", -1};
    if (anyChanged({"UPRP", "UPUS"})) return {"Change the unit properties of the triggers", -1};
    if (changed("SWNM")) return {"Change the switch names", -1};
    if (changed("WAV ") || files) return {"Change the sound files", -1};
    if (changed("SPRP")) return {"Change the scenario name and description", -1};
    if (changed("MASK")) return {"Change the fog of war", -1};
    if (anyChanged({"STR ", "STRx"})) return {"Change the texts", -1};
    return {"Change the map", -1};
}

// A step that changed only the protected tiles (Editor::protectTiles): "Protect the terrain (%d tiles)" or "Unprotect
// the terrain (%d tiles)"; else `name`.
Editor::StepName protectName(Editor::StepName name, const std::vector<uint8_t>* a, const std::vector<uint8_t>* b) {
    static const std::vector<uint8_t> none;
    const std::vector<uint8_t>& x = a ? *a : none;
    const std::vector<uint8_t>& y = b ? *b : none;
    if (name.text != "Change the map" || x == y) return name;
    int on = 0, off = 0;
    for (size_t i = 0; i < std::max(x.size(), y.size()); ++i) {
        const bool p = i < x.size() && x[i], q = i < y.size() && y[i];
        on += !p && q, off += p && !q;
    }
    return on >= off ? Editor::StepName{"Protect the terrain (%d tiles)", on} : Editor::StepName{"Unprotect the terrain (%d tiles)", off};
}

}  // namespace

// The sections of a snapshot, and of the map now.
static Parts partsOfChk(const Chk& chk) {
    Parts p;
    for (const Section& s : chk.sections) p.emplace_back(s.name, &s.data);
    return p;
}

Editor::StepName Editor::undoStepName(size_t k) const {
    const size_t n = undo_.size();
    if (k >= n) return {};
    auto parts = [](const Snapshot& s) {
        Parts p;
        for (const auto& part : s.parts) p.emplace_back(part.name, part.data.get());
        return p;
    };
    const Snapshot& from = undo_[n - 1 - k];
    return protectName(nameOf(parts(from), k == 0 ? partsOfChk(chk_) : parts(undo_[n - k]), !from.files.empty()), from.protect.get(),
                       k == 0 ? &protected_ : undo_[n - k].protect.get());
}

Editor::StepName Editor::redoStepName(size_t k) const {
    const size_t m = redo_.size();
    if (k >= m) return {};
    auto parts = [](const Snapshot& s) {
        Parts p;
        for (const auto& part : s.parts) p.emplace_back(part.name, part.data.get());
        return p;
    };
    const Snapshot& to = redo_[m - 1 - k];
    return protectName(nameOf(k == 0 ? partsOfChk(chk_) : parts(redo_[m - k]), parts(to), !to.files.empty()),
                       k == 0 ? &protected_ : redo_[m - k].protect.get(), to.protect.get());
}

}  // namespace stardraft
