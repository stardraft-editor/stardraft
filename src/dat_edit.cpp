#include "stardraft/dat_edit.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>

#include "file_write.h"
#include "tbl.h"
#include "stardraft/game_data.h"

namespace stardraft {

namespace {

struct DatFieldDef {
    const char* dat;
    int index;
    const char* name;
    int size, first, last, group, groupIndex;
    uint32_t fileOffset, memory;
    const char* ref;
    const char* flags;   // '|'-separated
    const char* values;  // '|'-separated
    int section, order;
};
struct DatFileDef {
    const char* name;
    int entries;
    uint32_t size;
    const char* sections;  // '|'-separated
};
struct DatMemoryRunDef {
    const char* dat;
    int index, first;
    uint32_t address;
};
#include "data/dat_schema.inc"

std::vector<std::string> split(const char* s) {
    std::vector<std::string> out;
    std::string cur;
    for (; s && *s; ++s) {
        if (*s == '|') out.push_back(cur), cur.clear();
        else cur += *s;
    }
    if (!cur.empty() || !out.empty()) out.push_back(cur);
    return out;
}

std::vector<DatSchema> buildSchemas() {
    std::vector<DatSchema> out;
    for (const auto& f : kDatFiles) {
        DatSchema s;
        s.name = f.name, s.entries = f.entries, s.fileSize = f.size, s.sections = split(f.sections);
        for (const auto& d : kDatFields) {
            if (s.name != d.dat) continue;
            DatField x;
            x.index = d.index, x.name = d.name, x.size = d.size, x.first = d.first, x.last = d.last;
            x.group = d.group, x.groupIndex = d.groupIndex, x.fileOffset = d.fileOffset, x.memory = d.memory;
            x.ref = d.ref, x.flags = split(d.flags), x.valueNames = split(d.values), x.section = d.section, x.order = d.order;
            for (const auto& r : kDatMemoryRuns)
                if (s.name == r.dat && x.index == r.index) x.moreMemory.push_back({r.first, r.address});
            s.fields.push_back(std::move(x));
        }
        out.push_back(std::move(s));
    }
    return out;
}

}  // namespace

#include "data/dat_names.inc"

const std::vector<std::string>& DatSchema::entryNames(const std::string& dat) {
    static const std::map<std::string, std::vector<std::string>> names = [] {
        std::map<std::string, std::vector<std::string>> m;
        for (const auto& d : kDatNames) m[d.dat].assign(d.names, d.names + d.count);
        return m;
    }();
    static const std::vector<std::string> none;
    auto it = names.find(dat);
    return it != names.end() ? it->second : none;
}

const std::vector<DatSchema>& DatSchema::all() {
    static const std::vector<DatSchema> schemas = buildSchemas();
    return schemas;
}

const DatSchema* DatSchema::find(const std::string& name) {
    for (const auto& s : all())
        if (s.name == name) return &s;
    return nullptr;
}

uint32_t DatTable::read(const Bytes& b, const DatField& f, int entry) {
    if (!f.covers(entry)) return 0;
    const size_t o = f.fileOffset + size_t(entry - f.first) * f.stride();
    uint32_t v = 0;
    for (int k = 0; k < f.size && o + size_t(k) < b.size(); ++k) v |= uint32_t(b[o + size_t(k)]) << (8 * k);
    return v;
}

bool DatTable::load(const GameData& data, const DatSchema& schema, std::string& error) {
    schema_ = &schema;
    if (!data.readFile("arr\\" + schema.name + ".dat", stock_, error)) return false;
    if (stock_.size() != schema.fileSize) {
        error = "arr\\" + schema.name + ".dat is " + std::to_string(stock_.size()) + " bytes; expected " + std::to_string(schema.fileSize);
        return false;
    }
    data_ = stock_;
    // sfxdata.dat Sound File is an index into sfxdata.tbl in the file, and a pointer to the string in the game: the
    // build needs the strings.
    names_.clear();
    if (schema.name == "sfxdata") {
        Bytes tbl;
        if (!data.readFile("arr\\sfxdata.tbl", tbl, error)) return false;
        TblStrings parsed = parseTbl(tbl);
        if (parsed.bad) {
            error = tblDamaged("arr\\sfxdata.tbl", parsed.bad);
            return false;
        }
        names_ = std::move(parsed.strings);
    }
    return true;
}

const DatField* DatTable::field(const std::string& name) const {
    for (const auto& f : schema_->fields)
        if (f.name == name) return &f;
    return nullptr;
}

bool DatTable::soundPreloaded(int entry) const {
    if (schema_->name != "sfxdata") return false;
    const DatField* flags = field("Flags");
    return flags && (stock(*flags, entry) & 1);
}

void DatTable::set(const DatField& f, int entry, uint32_t v) {
    if (!f.covers(entry)) return;
    const size_t o = f.fileOffset + size_t(entry - f.first) * f.stride();
    for (int k = 0; k < f.size && o + size_t(k) < data_.size(); ++k) data_[o + size_t(k)] = uint8_t(v >> (8 * k));
}

bool DatTable::entryChanged(int entry) const {
    for (const auto& f : schema_->fields)
        if (f.covers(entry) && changed(f, entry)) return true;
    return false;
}

int DatTable::changedCount() const {
    int n = 0;
    for (const auto& f : schema_->fields)
        for (int e = f.first; e <= f.last; ++e) n += changed(f, e);
    return n;
}

bool DatTable::loadEdits(const std::filesystem::path& file, std::string& error) {
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) {
        data_ = stock_;
        return true;
    }
    std::ifstream in(file, std::ios::binary);
    Bytes b((std::istreambuf_iterator<char>(in)), {});
    if (b.size() != stock_.size()) {
        error = file.string() + " is " + std::to_string(b.size()) + " bytes; a " + schema_->name + ".dat is " + std::to_string(stock_.size());
        return false;
    }
    data_ = std::move(b);
    return true;
}

bool DatTable::saveEdits(const std::filesystem::path& file, std::string& error) const {
    std::error_code ec;
    if (data_ == stock_) {
        std::filesystem::remove(file, ec);  // no edits: no file
        return true;
    }
    std::filesystem::create_directories(file.parent_path(), ec);
    return writeFile(file, std::string_view(reinterpret_cast<const char*>(data_.data()), data_.size()), error);
}

std::vector<std::pair<uint32_t, uint32_t>> DatField::memoryRanges() const {
    std::vector<std::pair<uint32_t, uint32_t>> r;
    if (!memory) return r;
    int from = first;
    uint32_t at = memory;
    for (const MemoryRun& run : moreMemory) {
        r.push_back({at, at + uint32_t(run.first - from) * stride()});
        from = run.first, at = run.address;
    }
    r.push_back({at, at + uint32_t(last - from + 1) * stride()});
    return r;
}

bool DatTable::pointerField(const DatField& f) const { return schema_->name == "sfxdata" && f.name == "Sound File"; }

std::string DatTable::editsScript() const {
    // Changed bytes by memory address, then one write per dword: SetMemoryX with a mask touches only
    // the changed bytes, so neighboring values (other entries, other fields) are left alone.
    std::map<uint32_t, std::pair<uint32_t, uint32_t>> dwords;  // address -> (value, mask)
    // sfxdata.dat Sound File: the game keeps a pointer to the file name (sfxdata.tbl, "sound\" not included). A
    // changed one points to a copy of the name in the map, or to the name of sound 0 for 0 (no file). The game loads
    // a sound when it first plays it, so the map's code changes the pointer in time; it loads sounds with the Preload
    // flag before that, so those keep their file.
    std::map<uint32_t, uint32_t> pointers;  // address -> file (sfxdata.tbl index, 1-based; 0: no file)
    for (const auto& f : schema_->fields) {
        if (!f.memory) continue;
        for (int e = f.first; e <= f.last; ++e) {
            if (!changed(f, e)) continue;
            const uint32_t addr = f.address(e), v = value(f, e);
            if (pointerField(f)) {
                if (!soundPreloaded(e) && v <= names_.size()) pointers[addr] = v;
                continue;
            }
            for (int k = 0; k < f.size; ++k) {
                const uint32_t a = addr + uint32_t(k), shift = 8 * (a & 3);
                auto& [val, mask] = dwords[a & ~3u];
                val |= ((v >> (8 * k)) & 0xFF) << shift;
                mask |= 0xFFu << shift;
            }
        }
    }
    std::ostringstream o;
    o << "// Generated by StarDraft from the project's " << schema_->name << ".dat edits. Do not edit: it is rewritten on\n"
         "// every compile.\n\n";
    char line[128];
    std::set<uint32_t> files;
    for (const auto& [addr, file] : pointers) files.insert(file);
    if (files.size() > files.count(0))
        o << "// Sound file names (sfxdata.tbl), each a new array. In epScript an EUDArray is its EPD number: its address is\n"
             "// 0x58A364 + 4 x EPD.\n";
    for (uint32_t file : files) {
        if (!file) continue;
        std::string name = names_[file - 1];
        name.push_back('\0');
        while (name.size() % 4) name.push_back('\0');
        o << "const sfxFile" << file << " = EUDArray(list(";
        for (size_t a = 0; a < name.size(); a += 4) {
            const auto b = [&](size_t k) { return uint32_t(uint8_t(name[a + k])); };
            std::snprintf(line, sizeof line, "%s0x%08X", a ? ", " : "", unsigned(b(0) | b(1) << 8 | b(2) << 16 | b(3) << 24));
            o << line;
        }
        o << "));  // " << names_[file - 1] << "\n";
    }
    if (files.size() > files.count(0)) o << "\n";
    o << "function onPluginStart() {\n";
    if (files.count(0))  // read first: sound 0 can be one of the changed ones
        if (const DatField* soundFile = field("Sound File"))
            std::snprintf(line, sizeof line, "    const sfxNoFile = dwread(0x%06X);\n", unsigned(soundFile->address(0))), o << line;
    for (const auto& [addr, vm] : dwords) {
        if (vm.second == 0xFFFFFFFFu) std::snprintf(line, sizeof line, "    SetMemory(0x%06X, SetTo, 0x%08X);\n", addr, vm.first);
        else std::snprintf(line, sizeof line, "    SetMemoryX(0x%06X, SetTo, 0x%08X, 0x%08X);\n", addr, vm.first, vm.second);
        o << line;
    }
    for (const auto& [addr, file] : pointers) {
        if (file) std::snprintf(line, sizeof line, "    SetMemory(0x%06X, SetTo, 0x58A364 + 4 * sfxFile%u);\n", addr, unsigned(file));
        else std::snprintf(line, sizeof line, "    SetMemory(0x%06X, SetTo, sfxNoFile);\n", addr);
        o << line;
    }
    o << "}\n";
    return o.str();
}

std::vector<std::string> DatTable::unappliedSoundFiles() const {
    // The same condition as the Sound File case in editsScript.
    std::vector<std::string> out;
    for (const auto& f : schema_->fields) {
        if (!f.memory || !pointerField(f)) continue;
        for (int e = f.first; e <= f.last; ++e) {
            if (!changed(f, e)) continue;
            const uint32_t v = value(f, e);
            const std::string sound = "Sound " + std::to_string(e) + " is not changed: ";
            if (soundPreloaded(e)) out.push_back(sound + "the game loads it before the map's code runs (Preload).");
            else if (v > names_.size())
                out.push_back(sound + "file " + std::to_string(v) + " is past the " + std::to_string(names_.size()) +
                              " names in sfxdata.tbl.");
        }
    }
    return out;
}

}  // namespace stardraft
