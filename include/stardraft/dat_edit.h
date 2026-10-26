#pragma once

// .dat editing for EUD projects: the field layout of each .dat (generated data,
// src/data/dat_schema.inc), a table of values (the game's stock file plus the project's edits),
// and the code that puts edits into a map at build time (masked memory writes in epScript).

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "stardraft/chk.h"

namespace stardraft {

class GameData;

struct DatField {
    int index = 0;            // position in the .dat's field list
    std::string name;
    int size = 1;             // bytes per value: 1, 2 or 4
    int first = 0, last = 0;  // entries the field covers
    int group = 1, groupIndex = 1;  // interleaved fields: group members stored side by side per entry
    uint32_t fileOffset = 0;  // of entry `first`
    uint32_t memory = 0;      // address of entry `first` in StarCraft's memory (1.16.1 layout, which
                              // euddraft maps onto Remastered)
    // A field in more than one array in memory (portdata.dat: idle portraits, then talking portraits): from
    // entry `first` of a run on, the values are at its address.
    struct MemoryRun {
        int first = 0;
        uint32_t address = 0;
    };
    std::vector<MemoryRun> moreMemory;
    std::string ref;          // the .dat this value is an index into ("" if none)
    std::vector<std::string> flags;      // bit names, when the value is a set of flags
    std::vector<std::string> valueNames; // names of the values, when it is a choice
    int section = 0, order = 0;          // where the editor shows it

    bool covers(int entry) const { return entry >= first && entry <= last; }
    uint32_t stride() const { return uint32_t(size * group); }
    uint32_t maxValue() const { return size == 4 ? 0xFFFFFFFFu : (1u << (8 * size)) - 1; }
    // Where an entry's value is in the game's memory (0: unknown).
    uint32_t address(int entry) const {
        if (!memory) return 0;
        int from = first;
        uint32_t at = memory;
        for (const MemoryRun& r : moreMemory)
            if (entry >= r.first) from = r.first, at = r.address;
        return at + uint32_t(entry - from) * stride();
    }
    // The memory ranges [begin, end) the field's values take.
    std::vector<std::pair<uint32_t, uint32_t>> memoryRanges() const;
};

struct DatSchema {
    std::string name;  // "units"
    int entries = 0;
    uint32_t fileSize = 0;
    std::vector<std::string> sections;  // section names, in order
    std::vector<DatField> fields;       // in file order
    // nullptr if unknown.
    static const DatSchema* find(const std::string& name);
    static const std::vector<DatSchema>& all();
    // A readable name for each entry ("Gauss Rifle (Normal)", "Die", "Scourge Shadow"); empty for units,
    // which take theirs from stat_txt.tbl. "icons" names the command icons (cmdicons.grp frames).
    static const std::vector<std::string>& entryNames(const std::string& dat);
};

class DatTable {
public:
    // The stock file from the game (arr\<name>.dat).
    bool load(const GameData& data, const DatSchema& schema, std::string& error);
    const DatSchema& schema() const { return *schema_; }
    // A table of a schema with no fields and no values (the editor's Strings tab in its Table view: a row for each
    // string, its text the one column).
    void setSchemaOnly(const DatSchema& schema) { schema_ = &schema, stock_.clear(), data_.clear(); }

    uint32_t value(const DatField& f, int entry) const { return read(data_, f, entry); }
    uint32_t stock(const DatField& f, int entry) const { return read(stock_, f, entry); }
    void set(const DatField& f, int entry, uint32_t v);
    bool changed(const DatField& f, int entry) const { return value(f, entry) != stock(f, entry); }
    bool entryChanged(int entry) const;
    int changedCount() const;  // values that differ from stock
    void reset(const DatField& f, int entry) { set(f, entry, stock(f, entry)); }
    // The whole table, for undo: read it, and put an earlier copy back.
    const Bytes& bytes() const { return data_; }
    void setBytes(const Bytes& b) { if (b.size() == data_.size()) data_ = b; }

    // The project's edits: a complete .dat file (written only when something differs from stock;
    // removed when nothing does). Loading a missing file is fine: no edits. A file that does not load is not
    // saved over or removed: saveEdits refuses until a load succeeds.
    bool loadEdits(const std::filesystem::path& file, std::string& error);
    bool saveEdits(const std::filesystem::path& file, std::string& error) const;
    bool editsFailed() const { return editsFailed_; }  // the last loadEdits did not load the file

    // epScript: one masked SetMemoryX per dword holding changed bytes, run once at map start. sfxdata.dat Sound
    // File: a pointer to a copy of the file name (see soundPreloaded).
    std::string editsScript() const;
    // One message per changed sfxdata.dat Sound File that editsScript leaves out (a file past the end of
    // sfxdata.tbl, or a Preload sound).
    std::vector<std::string> unappliedSoundFiles() const;

    const DatField* field(const std::string& name) const;
    // A value the game keeps as a pointer to a string, not as the .dat's index (sfxdata.dat Sound File).
    bool pointerField(const DatField& f) const;
    // sfxdata.dat: the game loads the sound before the map's code runs (the Preload flag, as the game has it), so a
    // change to its Sound File has no effect.
    bool soundPreloaded(int entry) const;
    // sfxdata.tbl's strings (sfxdata only): Sound File n is fileNames()[n - 1].
    const std::vector<std::string>& fileNames() const { return names_; }

private:
    static uint32_t read(const Bytes& b, const DatField& f, int entry);
    const DatSchema* schema_ = nullptr;
    Bytes stock_, data_;
    std::vector<std::string> names_;
    bool editsFailed_ = false;  // the project's file did not load
};

}  // namespace stardraft
