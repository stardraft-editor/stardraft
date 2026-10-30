#include "stardraft/mods/cmbw.h"

#include <StormLib.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <tuple>
#include <type_traits>

#include "byteio.h"
#include "stardraft/classic_settings.h"
#include "stardraft/game_data.h"
#include "stardraft/isom.h"
#include "stardraft/isom_core.h"
#include "stardraft/map_file.h"
#include "stardraft/mods/terrain_rules.h"
#include "stardraft/sprites.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"
#include "stardraft/units.h"
#include "tbl.h"

namespace stardraft::cmbw {

namespace fs = std::filesystem;

namespace {

// --- Files: the loose mpq folder over the 1.16.1 MPQs ---------------------------------------------------------------

std::basic_string<TCHAR> nativePath(const fs::path& p) {
    static_assert(std::is_same_v<TCHAR, fs::path::value_type>, "build the library with UNICODE on Windows");
    return p.native();
}

// The 1.16.1 name of a file: backslashes, and without Remastered's "SD\" folder (its SD files are the 1.16.1 ones).
std::string mpqName(std::string name) {
    std::replace(name.begin(), name.end(), '/', '\\');
    if (name.size() > 3 && (name[0] == 'S' || name[0] == 's') && (name[1] == 'D' || name[1] == 'd') && name[2] == '\\') name.erase(0, 3);
    return name;
}

class LayeredFiles final : public FileSource {
public:
    LayeredFiles(fs::path loose, const std::vector<fs::path>& mpqs) : loose_(std::move(loose)) {
        for (const fs::path& p : mpqs) {
            HANDLE h = nullptr;
            if (SFileOpenArchive(nativePath(p).c_str(), 0, MPQ_OPEN_READ_ONLY, &h)) archives_.push_back(h);
        }
    }
    ~LayeredFiles() override {
        for (HANDLE h : archives_) SFileCloseArchive(h);
    }
    size_t archives() const { return archives_.size(); }
    // A file that is in neither (`name` as readFile gets it, after mpqName).
    void addFile(const std::string& name, Bytes data) { extra_[lower(mpqName(name))] = std::move(data); }

    bool readFile(const std::string& name, Bytes& out, std::string& error) const override {
        const std::string n = mpqName(name);
        {
            std::ifstream in(loose_ / fs::path(n), std::ios::binary);
            if (in) {
                out.assign(std::istreambuf_iterator<char>(in), {});
                return true;
            }
        }
        if (auto it = extra_.find(lower(n)); it != extra_.end()) return out = it->second, true;
        std::lock_guard lock(mutex_);  // StormLib reads of one archive one at a time
        for (HANDLE a : archives_) {
            HANDLE f = nullptr;
            if (!SFileOpenFileEx(a, n.c_str(), SFILE_OPEN_FROM_MPQ, &f)) continue;
            DWORD high = 0;
            const DWORD size = SFileGetFileSize(f, &high);
            out.resize(size);
            DWORD read = 0;
            const bool ok = size == SFILE_INVALID_SIZE ? false : (size == 0 || (SFileReadFile(f, out.data(), size, &read, nullptr) && read == size));
            SFileCloseFile(f);
            if (ok) return true;
        }
        error = "file not found: " + name;
        return false;
    }

    bool hasFile(const std::string& name, uint64_t* size) const override {
        const std::string n = mpqName(name);
        std::error_code ec;
        const fs::path p = loose_ / fs::path(n);
        if (fs::is_regular_file(p, ec)) {
            if (size) *size = fs::file_size(p, ec);
            return true;
        }
        if (auto it = extra_.find(lower(n)); it != extra_.end()) {
            if (size) *size = it->second.size();
            return true;
        }
        std::lock_guard lock(mutex_);
        for (HANDLE a : archives_) {
            HANDLE f = nullptr;
            if (!SFileOpenFileEx(a, n.c_str(), SFILE_OPEN_FROM_MPQ, &f)) continue;
            DWORD high = 0;
            if (size) *size = SFileGetFileSize(f, &high);
            SFileCloseFile(f);
            return true;
        }
        return false;
    }

    std::vector<std::string> listFiles() const override {
        std::set<std::string> names;
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(loose_, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
            if (it->is_regular_file(ec)) {
                const std::u8string r = it->path().lexically_relative(loose_).u8string();  // (fs::relative asks the disk: slow)
                names.insert(mpqName(std::string(r.begin(), r.end())));
            }
        std::lock_guard lock(mutex_);
        for (HANDLE a : archives_) {
            SFILE_FIND_DATA fd;
            HANDLE find = SFileFindFirstFile(a, "*", &fd, nullptr);
            if (!find) continue;
            do names.insert(fd.cFileName);
            while (SFileFindNextFile(find, &fd));
            SFileFindClose(find);
        }
        return {names.begin(), names.end()};
    }

private:
    static std::string lower(std::string s) {
        for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }
    fs::path loose_;
    std::map<std::string, Bytes> extra_;
    std::vector<HANDLE> archives_;  // the first one first
    mutable std::mutex mutex_;
};

// --- .datex tables --------------------------------------------------------------------------------------------------

// A .datex file: its fields in order, each `size` bytes for each of `count` entries.
class Datex {
public:
    Datex() = default;
    Datex(Bytes bytes, std::initializer_list<int> fieldSizes) : bytes_(std::move(bytes)) {
        int entry = 0;
        for (int s : fieldSizes) entry += s;
        count_ = entry ? bytes_.size() / size_t(entry) : 0;
        if (count_ * size_t(entry) != bytes_.size()) count_ = 0;  // not this layout
        size_t at = 0;
        for (int s : fieldSizes) fields_.push_back({at, s}), at += size_t(s) * count_;
    }
    size_t count() const { return count_; }
    // Field `field` (its place in the list) of entry `i`; `part` for a field of more values (a box: 2 or 4 u16).
    uint32_t get(size_t field, size_t i, int part = 0, int partSize = 0) const {
        const auto [at, size] = fields_[field];
        const int s = partSize ? partSize : size;
        const size_t o = at + i * size_t(size) + size_t(part * s);
        return s == 1 ? bytes_[o] : s == 2 ? readU16(bytes_, o) : readU32(bytes_, o);
    }
    template <class T>
    std::vector<T> column(size_t field) const {
        std::vector<T> out(count_);
        for (size_t i = 0; i < count_; ++i) out[i] = T(get(field, i));
        return out;
    }

private:
    Bytes bytes_;
    size_t count_ = 0;
    std::vector<std::pair<size_t, int>> fields_;
};

bool readDatex(const GameData& data, const std::string& name, std::initializer_list<int> fields, Datex& out, std::string& error) {
    Bytes b;
    if (!data.readFile("arr\\" + name + ".datex", b, error)) return false;
    out = Datex(std::move(b), fields);
    if (!out.count()) return error = "arr\\" + name + ".datex has an unexpected size", false;
    return true;
}

// units.datex: the fields of units.dat, with graphics and the two weapons 2 bytes wide.
enum UnitField : size_t {
    UGraphics, USubunit1, USubunit2, UInfestation, UConstruction, UDirection, UShieldEnable, UShields, UHitPoints,
    UElevation, UUnknown, URank, UCompIdle, UHumanIdle, UReturnIdle, UAttackUnit, UAttackMove, UGroundWeapon,
    UMaxGroundHits, UAirWeapon, UMaxAirHits, UAiInternal, UFlags, UTargetRange, USight, UArmorUpgrade, USize, UArmor,
    URightClick, UReady, UWhatStart, UWhatEnd, UPissStart, UPissEnd, UYesStart, UYesEnd, UPlacement, UAddonPos,
    UDimensions, UPortrait, UMinerals, UGas, UTime, URequirements, UGroupFlags, USupplyGiven, USupplyUsed,
    USpaceNeeded, USpaceGiven, UBuildScore, UDestroyScore, UMapString, UBroodWar, UAvailability,
};
const std::initializer_list<int> kUnitFields = {2, 2, 2, 2, 4, 1, 1, 2, 4, 1, 1, 1, 1, 1, 1, 1, 1, 2, 1, 2, 1, 1, 4, 1, 1, 1, 1,
                                                1, 1, 2, 2, 2, 2, 2, 2, 2, 4, 4, 8, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 2, 2, 2, 1, 2};
// images.datex: as images.dat.
enum ImageField : size_t { IGrp, ITurns, IClickable, IFullIscript, IDrawIfCloaked, IDrawFunction, IRemap, IIscript,
                           IShield, IAttack, IDamage, ISpecial, ILanding, ILiftoff };
const std::initializer_list<int> kImageFields = {4, 1, 1, 1, 1, 1, 1, 4, 4, 4, 4, 4, 4, 4};
// sprites.datex: image, health bar, unknown, visible, selection circle, its vertical offset (all sprites).
const std::initializer_list<int> kSpriteFields = {2, 1, 1, 1, 1, 1};
// flingy.datex: as flingy.dat (sprite, top speed, acceleration, halt distance, turn radius, unused, move control).
const std::initializer_list<int> kFlingyFields = {2, 4, 2, 4, 1, 1, 1};

// A .tbl string's two parts after the first ("Terran Phalanx\0Tank Mode\0Ground Units\0"), "*" as empty.
std::pair<std::string, std::string> tblParts(const Bytes& tbl, size_t index, const std::string& first) {
    if (2 + 2 * index + 1 >= tbl.size()) return {};
    size_t o = std::min<size_t>(readU16(tbl, 2 + 2 * index) + first.size() + 1, tbl.size());
    auto next = [&]() {
        size_t e = o;
        while (e < tbl.size() && tbl[e]) ++e;
        std::string part(tbl.begin() + long(o), tbl.begin() + long(e));
        o = std::min(e + 1, tbl.size());
        return part == "*" ? std::string() : part;
    };
    std::string a = next();
    return {a, next()};
}

// The units' tech tiers by id (tools/plugins/cmbw/cmbw_units.py, from the website's export: the install has no tiers).
struct UnitTier {
    uint16_t id;
    uint8_t tier;
};
const UnitTier kUnitTiers[] = {
#include "cmbw_unit_tiers.inc"
};

std::string lowerText(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The parts of a unit's subtitle, trimmed ("Adrenaline, Upstart": Adrenaline and Upstart).
std::vector<std::string> subtitleParts(const std::string& subtitle) {
    std::vector<std::string> out;
    for (size_t at = 0; at <= subtitle.size();) {
        const size_t end = std::min(subtitle.find(',', at), subtitle.size());
        std::string part = subtitle.substr(at, end - at);
        while (!part.empty() && part.front() == ' ') part.erase(part.begin());
        while (!part.empty() && part.back() == ' ') part.pop_back();
        if (!part.empty()) out.push_back(part);
        at = end + 1;
    }
    return out;
}

// Each named unit's palette section and order, from the install's files only (unitnames.tbl: the name, part 2 the
// faction or a mode ("*": the default faction), part 3 the group; units.datex: the flags and the subunits):
// - The race: the name's first word when 3 or more units of the main groups have it (Terran, Askosi), else by units.dat's
//   group flags, else Neutral. Terran, Zerg and Protoss first, then the others in the order of their first units.
// - "Terran · Ground units" (with a middle dot; Ground units, Air units, Buildings by units.dat's flags) for the main
//   groups (Ground Units, Air Units, Buildings, Structures, Addons, Factional Units).
// - "Terran - Golden Republic · Ground units" for a Factional Units unit with a faction in part 2: a value that 2 or more
//   Factional Units units of the race have (subunits not counted). A value of one unit is a mode (Tank Mode): that unit
//   is in its race's sections. The factions in the order of their first units.
// - The race's "Special units" for the other groups (Special, Heroes, Powerups); Neutral units by their flags only.
// - "Subunits": the units that units.dat gives other units as subunits (turrets), after the races.
// - "Unused slots" ("Reserved", "unused", the Unused group) last.
// In each section the units by tier (kUnitTiers; the ones with no tier after), then by id.
// Each race's units of the main groups (its factions' too) are also in its main category ("Terran units"), by kind
// (Ground units, Air units, Buildings), then tier.
void placeUnits(std::vector<UnitType>& types, const std::set<uint16_t>& subunits) {
    static const std::set<std::string> kMain = {"ground units", "air units", "buildings", "structures", "addons", "factional units", "faction units"};
    auto factional = [](const UnitType& t) {
        const std::string g = lowerText(t.editorGroup);
        return g == "factional units" || g == "faction units";
    };
    auto firstWord = [](const std::string& name) { return name.substr(0, name.find(' ')); };
    std::map<std::string, std::pair<int, uint16_t>> words;  // (a first word: its units, its first unit)
    for (const UnitType& t : types) {
        if (t.name.empty() || !kMain.count(lowerText(t.editorGroup))) continue;
        ++words.emplace(firstWord(t.name), std::pair<int, uint16_t>{0, t.id}).first->second.first;
    }
    std::vector<std::string> races = {"Terran", "Zerg", "Protoss"};
    std::vector<std::pair<uint16_t, std::string>> more;
    for (const auto& [w, n] : words)
        if (n.first >= 3 && std::find(races.begin(), races.end(), w) == races.end()) more.push_back({n.second, w});
    std::sort(more.begin(), more.end());
    for (const auto& m : more) races.push_back(m.second);
    auto raceOf = [&](const UnitType& t) {
        const std::string w = firstWord(t.name);
        if (std::find(races.begin(), races.end(), w) != races.end()) return w;
        return std::string((t.groupFlags & UnitGroup::Zerg) ? "Zerg" : (t.groupFlags & UnitGroup::Terran) ? "Terran"
                         : (t.groupFlags & UnitGroup::Protoss) ? "Protoss" : "Neutral");
    };
    std::map<std::pair<std::string, std::string>, std::pair<int, uint16_t>> values;  // (race, part 2 value: its units, its first unit)
    for (const UnitType& t : types)
        if (!t.name.empty() && factional(t) && !subunits.count(t.id))
            for (const std::string& v : subtitleParts(t.subtitle))
                ++values.emplace(std::pair{raceOf(t), v}, std::pair<int, uint16_t>{0, t.id}).first->second.first;
    std::map<std::string, std::vector<std::pair<uint16_t, std::string>>> factions;
    for (const auto& [key, n] : values)
        if (n.first >= 2) factions[key.first].push_back({n.second, key.second});
    for (auto& [race, list] : factions) std::sort(list.begin(), list.end());
    std::map<uint16_t, int> tiers;
    for (const UnitTier& u : kUnitTiers) tiers.emplace(u.id, u.tier);

    const std::string dot = "\xC2\xB7";  // (UTF-8 middle dot)
    auto kind = [](const UnitType& t) { return (t.flags & 0x1) ? 2 : (t.flags & 0x4) ? 1 : 0; };  // (building, flyer, ground)
    static const char* kKinds[] = {"Ground units", "Air units", "Buildings"};
    for (UnitType& t : types) {
        if (t.name.empty()) continue;
        const std::string n = lowerText(t.name), group = lowerText(t.editorGroup);
        if (n == "reserved" || n == "unused" || group == "unused") {  // (the mod's free slots: last, in one section)
            t.paletteSection = "Unused slots";
            t.paletteOrder = 900000000 + t.id;
            continue;
        }
        if (subunits.count(t.id)) {
            t.paletteSection = "Subunits";
            t.paletteOrder = 800000000 + t.id;
            continue;
        }
        if (auto it = tiers.find(t.id); it != tiers.end()) t.paletteTier = it->second;
        const int place = (t.paletteTier > 0 ? std::min(t.paletteTier, 98) : 99) * 10000 + t.id;  // (below 1000000)
        const std::string race = raceOf(t);
        const int r = std::min(int(std::find(races.begin(), races.end(), race) - races.begin()), 7);  // (Neutral: after the races)
        const int k = kind(t);
        if (race != "Neutral" && !kMain.count(group)) {
            t.paletteSection = race + " " + dot + " Special units";
            t.paletteOrder = r * 100000000 + (96 + k) * 1000000 + place;
            continue;
        }
        int f = 0;  // (the faction: its place in the race's list, from 1; 0 none)
        std::string faction;
        if (auto it = factions.find(race); it != factions.end() && factional(t))
            for (const std::string& v : subtitleParts(t.subtitle))
                for (size_t i = 0; i < it->second.size() && !f; ++i)
                    if (it->second[i].second == v) f = int(i) + 1, faction = v;
        f = std::min(f, 30);
        t.paletteSection = race + (f ? " - " + faction : std::string()) + " " + dot + " " + kKinds[k];
        t.paletteOrder = r * 100000000 + (f ? 3 * f + k : k) * 1000000 + place;
        if (race != "Neutral")  // (the race's main category: its units and its factions' by kind, then tier)
            t.paletteGroup = race + " units", t.paletteGroupSection = kKinds[k], t.paletteGroupOrder = k * 1000000 + place;
    }
}

bool loadTypes(const GameData& data, UnitTypes& out, std::string& error) {
    Datex d;
    if (!readDatex(data, "units", kUnitFields, d, error)) return false;
    Bytes names, stat;
    if (!data.readFile("rez\\unitnames.tbl", names, error) || !data.readFile("rez\\stat_txt.tbl", stat, error)) return false;
    const TblStrings n = parseTbl(names), s = parseTbl(stat);
    // arr\units.txt: a line for each id; "Extended Unit #N" is a free slot (no unit), whatever unitnames.tbl has there
    Bytes list;
    std::string listError;
    std::vector<bool> free(d.count());
    if (data.readFile("arr\\units.txt", list, listError))
        for (size_t at = 0, i = 0; at < list.size() && i < free.size(); ++i) {
            size_t e = at;
            while (e < list.size() && list[e] != '\n') ++e;
            free[i] = lowerText(std::string(list.begin() + long(at), list.begin() + long(e))).rfind("extended unit", 0) == 0;
            at = e + 1;
        }
    std::vector<UnitType> types(d.count());
    for (size_t i = 0; i < d.count(); ++i) {
        UnitType& t = types[i];
        t.id = uint16_t(i);
        if (i < n.strings.size() && !free[i]) {
            t.name = n.strings[i];
            std::tie(t.subtitle, t.editorGroup) = tblParts(names, i, t.name);
        }
        // (the mod's markers, as its Town Center Marker: they go on top of buildings; the data marks them no other way)
        t.blocksNothing = t.name.find("Marker") != std::string::npos;
        t.hasShields = d.get(UShieldEnable, i) != 0;
        t.shields = uint16_t(d.get(UShields, i));
        t.hitPoints = d.get(UHitPoints, i);
        t.elevation = uint8_t(d.get(UElevation, i));
        t.flags = d.get(UFlags, i);
        t.placementWidth = uint16_t(d.get(UPlacement, i, 0, 2)), t.placementHeight = uint16_t(d.get(UPlacement, i, 1, 2));
        t.sizeLeft = uint16_t(d.get(UDimensions, i, 0, 2)), t.sizeUp = uint16_t(d.get(UDimensions, i, 1, 2));
        t.sizeRight = uint16_t(d.get(UDimensions, i, 2, 2)), t.sizeDown = uint16_t(d.get(UDimensions, i, 3, 2));
        t.groupFlags = uint8_t(d.get(UGroupFlags, i));
        t.availability = uint16_t(d.get(UAvailability, i));
        t.attackUnitOrder = uint8_t(d.get(UAttackUnit, i));
        t.portrait = uint16_t(d.get(UPortrait, i));
        t.addonX = int16_t(d.get(UAddonPos, i, 0, 2)), t.addonY = int16_t(d.get(UAddonPos, i, 1, 2));
    }
    std::set<uint16_t> subunits;  // (the units that named units have as subunits; 228: none)
    for (size_t i = 0; i < d.count(); ++i)
        for (const size_t field : {USubunit1, USubunit2})
            if (const uint32_t v = d.get(field, i); !types[i].name.empty() && v != 228 && v < d.count()) subunits.insert(uint16_t(v));
    placeUnits(types, subunits);
    out.assign(std::move(types), s.strings);
    return true;
}

// The loose folder of the files that `open` gave `data` (empty: none).
fs::path gLoose;

// The mod's iscript files: scripts\iscript_*.bin in the loose folder, in the order whose index an images.datex iscript
// ID has in its high 16 bits (general 0, terran 1, protoss 2, zerg 3, askosi 4, bloodsummon 5: each image's ID against
// the scripts that iscript_<name>.txt says it uses); others after them, by name.
std::vector<std::string> iscriptFiles() {
    static const char* const kOrder[] = {"general", "terran", "protoss", "zerg", "askosi", "bloodsummon"};
    std::vector<std::string> out;
    std::error_code ec;
    for (auto it = fs::directory_iterator(gLoose / "scripts", ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        const std::u8string n = it->path().filename().u8string();
        std::string s(n.begin(), n.end());
        std::string l = s;
        std::transform(l.begin(), l.end(), l.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (l.rfind("iscript_", 0) == 0 && l.size() > 4 && l.compare(l.size() - 4, 4, ".bin") == 0) out.push_back("scripts\\" + s);
    }
    auto rank = [](const std::string& path) {
        std::string l = path;
        std::transform(l.begin(), l.end(), l.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        for (size_t i = 0; i < std::size(kOrder); ++i)
            if (l == std::string("scripts\\iscript_") + kOrder[i] + ".bin") return int(i);
        return int(std::size(kOrder));
    };
    std::sort(out.begin(), out.end(), [&](const std::string& a, const std::string& b) {
        const int ra = rank(a), rb = rank(b);
        return ra != rb ? ra < rb : a < b;
    });
    // (a file of the order that is not there keeps its place: the IDs after it point at the right files)
    std::vector<std::string> placed;
    for (size_t i = 0; i < std::size(kOrder); ++i) {
        const std::string want = std::string("scripts\\iscript_") + kOrder[i] + ".bin";
        auto it = std::find_if(out.begin(), out.end(), [&](const std::string& p) { return rank(p) == int(i); });
        placed.push_back(it != out.end() ? *it : want);
    }
    for (const std::string& p : out)
        if (rank(p) == int(std::size(kOrder))) placed.push_back(p);
    return placed;
}

bool loadSprites(const GameData& data, UnitSprites& out, std::string& error) {
    Datex units, flingy, sprites, images;
    if (!readDatex(data, "units", kUnitFields, units, error) || !readDatex(data, "flingy", kFlingyFields, flingy, error) ||
        !readDatex(data, "sprites", kSpriteFields, sprites, error) || !readDatex(data, "images", kImageFields, images, error))
        return false;
    UnitSprites::Tables t;
    t.unitFlingy = units.column<uint16_t>(UGraphics);
    t.unitSubunit = units.column<uint16_t>(USubunit1);
    t.unitDirection = units.column<uint8_t>(UDirection);
    t.flingySprite = flingy.column<uint16_t>(0);
    t.spriteImage = sprites.column<uint16_t>(0);
    t.spriteSelCircle = sprites.column<uint8_t>(4);
    t.spriteSelOffset = sprites.column<uint8_t>(5);
    t.imageGrp = images.column<uint32_t>(IGrp);
    t.imageTurns = images.column<uint8_t>(ITurns);
    t.imageDrawFn = images.column<uint8_t>(IDrawFunction);
    t.imageRemap = images.column<uint8_t>(IRemap);
    t.imageScriptId = images.column<int>(IIscript);
    const ImageField lo[6] = {IAttack, IDamage, ISpecial, ILanding, ILiftoff, IShield};
    for (int k = 0; k < 6; ++k) t.imageOverlays[k] = images.column<uint32_t>(lo[k]);
    // The mod's iscript files (scripts\iscript_<race>.bin, loose), then the game's for the scripts they do not have.
    t.iscriptFiles = iscriptFiles();
    t.iscriptFiles.push_back("scripts\\iscript.bin");
    t.scriptIdFile = true;
    return out.loadTables(data, std::move(t), error);
}

// weapons.datex, upgrades.datex, techdata.datex: as the .dat files.
enum WeaponField : size_t { WLabel, WGraphics, WUnused, WTargets, WMinRange, WMaxRange, WUpgrade, WDamageType, WBehavior,
                            WRemoveAfter, WExplosion, WInner, WMedium, WOuter, WDamage, WBonus };
const std::initializer_list<int> kWeaponFields = {2, 4, 1, 2, 4, 4, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 2, 2};
enum UpgradeField : size_t { GMinerals, GMineralsFactor, GGas, GGasFactor, GTime, GTimeFactor, GUnknown, GIcon, GLabel, GRace,
                             GMaxRepeats, GBroodWar };
const std::initializer_list<int> kUpgradeFields = {2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1};
enum TechField : size_t { TMinerals, TGas, TTime, TEnergy, TUnknown, TIcon, TLabel, TRace, TUnused, TBroodWar };
const std::initializer_list<int> kTechFields = {2, 2, 2, 2, 4, 2, 2, 1, 1, 1};

// The mod's names of a .datex file's entries: arr\<name>.txt, one line each (the labels that the mod's tools show).
std::vector<std::string> labelLines(const GameData& data, const std::string& name) {
    Bytes b;
    std::string err;
    std::vector<std::string> out;
    if (!data.readFile("arr\\" + name + ".txt", b, err)) return out;
    std::string line;
    for (uint8_t c : b) {
        if (c == '\n') out.push_back(std::move(line)), line.clear();
        else if (c != '\r') line += char(c);
    }
    if (!line.empty()) out.push_back(std::move(line));
    return out;
}

// The map's settings sections (UNIx, UPGx, TECx) hold the first 228 units, 130 weapons, 61 upgrades and 44 technologies:
// the game reads those entries of the mod's tables.
bool loadSettings(const GameData& data, SettingDefaults& out, std::string& error) {
    Datex u, w, g, t;
    if (!readDatex(data, "units", kUnitFields, u, error) || !readDatex(data, "weapons", kWeaponFields, w, error) ||
        !readDatex(data, "upgrades", kUpgradeFields, g, error) || !readDatex(data, "techdata", kTechFields, t, error))
        return false;
    if (u.count() < size_t(kSettingUnits) || w.count() < size_t(kSettingWeapons) || g.count() < size_t(kSettingUpgrades) ||
        t.count() < size_t(kSettingTechs))
        return error = "the .datex files have fewer entries than a map's settings", false;
    auto weapon = [](uint32_t id) { return uint8_t(id < uint32_t(kSettingWeapons) ? id : kSettingWeapons); };
    for (size_t i = 0; i < size_t(kSettingUnits); ++i) {
        UnitSettings::Unit& s = out.units.units[i];
        s = {true, u.get(UHitPoints, i), uint16_t(u.get(UShields, i)), uint8_t(u.get(UArmor, i)), uint16_t(u.get(UTime, i)),
             uint16_t(u.get(UMinerals, i)), uint16_t(u.get(UGas, i)), 0};
        UnitSettings::Availability& a = out.units.availability[i];
        a.global = true, a.player.fill(true), a.usesDefault.fill(true);
        const uint32_t sub = u.get(USubunit1, i);
        auto& ws = out.unitWeapons[i];
        ws = {weapon(u.get(UGroundWeapon, i)), weapon(u.get(UAirWeapon, i)), uint8_t(kSettingWeapons), uint8_t(kSettingWeapons)};
        if (sub < u.count()) ws[2] = weapon(u.get(UGroundWeapon, sub)), ws[3] = weapon(u.get(UAirWeapon, sub));
    }
    for (size_t i = 0; i < size_t(kSettingWeapons); ++i) out.units.weapons[i] = {uint16_t(w.get(WDamage, i)), uint16_t(w.get(WBonus, i))};
    for (size_t i = 0; i < size_t(kSettingUpgrades); ++i) {
        out.upgrades.upgrades[i] = {true, uint16_t(g.get(GMinerals, i)), uint16_t(g.get(GMineralsFactor, i)), uint16_t(g.get(GGas, i)),
                                    uint16_t(g.get(GGasFactor, i)), uint16_t(g.get(GTime, i)), uint16_t(g.get(GTimeFactor, i))};
        UpgradeSettings::Levels& l = out.upgrades.levels[i];
        l.defaultMax = uint8_t(g.get(GMaxRepeats, i)), l.defaultStart = 0;
        l.max.fill(l.defaultMax), l.start.fill(0), l.usesDefault.fill(true);
        out.upgradeIcon[i] = uint16_t(g.get(GIcon, i));
    }
    for (size_t i = 0; i < size_t(kSettingTechs); ++i) {
        TechSettings::Tech& s = out.techs.techs[i];
        s = {true, uint16_t(t.get(TMinerals, i)), uint16_t(t.get(TGas, i)), uint16_t(t.get(TTime, i)), uint16_t(t.get(TEnergy, i))};
        TechSettings::Players& p = out.techs.players[i];
        p.defaultAvailable = true, p.defaultResearched = s.minerals == 0 && s.gas == 0;
        p.available.fill(true), p.researched.fill(p.defaultResearched), p.usesDefault.fill(true);
        out.techIcon[i] = uint16_t(t.get(TIcon, i));
    }
    out.weaponNames = labelLines(data, "weapons");
    out.upgradeNames = labelLines(data, "upgrades");
    out.techNames = labelLines(data, "techdata");
    return true;
}

// The terrains of the tile groups the mod's tilesets add (no isometric data in their .cv5), learned from the mod's maps
// (tools/plugins/cmbw/probes/cmbw_isom_learn.cpp): the Tiles list's terrain filter and the terrain checks then know
// them.
struct GroupTerrain {
    uint8_t era;
    uint16_t group, terrain, other;
};
const GroupTerrain kGroupTerrains[] = {
#include "cmbw_isom.inc"
};

// The mod's own pieces (a left and a right tile the maps use for one of the game's rects), from the same maps. The mod
// packs them as subtiles: a group's subtiles 0 and 1 are a piece, 2 and 3 another (some use the next group's subtile).
struct GroupPiece {
    uint8_t era;
    uint16_t left, right, game;
};
const GroupPiece kGroupPieces[] = {
#include "cmbw_isom_pieces.inc"
};

// Which of the mod terrains' pieces stand beside each other on the mod's maps
// (tools/plugins/cmbw/probes/cmbw_neighbors.cpp --inc), packed: base-64 numbers of 5 bits a character (the probe's
// writeInc tells the form).
const char* const kPieceNeighbors[] = {
#include "cmbw_neighbors.inc"
};

// Each pair of pieces of tileset `era` in kPieceNeighbors: a piece's left tile, its neighbor's left tile and the place
// (Tileset::pieceNeighbors: 0 under it, 1 a rect to the right and a row down, 2 to the right and a row up).
template <class F>
void forEachPieceNeighbor(int era, F&& f) {
    size_t line = 0;
    const char* at = kPieceNeighbors[0];
    auto digit = [&]() -> int {
        while (!*at)
            if (++line < std::size(kPieceNeighbors)) at = kPieceNeighbors[line];
            else return -1;
        const char c = *at++;
        return c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' ? 62 : 63;
    };
    bool end = false;
    auto number = [&]() -> unsigned {
        unsigned v = 0;
        for (int shift = 0;; shift += 5) {
            const int d = digit();
            if (d < 0) return end = true, v;
            v |= unsigned(d & 31) << shift;
            if (!(d & 32)) return v;
        }
    };
    while (true) {
        const unsigned tileset = number(), pieces = number();
        if (end) return;
        unsigned a = 0;
        for (unsigned p = 0; p < pieces && !end; ++p) {
            a += number();
            unsigned count[3];
            for (unsigned& n : count) n = number();
            for (int place = 0; place < 3; ++place) {
                int b = int(a);
                for (unsigned k = 0; k < count[place] && !end; ++k) {
                    const unsigned z = number();
                    b += z & 1 ? -int((z + 1) / 2) : int(z / 2);
                    if (int(tileset) == era) f(uint16_t(a), uint16_t(b), place);
                }
            }
        }
    }
}

// The mod's own terrains in the isometric terrain list, from its brush sets (Maps\CMBW-Root\CMBW-editor\brush): a set
// made like one of the game's borders, block for block (each edge kind a brush of 2 or 3 rects from the top, its
// variants as subtile pairs of the block's groups), takes that border's stand-in terrain. The stand-in's solid rects
// get the set's fill: two groups of the set, or a game terrain's own solid tiles.
struct ModTerrainBlock {
    uint16_t mod;          // the block's top group (the next rows: the next groups)
    uint16_t game[3];      // the game rect (left group) of each row from the top (0: no row)
    uint16_t variants = 0;  // its subtile pairs (0: the set's)
    uint16_t sub = 0;       // its first subtile (two blocks may share groups: one in subtiles 0-7, the other in 8-15)
    int16_t lowerSub = -1;  // the first subtile of its rows below the top (-1: sub; a corner made of two blocks' rows)
    int16_t lowerMod = -1;  // the group of its second row (-1: mod + 1; a corner made of two strips' rows)
};
constexpr uint16_t kNullFill = 0xFFFF;  // (a ModTerrainSet's fillTerrain: its fill the null tile, group 0's subtiles 0 and 1)
struct ModTerrainSet {
    uint8_t era;
    const char* name;
    uint16_t standIn, ground;  // the game terrain whose shapes it takes, and the terrain it borders
    uint16_t variants;         // subtile pairs in each block
    std::vector<ModTerrainBlock> blocks;
    uint16_t fillLeft, fillRight;  // its fill (0: fillTerrain's solid tiles; both 0 and fillTerrain 0: no fill, the
    uint16_t fillTerrain;          // stand-in's own tiles inside: a set of edges only)
    uint16_t groundTemplate = 0;   // a terrain a level higher: the game terrain its ground is in the template (0: the ground)
    const char* groundMod = nullptr;  // its ground a mod terrain before it (by name; groundTemplate 0: that one's template)
    uint16_t fillSubtiles = 0x3F;     // the fill groups' subtiles it takes (bit k: subtile k)
};
// The sets made from the maps (tools/plugins/cmbw/probes/cmbw_auto_rows.cpp): each rect of a set's brushes as a piece
// for the game rect the maps' ISOM has under it most; its border's two terrains its stand-in and its ground (src/mods/
// cmbw_auto_terrains.inc). They come after the hand-made rows of their tileset (kModTerrainSets).
struct AutoPiece {
    uint16_t game, left, right;
};
struct AutoTerrain {
    uint8_t era;
    const char* name;
    uint16_t standIn, ground;
    std::vector<AutoPiece> pieces;
};
#define AUTO(era, name, standIn, ground, ...) {era, name, standIn, ground, __VA_ARGS__},
const std::vector<AutoTerrain> kAutoTerrains = {
#include "cmbw_auto_terrains.inc"
};
#undef AUTO

const ModTerrainSet kModTerrainSets[] = {
    // Ash World: Asphalt-1-dirt, made like the game's Dirt/Lava (the mod's Lava-dirt1 is that border's own groups,
    // block for block: W north, E north, W south, E south, W outer, E outer, W inner, E inner).
    {3, "Asphalt (Dirt)", 3, 2, 5,
     {{739, {192, 194}}, {741, {196, 198}}, {745, {204, 206}}, {743, {200, 202}}, {747, {208, 210}}, {749, {212, 214}}, {753, {220, 222}},
      {751, {216, 218}}},
     788, 789, 0},
    // Badlands: Structure-Dirt1-Cliff, a building's outline on dirt, made like the game's Dirt/High Dirt cliff: its
    // top edges (2 rows), its front walls (3 rows) and its outer corners (3 rows), from the pieces' shapes and stack
    // links. The roof is Structure's own. The set has no inner corners (nor its door pieces here: 2409).
    {0, "Structure (Dirt)", 2, 1, 8,
     {{2402, {46, 44}}, {2404, {50, 48}}, {2412, {52, 54, 56}}, {2406, {58, 60, 62}}, {2415, {64, 66, 68}}, {2418, {70, 72, 74}}},
     0, 0, 8},
    // Badlands: Asphalt-Dirt2-Edge, asphalt on high dirt, made like Ash World's Dirt/Lava (blocks in their groups' order);
    // stand-in High Grass (Badlands' High Dirt/High Grass rects: Ash World's Dirt/Lava ones + 44), its fill the game's Asphalt.
    {0, "High Asphalt (High Dirt)", 7, 2, 5,
     {{1831, {236, 238}}, {1833, {240, 242}}, {1835, {244, 246}}, {1837, {248, 250}}, {1839, {252, 254}}, {1841, {256, 258}},
      {1843, {260, 262}}, {1845, {264, 266}}},
     0, 0, 5},
    // Asphalt-Grass1/2-Edge and Asphalt-Mud1-Edge: asphalt on Grass, High Grass and Mud, made as Asphalt on High Dirt (its
    // blocks in their groups' order); the ground is High Dirt in the template, the ground's own tiles beside the edge.
    {0, "Asphalt (Grass)", 7, 4, 5,
     {{3082, {236, 238}}, {3084, {240, 242}}, {3086, {244, 246}}, {3088, {248, 250}}, {3090, {252, 254}}, {3092, {256, 258}}, {3094, {260, 262}}, {3096, {264, 266}}},
     0, 0, 5, 2},
    {0, "High Asphalt (High Grass)", 7, 7, 5,
     {{3106, {236, 238}}, {3108, {240, 242}}, {3110, {244, 246}}, {3112, {248, 250}}, {3114, {252, 254}}, {3116, {256, 258}}, {3118, {260, 262}}, {3120, {264, 266}}},
     0, 0, 5, 2},
    // (Mud-Dirt2-Edge: mud puddles of four sides and no corners, inside High Dirt on the maps: not an isometric border.)
    {0, "Asphalt (Mud)", 7, 9, 5,
     {{3058, {236, 238}}, {3060, {240, 242}}, {3062, {244, 246}}, {3064, {248, 250}}, {3066, {252, 254}}, {3068, {256, 258}}, {3070, {260, 262}}, {3072, {264, 266}}},
     0, 0, 5, 2},
    // Badlands' cliff sets, made like the game's Dirt/High Dirt cliff (groups 44-90: W north 46/44, E north 50/48, W south
    // 58-62, E south 52-56, W outer 64-68, E outer 70-74, W inner 80/82, E inner 76/78), their blocks in their brushes'
    // order (W north, E north, W south, E south, W outer, E outer, one inner corner: the other from the nearest piece).
    // Structure-Dirt2-Cliff and Structure-Structure2-Cliff: Structure on High Dirt and on Structure (High Dirt and
    // Structure are Dirt in the template), made as Structure on Dirt (2587 and 2458: door pieces, not used; on Structure
    // its south blocks the other way round, seen on the map).
    {0, "High Structure (High Dirt)", 2, 2, 8,
     {{2580, {46, 44}}, {2582, {50, 48}}, {2584, {58, 60, 62}}, {2590, {52, 54, 56}}, {2593, {64, 66, 68}}, {2596, {70, 72, 74}},
      {2651, {80, 82}, 1}, {2653, {76, 78}, 1}},
     0, 0, 8, 1},
    {0, "Structure (Structure)", 2, 1, 8,
     {{2451, {46, 44}}, {2453, {50, 48}}, {2455, {58, 60, 62}}, {2461, {52, 54, 56}}, {2464, {64, 66, 68}}, {2467, {70, 72, 74}}},
     0, 0, 8, 1, "Structure (Dirt)"},
    // Asphalt-Coastal: asphalt on a cliff above Water, made like Badlands' Dirt/High Dirt cliff (44-90), named blocks, its
    // corners in subtiles 10 and 12 of the wall groups, its south blocks the other way round (seen on the map); Water is
    // Dirt in the template, the game's Asphalt on top.
    {0, "Asphalt (Water)", 2, 3, 4,
     {{2721, {46, 44}}, {2723, {50, 48}}, {2725, {52, 54, 56}}, {2728, {58, 60, 62}}, {2725, {64, 66, 68}, 1, 10}, {2725, {70, 72, 74}, 1, 12},
      {2721, {80, 82}, 1, 10}, {2721, {76, 78}, 1, 12}},
     0, 0, 5, 1},
    // Badlands' Installation sets (the gray Substructure, the blue Floor and Roof, the Plating on them: the campaign maps
    // build bases of them). Display-Substructure-Dirt1: Substructure on Dirt, made like the game's Dirt/Asphalt (312-342,
    // the layout of High Dirt/High Grass), its blocks in their groups' order; stand-in Asphalt, its fill 2212/2213.
    {0, "Substructure (Dirt)", 5, 1, 7,
     {{3410, {320, 322}}, {3412, {324, 326}}, {3414, {312, 314}}, {3416, {316, 318}}, {3418, {336, 338}}, {3420, {340, 342}},
      {3422, {328, 330}}, {3424, {332, 334}}},
     2212, 2213, 0},
    // Substructure-Floor-cliff: the blue Floor on Substructure, its walls made like the game's Asphalt/Structure (360-398:
    // the layout of Dirt/High Dirt 44-82, +316, each pair's groups the other way round); its north and inner blocks
    // in subtiles 0, 2, 4 and 6 of 2214/2215; the Floor's fill 2108/2109.
    {0, "Floor (Substructure)", 8, 5, 6,
     {{2214, {360, 362}, 1, 0}, {2214, {364, 366}, 1, 2}, {2216, {368, 370, 372}}, {2219, {374, 376, 378}}, {2222, {380, 382, 384}, 7},
      {2225, {386, 388, 390}, 7}, {2214, {392, 394}, 1, 4}, {2214, {396, 398}, 1, 6}},
     2108, 2109, 0, 0, "Substructure (Dirt)"},
    // Roof-cliff: the Roof on Floor, a level higher (the Floor read as Asphalt in the template), made as Floor on
    // Substructure (its south blocks the other way round too); the Roof's fill 2106/2107.  (Its walls' variants join only
    // beside some others: the brush takes the ones the maps have side by side, Tileset::pieceNeighbors.)
    {0, "Roof (Floor)", 8, 5, 7,
     {{2110, {360, 362}}, {2112, {364, 366}}, {2114, {368, 370, 372}}, {2117, {374, 376, 378}}, {2120, {380, 382, 384}}, {2123, {386, 388, 390}},
      {2128, {392, 394}, 1}, {2126, {396, 398}, 1}},
     2106, 2107, 0, 5, "Floor (Substructure)"},
    // The Plating sets: flat edges, made as Substructure (the game's Dirt/Asphalt; the ground read as Dirt), the blocks
    // by their names (W north, E north, W south, E south, W outer, E outer, W inner, E inner) in the order that
    // Substructure's took; each set's fill the two groups before its edges. (Roof-Plating-edge: its outer corners are a
    // top row of one block and a bottom row of another, and it has no inner corners: not done.) The fills of Plating on
    // Floor and on Substructure: subtile 1 only (as the maps have them; the others are lines of a pattern).
    {0, "Plating (Floor)", 5, 1, 2,
     {{2184, {312, 314}, 2, 0}, {2184, {316, 318}, 2, 4}, {2186, {324, 326}, 2, 0}, {2186, {320, 322}, 2, 4}, {2184, {328, 330}, 2, 8},
      {2184, {332, 334}, 2, 12}, {2186, {336, 338}, 2, 12}, {2186, {340, 342}, 2, 8}},
     2182, 2183, 0, 1, "Floor (Substructure)", 0x2},
    {0, "Plating (Substructure)", 5, 1, 1,
     {{2202, {312, 314}, 1, 0}, {2202, {316, 318}, 1, 2}, {2204, {324, 326}, 1, 0}, {2204, {320, 322}, 1, 2}, {2202, {328, 330}, 1, 4},
      {2202, {332, 334}, 1, 6}, {2204, {336, 338}, 1, 4}, {2204, {340, 342}, 1, 6}},
     2200, 2201, 0, 1, "Substructure (Dirt)", 0x2},
    {0, "Plating 2 (Substructure)", 5, 1, 1,
     {{2208, {312, 314}, 1, 0}, {2208, {316, 318}, 1, 2}, {2210, {324, 326}, 1, 0}, {2210, {320, 322}, 1, 2}, {2208, {328, 330}, 1, 4},
      {2208, {332, 334}, 1, 6}, {2210, {336, 338}, 1, 4}, {2210, {340, 342}, 1, 6}},
     2206, 2207, 0, 1, "Substructure (Dirt)"},
    // Substructure-Asphalt-Cliff-1 and Substructure-Dirt-Cliff-1: the Floor on a wall (of Substructure) above Asphalt
    // and Dirt (so the maps have them; the sets' last brush is the Floor's fill), made as Asphalt above Water (Badlands'
    // Dirt/High Dirt cliff; Asphalt read as Dirt). Unnamed blocks: the north ones in one group (W subtiles 0-4, E 6-10), two south, two outer, the inner corners in
    // subtiles 14 and 12 of the first.
    {0, "Floor (Asphalt)", 2, 5, 6,
     {{2680, {46, 44}, 3, 0}, {2680, {50, 48}, 3, 6}, {2682, {58, 60, 62}}, {2685, {52, 54, 56}}, {2688, {64, 66, 68}, 7}, {2691, {70, 72, 74}, 7},
      {2680, {80, 82}, 1, 14}, {2680, {76, 78}, 1, 12}},
     2108, 2109, 0, 1},
    {0, "Floor (Dirt)", 2, 1, 6,
     {{2278, {46, 44}, 3, 0}, {2278, {50, 48}, 3, 6}, {2280, {58, 60, 62}}, {2283, {52, 54, 56}}, {2286, {64, 66, 68}, 7}, {2289, {70, 72, 74}, 7},
      {2680, {80, 82}, 1, 14}, {2680, {76, 78}, 1, 12}},
     2108, 2109, 0},
    // Display-Floor-Dirt2: the Floor on High Dirt, the same layout (its groups +147), made like the game's High Dirt/High
    // Grass (236-266: Dirt/Asphalt's -76); stand-in High Grass, the Floor's fill.
    {0, "High Floor (High Dirt)", 7, 2, 7,
     {{3557, {244, 246}}, {3559, {248, 250}}, {3561, {236, 238}}, {3563, {240, 242}}, {3565, {260, 262}}, {3567, {264, 266}},
      {3569, {252, 254}}, {3571, {256, 258}}},
     2108, 2109, 0},
    // Jungle-Dirt1: the mod's green Jungle on Dirt (Early Retirement), its edges groups of the game's numbers that the game
    // leaves without isometric data; made like the game's Dirt/Grass (188-218: High Dirt/High Grass's -48), its blocks
    // two in a group (W in subtiles 0-4, E in 6-10), the fill 608/609.
    {0, "Jungle (Dirt)", 4, 1, 3,
     {{520, {188, 190}, 3, 0}, {520, {192, 194}, 3, 6}, {522, {200, 202}, 3, 0}, {522, {196, 198}, 3, 6}, {524, {204, 206}, 3, 0},
      {524, {208, 210}, 3, 6}, {526, {212, 214}, 3, 6}, {526, {216, 218}, 3, 0}},
     608, 609, 0},
    // Display-Substructure-Jungle1: Substructure on Jungle, made as Substructure (its groups -24), the Jungle read as Dirt.
    // (Display-Floor-Jungle2: the Floor beside jungle on high ground, which the mod has no set for: not done.)
    {0, "Substructure (Jungle)", 5, 1, 7,
     {{3386, {320, 322}}, {3388, {324, 326}}, {3390, {312, 314}}, {3392, {316, 318}}, {3394, {336, 338}}, {3396, {340, 342}},
      {3398, {328, 330}}, {3400, {332, 334}}},
     2212, 2213, 0, 1, "Jungle (Dirt)"},
    // Caution-Substructure: a caution-striped line around an area of Substructure, made as Substructure (the same layout,
    // one variant), on Substructure (read as Dirt).
    {0, "Caution (Substructure)", 5, 1, 1,
     {{942, {320, 322}}, {944, {324, 326}}, {946, {312, 314}}, {948, {316, 318}}, {950, {336, 338}}, {952, {340, 342}},
      {954, {328, 330}}, {956, {332, 334}}},
     2212, 2213, 0, 1, "Substructure (Dirt)"},
    // Caution-Halfsphalt: the light gray floor (the maps' "halfsphalt", 758/759) with a caution-striped edge, on
    // Substructure, made as Substructure.
    {0, "Halfsphalt (Substructure)", 5, 1, 7,
     {{870, {320, 322}}, {872, {324, 326}}, {874, {312, 314}}, {876, {316, 318}}, {878, {336, 338}}, {880, {340, 342}},
      {882, {328, 330}}, {884, {332, 334}}},
     758, 759, 0, 1, "Substructure (Dirt)"},
    // Structure-Halfsphalt-2: Structure (its walls, the game's roof on top) on the light gray floor, made like the game's
    // Asphalt/Structure (360-398, as Floor on Substructure); its inner corners Structure on High Dirt's (2651/2653).
    // (Urban-Halfsphalt-2: four straight edges, eight variants each, no corners: not an isometric border.)
    {0, "Structure (Halfsphalt)", 8, 5, 8,
     {{3215, {360, 362}}, {3217, {364, 366}}, {3219, {368, 370, 372}}, {3225, {374, 376, 378}}, {3228, {380, 382, 384}},
      {3231, {386, 388, 390}}, {2651, {392, 394}, 1}, {2653, {396, 398}, 1}},
     0, 0, 8, 0, "Halfsphalt (Substructure)"},
    // Roof-Plating-edge: Plating on the Roof, made as Plating on Floor; its blocks all in 2194/2195 (W north, E north, W
    // south, E south in subtiles 0, 4, 8, 12, two variants each), its outer corners a north block's top row over a south
    // block's bottom row, its inner corners the other way round; the fill 2192/2193 (subtile 1).
    {0, "Plating (Roof)", 5, 1, 2,
     {{2194, {312, 314}, 2, 0}, {2194, {316, 318}, 2, 4}, {2194, {324, 326}, 2, 8}, {2194, {320, 322}, 2, 12}, {2194, {328, 330}, 2, 0, 8},
      {2194, {332, 334}, 2, 4, 12}, {2194, {336, 338}, 2, 8, 0}, {2194, {340, 342}, 2, 12, 4}},
     2192, 2193, 0, 1, "Roof (Floor)", 0x2},
    // Urban-Halfsphalt-2: the light gray floor (758/759) inside Structure's roof, made as Substructure (the game's
    // Dirt/Asphalt; Structure read as Dirt). Its four edges are strips of two rows (NW 790/791, NE 792/793, SW 788/789,
    // SE 786/787), eight variants each; its corners are two strips meeting, as the maps have them: a tip a strip's top
    // row over another's bottom row (W: 790 over 789, E: 792 over 787), a notch the other way (left: 788 over 791,
    // right: 786 over 793). The variants beside each other: the maps' (Tileset::pieceNeighbors).
    {0, "Halfsphalt (Structure)", 5, 8, 8,
     {{790, {312, 314}}, {792, {316, 318}}, {786, {320, 322}}, {788, {324, 326}}, {790, {328, 330}, 8, 0, -1, 789},
      {792, {332, 334}, 8, 0, -1, 787}, {786, {336, 338}, 8, 0, -1, 793}, {788, {340, 342}, 8, 0, -1, 791}},
     758, 759, 0, 1},
    // Display-Floor-Jungle2: the mod's jungle in a raised Floor, at the Floor's height (Hills of Tyche): Floor on High
    // Dirt's layout (its groups -195) the other way round (the jungle inside: its NW edge is the Floor's SE, its tips the
    // Floor's notches), on Floor above Dirt (High Dirt in the template); the jungle's fill 608/609.
    {0, "Jungle (Floor)", 7, 2, 7,
     {{3362, {236, 238}}, {3364, {240, 242}}, {3366, {244, 246}}, {3368, {248, 250}}, {3370, {252, 254}}, {3372, {256, 258}},
      {3374, {260, 262}}, {3376, {264, 266}}},
     608, 609, 0, 2, "Floor (Dirt)"},
    // The Floor a level up, on Asphalt (Water)'s asphalt (the asphalt on a cliff above Water): Floor (Asphalt)'s walls,
    // Asphalt (Water) read as Dirt in the template (as Structure (Structure) on Structure (Asphalt)).
    {0, "High Floor (Asphalt (Water))", 2, 5, 6,
     {{2680, {46, 44}, 3, 0}, {2680, {50, 48}, 3, 6}, {2682, {58, 60, 62}}, {2685, {52, 54, 56}}, {2688, {64, 66, 68}, 7}, {2691, {70, 72, 74}, 7},
      {2680, {80, 82}, 1, 14}, {2680, {76, 78}, 1, 12}},
     2108, 2109, 0, 1, "Asphalt (Water)"},
    // The same on the asphalt on High Dirt and on High Grass: their fill is the game's Asphalt as Asphalt (Water)'s, at
    // low ground (VF4); the Floor's walls go from it to middle ground, the High Dirt's height around it.
    {0, "High Floor (High Asphalt (High Dirt))", 2, 5, 6,
     {{2680, {46, 44}, 3, 0}, {2680, {50, 48}, 3, 6}, {2682, {58, 60, 62}}, {2685, {52, 54, 56}}, {2688, {64, 66, 68}, 7}, {2691, {70, 72, 74}, 7},
      {2680, {80, 82}, 1, 14}, {2680, {76, 78}, 1, 12}},
     2108, 2109, 0, 1, "High Asphalt (High Dirt)"},
    {0, "High Floor (High Asphalt (High Grass))", 2, 5, 6,
     {{2680, {46, 44}, 3, 0}, {2680, {50, 48}, 3, 6}, {2682, {58, 60, 62}}, {2685, {52, 54, 56}}, {2688, {64, 66, 68}, 7}, {2691, {70, 72, 74}, 7},
      {2680, {80, 82}, 1, 14}, {2680, {76, 78}, 1, 12}},
     2108, 2109, 0, 1, "High Asphalt (High Grass)"},
    // Badlands' dirt cliff sets: the game's Dirt/High Dirt cliff (44-90) as the maps' ISOM has their pieces
    // (cmbw_auto_rows): W north, E north, E south, W south (8 variants each), more south variants (W in subtiles 8-14),
    // outer corners (W in 0-2, E in 4-6), inner corners (E in 0-4, W in 6-10: copies of the game's 76/78 and 80/82,
    // cmbw_group_match); the four sets one layout (their groups from 2973, 3130, 2770, 1685). Their art is a lighter
    // dirt than the game's tiles under and over them: squares where they meet, as on the maps (no pieces between those
    // dirts: the mod's to make). Dirtcliff-mud1 and -Grass1: High Dirt over Mud and over Grass (Mud and Grass are Dirt
    // in the template; the game's High Dirt on top).
    {0, "High Dirt (Mud)", 2, 9, 8,
     {{2973, {46, 44}, 7, 0}, {2975, {50, 48}, 7, 0}, {2977, {52, 54, 56}, 8, 0}, {2983, {58, 60, 62}, 8, 0}, {2980, {52, 54, 56}, 4, 0},
      {2980, {58, 60, 62}, 4, 8}, {2986, {64, 66, 68}, 2, 0}, {2986, {70, 72, 74}, 2, 4}, {2989, {76, 78}, 3, 0}, {2989, {80, 82}, 3, 6}},
     0, 0, 0, 1},
    {0, "High Dirt (Grass)", 2, 4, 8,
     {{3130, {46, 44}, 7, 0}, {3132, {50, 48}, 7, 0}, {3134, {52, 54, 56}, 8, 0}, {3140, {58, 60, 62}, 8, 0}, {3137, {52, 54, 56}, 4, 0},
      {3137, {58, 60, 62}, 4, 8}, {3143, {64, 66, 68}, 2, 0}, {3143, {70, 72, 74}, 2, 4}, {3146, {76, 78}, 3, 0}, {3146, {80, 82}, 3, 6}},
     0, 0, 0, 1},
    // Dirt-Dirt2-cliff and Dirt-Dirt3-cliff: the mod's lighter dirt (2768/2769) a level up on High Dirt (the maps have
    // the walls' foot on High Dirt's tiles: a third height, High Dirt read as Dirt in the template), and a level up again.
    // The concave corners that the brush files leave out are in the sets' next groups (2788-2797, 1703-1712), recolored
    // copies of the game's cliff rects 84-94 (cmbw_group_match); the game's 96 and 98 have no copy: the null tile.
    {0, "Higher Dirt (High Dirt)", 2, 2, 8,
     {{2770, {46, 44}, 7, 0}, {2772, {50, 48}, 7, 0}, {2774, {52, 54, 56}, 8, 0}, {2780, {58, 60, 62}, 8, 0}, {2777, {52, 54, 56}, 4, 0},
      {2777, {58, 60, 62}, 4, 8}, {2783, {64, 66, 68}, 2, 0}, {2783, {70, 72, 74}, 2, 4}, {2786, {76, 78}, 3, 0}, {2786, {80, 82}, 3, 6},
      {2788, {84}, 7, 0}, {2790, {84}, 5, 0}, {2789, {86}, 7, 0}, {2791, {86}, 5, 0}, {2792, {88}, 6, 0}, {2793, {90}, 6, 0},
      {2794, {92}, 7, 0}, {2796, {92}, 5, 0}, {2795, {94}, 7, 0}, {2797, {94}, 5, 0}},
     2768, 2769, 0, 1},
    {0, "Highest Dirt (Higher Dirt)", 2, 1, 8,
     {{1685, {46, 44}, 7, 0}, {1687, {50, 48}, 7, 0}, {1689, {52, 54, 56}, 8, 0}, {1695, {58, 60, 62}, 8, 0}, {1692, {52, 54, 56}, 4, 0},
      {1692, {58, 60, 62}, 4, 8}, {1698, {64, 66, 68}, 2, 0}, {1698, {70, 72, 74}, 2, 4}, {1701, {76, 78}, 3, 0}, {1701, {80, 82}, 3, 6},
      {1703, {84}, 7, 0}, {1705, {84}, 5, 0}, {1704, {86}, 7, 0}, {1706, {86}, 5, 0}, {1707, {88}, 6, 0}, {1708, {90}, 6, 0},
      {1709, {92}, 7, 0}, {1711, {92}, 5, 0}, {1710, {94}, 7, 0}, {1712, {94}, 5, 0}},
     2768, 2769, 0, 1, "Higher Dirt (High Dirt)"},
    // Basilica-dirt1-cliff and -dirt2-cliff: a basilica on Dirt and on High Dirt (in the game's old groups 614-625 and
    // 662-673), made as Ice's Basilica (Snow) (the same layout); the floor 614/615 and 662/663, subtile 0.
    {0, "Basilica (Dirt)", 2, 1, 8,
     {{616, {46, 44}, 1, 0}, {616, {50, 48}, 3, 10}, {618, {58, 60, 62}, 1, 0}, {618, {52, 54, 56}, 3, 10}, {621, {64, 66, 68}, 4, 0},
      {621, {70, 72, 74}, 3, 10}, {624, {80, 82}, 4, 8}, {624, {76, 78}, 4, 0}},
     614, 615, 0, 0, nullptr, 0x1},
    {0, "High Basilica (High Dirt)", 2, 2, 8,
     {{664, {46, 44}, 1, 0}, {664, {50, 48}, 3, 10}, {666, {58, 60, 62}, 1, 0}, {666, {52, 54, 56}, 3, 10}, {669, {64, 66, 68}, 4, 0},
      {669, {70, 72, 74}, 3, 10}, {672, {80, 82}, 4, 8}, {672, {76, 78}, 4, 0}},
     662, 663, 0, 1, nullptr, 0x1},
    // Ash World: Structure-1-dirt, the same building outline on dirt with Ash World's Dirt/High Dirt cliff (its groups
    // follow Badlands' layout), and inner corners (the rects of the diamonds with three high corners: W inner the one
    // low at the west, E inner at the east). The roof: 734/735 (the plating the mod's maps put inside these edges).
    {3, "Structure (Dirt)", 5, 2, 8,
     {{463, {120, 122}}, {465, {124, 126}}, {473, {134, 136, 138}}, {467, {128, 130, 132}}, {476, {140, 142, 144}}, {479, {146, 148, 150}},
      {856, {156, 158}, 4}, {854, {152, 154}, 4}},
     734, 735, 0},
    // Ash World: the mod's flat sets on Dirt ("dirt1") and High Dirt ("dirt2"), made like its Dirt/Lava, their blocks in
    // their groups' order (W north, E north, E south, W south, W outer, E outer, E inner, W inner), each with its fill.
    // Crushedrock-edge-dirt1: stand-in Broken Rock (Ash World's Dirt/Broken Rock rects: Dirt/Lava's + 192).
    {3, "Crushed Rock (Dirt)", 8, 2, 5,
     {{1311, {384, 386}}, {1313, {388, 390}}, {1315, {392, 394}}, {1317, {396, 398}}, {1319, {400, 402}}, {1321, {404, 406}},
      {1323, {408, 410}}, {1325, {412, 414}}},
     1336, 1337, 0},
    // Crushedrock-edge-dirt2: stand-in High Shale (High Dirt/High Shale: + 136; the game has no inner corners there).
    {3, "High Crushed Rock (High Dirt)", 7, 5, 5,
     {{1338, {328, 330}}, {1340, {332, 334}}, {1342, {336, 338}}, {1344, {340, 342}}, {1346, {344, 346}}, {1348, {348, 350}}},
     1362, 1363, 0},
    // Lava-dirt2: the mod's lava on high dirt; stand-in High Lava (High Dirt/High Lava: + 48).
    {3, "High Lava (High Dirt)", 6, 5, 5,
     {{1834, {240, 242}}, {1836, {244, 246}}, {1838, {248, 250}}, {1840, {252, 254}}, {1842, {256, 258}}, {1844, {260, 262}},
      {1846, {264, 266}}, {1848, {268, 270}}},
     1882, 1883, 0},
    // Cooled Lava-1: the mod's cooled lava on dirt; stand-in Shale (Dirt/Shale: + 96; the game has no inner corners there).
    // Its first group is its fill.
    {3, "Cooled Lava (Dirt)", 4, 2, 5,
     {{2174, {288, 290}}, {2176, {292, 294}}, {2178, {296, 298}}, {2180, {300, 302}}, {2182, {304, 306}}, {2184, {308, 310}}},
     2172, 2173, 0},
    // The mod's High Dirt cliffs onto other ground: the ground below the cliff is the mod terrain, High Dirt its ground
    // (made like the game's Dirt/High Dirt cliff, Structure on Dirt's stacks; stand-in Dirt, the cliff's low side).
    // Cliff-lava1: named blocks (W north, E north, W south, E south, W outer, E outer, W inner, E inner); Lava's tiles below.
    {3, "Lava (High Dirt cliffs)", 2, 5, 6,
     {{948, {120, 122}}, {950, {124, 126}}, {952, {128, 130, 132}}, {955, {134, 136, 138}}, {958, {140, 142, 144}}, {961, {146, 148, 150}},
      {966, {152, 154}}, {964, {156, 158}}},
     0, 0, 3},
    // Cliff-crushedrock1: blocks in their groups' order (W north, E north, W south, E south, W outer, E outer, E inner, W
    // inner: its south blocks the other way round from the other named sets' order, seen on the map); Crushed Rock's fill
    // below.
    // Cliff-dirt2: a cliff onto "dirt2" (High Dirt), its top a third level: Higher Dirt on High Dirt, made like the
    // game's High Dirt cliff on Dirt (High Dirt for Higher Dirt, Dirt for High Dirt); High Dirt's tiles on top.
    {3, "Higher Dirt (High Dirt)", 5, 5, 6,
     {{894, {120, 122}}, {896, {124, 126}}, {898, {128, 130, 132}}, {901, {134, 136, 138}}, {904, {140, 142, 144}}, {907, {146, 148, 150}},
      {912, {152, 154}}, {910, {156, 158}}},
     0, 0, 5, 2},
    {3, "Crushed Rock (High Dirt cliffs)", 2, 5, 6,
     {{2096, {120, 122}}, {2098, {124, 126}}, {2100, {128, 130, 132}}, {2103, {134, 136, 138}}, {2106, {140, 142, 144}}, {2109, {146, 148, 150}},
      {2112, {156, 158}}, {2114, {152, 154}}},
     1336, 1337, 0},
    // On Higher Dirt ("dirt3"): the High Dirt sets' blocks, 26 and 24 groups on; Higher Dirt is High Dirt in the template.
    {3, "Higher Crushed Rock (Higher Dirt)", 7, 5, 5,
     {{1364, {328, 330}}, {1366, {332, 334}}, {1368, {336, 338}}, {1370, {340, 342}}, {1372, {344, 346}}, {1374, {348, 350}}},
     1388, 1389, 0, 0, "Higher Dirt (High Dirt)"},
    {3, "Higher Lava (Higher Dirt)", 6, 5, 5,
     {{1858, {240, 242}}, {1860, {244, 246}}, {1862, {248, 250}}, {1864, {252, 254}}, {1866, {256, 258}}, {1868, {260, 262}},
      {1870, {264, 266}}, {1872, {268, 270}}},
     1884, 1885, 0, 0, "Higher Dirt (High Dirt)"},
    // Structure-1-asphalt: Structure onto the mod's Asphalt, made as Structure on Dirt (the same blocks' layout: named
    // south blocks as Structure-1-dirt's); Asphalt is Dirt in the template.
    {3, "Structure (Asphalt)", 5, 2, 7,
     {{432, {120, 122}}, {434, {124, 126}}, {442, {134, 136, 138}}, {436, {128, 130, 132}}, {445, {140, 142, 144}}, {448, {146, 148, 150}},
      {856, {156, 158}, 4}, {854, {152, 154}, 4}},
     734, 735, 0, 2, "Asphalt (Dirt)"},
    // Flagstones-edge-crushedrock1/2/3: flagstones on the mod's Crushed Rock (on Dirt, High Dirt, Higher Dirt), made like
    // the game's Dirt/Lava edge (on Dirt) and High Dirt/High Lava edge (higher): named blocks, the E ones in subtiles 8-15
    // of the W ones' groups (on Dirt its south ones the other way round, seen on the map); the inner corners one variant
    // each. Crushed Rock is Dirt or High Dirt in the template.
    {3, "Flagstones (Crushed Rock)", 3, 2, 4,
     {{1770, {192, 194}}, {1770, {196, 198}, 0, 8}, {1772, {204, 206}}, {1772, {200, 202}, 0, 8}, {1774, {208, 210}}, {1774, {212, 214}, 0, 8},
      {1776, {216, 218}, 1}, {1776, {220, 222}, 1, 2}},
     1794, 1795, 0, 2, "Crushed Rock (Dirt)"},
    {3, "High Flagstones (Crushed Rock)", 6, 5, 4,
     {{1778, {240, 242}}, {1778, {244, 246}, 0, 8}, {1780, {252, 254}}, {1780, {248, 250}, 0, 8}, {1782, {256, 258}}, {1782, {260, 262}, 0, 8},
      {1784, {264, 266}, 1}, {1784, {268, 270}, 1, 2}},
     1812, 1813, 0, 5, "High Crushed Rock (High Dirt)"},
    {3, "Higher Flagstones (Crushed Rock)", 6, 5, 4,
     {{1786, {240, 242}}, {1786, {244, 246}, 0, 8}, {1788, {252, 254}}, {1788, {248, 250}, 0, 8}, {1790, {256, 258}}, {1790, {260, 262}, 0, 8},
      {1792, {264, 266}, 1}, {1792, {268, 270}, 1, 2}},
     1814, 1815, 0, 5, "Higher Crushed Rock (Higher Dirt)"},
    // Magmacliff-lava1 and Magmacliff-crushedrock-1: lava and crushed rock above a magma cliff, made like the game's
    // Dirt/Magma cliff (Ash World's Dirt/High Dirt cliff groups less 72, block for block): stand-in Dirt, ground Magma.
    {3, "Lava (Magma)", 2, 1, 6,
     {{1275, {48, 50}}, {1277, {52, 54}}, {1279, {56, 58, 60}}, {1282, {62, 64, 66}}, {1285, {68, 70, 72}}, {1288, {74, 76, 78}},
      {1293, {80, 82}}, {1291, {84, 86}}},
     0, 0, 3},
    {3, "Crushed Rock (Magma)", 2, 1, 6,
     {{2258, {48, 50}}, {2260, {52, 54}}, {2265, {56, 58, 60}}, {2262, {62, 64, 66}}, {2268, {68, 70, 72}}, {2271, {74, 76, 78}},
      {2276, {80, 82}}, {2274, {84, 86}}},
     1336, 1337, 0},
    // Basilica-cliff-crushedrock1: Basilica standing on Crushed Rock (Otherworld, Ashey Alternation), made as Structure on
    // Dirt (the maps put its blocks on Ash World's Dirt/High Dirt cliff, 120-158; named blocks sharing their groups: W
    // ones from the left, E ones from the right); its floor 1426/1427.
    {3, "Basilica (Crushed Rock)", 5, 2, 5,
     {{1408, {120, 122}, 5, 0}, {1408, {124, 126}, 3, 10}, {1410, {134, 136, 138}, 4, 0}, {1410, {128, 130, 132}, 4, 8},
      {1413, {140, 142, 144}, 5, 0}, {1413, {146, 148, 150}, 3, 10}, {1416, {152, 154}, 4, 0}, {1416, {156, 158}, 4, 8}},
     1426, 1427, 0, 2, "Crushed Rock (Dirt)"},
    // Basilica-cliff-crushedrock2: the same Basilica a level up, on Crushed Rock on High Dirt (Smokeshow), made as Higher
    // Dirt (High Dirt for its ground); its floor 1468/1469.
    {3, "High Basilica (Crushed Rock)", 5, 5, 5,
     {{1439, {120, 122}, 5, 0}, {1439, {124, 126}, 3, 10}, {1441, {134, 136, 138}, 5, 0}, {1441, {128, 130, 132}, 3, 10},
      {1444, {140, 142, 144}, 5, 0}, {1444, {146, 148, 150}, 3, 10}, {1447, {152, 154}, 4, 0}, {1447, {156, 158}, 4, 8}},
     1468, 1469, 0, 2, "High Crushed Rock (High Dirt)"},
    // Basilica-cliff-basilica1 (and -2, another look): a basilica a level up on Basilica (Crushed Rock), and
    // Basilica-cliff-flagstones1: a basilica on Flagstones (Crushed Rock), as High Basilica (Crushed Rock) and Basilica
    // (Crushed Rock) (blocks the same way); the floor 1426/1427.
    {3, "Basilica (Basilica)", 5, 5, 5,
     {{2010, {120, 122}, 5, 0}, {2010, {124, 126}, 3, 10}, {2012, {134, 136, 138}, 5, 0}, {2012, {128, 130, 132}, 3, 10},
      {2015, {140, 142, 144}, 5, 0}, {2015, {146, 148, 150}, 3, 10}, {2018, {152, 154}, 4, 0}, {2018, {156, 158}, 4, 8},
      {2051, {120, 122}, 5, 0}, {2051, {124, 126}, 3, 10}, {2053, {134, 136, 138}, 5, 0}, {2053, {128, 130, 132}, 3, 10},
      {2056, {140, 142, 144}, 5, 0}, {2056, {146, 148, 150}, 3, 10}, {2059, {152, 154}, 4, 0}, {2059, {156, 158}, 4, 8}},
     1426, 1427, 0, 2, "Basilica (Crushed Rock)"},
    {3, "Basilica (Flagstones)", 5, 2, 5,
     {{1645, {120, 122}, 5, 0}, {1645, {124, 126}, 3, 10}, {1647, {134, 136, 138}, 5, 0}, {1647, {128, 130, 132}, 3, 10},
      {1650, {140, 142, 144}, 5, 0}, {1650, {146, 148, 150}, 3, 10}, {1653, {152, 154}, 4, 0}, {1653, {156, 158}, 4, 8}},
     1426, 1427, 0, 2, "Flagstones (Crushed Rock)"},
    // Cliff-dirt3: a fourth level on Higher Dirt, made as Higher Dirt (High Dirt) (named blocks); High Dirt's tiles on top.
    {3, "Highest Dirt (Higher Dirt)", 5, 5, 6,
     {{1501, {120, 122}}, {1503, {124, 126}}, {1508, {128, 130, 132}}, {1505, {134, 136, 138}}, {1511, {140, 142, 144}}, {1514, {146, 148, 150}},
      {1519, {152, 154}, 2}, {1517, {156, 158}, 2}},
     0, 0, 5, 2, "Higher Dirt (High Dirt)"},
    // Shale-dirt-3: shale on Higher Dirt, as Crushed Rock (Higher Dirt) (blocks in their groups' order); the game's Shale
    // inside (its edges' own shale; High Shale's tiles are lighter).
    {3, "Higher Shale (Higher Dirt)", 7, 5, 4,
     {{1994, {328, 330}}, {1996, {332, 334}}, {1998, {336, 338}}, {2000, {340, 342}}, {2002, {344, 346}}, {2004, {348, 350}}},
     0, 0, 4, 0, "Higher Dirt (High Dirt)"},
    // Basilica-cliff-crushedrock3: the Basilica a level up again, on Crushed Rock on Higher Dirt, made as High Basilica
    // (Crushed Rock) (the same layout, its groups from 1470); the floor 1468/1469. Its south walls' foot shows boxes on the
    // crushed rock: squares, as on its map.
    {3, "Higher Basilica (Higher Crushed Rock)", 5, 5, 5,
     {{1470, {120, 122}, 5, 0}, {1470, {124, 126}, 3, 10}, {1472, {134, 136, 138}, 5, 0}, {1472, {128, 130, 132}, 3, 10},
      {1475, {140, 142, 144}, 5, 0}, {1475, {146, 148, 150}, 3, 10}, {1478, {152, 154}, 4, 0}, {1478, {156, 158}, 4, 8}},
     1468, 1469, 0, 2, "Higher Crushed Rock (Higher Dirt)"},
    // Desert: Driedmudedge-highdirt, dried mud on high dirt, made like Ash World's Dirt/Lava block for block; its
    // stand-in is High Sand Dunes (the flat terrain the game borders with High Dirt: its rects are Ash World's + 36), its fill the game's Dried Mud.
    // (its inner corners are named the other way round: W inner is the game's E inner block, seen on the map)
    {5, "High Dried Mud (High Dirt)", 9, 2, 5,
     {{1716, {228, 230}}, {1718, {232, 234}}, {1722, {240, 242}}, {1720, {236, 238}}, {1724, {244, 246}}, {1726, {248, 250}},
      {1728, {252, 254}}, {1730, {256, 258}}},
     0, 0, 13},
    // Desert: the mod's asphalt and cooled lava on Dirt and on High Dirt (blocks in their groups' order). On Dirt the
    // stand-in is Rocky Ground (Desert's Dirt/Rocky Ground rects: Ash World's Dirt/Lava ones + 84; no inner corners), on
    // High Dirt High Sand Dunes (+ 36). The asphalt's fill: AsphaltEdge-Dirt3's (700/701); the cooled lava's first group.
    {5, "Asphalt (Dirt)", 6, 1, 5,
     {{626, {276, 278}}, {628, {280, 282}}, {630, {284, 286}}, {632, {288, 290}}, {634, {292, 294}}, {636, {296, 298}}},
     700, 701, 0},
    {5, "High Asphalt (High Dirt)", 9, 2, 5,
     {{650, {228, 230}}, {652, {232, 234}}, {654, {236, 238}}, {656, {240, 242}}, {658, {244, 246}}, {660, {248, 250}},
      {662, {252, 254}}, {664, {256, 258}}},
     700, 701, 0},
    {5, "Cooled Lava (Dirt)", 6, 1, 5,
     {{2818, {276, 278}}, {2820, {280, 282}}, {2822, {284, 286}}, {2824, {288, 290}}, {2826, {292, 294}}, {2828, {296, 298}}},
     2816, 2817, 0},
    // Platform: Platform-cliff-lowplatform, the mod's platform cliff on low platform (the game's Low Platform/Platform
    // cliff drawn again): edges only, the game's Platform on top (a fill of Platform's own tiles would make all of
    // Platform this terrain). Its stand-in Platform (Low Platform/Platform: Ash World's Dirt/High Dirt cliff + 270); its
    // blocks unnamed, in the order of Platform-Higherplatformcliff's named ones.
    {1, "Platform Cliff (Low Platform)", 2, 9, 5,
     {{1716, {390, 392}}, {1718, {394, 396}}, {1720, {404, 406, 408}}, {1723, {398, 400, 402}}, {1726, {410, 412, 414}},
      {1729, {416, 418, 420}}, {1732, {426, 428}}, {1734, {422, 424}}},
     0, 0, 0},
    // Platform-clioff-plating2: Platform on a cliff above Plating, made as Platform Cliff (the game's Low Platform/Platform
    // cliff, the same blocks): Plating is Low Platform in the template, Platform's tiles on top (the set's platform is a
    // shade lighter: a faint band inside its edge; the set has no fill of its own).
    {1, "Platform (Plating)", 2, 11, 5,
     {{1648, {390, 392}}, {1650, {394, 396}}, {1652, {404, 406, 408}}, {1655, {398, 400, 402}}, {1658, {410, 412, 414}},
      {1661, {416, 418, 420}}, {1664, {426, 428}}, {1666, {422, 424}}},
     0, 0, 2, 9},
    // Platform-Higherplatformcliff: a platform a level above High Platform, made like the game's High Platform cliff on
    // Platform (its groups 230-268, block for block as Ash World's Dirt/High Dirt cliff); named blocks, its south ones
    // named the other way round (seen on the map); High Platform's tiles on top.
    {1, "Higher Platform (High Platform)", 4, 4, 6,
     {{2234, {230, 232}}, {2236, {234, 236}}, {2238, {244, 246, 248}}, {2241, {238, 240, 242}}, {2244, {250, 252, 254}},
      {2247, {256, 258, 260}}, {2250, {262, 264}}, {2252, {266, 268}}},
     0, 0, 4, 2},
    // Spacecliff-lowplatform: Low Platform at Platform's height above Space (the maps put its blocks on the game's
    // Space/Platform edge, 302-340, in its order); Low Platform's own tiles on top.
    {1, "Low Platform (Space)", 2, 1, 5,
     {{2628, {302, 304}}, {2630, {306, 308}}, {2632, {310, 312, 314}}, {2635, {316, 318, 320}}, {2638, {322, 324, 326}}, {2641, {328, 330, 332}},
      {2644, {334, 336}}, {2646, {338, 340}}},
     0, 0, 9},
    // Dirtcliff-Space: dirt at Platform's height above Space (the maps put its blocks on the game's Space/Platform edge,
    // 302-340: W south 1831 and 1834 (subtiles 0-6) on 316-320, E south 1837 and 1834 (8-14) on 310-314; the outer corners
    // 1840 (W 0-2, E 4-6), the inner ones 1843 (on 334-336: 6-10, on 338-340: 0-4)); dirt 1788/1789 on top (as its edges' dirt).
    {1, "Dirt (Space)", 2, 1, 7,
     {{1827, {302, 304}}, {1829, {306, 308}}, {1831, {316, 318, 320}}, {1834, {316, 318, 320}, 4, 0}, {1837, {310, 312, 314}},
      {1834, {310, 312, 314}, 4, 8}, {1840, {322, 324, 326}, 2, 0}, {1840, {328, 330, 332}, 2, 4}, {1843, {334, 336}, 3, 6}, {1843, {338, 340}, 3, 0}},
     1788, 1789, 0},
    // Dirtcliff-SpaceLow: dirt at Low Platform's height above Space (Megalith Empire, Rageslave), made as Dirt above Space
    // (the maps' rects: W south 2140 on 316-320, E south 2146 on 310-314; 2143 is not W south's: other places; the inner
    // corners 2152 as Dirt above Space's); dirt 1790/1791 on top.
    {1, "Low Dirt (Space)", 2, 1, 7,
     {{2136, {302, 304}}, {2138, {306, 308}}, {2140, {316, 318, 320}, 8}, {2146, {310, 312, 314}, 8}, {2149, {322, 324, 326}, 2, 0},
      {2149, {328, 330, 332}, 2, 4}, {2152, {334, 336}, 3, 6}, {2152, {338, 340}, 3, 0}},
     1790, 1791, 0},
    // Spacecliff-DirtLow: Platform on a cliff above the low dirt (Estacion Fantasma), made on the game's Low
    // Platform/Platform cliff (390-428) as the maps have its blocks, Low Dirt above Space its ground (Low Platform in
    // the template); Platform's tiles on top.
    {1, "Platform (Low Dirt)", 2, 9, 5,
     {{2188, {390, 392}}, {2190, {394, 396}}, {2192, {398, 400, 402}}, {2195, {404, 406, 408}}, {2198, {410, 412, 414}},
      {2201, {416, 418, 420}}, {2204, {422, 424}}, {2206, {426, 428}}},
     0, 0, 2, 9, "Low Dirt (Space)"},
    // Dirtcliff-Dirt: Platform-height dirt on a cliff above the low dirt (Megalith Empire, Rageslave), made as Platform
    // Cliff (the maps put its blocks on the game's Low Platform/Platform cliff, 390-428; its group 1874 is not a wall's),
    // Low Dirt above Space its ground; dirt 1788/1789 on top (as Dirt above Space).
    {1, "Dirt Cliff (Low Dirt)", 2, 9, 7,
     {{1867, {390, 392}}, {1869, {394, 396}}, {1871, {404, 406, 408}, 8}, {1877, {398, 400, 402}, 8}, {1880, {410, 412, 414}, 2, 0},
      {1880, {416, 418, 420}, 2, 4}, {1883, {426, 428}, 3, 0}, {1883, {422, 424}, 3, 6}},
     1788, 1789, 0, 9, "Low Dirt (Space)"},
    // Dirtcliff-Hdirt: dirt a level higher, on a cliff above the dirt at Platform's height (Infinite Velocity, FiniteSpeed),
    // made as Higher Platform (the maps put its blocks on the game's Platform/High Platform cliff, 230-268; W south's
    // variants also in 1973), Dirt above Space its ground; dirt 1786/1787 on top (beside its walls on the maps).
    {1, "High Dirt Cliff (Dirt)", 4, 2, 7,
     {{1966, {230, 232}}, {1968, {234, 236}}, {1970, {244, 246, 248}, 8}, {1973, {244, 246, 248}, 8}, {1976, {238, 240, 242}, 8},
      {1979, {250, 252, 254}, 2, 0}, {1979, {256, 258, 260}, 2, 4}, {1982, {262, 264}, 3, 0}, {1982, {266, 268}, 3, 6}},
     1786, 1787, 0, 2, "Dirt (Space)"},
    // Plating-edge-lowplatform: a patch of Plating on Low Platform (Celestial Rift), made as Plating on Roof on the game's
    // Platform/Solar Array edge (166-196, the Roof edge's layout): NW/NE strips 1696, SW/SE 1698, W/E tips 1700, W/E
    // notches 1702 (subtiles 0-4 and 6-10, three variants each); Plating's tiles inside, Low Platform its
    // ground (Platform in the template).
    {1, "Plating (Low Platform)", 8, 9, 3,
     {{1696, {166, 168}, 3, 0}, {1696, {170, 172}, 3, 6}, {1698, {178, 180}, 3, 0}, {1698, {174, 176}, 3, 6}, {1700, {182, 184}, 3, 0},
      {1700, {186, 188}, 3, 6}, {1702, {190, 192}, 3, 0}, {1702, {194, 196}, 3, 6}},
     0, 0, 11, 2},
    // Plating-edge-dirt1 and -dirt2: the same patch of Plating on the low dirt and on the dirt at Platform's height.
    {1, "Plating (Low Dirt)", 8, 9, 3,
     {{1684, {166, 168}, 3, 0}, {1684, {170, 172}, 3, 6}, {1686, {178, 180}, 3, 0}, {1686, {174, 176}, 3, 6}, {1688, {182, 184}, 3, 0},
      {1688, {186, 188}, 3, 6}, {1690, {190, 192}, 3, 0}, {1690, {194, 196}, 3, 6}},
     0, 0, 11, 2, "Low Dirt (Space)"},
    {1, "Plating (Dirt)", 8, 2, 3,
     {{2315, {166, 168}, 3, 0}, {2315, {170, 172}, 3, 6}, {2317, {178, 180}, 3, 0}, {2317, {174, 176}, 3, 6}, {2319, {182, 184}, 3, 0},
      {2319, {186, 188}, 2, 6}, {2321, {190, 192}, 3, 0}, {2321, {194, 196}, 3, 6}},
     0, 0, 11, 2, "Dirt (Space)"},
    // Catwalk-Spacecliff: the Elevated Catwalk's floor over Space (vz02), on the game's Space/Platform edge (302-340, as
    // the maps have it): each group a W block (subtiles 0-4) and an E one (6-10); the catwalk's tiles inside.
    {1, "Catwalk (Space)", 2, 1, 3,
     {{1548, {302, 304}, 3, 0}, {1548, {306, 308}, 3, 6}, {1550, {316, 318, 320}, 3, 0}, {1550, {310, 312, 314}, 3, 6},
      {1553, {322, 324, 326}, 3, 0}, {1553, {328, 330, 332}, 3, 6}, {1556, {334, 336}, 3, 0}, {1556, {338, 340}, 3, 6}},
     0, 0, 13},
    // (Organ-Cliff-Flesh-1: its walls have flesh on both sides, and flesh is no isometric terrain: the maps place its
    // tiles 2882/2883 by hand on Platform; the walls are on no map.)
    // Platform-cliff-plating1: High Platform on a cliff above Plating (Pendulum), as Higher Platform (High Platform) on the
    // game's High Platform/Platform cliff (230-268; the maps put its south blocks there); no inner corners: the game's.
    // Plating is Platform in the template; High Platform's tiles on top.
    {1, "High Platform (Plating)", 4, 11, 5,
     {{1589, {230, 232}}, {1591, {234, 236}}, {1593, {244, 246, 248}}, {1596, {238, 240, 242}}, {1599, {250, 252, 254}},
      {1602, {256, 258, 260}}},
     0, 0, 4, 2},
    // Cliff-Dirt-3: the dirt a level above High Dirt Cliff (Dirt), made as that one (the same layout, its groups from 2051);
    // High Dirt Cliff is Platform in the template; the lighter dirt 1786/1787 on top. Its foot is that dirt too: on High
    // Dirt Cliff's own dirt it joins, the squares where the maps have none.
    {1, "Higher Dirt Cliff (High Dirt Cliff)", 4, 2, 7,
     {{2051, {230, 232}}, {2053, {234, 236}}, {2055, {244, 246, 248}, 8}, {2058, {244, 246, 248}, 8}, {2061, {238, 240, 242}, 8},
      {2064, {250, 252, 254}, 2, 0}, {2064, {256, 258, 260}, 2, 4}, {2067, {262, 264}, 3, 0}, {2067, {266, 268}, 3, 6}},
     1786, 1787, 0, 2, "High Dirt Cliff (Dirt)"},
    {5, "High Cooled Lava (High Dirt)", 9, 2, 5,
     {{2844, {228, 230}}, {2846, {232, 234}}, {2848, {236, 238}}, {2850, {240, 242}}, {2852, {244, 246}}, {2854, {248, 250}},
      {2856, {252, 254}}, {2858, {256, 258}}},
     2842, 2843, 0},
    // Compoundcliff-dirt: compound cliffs on dirt (named, its inner corners the other way round, seen on the map; W and E blocks share groups,
    // one in subtiles 0-7, the other in 8-15); stand-in High Dirt (Desert's Dirt/High Dirt cliff from Ash
    // World's), its roof the game's Compound.
    {5, "Compound (Dirt)", 2, 1, 4,
     {{1740, {38, 36}, 0, 0}, {1740, {42, 40}, 0, 8}, {1742, {44, 46, 48}, 0, 8}, {1742, {50, 52, 54}, 0, 0}, {1745, {56, 58, 60}, 0, 0},
      {1745, {62, 64, 66}, 0, 8}, {1748, {68, 70}, 0, 8}, {1748, {72, 74}, 0, 0}},
     0, 0, 8},
    // Jungle: Templecliff-Dirt, temple cliffs on dirt (named, its inner corners the other way round, seen on the map; W
    // and E blocks share groups), stand-in High Dirt (Jungle's
    // Dirt/High Dirt cliff as Desert's), its roof the game's Temple.
    {4, "Temple (Dirt)", 2, 1, 4,
     {{1578, {38, 36}, 0, 0}, {1578, {42, 40}, 0, 8}, {1580, {44, 46, 48}, 0, 8}, {1580, {50, 52, 54}, 0, 0}, {1583, {56, 58, 60}, 0, 0},
      {1583, {62, 64, 66}, 0, 8}, {1586, {68, 70}, 0, 8}, {1586, {72, 74}, 0, 0}},
     0, 0, 8},
    // Ice: Compound-snow, outpost cliffs on snow (named), stand-in High Snow (Ice's Snow/High Snow cliff as Desert's
    // Dirt/High Dirt), its roof the game's Outpost.
    {6, "Outpost (Snow)", 2, 1, 4,
     {{1617, {38, 36}, 0, 0}, {1617, {42, 40}, 0, 8}, {1619, {44, 46, 48}, 0, 0}, {1619, {50, 52, 54}, 0, 8}, {1622, {56, 58, 60}, 0, 0},
      {1622, {62, 64, 66}, 0, 8}, {1625, {68, 70}, 0, 8}, {1625, {72, 74}, 0, 0}},
     0, 0, 8},
    // Ice: Asphalt-Snow-1/2, asphalt on Snow and High Snow, made as Desert's Asphalt on Dirt / High Dirt (stand-ins Rocky
    // Snow and High Dirt: the borders with that layout); blocks in their groups' order.
    {6, "Asphalt (Snow)", 6, 1, 5,
     {{784, {276, 278}}, {786, {280, 282}}, {788, {284, 286}}, {790, {288, 290}}, {792, {292, 294}}, {794, {296, 298}}},
     860, 862, 0},
    {6, "High Asphalt (High Snow)", 9, 2, 5,
     {{808, {228, 230}}, {810, {232, 234}}, {812, {236, 238}}, {814, {240, 242}}, {816, {244, 246}}, {818, {248, 250}},
      {820, {252, 254}}, {822, {256, 258}}},
     860, 862, 0},
    // Mud-Dirt1/2 and Mud-Grass1/2: the mod's mud on Ice's Dirt, High Dirt, Grass and High Grass, made like the game's High
    // Snow/High Dirt edge (228-258, blocks in their groups' order, as Twilight's Mud); the ground is High Snow in the template.
    {6, "Mud (Dirt)", 9, 4, 6,
     {{732, {228, 230}}, {734, {232, 234}}, {736, {236, 238}}, {738, {240, 242}}, {740, {244, 246}}, {742, {248, 250}}, {744, {252, 254}}, {746, {256, 258}}},
     756, 757, 0, 2},
    {6, "High Mud (High Dirt)", 9, 9, 6,
     {{758, {228, 230}}, {760, {232, 234}}, {762, {236, 238}}, {764, {240, 242}}, {766, {244, 246}}, {768, {248, 250}}, {770, {252, 254}}, {772, {256, 258}}},
     782, 783, 0, 2},
    {6, "Mud (Grass)", 9, 7, 6,
     {{2191, {228, 230}}, {2193, {232, 234}}, {2195, {236, 238}}, {2197, {240, 242}}, {2199, {244, 246}}, {2201, {248, 250}}, {2203, {252, 254}}, {2205, {256, 258}}},
     756, 757, 0, 2},
    {6, "High Mud (High Grass)", 9, 10, 6,
     {{2216, {228, 230}}, {2218, {232, 234}}, {2220, {236, 238}}, {2222, {240, 242}}, {2224, {244, 246}}, {2226, {248, 250}}, {2228, {252, 254}}, {2230, {256, 258}}},
     782, 783, 0, 2},
    // Compound-snow2-cliff and Compound-cliff-compound2: Outpost on High Snow and on Outpost, made as Outpost on Snow
    // (Compound-snow: the W blocks in subtiles 0-8, the E ones from 10). (Basilica-snowcliff / -hsnowcliff: no floor
    // group of its own found in Ice, not done.) Asphalt's fill: 860 and 862 (single textures; 858 is dirt).
    {6, "High Outpost (High Snow)", 2, 2, 4,
     {{1718, {38, 36}, 5, 0}, {1718, {42, 40}, 3, 10}, {1720, {44, 46, 48}, 5, 0}, {1720, {50, 52, 54}, 3, 10}, {1723, {56, 58, 60}, 5, 0},
      {1723, {62, 64, 66}, 3, 10}, {1726, {68, 70}, 4, 8}, {1726, {72, 74}, 4, 0}},
     0, 0, 8, 1},
    {6, "Outpost (Outpost)", 2, 1, 4,
     {{1588, {38, 36}, 5, 0}, {1588, {42, 40}, 3, 10}, {1590, {44, 46, 48}, 5, 0}, {1590, {50, 52, 54}, 3, 10}, {1593, {56, 58, 60}, 5, 0},
      {1593, {62, 64, 66}, 3, 10}, {1596, {68, 70}, 4, 8}, {1596, {72, 74}, 4, 0}},
     0, 0, 8, 1, "Outpost (Snow)"},
    // Outpost-Coastal-Cliff-1: Outpost walls standing in water, made as Outpost on Snow (the game's cliff 36-74, blocks in
    // their brushes' order); Water is Snow in the template.
    {6, "Outpost (Water)", 2, 5, 4,
     {{2338, {38, 36}}, {2340, {42, 40}}, {2345, {44, 46, 48}}, {2342, {50, 52, 54}}, {2348, {56, 58, 60}}, {2351, {62, 64, 66}},
      {2356, {72, 74}}, {2354, {68, 70}}},
     0, 0, 8, 1},
    // Twilight: Mud-dirt-2, the mod's mud on high dirt (its lighter ground is High Dirt), made like Ash World's
    // Dirt/Lava; its stand-in High Crushed Rock (Twilight's High Dirt border: its rects as Desert's, Ash World's + 36).
    // Its blocks unnamed: in their groups' order (as the named sets' groups go: W north, E north, E south, W south, W
    // outer, E outer, E inner, W inner). Its fill: 1830/1831.
    {7, "High Mud (High Dirt)", 9, 2, 6,
     {{1832, {228, 230}}, {1834, {232, 234}}, {1836, {236, 238}}, {1838, {240, 242}}, {1840, {244, 246}}, {1842, {248, 250}},
      {1844, {252, 254}}, {1846, {256, 258}}},
     1830, 1831, 0},
    // Jungle, Desert, Ice and Twilight share the game's Dirt/High Dirt cliff (groups 36-74: W north 38/36, E north 42/40,
    // W south 44-48, E south 50-54, W outer 56-60, E outer 62-66, E inner 68/70, W inner 72/74, as Temple on Dirt shows).
    // Third heights: Highdirt-Higherdirtcliff (Jungle), Dirtcliff-Dirt2-Dirt3 (Desert), Highsnow-Highersnow-Cliff (Ice),
    // Cliff-dirt-2 (Twilight; Cliff-dirt-3 and Ice's Highsnow-Highersno-Cliff: the same walls in another look, more
    // variants), their blocks in their brushes' order; on top the set's own ground (its main block: the
    // mod draws the third level lighter), in Ice High Snow's tiles.
    {5, "Higher Dirt (High Dirt)", 2, 2, 8,
     {{1594, {38, 36}}, {1596, {42, 40}}, {1598, {44, 46, 48}}, {1601, {50, 52, 54}}, {1604, {56, 58, 60}}, {1607, {62, 64, 66}},
      {1610, {72, 74}, 6}},
     1592, 1593, 0, 1},
    // On Higher Dirt: AsphaltEdge-Dirt3, Driedmud-dirt-3 and Sanddunes-Dirt-3, made as on High Dirt (blocks in their groups'
    // order); Higher Dirt is High Dirt in the template.
    {5, "Higher Asphalt (Higher Dirt)", 9, 2, 5,
     {{676, {228, 230}}, {678, {232, 234}}, {680, {236, 238}}, {682, {240, 242}}, {684, {244, 246}}, {686, {248, 250}}, {688, {252, 254}}, {690, {256, 258}}},
     700, 701, 0, 0, "Higher Dirt (High Dirt)"},
    {5, "Higher Dried Mud (Higher Dirt)", 9, 2, 5,
     {{702, {228, 230}}, {704, {232, 234}}, {706, {236, 238}}, {708, {240, 242}}, {710, {244, 246}}, {712, {248, 250}}, {714, {252, 254}}, {716, {256, 258}}},
     0, 0, 13, 0, "Higher Dirt (High Dirt)"},
    {5, "Higher Sand Dunes (Higher Dirt)", 9, 2, 6,
     {{839, {228, 230}}, {841, {232, 234}}, {843, {236, 238}}, {845, {240, 242}}, {847, {244, 246}}, {849, {248, 250}}, {851, {252, 254}}, {853, {256, 258}}},
     864, 865, 0, 0, "Higher Dirt (High Dirt)"},
    // (Ice's Higher Snow: its fill the mod's lighter third-level snow 1556/1557, as the maps have it beside its walls)
    {6, "Higher Snow (High Snow)", 2, 2, 6,
     {{1433, {38, 36}}, {1435, {42, 40}}, {1437, {44, 46, 48}}, {1440, {50, 52, 54}}, {1443, {56, 58, 60}}, {1446, {62, 64, 66}},
      {1473, {38, 36}, 5, 2}, {1475, {42, 40}, 6}, {1477, {44, 46, 48}, 6}, {1480, {50, 52, 54}, 6}, {1483, {56, 58, 60}, 6},
      {1486, {62, 64, 66}, 6}, {1489, {72, 74}, 6},
      {1449, {72, 74}}},
     1556, 1557, 0, 1},
    // Water-Snow-Cliff (Ice) and Water-Hdirt (Jungle): water below a high cliff, the water as the mod terrain (stand-in the
    // cliff's low ground, ground its high one, made like the game's cliff 36-74); the game's Water as fill. Both outer
    // corners and both inner corners in one group pair each (W first, E after; Jungle's outer corners the other way round,
    // seen on the map). Ice's is below a Snow cliff (Snow is High Dirt in the template).
    {6, "Water (Snow cliffs)", 1, 1, 6,
     {{1747, {38, 36}, 5}, {1749, {42, 40}, 5}, {1754, {44, 46, 48}}, {1751, {50, 52, 54}}, {1760, {56, 58, 60}, 3}, {1757, {62, 64, 66}, 3},
      {1763, {72, 74}, 2, 0}, {1763, {68, 70}, 2, 4}},
     0, 0, 5, 2},
    // Compound-cliff-grass1: Outpost walls on Grass (Alpha Prey, Secluded Reliquary), made as Outpost on High Snow (W blocks
    // in subtiles 0-6, E ones from 8; the inner corners 2048 subtiles 8 and 0); Grass is Snow in the template.
    {6, "Outpost (Grass)", 2, 7, 4,
     {{2040, {38, 36}, 4, 0}, {2040, {42, 40}, 4, 8}, {2042, {44, 46, 48}, 4, 0}, {2042, {50, 52, 54}, 4, 8}, {2045, {56, 58, 60}, 4, 0},
      {2045, {62, 64, 66}, 4, 8}, {2048, {68, 70}, 4, 8}, {2048, {72, 74}, 4, 0}},
     0, 0, 8, 1},
    // Compound-cliff-grass2: the same on High Grass (its groups 29 on; High Grass is Snow in the template, as High Snow is).
    {6, "High Outpost (High Grass)", 2, 10, 4,
     {{2069, {38, 36}, 4, 0}, {2069, {42, 40}, 4, 8}, {2071, {44, 46, 48}, 4, 0}, {2071, {50, 52, 54}, 4, 8}, {2074, {56, 58, 60}, 4, 0},
      {2074, {62, 64, 66}, 4, 8}, {2077, {68, 70}, 4, 8}, {2077, {72, 74}, 4, 0}},
     0, 0, 8, 1},
    // Outpost-Submerged-Cliff-Lower-1: a sunken outpost floor under Water (2336/2337; Wellington, Chains of Shambala), its
    // edges flat: made like the game's High Snow/High Dirt edge (228-258, blocks in their groups' order, as Mud on Dirt);
    // Water is High Snow in the template.
    {6, "Submerged Outpost (Water)", 9, 5, 4,
     {{2406, {228, 230}}, {2408, {232, 234}}, {2410, {236, 238}}, {2412, {240, 242}}, {2414, {244, 246}}, {2416, {248, 250}},
      {2418, {252, 254}}, {2420, {256, 258}}},
     2336, 2337, 0, 2},
    // Chainlink-outpost-2: a chain-link mesh floor (2462/2463) on Outpost (Mouth of Bathala, Chains of
    // Shambala, Wellington), its edges flat: made like the game's High Snow/High Dirt edge (228-258, as Mud on Dirt;
    // its south blocks the other way round, its corner pairs too: seen painted); Outpost is High Snow in the template.
    {6, "Mesh (Outpost)", 9, 8, 8,
     {{2464, {228, 230}}, {2466, {232, 234}}, {2470, {236, 238}}, {2468, {240, 242}}, {2476, {244, 246}}, {2478, {248, 250}},
      {2472, {252, 254}}, {2474, {256, 258}}},
     2462, 2463, 0, 2},
    // Chainlink-outpost-3: the mesh floor (2686/2687) on High Outpost (The Initiate), made as Mesh on Outpost.
    {6, "High Mesh (High Outpost)", 9, 12, 8,
     {{2669, {228, 230}}, {2671, {232, 234}}, {2675, {236, 238}}, {2673, {240, 242}}, {2681, {244, 246}}, {2683, {248, 250}},
      {2677, {252, 254}}, {2679, {256, 258}}},
     2686, 2687, 0, 2},
    // Dirt-Cliff-Snow-1: snow at the foot of High Dirt cliffs (Ozone: a hollow of snow in High Dirt), made as Water at
    // High Dirt cliffs (blocks in their brushes'
    // order: W north 2607, E north 2609, W south 2611, E south 2614 (seen painted), W outer 2617, E outer 2620, inner 2623 and 2625);
    // the game's Snow below.
    {6, "Snow (High Dirt cliffs)", 1, 9, 6,
     {{2607, {38, 36}}, {2609, {42, 40}}, {2611, {44, 46, 48}}, {2614, {50, 52, 54}}, {2617, {56, 58, 60}}, {2620, {62, 64, 66}},
      {2623, {72, 74}}, {2625, {68, 70}}},
     0, 0, 1, 2},
    // Outpost-Chainlink-Cliff-2: High Outpost on a cliff above the chain-link mesh floor (Mouth of Bathala), on the
    // game's Snow/High Snow cliff (36-74), Mesh (Outpost) its ground; each block W (subtiles 0-6) and E (8-14) in one group;
    // High Outpost's tiles on top.
    {6, "High Outpost (Mesh)", 2, 8, 4,
     {{2480, {38, 36}, 4, 0}, {2480, {42, 40}, 4, 8}, {2482, {44, 46, 48}, 4, 0}, {2482, {50, 52, 54}, 4, 8}, {2485, {56, 58, 60}, 4, 0},
      {2485, {62, 64, 66}, 4, 8}, {2488, {68, 70}, 4, 8}, {2488, {72, 74}, 4, 0}},
     0, 0, 12, 1, "Mesh (Outpost)"},
    // Moguls-highsnow: moguls on High Snow (Dead Ringer), on the game's High Snow/High Dirt edge (228-258, blocks
    // in their groups' order); its own moguls 1716/1717 inside (as the maps have them).
    {6, "High Moguls (High Snow)", 9, 2, 5,
     {{1690, {228, 230}}, {1692, {232, 234}}, {1694, {236, 238}}, {1696, {240, 242}}, {1698, {244, 246}}, {1700, {248, 250}},
      {1702, {252, 254}}, {1704, {256, 258}}},
     1716, 1717, 0, 2},
    // Moguls-highersnow: the same moguls on Higher Snow (Autistic Years), its groups 24 before; its moguls 1714/1715.
    {6, "Higher Moguls (Higher Snow)", 9, 2, 5,
     {{1666, {228, 230}}, {1668, {232, 234}}, {1670, {236, 238}}, {1672, {240, 242}}, {1674, {244, 246}}, {1676, {248, 250}},
      {1678, {252, 254}}, {1680, {256, 258}}},
     1714, 1715, 0, 2, "Higher Snow (High Snow)"},
    // Dirt-cliff-dirt1: dirt at the foot of High Dirt cliffs (Qinngua Valley), made as Snow (High Dirt cliffs) (blocks in
    // their brushes' order; the two inner corners in one block, the W one in its first subtiles); the game's Dirt below.
    {6, "Dirt (High Dirt cliffs)", 1, 9, 6,
     {{2133, {38, 36}}, {2135, {42, 40}}, {2137, {44, 46, 48}}, {2140, {50, 52, 54}}, {2143, {56, 58, 60}}, {2146, {62, 64, 66}},
      {2149, {72, 74}, 4, 0}, {2149, {68, 70}, 2, 8}},
     0, 0, 4, 2},
    // Outpost-Chainlink-3: an outpost a level above High Outpost on a cliff above the mesh floor (The Initiate), made as
    // High Outpost (Mesh) with Mesh (High Outpost) its ground (blocks in their brushes' order, each W in its first five
    // subtiles, E in the last three; the inner corners as High Outpost (Mesh)'s); High Outpost's tiles on top.
    {6, "Higher Outpost (Mesh)", 2, 12, 8,
     {{2733, {38, 36}, 5, 0}, {2733, {42, 40}, 3, 10}, {2735, {44, 46, 48}, 5, 0}, {2735, {50, 52, 54}, 3, 10}, {2738, {56, 58, 60}, 5, 0},
      {2738, {62, 64, 66}, 3, 10}, {2741, {68, 70}, 4, 8}, {2741, {72, 74}, 4, 0}},
     0, 0, 12, 1, "High Mesh (High Outpost)"},
    // Basilica-hsnowcliff: a basilica a level above High Snow, its walls a low rim (Agrath's Peak), made as Basilica
    // (Flagstones) in Twilight: its W north and W south first variants only (the others do not line up), the E ones in
    // the last subtiles; its floor 2038/2039 subtile 1, as on the maps. High Snow is Snow in the template.
    {6, "High Basilica (High Snow)", 2, 2, 8,
     {{1921, {38, 36}, 1, 0}, {1921, {42, 40}, 3, 10}, {1923, {44, 46, 48}, 1, 0}, {1923, {50, 52, 54}, 3, 10},
      {1926, {56, 58, 60}, 5, 0}, {1926, {62, 64, 66}, 3, 10}, {1929, {68, 70}, 4, 8}, {1929, {72, 74}, 4, 0}},
     2038, 2039, 0, 1, nullptr, 0x2},
    // Basilica-snowcliff: the same on Snow (Eclipsed), its blocks the same way.
    {6, "Basilica (Snow)", 2, 1, 8,
     {{1892, {38, 36}, 1, 0}, {1892, {42, 40}, 3, 10}, {1894, {44, 46, 48}, 1, 0}, {1894, {50, 52, 54}, 3, 10},
      {1897, {56, 58, 60}, 5, 0}, {1897, {62, 64, 66}, 3, 10}, {1900, {68, 70}, 4, 8}, {1900, {72, 74}, 4, 0}},
     2038, 2039, 0, 0, nullptr, 0x2},
    // Basilica-hhsnowcliff: the same on Higher Snow (Autistic Years), its blocks the same way.
    {6, "Higher Basilica (Higher Snow)", 2, 2, 8,
     {{1950, {38, 36}, 1, 0}, {1950, {42, 40}, 3, 10}, {1952, {44, 46, 48}, 1, 0}, {1952, {50, 52, 54}, 3, 10},
      {1955, {56, 58, 60}, 5, 0}, {1955, {62, 64, 66}, 3, 10}, {1958, {68, 70}, 4, 8}, {1958, {72, 74}, 4, 0}},
     2038, 2039, 0, 1, "Higher Snow (High Snow)", 0x2},
    {7, "Higher Dirt (High Dirt)", 2, 2, 8,
     {{1678, {38, 36}}, {1680, {42, 40}}, {1682, {44, 46, 48}}, {1685, {50, 52, 54}}, {1688, {56, 58, 60}}, {1691, {62, 64, 66}},
      {1774, {38, 36}}, {1776, {42, 40}}, {1778, {44, 46, 48}}, {1781, {50, 52, 54}}, {1784, {56, 58, 60}}, {1787, {62, 64, 66}},
      {1882, {72, 74}, 6}, {1884, {68, 70}, 6}},
     1676, 1677, 0, 1},
    // Twilight on Higher Dirt: Crushedrock-dirt3 and Mud-dirt-3, the High Dirt sets' layout (Mud on High Dirt's: the
    // game's High Dirt/High Crushed Rock edge, blocks in their groups' order); Higher Dirt is High Dirt in the template.
    {7, "Higher Crushed Rock (Higher Dirt)", 9, 2, 5,
     {{1988, {228, 230}}, {1990, {232, 234}}, {1992, {236, 238}}, {1994, {240, 242}}, {1996, {244, 246}}, {1998, {248, 250}},
      {2000, {252, 254}}, {2002, {256, 258}}},
     1986, 1987, 0, 2, "Higher Dirt (High Dirt)"},
    {7, "Higher Mud (Higher Dirt)", 9, 2, 6,
     {{1858, {228, 230}}, {1860, {232, 234}}, {1862, {236, 238}}, {1864, {240, 242}}, {1866, {244, 246}}, {1868, {248, 250}},
      {1870, {252, 254}}, {1872, {256, 258}}},
     1856, 1857, 0, 2, "Higher Dirt (High Dirt)"},
    // Crushedrock-basilica-2: crushed rock on High Basilica, made as Crushed Rock on Higher Dirt (blocks in their groups'
    // order); High Basilica is High Dirt in the template.
    {7, "High Crushed Rock (High Basilica)", 9, 12, 5,
     {{2298, {228, 230}}, {2300, {232, 234}}, {2302, {236, 238}}, {2304, {240, 242}}, {2306, {244, 246}}, {2308, {248, 250}}, {2310, {252, 254}}, {2312, {256, 258}},
      {2076, {228, 230}}, {2078, {232, 234}}, {2080, {236, 238}}, {2082, {240, 242}}, {2084, {244, 246}}, {2086, {248, 250}}, {2088, {252, 254}},
      {2090, {256, 258}}},
     1986, 1987, 0, 2},
    // Cliff-Dirt-Crushedrock-1/2: dirt cliffs onto Crushed Rock (below High Dirt) and High Crushed Rock (below Higher
    // Dirt), the ground below as the mod terrain (stand-in Dirt); blocks in their brushes' order.
    {7, "Crushed Rock (High Dirt cliffs)", 1, 2, 8,
     {{2172, {38, 36}}, {2174, {42, 40}}, {2176, {44, 46, 48}}, {2179, {50, 52, 54}}, {2182, {56, 58, 60}}, {2185, {62, 64, 66}},
      {2188, {72, 74}, 6}},
     0, 0, 4},
    {7, "High Crushed Rock (Higher Dirt cliffs)", 1, 2, 8,
     {{2229, {38, 36}}, {2231, {42, 40}}, {2233, {44, 46, 48}}, {2236, {50, 52, 54}}, {2239, {56, 58, 60}}, {2242, {62, 64, 66}},
      {2245, {72, 74}, 6}},
     0, 0, 9, 2, "Higher Dirt (High Dirt)"},
    // Basilicacliff1-flagstones: Basilica on Flagstones, made as Compound on Dirt (named blocks, the W ones first in their
    // groups, the E ones after; its W north and W south other variants do not line up: their first only, seen on the
    // map); Flagstones is Dirt in the template. Basilicacliff2-flagstones and Cliff-basilica-flagstones-3: the same walls
    // in other looks, their blocks the same way.
    {7, "Basilica (Flagstones)", 2, 7, 5,
     {{1514, {38, 36}, 1, 0}, {1514, {42, 40}, 3, 10}, {1516, {44, 46, 48}, 1, 0}, {1516, {50, 52, 54}, 3, 10}, {1519, {56, 58, 60}, 5, 0},
      {1519, {62, 64, 66}, 3, 10}, {1522, {68, 70}, 4, 8}, {1522, {72, 74}, 4, 0},
      {1532, {38, 36}, 1, 0}, {1532, {42, 40}, 3, 10}, {1534, {44, 46, 48}, 1, 0}, {1534, {50, 52, 54}, 3, 10}, {1537, {56, 58, 60}, 5, 0},
      {1537, {62, 64, 66}, 3, 10}, {1540, {68, 70}, 4, 8}, {1540, {72, 74}, 4, 0},
      {1945, {38, 36}, 1, 0}, {1945, {42, 40}, 3, 10}, {1947, {44, 46, 48}, 1, 0}, {1947, {50, 52, 54}, 3, 10}, {1950, {56, 58, 60}, 5, 0},
      {1950, {62, 64, 66}, 3, 10}, {1953, {68, 70}, 4, 8}, {1953, {72, 74}, 4, 0}},
     0, 0, 8, 1},
    // Chasm-1-cliff: a chasm, black, at the foot of Dirt cliffs (Lunatic Fringe, The Benefactor), made as the game's
    // Dirt/Water cliff (108-138: the maps put its blocks there) with the chasm for Water; its floor the null tile, black,
    // as on the maps (the set's own floor rect, 2338 subtiles 12/13, is dark water).
    {7, "Chasm (Dirt)", 3, 1, 8,
     {{2322, {110, 108}}, {2324, {114, 112}}, {2326, {116, 118, 120}}, {2329, {122, 124, 126}}, {2332, {128, 130, 132}}, {2335, {134, 136, 138}},
      {2338, {140, 142}, 3, 0}, {2338, {144, 146}, 3, 6}, {2340, {140, 142}, 3, 0}, {2340, {144, 146}, 3, 6}},
     0, 0, kNullFill},
    // Coast-artificial-crushedrock: a sea wall between Water and Crushed Rock (Excise), made as the game's Dirt/Water
    // cliff, as Chasm (Dirt); each block both ways (the maps: its W walls in the first subtiles, its E walls after); its
    // water the game's; Crushed Rock is Dirt in the template.
    // (Flagstones-dirt3: flagstone patches with ragged edges and no isometric shapes, as the game's Flagstones-dirt1/2;
    // its brushes are in the Doodads palette.)
    {7, "Sea Wall (Crushed Rock)", 3, 4, 8,
     {{2100, {110, 108}, 4, 0}, {2100, {114, 112}, 4, 8}, {2102, {116, 118, 120}, 5, 0}, {2102, {122, 124, 126}, 3, 10},
      {2105, {128, 130, 132}, 3, 0}, {2105, {134, 136, 138}, 4, 8}, {2108, {144, 146}, 4, 0}, {2108, {140, 142}, 4, 8}},
     0, 0, 3, 1},
    // Cliff-basilica-3 and -2 (another look) and Cliff-basilica-crushedrock-3: a basilica a level above High Basilica
    // (Omnikin, Terminus) and Crushed Rock (Quasar Quartet), made as Basilica (Flagstones) (the W north and W south first
    // variants only); High Basilica's tiles on the first, Basilica's on the last. Their grounds are Dirt in the template.
    {7, "High Basilica (High Basilica)", 2, 12, 5,
     {{2035, {38, 36}, 1, 0}, {2035, {42, 40}, 3, 10}, {2037, {44, 46, 48}, 1, 0}, {2037, {50, 52, 54}, 3, 10},
      {2040, {56, 58, 60}, 5, 0}, {2040, {62, 64, 66}, 3, 10}, {2043, {68, 70}, 4, 8}, {2043, {72, 74}, 4, 0},
      {1733, {38, 36}, 1, 0}, {1733, {42, 40}, 3, 10}, {1735, {44, 46, 48}, 1, 0}, {1735, {50, 52, 54}, 3, 10},
      {1738, {56, 58, 60}, 5, 0}, {1738, {62, 64, 66}, 3, 10}, {1741, {68, 70}, 4, 8}, {1741, {72, 74}, 4, 0}},
     0, 0, 12, 1},
    {7, "Basilica (Crushed Rock)", 2, 4, 5,
     {{1904, {38, 36}, 1, 0}, {1904, {42, 40}, 3, 10}, {1906, {44, 46, 48}, 1, 0}, {1906, {50, 52, 54}, 3, 10},
      {1909, {56, 58, 60}, 5, 0}, {1909, {62, 64, 66}, 3, 10}, {1912, {68, 70}, 4, 8}, {1912, {72, 74}, 4, 0}},
     0, 0, 8, 1},
    // Templecliff-Dirt2/-Temple and Compoundcliff-highdirt/Compound-cliff-Compound: Temple and Compound on High Dirt and
    // on themselves, made as on Dirt (the same blocks' layout, other groups); their ground is Dirt in the template.
    {4, "High Temple (High Dirt)", 2, 2, 4,
     {{1634, {38, 36}, 0, 0}, {1634, {42, 40}, 0, 8}, {1636, {44, 46, 48}, 0, 8}, {1636, {50, 52, 54}, 0, 0}, {1639, {56, 58, 60}, 0, 0},
      {1639, {62, 64, 66}, 0, 8}, {1642, {68, 70}, 2, 8}, {1642, {72, 74}, 2, 0}},
     0, 0, 8, 1},
    {4, "Temple (Temple)", 2, 1, 4,
     {{1652, {38, 36}, 0, 0}, {1652, {42, 40}, 0, 8}, {1654, {44, 46, 48}, 0, 8}, {1654, {50, 52, 54}, 0, 0}, {1657, {56, 58, 60}, 0, 0},
      {1657, {62, 64, 66}, 0, 8}, {1660, {68, 70}, 2, 8}, {1660, {72, 74}, 2, 0}},
     0, 0, 8, 1, "Temple (Dirt)"},
    // Templecliff-Ruins: Temple on Ruins, made as Temple on Dirt; Ruins is Dirt in the template.
    {4, "Temple (Ruins)", 2, 7, 4,
     {{1598, {38, 36}, 0, 0}, {1598, {42, 40}, 0, 8}, {1600, {44, 46, 48}, 0, 8}, {1600, {50, 52, 54}, 0, 0}, {1603, {56, 58, 60}, 0, 0},
      {1603, {62, 64, 66}, 0, 8}, {1606, {68, 70}, 2, 8}, {1606, {72, 74}, 2, 0}},
     0, 0, 8, 1},
    // Dirt-Asphaltedge and Highdirt-Asphaltedge: asphalt on Dirt and High Dirt, made as Desert's (named blocks in the same
    // order as its groups); stand-ins Rocky Ground and High Jungle. Fill: 2047 and 2324 (single textures).
    {4, "Asphalt (Dirt)", 6, 1, 5,
     {{2021, {276, 278}}, {2023, {280, 282}}, {2025, {284, 286}}, {2027, {288, 290}}, {2029, {292, 294}}, {2031, {296, 298}}},
     2047, 2324, 0},
    {4, "High Asphalt (High Dirt)", 9, 2, 5,
     {{2300, {228, 230}}, {2302, {232, 234}}, {2304, {236, 238}}, {2306, {240, 242}}, {2308, {244, 246}}, {2310, {248, 250}},
      {2312, {252, 254}}, {2314, {256, 258}}},
     2047, 2324, 0},
    // Grass-Dirt1/2: the mod's grass on Dirt and High Dirt, made like the game's Jungle/Dirt edge (180-210: W north 180, E north
    // 184, W south 188, E south 192, W outer 196, E outer 200, W inner 204, E inner 208) and High Jungle/High Dirt edge (228-258,
    // as Asphalt on High Dirt; its south blocks the other way round, seen on the map); both outer corners in one group pair (W
    // in subtiles 0-4, E in 6-10); each with its main fill.
    {4, "Grass (Dirt)", 4, 1, 5,
     {{2052, {180, 182}}, {2054, {184, 186}}, {2056, {188, 190}}, {2058, {192, 194}}, {2060, {196, 198}, 3, 0}, {2060, {200, 202}, 3, 6},
      {2062, {204, 206}, 3}, {2064, {208, 210}, 3}},
     2082, 2083, 0},
    {4, "High Grass (High Dirt)", 9, 2, 5,
     {{2861, {228, 230}}, {2863, {232, 234}}, {2865, {236, 238}}, {2867, {240, 242}}, {2869, {244, 246}, 3, 0}, {2869, {248, 250}, 3, 6},
      {2871, {252, 254}, 3}, {2873, {256, 258}, 3}},
     2890, 2891, 0},
    // Mud-Grass1 (and -2, -3: other looks, the same layout): mud on that grass, made as Asphalt on Dirt (stand-in Rocky Ground; blocks in their groups' order); the
    // grass is Dirt in the template; the game's Mud as fill.
    {4, "Mud (Grass)", 6, 1, 5,
     {{3554, {276, 278}}, {3556, {280, 282}}, {3558, {284, 286}}, {3560, {288, 290}}, {3562, {292, 294}}, {3564, {296, 298}},
      {3566, {276, 278}}, {3568, {280, 282}}, {3570, {284, 286}}, {3572, {288, 290}}, {3574, {292, 294}}, {3576, {296, 298}},
      {3578, {276, 278}}, {3580, {280, 282}}, {3582, {284, 286}}, {3584, {288, 290}}, {3586, {292, 294}}, {3588, {296, 298}}},
     0, 0, 13, 1, "Grass (Dirt)"},
    {4, "Water (High Dirt cliffs)", 1, 2, 6,
     {{3889, {38, 36}, 5}, {3891, {42, 40}, 5}, {3893, {44, 46, 48}}, {3896, {50, 52, 54}}, {3899, {56, 58, 60}, 2, 4},
      {3899, {62, 64, 66}, 2, 0}, {3902, {72, 74}, 3, 0}, {3902, {68, 70}, 3, 6}},
     0, 0, 3},
    // Dirtcliff-Jungle-cliff: Jungle at the foot of High Dirt cliffs, made as Water at High Dirt cliffs (the maps put its
    // blocks on the game's cliff 36-66: W south 2570 on 44-48, E south 2576 and 2573 on 50-54); its outer corners in 2579
    // (W subtiles 0-4, E 6); its inner corners are the game's own (68-74).
    {4, "Jungle (High Dirt cliffs)", 1, 2, 5,
     {{2566, {38, 36}}, {2568, {42, 40}}, {2570, {44, 46, 48}, 8}, {2576, {50, 52, 54}, 8}, {2573, {50, 52, 54}, 8}, {2579, {56, 58, 60}, 3, 0},
      {2579, {62, 64, 66}, 1, 6}},
     0, 0, 4},
    // Cliff-RaisedJungle-Dirt-2: a dirt cliff with Raised Jungle at its foot (The Underwood, Abiogenesis), made as the game's
    // Dirt/High Dirt cliff (36-74) with Raised Jungle for its Dirt; W south's variants also in 3950 (subtiles 0-6), E south's
    // in 3950 (8-14), the outer corners in 3956 (W 0-4, E 6), the inner ones in 3735 (W 0-4, E 6-10); its dirt 3672/3673.
    // Cliff-RaisedJungle-Dirt-3 (the same walls, another look; Abiogenesis): more variants of its edges.
    {4, "Dirt (Raised Jungle)", 2, 5, 7,
     {{3943, {38, 36}}, {3945, {42, 40}}, {3947, {44, 46, 48}, 8}, {3950, {44, 46, 48}, 4, 0}, {3953, {50, 52, 54}, 8}, {3950, {50, 52, 54}, 4, 8}, {3956, {56, 58, 60}, 3, 0}, {3956, {62, 64, 66}, 1, 6},
      {4013, {38, 36}}, {4015, {42, 40}}, {4017, {44, 46, 48}, 8}, {4020, {44, 46, 48}, 4, 0}, {4023, {50, 52, 54}, 8}, {4020, {50, 52, 54}, 4, 8},
      {3735, {72, 74}, 3, 0}, {3735, {68, 70}, 3, 6}},
     3672, 3673, 0, 1},
    // Mud-Jungle (and -Jungle2) and Mud-Rjungle-2 (and -3; other looks, the same layout): the game's Mud on Jungle and on Raised Jungle, made as Mud on Grass (the game's
    // Dirt/Mud edge, 276-298, blocks in their groups' order; the ground is Dirt in the template).
    {4, "Mud (Jungle)", 6, 4, 5,
     {{3450, {276, 278}}, {3452, {280, 282}}, {3454, {284, 286}}, {3456, {288, 290}}, {3458, {292, 294}}, {3460, {296, 298}},
      {3474, {276, 278}}, {3476, {280, 282}}, {3478, {284, 286}}, {3480, {288, 290}}, {3482, {292, 294}}, {3484, {296, 298}}},
     0, 0, 13, 1},
    {4, "Mud (Raised Jungle)", 6, 5, 5,
     {{3462, {276, 278}}, {3464, {280, 282}}, {3466, {284, 286}}, {3468, {288, 290}}, {3470, {292, 294}}, {3472, {296, 298}},
      {3486, {276, 278}}, {3488, {280, 282}}, {3490, {284, 286}}, {3492, {288, 290}}, {3494, {292, 294}}, {3496, {296, 298}}},
     0, 0, 13, 1},
    // Mud-Dirt-2-High: mud on High Dirt (the maps put its blocks on the game's High Dirt/High Jungle edge, 228-250: the
    // Dirt/Mud edge's layout a level up); its own mud 2466/2467.
    {4, "High Mud (High Dirt)", 9, 2, 5,
     {{2453, {228, 230}}, {2455, {232, 234}}, {2457, {236, 238}}, {2459, {240, 242}}, {2461, {244, 246}}, {2463, {248, 250}}},
     2466, 2467, 0},
    // Asphalt-Structureedge: a structure on a cliff above Asphalt on Dirt (Wall Street War), made as the game's Dirt/High
    // Dirt cliff (36-74: the maps put its blocks there), Asphalt on Dirt its ground; the roof 2152/2153 on top.
    {4, "Structure (Asphalt)", 2, 1, 8,
     {{2121, {38, 36}}, {2123, {42, 40}}, {2131, {44, 46, 48}}, {2125, {50, 52, 54}}, {2134, {56, 58, 60}}, {2137, {62, 64, 66}},
      {3624, {72, 74}, 1}, {3626, {68, 70}, 1}},
     2152, 2153, 0, 1, "Asphalt (Dirt)"},
    // Structure-cliff-structure2: a structure a level higher on Structure (Asphalt), the same layout as Asphalt-
    // Structureedge (its walls on no map; its roof 2298/2299 is). Structure-cliff-structure3: its walls in another look.
    {4, "Structure (Structure)", 2, 2, 8,
     {{2898, {38, 36}}, {2900, {42, 40}}, {2908, {44, 46, 48}}, {2902, {50, 52, 54}}, {2911, {56, 58, 60}}, {2914, {62, 64, 66}},
      {3628, {72, 74}, 1}, {3630, {68, 70}, 1},
      {2929, {38, 36}}, {2931, {42, 40}}, {2939, {44, 46, 48}}, {2933, {50, 52, 54}}, {2942, {56, 58, 60}}, {2945, {62, 64, 66}}},
     2298, 2299, 0, 1, "Structure (Asphalt)"},
    // Rjungle-Rjungle cliff: a ledge of Raised Jungle on Raised Jungle (Abiogenesis), made on the game's Jungle/Raised
    // Jungle edge (360-382; its north and south strips the other way round from the maps' rects: seen painted), Raised
    // Jungle its ground (Jungle in the template). RaisedJungle-cliff-RaisedJungle-3: the same ledge in another look, its
    // blocks in the same brush order.
    {4, "Raised Jungle (Raised Jungle)", 5, 5, 4,
     {{3836, {368, 370}}, {3834, {372, 374}}, {3832, {360, 362}}, {3830, {364, 366}}, {3838, {376, 378}, 3}, {3840, {380, 382}, 3},
      {2332, {368, 370}}, {2330, {372, 374}}, {2328, {360, 362}}, {2326, {364, 366}}, {2334, {376, 378}, 3}, {2336, {380, 382}, 3}},
     0, 0, 5, 4},
    // Temple-Rjungle-cliff-2: Temple on a cliff above Raised Jungle (Vestal Shrine), made as Temple (Ruins) (W and E blocks
    // in one group each: north 3402, south 3404, outer 3407, inner 3410); Raised Jungle is Dirt in the template.
    {4, "Temple (Raised Jungle)", 2, 5, 4,
     {{3402, {38, 36}, 4, 0}, {3402, {42, 40}, 4, 8}, {3404, {44, 46, 48}, 4, 8}, {3404, {50, 52, 54}, 4, 0}, {3407, {56, 58, 60}, 4, 0},
      {3407, {62, 64, 66}, 4, 8}, {3410, {68, 70}, 4, 0}, {3410, {72, 74}, 4, 8}},
     0, 0, 8, 1},
    // Higherdirt-Higherdirtcliff: Jungle's Higher Dirt, on a cliff above the lighter dirt of Dirt (Raised Jungle) (its walls'
    // foot that dirt; 300, Pasta Masta), made as Desert's Higher
    // Dirt (named blocks; W south's variants also in 3726 subtiles 0-6, E south's in 3726 8-14; outer corners 3732 W 0-2,
    // E 4-6; inner 3735 W 0-4, E 6-10); the lighter dirt 3672/3673 on top (as the walls' top).
    {4, "Higher Dirt (Dirt)", 2, 2, 7,
     {{3719, {38, 36}}, {3721, {42, 40}}, {3723, {44, 46, 48}, 8}, {3726, {44, 46, 48}, 4, 0}, {3729, {50, 52, 54}, 8}, {3726, {50, 52, 54}, 4, 8},
      {3732, {56, 58, 60}, 2, 0}, {3732, {62, 64, 66}, 2, 4}, {3735, {72, 74}, 3, 0}, {3735, {68, 70}, 3, 6}},
     3672, 3673, 0, 1, "Dirt (Raised Jungle)"},
    // Grass-Dirt3: grass on Higher Dirt (Dirt) (Pasta Masta), made as Mud (High Dirt) (blocks in their groups' order on
    // the game's High Dirt/High Jungle edge); its grass 1156/1157.
    {4, "Higher Grass (Higher Dirt)", 9, 2, 5,
     {{3222, {228, 230}}, {3224, {232, 234}}, {3226, {236, 238}}, {3228, {240, 242}}, {3232, {244, 246}}, {3230, {248, 250}}},
     1156, 1157, 0, 2, "Higher Dirt (Dirt)"},
    // (RaisedJungle-Edge-Dirt-2: square patches of grass on dirt, straight edges and their corners on the maps: not an
    // isometric border.)
    // Asphalt-edge-jungle1: asphalt on Jungle, on the game's Dirt/Mud edge (576-606: the maps put its blocks there, in
    // their groups' order) with the asphalt for Mud; Asphalt (Dirt)'s fill. Jungle is Dirt in the template.
    {4, "Asphalt (Jungle)", 13, 4, 5,
     {{2441, {576, 578}}, {2443, {580, 582}}, {2445, {584, 586}}, {2447, {588, 590}}, {2449, {592, 594}}, {2525, {596, 598}},
      {2527, {600, 602}}, {2529, {604, 606}}},
     2047, 2324, 0, 1},
    // Asphalt-edge-grass1 and -grass2: asphalt on Grass (Dirt) (Wall Street War), as Asphalt (Jungle), and on Grass (High
    // Dirt) (Pasta Masta), on the game's High Dirt/High Jungle edge (228-258) as Asphalt (High Dirt); blocks in their
    // groups' order. The grounds are Dirt and High Dirt in the template.
    {4, "Asphalt (Grass)", 13, 1, 5,
     {{1861, {576, 578}}, {1863, {580, 582}}, {1865, {584, 586}}, {1867, {588, 590}}, {1869, {592, 594}}, {1871, {596, 598}},
      {1873, {600, 602}}, {1875, {604, 606}}},
     2047, 2324, 0, 1, "Grass (Dirt)"},
    {4, "High Asphalt (High Grass)", 9, 2, 5,
     {{1885, {228, 230}}, {1887, {232, 234}}, {1889, {236, 238}}, {1891, {240, 242}}, {1893, {244, 246}}, {1895, {248, 250}},
      {1897, {252, 254}}, {1899, {256, 258}}},
     2047, 2324, 0, 2, "High Grass (High Dirt)"},
    // Mud-Dirt-3-Highest: mud on Higher Dirt, made as Mud (High Dirt) (blocks in their groups' order; its corner blocks two
    // variants); its mud 2466/2467. Higher Dirt is High Dirt in the template.
    {4, "Higher Mud (Higher Dirt)", 9, 2, 5,
     {{2484, {228, 230}}, {2486, {232, 234}}, {2488, {236, 238}}, {2490, {240, 242}}, {2492, {244, 246}, 2}, {2494, {248, 250}, 2}},
     2466, 2467, 0, 2, "Higher Dirt (Dirt)"},
    // Structure-cliff-grass1: a structure on a cliff above Grass (Dirt), as Structure (Asphalt) (its blocks in its
    // brushes' order, the inner corners the same); the roof 2152/2153.
    {4, "Structure (Grass)", 2, 1, 8,
     {{1909, {38, 36}}, {1911, {42, 40}}, {1919, {44, 46, 48}}, {1913, {50, 52, 54}}, {1922, {56, 58, 60}}, {1925, {62, 64, 66}},
      {3624, {72, 74}, 1}, {3626, {68, 70}, 1}},
     2152, 2153, 0, 1, "Grass (Dirt)"},
    // Higherdirt-Asphaltedge: asphalt on Higher Dirt, as Asphalt (High Dirt) (blocks in their groups' order); its fill.
    // Higher Dirt is High Dirt in the template.
    {4, "Higher Asphalt (Higher Dirt)", 9, 2, 5,
     {{1837, {228, 230}}, {1839, {232, 234}}, {1841, {236, 238}}, {1843, {240, 242}}, {1845, {244, 246}}, {1847, {248, 250}},
      {1849, {252, 254}}, {1851, {256, 258}}},
     2047, 2324, 0, 2, "Higher Dirt (Dirt)"},
    // Water-HHdirt: water at the foot of Higher Dirt cliffs, Water (High Dirt cliffs)'s layout 27 groups on (its outer and
    // inner corners both ways in one group pair); the game's Water below. Higher Dirt is High Dirt in the template.
    {4, "Water (Higher Dirt cliffs)", 1, 2, 6,
     {{3916, {38, 36}, 5}, {3918, {42, 40}, 5}, {3920, {44, 46, 48}}, {3923, {50, 52, 54}}, {3926, {56, 58, 60}, 2, 4},
      {3926, {62, 64, 66}, 2, 0}, {3929, {72, 74}, 3, 0}, {3929, {68, 70}, 3, 6}},
     0, 0, 3, 2, "Higher Dirt (Dirt)"},
    // Asphalt-Coast: a dock rim between Water and Asphalt (Dirt) (Wall Street War), made as the game's Dirt/Water cliff
    // (108-146), as Twilight's Sea Wall (named blocks; the set has no E outer and no inner corners: the game's there); the
    // game's Water inside. Asphalt is Dirt in the template.
    {4, "Water (Asphalt)", 3, 1, 8,
     {{3817, {110, 108}, 7, 0}, {3819, {114, 112}, 5, 0}, {3821, {116, 118, 120}, 1, 0}, {3824, {122, 124, 126}, 1, 0},
      {3821, {128, 130, 132}, 2, 10}},
     0, 0, 3, 1, "Asphalt (Dirt)"},
    // Highdirt-Higherdirtcliff: a cliff a level above the game's High Dirt, made as Higher Dirt (Dirt) (the same layout,
    // its groups from 3632); High Dirt is Dirt in the template, the game's High Dirt on top.
    {4, "Higher Dirt 2 (High Dirt)", 2, 2, 7,
     {{3632, {38, 36}}, {3634, {42, 40}}, {3636, {44, 46, 48}, 8}, {3639, {44, 46, 48}, 4, 0}, {3642, {50, 52, 54}, 8}, {3639, {50, 52, 54}, 4, 8},
      {3645, {56, 58, 60}, 2, 0}, {3645, {62, 64, 66}, 2, 4}, {3648, {72, 74}, 3, 0}, {3648, {68, 70}, 3, 6}},
     0, 0, 0, 1},
    {5, "High Compound (High Dirt)", 2, 2, 4,
     {{1698, {38, 36}, 0, 0}, {1698, {42, 40}, 0, 8}, {1700, {44, 46, 48}, 0, 8}, {1700, {50, 52, 54}, 0, 0}, {1703, {56, 58, 60}, 0, 0},
      {1703, {62, 64, 66}, 0, 8}, {1706, {68, 70}, 0, 8}, {1706, {72, 74}, 0, 0}},
     0, 0, 8, 1},
    {5, "Compound (Compound)", 2, 1, 4,
     {{2421, {38, 36}, 0, 0}, {2421, {42, 40}, 0, 8}, {2423, {44, 46, 48}, 0, 8}, {2423, {50, 52, 54}, 0, 0}, {2426, {56, 58, 60}, 0, 0},
      {2426, {62, 64, 66}, 0, 8}, {2429, {68, 70}, 0, 8}, {2429, {72, 74}, 0, 0}},
     0, 0, 8, 1, "Compound (Dirt)"},
    // Dirtcliff-2-Sanddunes: Higher Dirt mesas on High Sand Dunes (Three Hong Kong, Germination), made as Higher Dirt (the
    // jungle layout's cliff 36-74; High Sand Dunes read as Dirt): W north 2245, E north 2247, W south 2249, E south 2252,
    // W outer 2255, E outer 2258 (8 variants each), the inner corners 2261 (W subtiles 0-4, E 6-10); Higher Dirt's fill.
    {5, "Higher Dirt (High Sand Dunes)", 2, 9, 8,
     {{2245, {38, 36}}, {2247, {42, 40}}, {2249, {44, 46, 48}}, {2252, {50, 52, 54}}, {2255, {56, 58, 60}}, {2258, {62, 64, 66}},
      {2261, {72, 74}, 3, 0}, {2261, {68, 70}, 3, 6}},
     1592, 1593, 0, 1},
    // Dirtcliff-3-higherdirt: a cliff of Higher Dirt with Higher Dirt at its foot (Three Hong Kong: its walls' foot is the
    // lighter dirt), made as Higher Dirt on High Sand Dunes (the
    // same layout: W north 1626, E north 1628, W south 1630, E south 1633, W outer 1636, E outer 1639, 8 variants each; the
    // inner corners 1642, W subtiles 0-4, E 6-10); Higher Dirt's fill.
    {5, "Higher Dirt Cliff (Higher Dirt)", 2, 2, 8,
     {{1626, {38, 36}}, {1628, {42, 40}}, {1630, {44, 46, 48}}, {1633, {50, 52, 54}}, {1636, {56, 58, 60}}, {1639, {62, 64, 66}},
      {1642, {72, 74}, 3, 0}, {1642, {68, 70}, 3, 6}},
     1592, 1593, 0, 1, "Higher Dirt (High Dirt)"},
    // Dirtcliff-1-Sanddunes: High Dirt standing on Sand Dunes (named blocks: the E ones' variants in their groups from the
    // right), made as the game's Dirt/High Dirt cliff with Sand Dunes for its Dirt; High Dirt's own tiles on top.
    {5, "High Dirt (Sand Dunes)", 2, 4, 8,
     {{2179, {38, 36}}, {2181, {42, 40}}, {2183, {44, 46, 48}}, {2186, {50, 52, 54}}, {2189, {56, 58, 60}}, {2192, {62, 64, 66}},
      {2195, {72, 74}, 4, 0}, {2195, {68, 70}, 2, 8}},
     0, 0, 2, 1},
    // Tar-Cliff-Sanddunes-1 and -2 (another look, 32 groups on; Blood Drought): tar below High Sand Dunes' cliffs (Tectonic Kingdom), made as Water at High Dirt cliffs
    // (the same layout: W north 2479, E north 2481, W south 2483, E south 2486, W outer 2489, E outer 2492, 8 variants
    // each; the inner corners 2495, E subtiles 0-4, W 6-10: seen painted); the game's Tar below, High Sand Dunes above.
    {5, "Tar (Sand Dunes cliffs)", 1, 9, 8,
     {{2479, {38, 36}}, {2481, {42, 40}}, {2483, {44, 46, 48}}, {2486, {50, 52, 54}}, {2489, {56, 58, 60}}, {2492, {62, 64, 66}},
      {2495, {68, 70}, 3, 0}, {2495, {72, 74}, 3, 6},
      {2511, {38, 36}}, {2513, {42, 40}}, {2515, {44, 46, 48}}, {2518, {50, 52, 54}}, {2521, {56, 58, 60}}, {2524, {62, 64, 66}},
      {2527, {68, 70}, 3, 0}, {2527, {72, 74}, 3, 6}},
     0, 0, 3, 2},
    // Structurecliff-asphalt and Structurecliff-structure: a structure on a cliff above Asphalt (Dirt), and a level higher
    // on that structure, as Jungle's Structure (Asphalt) and Structure (Structure) (blocks in their brushes' order; the
    // second has no inner corners of its own: the first one's); the roof 2114/2115 (beside the inner corners).
    {5, "Structure (Asphalt)", 2, 1, 8,
     {{1815, {38, 36}}, {1817, {42, 40}}, {1825, {44, 46, 48}}, {1819, {50, 52, 54}}, {1828, {56, 58, 60}}, {1831, {62, 64, 66}},
      {2110, {72, 74}, 1}, {2112, {68, 70}, 1}},
     2114, 2115, 0, 1, "Asphalt (Dirt)"},
    {5, "Structure (Structure)", 2, 2, 8,
     {{1932, {38, 36}}, {1934, {42, 40}}, {1942, {44, 46, 48}}, {1936, {50, 52, 54}}, {1945, {56, 58, 60}}, {1948, {62, 64, 66}},
      {2110, {72, 74}, 1}, {2112, {68, 70}, 1}},
     2114, 2115, 0, 1, "Structure (Asphalt)"},
    // Driedmud-sunkenpit-1: dried mud on Sandy Sunken Pit, on the game's Dirt/Dried Mud edge (576-606), as Jungle's Asphalt
    // (Jungle) (blocks in their groups' order); the game's Dried Mud inside. Sandy Sunken Pit is Dirt in the template.
    {5, "Dried Mud (Sandy Sunken Pit)", 13, 5, 5,
     {{2317, {576, 578}}, {2319, {580, 582}}, {2321, {584, 586}}, {2323, {588, 590}}, {2325, {592, 594}}, {2327, {596, 598}},
      {2329, {600, 602}}, {2331, {604, 606}}},
     0, 0, 13, 1},
};

void addModTerrains(Tileset& t) {
    std::vector<std::array<uint16_t, 2>> terrains;
    // A set's pieces: its blocks' rows for the template's rects, and for the template border's rects that the set has no
    // block for (where its edge meets another border) the null tile: the gap shows, for the mod's makers to fill (no
    // other piece in its place).
    int* emptyBlocks = nullptr;  // (the set's count of blocks that gave no pieces)
    size_t* setPieces = nullptr;  // (how many of the pieces are its set's own, the first ones; the others its fill)
    auto blockPieces = [&](std::vector<std::array<uint16_t, 3>>& pieces, const std::vector<ModTerrainBlock>& blocks, uint16_t variants, uint16_t ta,
                           uint16_t tb, std::vector<uint16_t>& gaps) {
        auto add = [&](uint16_t game, uint16_t l, uint16_t r) {
            if (t.megatileFor(l) > 0 && t.megatileFor(r) > 0) pieces.push_back({game, l, r});
        };
        for (const ModTerrainBlock& b : blocks) {
            const size_t before = pieces.size();
            for (uint16_t row = 0; row < 3 && b.game[row]; ++row)
                for (uint16_t v = 0; v < (b.variants ? b.variants : variants); ++v) {
                    const uint16_t g = uint16_t(row && b.lowerMod >= 0 ? b.lowerMod + row - 1 : b.mod + row);
                    const uint16_t k = uint16_t((row && b.lowerSub >= 0 ? b.lowerSub : b.sub) + 2 * v);
                    if (k + 1 < 16) add(b.game[row], uint16_t(g << 4 | k), uint16_t(g << 4 | (k + 1)));
                }
            if (pieces.size() == before && emptyBlocks) ++*emptyBlocks;
        }
        if (setPieces) *setPieces = pieces.size();
        std::set<uint16_t> covered;
        for (const auto& p : pieces) covered.insert(p[0]);
        const IsomTiles* tiles = IsomTiles::forTileset(t);
        for (size_t g = 0; g + 1 < terrains.size() && tiles; ++g) {
            if (covered.count(uint16_t(g)) || !terrains[g][1] || tiles->rectsOf(uint16_t(g)).empty()) continue;
            if (std::minmax(terrains[g][0], terrains[g][1]) != std::minmax(ta, tb)) continue;
            gaps.push_back(uint16_t(g));
        }
    };
    // (the gaps' null tiles (group 0's subtiles 0 and 1), the last pieces: ModTerrain::gapPieces)
    auto addGaps = [](Tileset::ModTerrain& m, const std::vector<uint16_t>& gaps) {
        for (uint16_t g : gaps) m.pieces.push_back({g, 0, 1});
        m.gapPieces = gaps.size();
    };
    // (a terrain's id: its row's place among its tileset's rows, also when a row before it is not made: saved maps keep
    // the ids, so a new row goes after the last of its tileset; cmbw_test checks the ids)
    uint16_t index = 0;
    for (const ModTerrainSet& s : kModTerrainSets) {
        if (s.era != (t.era & 7)) continue;
        if (terrains.empty()) terrains = tileGroupTerrains(t);
        Tileset::ModTerrain m;
        m.id = uint16_t(Tileset::kModTerrainIds + index++), m.standIn = s.standIn, m.ground = s.ground, m.name = s.name, m.draft = true; m.fillKey = s.fillLeft ? uint32_t(s.fillLeft) << 16 | s.fillRight : 0xFFFF0000u | (s.fillTerrain ? s.fillTerrain : s.standIn);  // (no fill: the stand-in's own tiles inside)
        m.fill = s.fillLeft || s.fillTerrain;
        m.groundTemplate = s.groundTemplate;
        if (s.groundMod) {  // (a mod terrain as its ground)
            const Tileset::ModTerrain* g = nullptr;
            for (const Tileset::ModTerrain& o : t.modTerrains)
                if (o.name == s.groundMod) g = &o;
            if (!g) continue;
            m.ground = g->id;
            if (!m.groundTemplate) m.groundTemplate = g->standIn;
        }
        auto add = [&](uint16_t game, uint16_t l, uint16_t r) {
            if (t.megatileFor(l) > 0 && t.megatileFor(r) > 0) m.pieces.push_back({game, l, r});
        };
        emptyBlocks = &m.emptyBlocks, setPieces = &m.setPieces;
        std::vector<uint16_t> gaps;
        blockPieces(m.pieces, s.blocks, s.variants, s.standIn, m.groundTemplate ? m.groundTemplate : m.ground, gaps);
        emptyBlocks = nullptr, setPieces = nullptr;
        const IsomTiles* tiles = IsomTiles::forTileset(t);
        // The fill: the stand-in's solid rects get the set's fill groups (their first 6 subtiles, the others carry
        // markings; or the set's fillSubtiles), or a game terrain's solid rects (each with its variants, the rare ones rarely).
        std::vector<std::pair<uint16_t, uint16_t>> fills;
        if (s.fillTerrain == kNullFill) {  // (the null tile, black: a chasm's floor, as the maps have it)
            fills.push_back({0, 1});
        } else if (s.fillLeft) {
            for (uint16_t k = 0; k < 16; ++k)
                if ((s.fillSubtiles >> k & 1) && s.fillRight < t.groups.size() && t.groups[s.fillLeft].megatiles[k] && t.groups[s.fillRight].megatiles[k])
                    fills.push_back({uint16_t(s.fillLeft << 4 | k), uint16_t(s.fillRight << 4 | k)});
        } else if (tiles) {
            for (size_t g = 0; g + 1 < terrains.size(); ++g)
                if (terrains[g][0] == s.fillTerrain && !terrains[g][1] && !tiles->rectsOf(uint16_t(g)).empty())
                    for (uint16_t k = 0; k < 16; ++k)  // (the first variants 4 times as often: the later ones are the rare tiles)
                        if (t.groups[g].megatiles[k] && t.groups[g + 1].megatiles[k])
                            for (int n = 0; n < (k < 4 ? 4 : 1); ++n) fills.push_back({uint16_t(g << 4 | k), uint16_t((g + 1) << 4 | k)});
        }
        // Each of the game's solid groups of the stand-in (a rect's left tile is looked up); not the mod's own groups that
        // its maps show on that terrain (Tileset::groupTerrains: other mod terrains' pieces and fills among them).
        std::set<uint16_t> learned;
        for (const auto& gt : t.groupTerrains) learned.insert(gt[0]);
        for (size_t g = 0; g < terrains.size(); ++g)
            if (terrains[g][0] == s.standIn && !terrains[g][1] && !learned.count(uint16_t(g)))
                for (const auto& [l, r] : fills)
                    if (s.fillTerrain == kNullFill) m.pieces.push_back({uint16_t(g), l, r});  // (megatile 0: black)
                    else add(uint16_t(g), l, r);
        addGaps(m, gaps);
        t.modTerrains.push_back(std::move(m));
    }
    // The sets made from the maps, after the hand-made rows (their ids go on from those).
    std::set<std::string> names;
    for (const auto& m : t.modTerrains) names.insert(m.name);
    for (const AutoTerrain& a : kAutoTerrains) {
        if (a.era != (t.era & 7)) continue;
        if (terrains.empty()) terrains = tileGroupTerrains(t);
        Tileset::ModTerrain m;
        m.id = uint16_t(Tileset::kModTerrainIds + index++), m.standIn = a.standIn, m.ground = a.ground, m.name = a.name, m.fromMaps = true, m.draft = true;
        while (names.count(m.name)) m.name += " 2";  // (a name a hand-made row has)
        names.insert(m.name);
        for (const AutoPiece& p : a.pieces)
            if (t.megatileFor(p.left) > 0 && t.megatileFor(p.right) > 0) m.pieces.push_back({p.game, p.left, p.right});
        // The border's rects the maps show no piece for: the null tile (blockPieces with no blocks).
        m.setPieces = m.pieces.size();
        std::vector<uint16_t> gaps;
        blockPieces(m.pieces, {}, 0, a.standIn, a.ground, gaps);
        // The stand-in's solid rects the maps show no piece for: the pieces of one they do (its fill), else its own tiles.
        std::vector<std::pair<uint16_t, uint16_t>> fills;
        std::set<uint16_t> covered;
        for (const auto& p : m.pieces) {
            covered.insert(p[0]);
            if (p[0] < terrains.size() && terrains[p[0]][0] == a.standIn && !terrains[p[0]][1]) fills.push_back({p[1], p[2]});
        }
        m.fill = !fills.empty();
        for (size_t g = 0; g < terrains.size() && !fills.empty(); ++g)
            if (terrains[g][0] == a.standIn && !terrains[g][1] && !covered.count(uint16_t(g)))
                for (const auto& [l, r] : fills) m.pieces.push_back({uint16_t(g), l, r});
        addGaps(m, gaps);
        if (!m.pieces.empty()) t.modTerrains.push_back(std::move(m));
    }
    // Variants: a terrain on another ground with its ground's tiles inside, where the template does not let it stand as on a
    // twin (another game terrain there: Floor (Asphalt) on Asphalt (Grass), whose asphalt is High Grass in the template):
    // its set again with that ground, read as its own ground's template terrain. Not in the palette: the brush takes the
    // one for the ground under it (Editor::brushTerrain). After all the others (saved maps keep the ids before them).
    const IsomTerrainTable* game = IsomTerrainTable::forEra(t.era);
    auto fillOf = [&](uint16_t id) -> uint32_t {
        if (const Tileset::ModTerrain* m = t.modTerrain(id)) return m->fillKey;
        return 0xFFFF0000u | id;
    };
    auto on = [&](uint16_t a, uint16_t b) {  // (a stands on b, maybe a level or more above it)
        for (int n = 0; n < 16; ++n) {
            const Tileset::ModTerrain* m = t.modTerrain(a);
            if (!m) return false;
            if ((a = m->ground) == b) return true;
        }
        return false;
    };
    // (two grounds the editor makes twins: one fill, stand-in and template ground; a terrain on one stands on the other)
    auto twins = [&](uint16_t a, uint16_t b) {
        const Tileset::ModTerrain *x = t.modTerrain(a), *y = t.modTerrain(b);
        return a == b || (x && y && x->fillKey && x->fillKey == y->fillKey && x->standIn == y->standIn &&
                          (x->groundTemplate ? x->groundTemplate : x->ground) == (y->groundTemplate ? y->groundTemplate : y->ground));
    };
    auto nameOf = [&](uint16_t id) -> std::string {  // (a game terrain's as a mod's tab shows it: "Structure (Asphalt)")
        if (const Tileset::ModTerrain* m = t.modTerrain(id)) return m->name;
        if (!game) return "";
        uint16_t root = 1;  // (the tileset's main terrain: the one with the most borders)
        int most = -1;
        for (uint16_t a = 1; a <= game->terrainCount(); ++a) {
            int n = 0;
            for (uint16_t b = 1; b <= game->terrainCount(); ++b) n += a != b && game->linked(a, b);
            if (n > most) most = n, root = a;
        }
        const std::vector<uint16_t> way = game->path(root, id);
        const std::string own = game->terrainName(id);
        return way.size() >= 2 ? own + " (" + game->terrainName(way[way.size() - 2]) + ")" : own;
    };
    const size_t made = t.modTerrains.size();
    uint16_t variantIndex = 0;
    for (bool more = true; more && game;) {
        more = false;
        for (size_t i = 0; i < made; ++i) {
            const Tileset::ModTerrain s = t.modTerrains[i];
            const uint32_t key = fillOf(s.ground);
            if (!key || s.pieces.empty()) continue;
            const Tileset::ModTerrain* g = t.modTerrain(s.ground);
            const uint16_t gStand = g ? g->standIn : s.ground, gTempl = g ? (g->groundTemplate ? g->groundTemplate : g->ground) : 0;
            std::vector<uint16_t> grounds;
            for (uint16_t id = 1; id < Tileset::kModTerrainIds; ++id)
                if (game->isTerrain(id)) grounds.push_back(id);
            for (const Tileset::ModTerrain& o : t.modTerrains) grounds.push_back(o.id);
            for (uint16_t id : grounds) {
                if (id == s.ground || id == s.id || fillOf(id) != key || on(id, s.id) || on(s.id, id)) continue;
                const Tileset::ModTerrain* o = t.modTerrain(id);
                if (o && o->standIn == gStand && (!g || (o->groundTemplate ? o->groundTemplate : o->ground) == gTempl)) continue;  // (a twin)
                // (the variants that do not paint: Structure (Structure) on Structure (Halfsphalt), a wall on a wall that the
                // template cannot layer; its strokes change corners far around them, cmbw_stroke_bench)
                static const std::set<std::tuple<int, std::string, std::string>> kNoVariant = {{0, "Structure (Structure)", "Structure (Halfsphalt)"}};
                if (kNoVariant.count({t.era & 7, s.name, nameOf(id)})) continue;
                if (std::any_of(t.modTerrains.begin(), t.modTerrains.end(),
                                [&](const Tileset::ModTerrain& x) { return twins(x.ground, id) && !x.pieces.empty() && x.pieces[0] == s.pieces[0]; }))
                    continue;  // (its pieces on that ground or a twin of it: a row of its own, or a variant made already)
                if (Tileset::kModVariantIds + variantIndex >= Tileset::kModTerrainIds + IsomTerrainTable::kExtraSlots) break;
                Tileset::ModTerrain v = s;
                v.id = uint16_t(Tileset::kModVariantIds + variantIndex++), v.ground = id, v.groundTemplate = s.groundTemplate ? s.groundTemplate : s.ground;
                v.variantOf = s.id;
                v.name = s.name.substr(0, s.name.find(" (")) + " (" + nameOf(id) + ")";
                while (std::any_of(t.modTerrains.begin(), t.modTerrains.end(), [&](const Tileset::ModTerrain& x) { return x.name == v.name; }))
                    v.name += " 2";
                t.modTerrains.push_back(std::move(v));
                more = true;
            }
        }
    }
}

// The terrain rules files over the built-in rows: the one the mod ships (<install>\mpq	errains.sdterrain), then the
// temporary override (useTerrainOverride). Each replaces terrains by id or adds new ones (terrainrules::apply).
std::filesystem::path gTerrainOverride;
std::mutex gTerrainOverrideLock;  // (a tileset can load on a worker thread)
thread_local bool tSkipOverride = false;  // (skipTerrainOverride)
thread_local bool tSkipRepaired = false;  // (skipRepairedRules)
void applyTerrainFiles(Tileset& t) {
    std::filesystem::path override;
    if (!tSkipOverride) {
        std::lock_guard<std::mutex> lock(gTerrainOverrideLock);
        override = gTerrainOverride;
    }
    for (const std::filesystem::path& f : {findInstall().empty() ? std::filesystem::path() : findInstall() / "mpq" / "terrains.sdterrain", override}) {
        std::error_code ec;
        if (f.empty() || !std::filesystem::exists(f, ec)) continue;
        terrainrules::Rules r;
        std::string err;
        if (terrainrules::load(f, r, err)) terrainrules::apply(t, r);
    }
}

void loadTileset(Tileset& t) {
    for (const GroupTerrain& g : kGroupTerrains)
        if (g.era == (t.era & 7)) t.groupTerrains.push_back({g.group, g.terrain, g.other});
    for (const GroupPiece& p : kGroupPieces)
        if (p.era == (t.era & 7) && t.megatileFor(p.left) >= 0 && t.megatileFor(p.right) >= 0) t.groupPieces.push_back({p.left, p.right, p.game});
    addModTerrains(t);
    if (!tSkipRepaired) {  // The rows repaired (tools/plugins/cmbw/probes/cmbw_draft_fix.cpp, cmbw_repaired_rules_*.inc: one file per
        // channel of the mod, their tilesets differ): every template rect present (the ones a row had none for NULL: no
        // borrowing of the template's tiles, a game tile in a mod border hides a height decision), stray variants dropped,
        // mirrored roles' tiles put right; the rows stay drafts. A tileset whose fingerprint differs (the other channel, or
        // the mod changed its groups) takes neither. The repair probe skips them (skipRepairedRules): it starts from the rows.
        static const uint8_t kRepairedPrerelease[] = {
#include "cmbw_repaired_rules_prerelease.inc"
        };
        static const uint8_t kRepairedRelease[] = {
#include "cmbw_repaired_rules_release.inc"
        };
        for (const auto& [bytes, size] : {std::make_pair(kRepairedPrerelease, sizeof kRepairedPrerelease), std::make_pair(kRepairedRelease, sizeof kRepairedRelease)}) {
            terrainrules::Rules r;
            std::string err;
            if (terrainrules::read(std::vector<uint8_t>(bytes, bytes + size), r, err) && terrainrules::apply(t, r)) break;
        }
    }
    applyTerrainFiles(t);
    forEachPieceNeighbor(t.era & 7, [&t](uint16_t a, uint16_t b, int place) {
        t.pieceNeighbors.insert(Tileset::pieceNeighborKey(a, b, place));
        t.pieceNeighborTiles.insert(a), t.pieceNeighborTiles.insert(b);
    });
}

// The mod's player colors (Cosmonarchy-GPTP BW/enumerations.h, player_color_names): a map's COLR byte is the row of
// the mod's game\tunit.pcx (256 rows of 8) the game colors that player's units with; these have names.
const std::vector<std::string>& colorNames() {
    static const std::vector<std::string> names = {
        "Red", "Blue", "Teal", "Purple", "Orange", "Brown", "White", "Yellow", "Green", "Lemon", "Peach",
        "Royal Blue", "Asparagus", "Deep Periwinkle", "Periwinkle", "Cyan", "Black", "Russian Green", "Alice Blue",
        "Harlequin", "Saffron", "Maya Blue", "Dark Olive", "Light Olive", "Bronze", "Tyrian Purple", "Ultramarine",
        "Celadon", "Maroon", "Fern Green", "Pumpkin", "Vermillion", "Magma", "Umber", "Electric Blue",
        "Eggshell Blue", "Palatinate Purple", "Jet Black", "Goldenrod", "Blood", "Silver Pink", "Dim Grey", "Grey",
        "Coral", "Salmon", "Vivid Red", "Glacial", "Silver", "Platinum", "Aero", "Vine Green", "Cosmic Cobalt",
        "Dark Green", "Turquoise", "Rainbow", "Arylide Yellow", "Mint", "Smoky Black", "Snow", "Saddle Brown",
        "Zaffre", "Yinmn Blue", "Electric Orange", "Brass", "Scarlet", "Emerald", "Sienna", "Cornflower Blue",
        "Mango", "Chestnut", "Neon Blue", "Dark Sapphire", "Mahogany", "Wine", "Auburn", "Cordovan", "Sunglow",
        "Magenta", "Vivid Orange", "Dark Bronze", "Burnt Umber", "Iridescent Yellow", "Iridescent Green",
        "Iridescent Blue", "Iridescent Violet", "Rainbow New", "Redwood", "Beaver", "Liver", "Delft Blue",
        "Dark Coral", "Persian Orange", "Tomato", "Bistre Brown", "Beige", "Tea Green", "Earthtone",
        "Japanese Violet", "Dark Spring Green", "Golden Brown", "B'dazzled Blue", "Coyote Brown", "Midnight Green",
        "Patriotic Freedom", "Palatinate Blue", "Dark Lavender", "Forest Green", "Zinnwaldite Brown", "Neon Carrot",
        "Aquamarine", "Metzenic Prozac"};
    return names;
}

const UnitDataLoader kLoader{&loadTypes, &loadSprites, &loadSettings, &loadTileset, &colorNames, &crigRenameLocation};

}  // namespace

bool unitRanges(const GameData& data, uint16_t unit, UnitRanges& out) {
    // (the tables of each GameData, read once)
    struct Tables {
        Datex u, w;
        bool ok = false;
    };
    thread_local std::map<const GameData*, Tables> cache;  // (each thread its own)
    Tables& t = cache[&data];
    if (!t.ok) {
        std::string err;
        t.ok = readDatex(data, "units", kUnitFields, t.u, err) && readDatex(data, "weapons", kWeaponFields, t.w, err);
        if (!t.ok) return false;
    }
    if (unit >= t.u.count() || unit == 228) return false;
    auto range = [&](uint32_t w) { return w != 130 && w < t.w.count() ? int(t.w.get(WMaxRange, w)) : 0; };  // (130: none)
    out = {};
    uint32_t g = t.u.get(UGroundWeapon, unit), a = t.u.get(UAirWeapon, unit);
    const uint32_t turret = t.u.get(USubunit1, unit);
    if (g == 130 && a == 130 && turret != 228 && turret < t.u.count()) g = t.u.get(UGroundWeapon, turret), a = t.u.get(UAirWeapon, turret);
    out.ground = range(g), out.air = range(a), out.seek = int(t.u.get(UTargetRange, unit)), out.sight = int(t.u.get(USight, unit));
    out.flags = t.u.get(UFlags, unit), out.flyer = (out.flags & 0x4) != 0;
    // Cosmonarchy-GPTP can_use_cliff_advantage and unit_id_is_trap: these get no height advantage.
    static const std::set<uint16_t> kNone = {32, 413, 620, 598, 148, 256, 373, 371, 374, 409, 180, 589, 564, 567, 375, 13, 507};
    out.cliff = !out.flyer && !kNone.count(unit);
    return true;
}

int tileHeight(const Tileset& ts, uint16_t value) {
    const int m = ts.megatileFor(value);
    if (m < 0 || size_t(m) >= ts.minitileFlags.size()) return 0;
    int sum = 0;
    for (uint16_t f : ts.minitileFlags[size_t(m)])
        if (f & 1) sum += ((f >> 1) & 3) + int(int8_t(f >> 8));
    const int h = (sum + (sum < 0 ? -8 : 8)) / 16;
    return std::min(h, 127);
}

bool looksLikeModMap(const fs::path& map) {
    std::error_code ec;
    const fs::path install = findInstall();
    if (!install.empty()) {
        const fs::path root = fs::weakly_canonical(install.parent_path(), ec), file = fs::weakly_canonical(map, ec);
        const auto r = root.native(), f = file.native();
        if (!r.empty() && f.size() > r.size() && std::equal(r.begin(), r.end(), f.begin(), [](auto a, auto b) { return std::towlower(a) == std::towlower(b); }))
            return true;
    }
    Bytes raw;
    std::string err;
    if (!readMapFile(map, raw, err)) return false;
    for (const Unit& u : readUnits(Chk::parse(raw)))
        if (u.type >= 228) return true;
    return false;
}

namespace {

// The one file of an SCMDraft brush archive (.scmdbrh: an MPQ without a listfile, so StormLib names it FileNNNNNNNN).
bool readBrushArchive(const fs::path& file, Bytes& out) {
    HANDLE a = nullptr;
    if (!SFileOpenArchive(nativePath(file).c_str(), 0, MPQ_OPEN_READ_ONLY | MPQ_OPEN_FORCE_MPQ_V1, &a)) return false;
    bool ok = false;
    SFILE_FIND_DATA fd;
    if (HANDLE find = SFileFindFirstFile(a, "*", &fd, nullptr)) {
        HANDLE f = nullptr;
        if (SFileOpenFileEx(a, fd.cFileName, SFILE_OPEN_FROM_MPQ, &f)) {
            DWORD high = 0, read = 0;
            const DWORD size = SFileGetFileSize(f, &high);
            out.resize(size == SFILE_INVALID_SIZE ? 0 : size);
            ok = !out.empty() && SFileReadFile(f, out.data(), DWORD(out.size()), &read, nullptr) && read == out.size();
            SFileCloseFile(f);
        }
        SFileFindClose(find);
    }
    SFileCloseArchive(a);
    return ok;
}

// A brush file's brushes: "BRSH" chunks (tags are written backwards, "HSRB"; each tag has a u32 size), each with NAME
// ("Folder\Name"), TSET (u16 tileset) and TERR (u16 width, u16 height, the tiles as u16). Some files had each 0x0A byte
// written as 0x0D 0x0A (a text-mode write): a brush whose sizes do not fit its bytes is read again with that undone,
// and left out when it still does not fit.
std::vector<TerrainStamp> parseBrushes(const Bytes& d, uint16_t era) {
    std::vector<TerrainStamp> out;
    std::map<std::string, int> seen;
    auto chunks = [](const Bytes& b, size_t from, size_t to) {
        std::map<std::string, Bytes> c;
        for (size_t p = from; p + 8 <= to;) {
            const std::string tag{char(b[p + 3]), char(b[p + 2]), char(b[p + 1]), char(b[p])};
            const size_t size = readU32(b, p + 4);
            if (p + 8 + size > to) break;
            c[tag].assign(b.begin() + long(p + 8), b.begin() + long(p + 8 + size));
            p += 8 + size;
        }
        return c;
    };
    auto fits = [](std::map<std::string, Bytes>& c) {
        const Bytes& t = c["TERR"];
        return c.count("NAME") && t.size() >= 4 && t.size() == 4 + 2 * size_t(readU16(t, 0)) * readU16(t, 2);
    };
    const uint8_t tag[4] = {'H', 'S', 'R', 'B'};
    std::vector<size_t> starts;
    for (size_t p = 0; p + 4 <= d.size(); ++p)
        if (std::equal(tag, tag + 4, d.begin() + long(p))) starts.push_back(p);
    for (size_t k = 0; k < starts.size(); ++k) {
        const size_t from = starts[k] + 8, to = k + 1 < starts.size() ? starts[k + 1] : d.size();
        auto c = chunks(d, from, to);
        if (!fits(c)) {  // (a text-mode write: 0x0D 0x0A back to 0x0A)
            Bytes span;
            for (size_t p = from; p < to; ++p)
                if (!(d[p] == 0x0D && p + 1 < to && d[p + 1] == 0x0A)) span.push_back(d[p]);
            c = chunks(span, 0, span.size());
            if (!fits(c)) continue;
        }
        if (c.count("TSET") && c["TSET"].size() >= 2 && readU16(c["TSET"], 0) != era) continue;
        const Bytes& t = c["TERR"];
        TerrainStamp s;
        s.era = era, s.width = readU16(t, 0), s.height = readU16(t, 2);
        if (s.width < 1 || s.height < 1 || s.width > kTerrainStampSizeMax || s.height > kTerrainStampSizeMax) continue;
        std::string name(c["NAME"].begin(), c["NAME"].end());
        name = name.substr(0, name.find('\0'));
        for (char& ch : name)
            if (ch == '\\' || ch == '/' || ch == '_') ch = ch == '_' ? '-' : '_';  // (folders: "_" makes the list's groups)
        name = "Cosmonarchy_" + name;
        if (const int n = ++seen[name]; n > 1) name += " " + std::to_string(n);  // (brushes that share a name: numbered)
        s.name = name;
        for (int i = 0; i < s.width * s.height; ++i) {
            const uint16_t v = readU16(t, 4 + 2 * size_t(i));
            s.tiles.push_back(v), s.mask.push_back(v != 0);  // (tile 0: not in the brush)
        }
        out.push_back(std::move(s));
    }
    return out;
}

}  // namespace

const std::vector<TerrainStamp>& brushStamps(const fs::path& install, uint16_t era) {
    thread_local std::map<uint16_t, std::vector<TerrainStamp>> cache;  // (each thread its own)
    auto [it, fresh] = cache.try_emplace(uint16_t(era & 7));
    if (fresh && !install.empty()) {
        static const char* kFiles[8] = {"badlands", "platform", "install", "ashworld", "jungle", "desert", "ice", "twilight"};
        const fs::path file = install.parent_path() / "Starcraft" / "Maps" / "CMBW-Root" / "CMBW-editor" / "brush" /
                              (std::string("Cosmonarchy-") + kFiles[era & 7] + ".scmdbrh");
        Bytes d;
        if (readBrushArchive(file, d)) it->second = parseBrushes(d, uint16_t(era & 7));
    }
    return it->second;
}

// --- CRIGs ----------------------------------------------------------------------------------------------------------

namespace {
struct CrigRow {
    const char* kind;
    const char* names;
    const char* args;
    bool readsMore;
};
const CrigRow kCrigRows[] = {
#include "cmbw_crigs.inc"
};
std::vector<std::string> splitOn(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s)
        if (c == sep) out.push_back(cur), cur.clear();
        else cur += c;
    if (!cur.empty()) out.push_back(cur);
    return out;
}
bool isQuotaOp(const std::string& w) { return w == "==" || w == "!=" || w == "<" || w == ">" || w == "<=" || w == ">=" || w == "="; }
}  // namespace

const std::vector<CrigCommand>& crigCommands() {
    static const std::vector<CrigCommand> all = [] {
        std::vector<CrigCommand> v;
        for (const CrigRow& r : kCrigRows) {
            CrigCommand c;
            c.kind = r.kind, c.names = splitOn(r.names, '|'), c.readsMore = r.readsMore;
            for (const std::string& a : splitOn(r.args, ',')) {
                const size_t sp = a.find(' ');
                CrigArg arg;
                arg.kind = a.substr(0, sp), arg.name = sp == std::string::npos ? "" : a.substr(sp + 1);
                if (!arg.kind.empty() && arg.kind.back() == '?') arg.optional = true, arg.kind.pop_back();
                c.args.push_back(arg);
            }
            v.push_back(std::move(c));
        }
        return v;
    }();
    return all;
}

std::vector<std::string> crigWords(const std::string& line) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        if (i >= line.size()) break;
        const size_t from = i;
        if (line[i] == '"') {
            i = line.find('"', i + 1);
            i = i == std::string::npos ? line.size() : i + 1;
        } else if (line[i] == '(') {
            int depth = 0;
            for (; i < line.size(); ++i) {
                depth += line[i] == '(' ? 1 : line[i] == ')' ? -1 : 0;
                if (depth == 0) {
                    ++i;
                    break;
                }
            }
        } else
            while (i < line.size() && line[i] != ' ' && line[i] != '\t') ++i;
        out.push_back(line.substr(from, i - from));
    }
    return out;
}

CrigLine parseCrigLine(const std::string& line) {
    CrigLine l;
    std::vector<std::string> w = crigWords(line);
    size_t k = 0;
    auto lower = [](std::string s) {
        for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    if (k < w.size() && lower(w[k]) == "condition") l.condition = true, ++k;
    if (k < w.size()) {
        const std::string name = lower(w[k]);
        for (const CrigCommand& c : crigCommands())
            if ((c.kind == "condition") == l.condition && std::find(c.names.begin(), c.names.end(), name) != c.names.end()) l.command = &c;
    }
    if (!l.command) return CrigLine{nullptr, false, {}, line};
    ++k;
    for (const CrigArg& a : l.command->args) {
        if (k >= w.size()) break;
        std::string v = w[k++];
        const bool quota = a.kind == "quota" || a.kind == "timequota";  // (an equality is the operator alone: "== terran")
        if (quota && isQuotaOp(v) && k < w.size()) v += " " + w[k++];  // (">= 5": the operator and its value)
        l.values.push_back(v);
    }
    for (; k < w.size(); ++k) l.rest += (l.rest.empty() ? "" : " ") + w[k];
    return l;
}

std::vector<std::string> crigProblems(const Chk& chk) {
    std::vector<std::string> out;
    const ClassicTriggers t = readClassicTriggers(chk);
    const StringTable strings = StringTable::read(chk);
    auto lower = [](std::string s) {
        for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    auto unquote = [](std::string s) { return s.size() >= 2 && s.front() == '"' && s.back() == '"' ? s.substr(1, s.size() - 2) : s; };
    // The map's location names (MRGN: 20 bytes each, the name's string at 16), and Anywhere.
    std::set<std::string> locations = {"anywhere"};
    if (const Section* m = chk.find("MRGN"))
        for (size_t i = 0; i + 20 <= m->data.size(); i += 20)
            if (const uint32_t id = uint32_t(m->data[i + 16] | m->data[i + 17] << 8); id && id <= strings.size())
                locations.insert(lower(strings.get(id)));
    // (one letter off: a letter more, less or other)
    auto oneOff = [](const std::string& a, const std::string& b) {
        if (a == b || a.size() + 1 < b.size() || b.size() + 1 < a.size()) return false;
        size_t i = 0, j = 0;
        int edits = 0;
        while (i < a.size() && j < b.size()) {
            if (a[i] == b[j]) ++i, ++j;
            else {
                if (++edits > 1) return false;
                if (a.size() > b.size()) ++i;
                else if (b.size() > a.size()) ++j;
                else ++i, ++j;
            }
        }
        return edits + int(a.size() - i) + int(b.size() - j) <= 1;
    };
    for (size_t ti = 0; ti < t.triggers.size(); ++ti)
        for (const ClassicAction& a : t.triggers[ti].actions) {
            if (a.type != 47 || !a.text || (a.flags & 2) || a.text > strings.size()) continue;  // (Comments that are on)
            const std::string& text = strings.get(a.text);
            size_t start = 0;
            while (start <= text.size()) {
                size_t end = start;
                while (end < text.size() && text[end] != char(10) && text[end] != char(13)) ++end;
                const std::string line = text.substr(start, end - start);
                start = end + 1;
                if (line.empty()) continue;
                auto problem = [&](const std::string& what) {
                    out.push_back("Trigger " + std::to_string(ti + 1) + ": \"" + line + "\": " + what);
                };
                const CrigLine l = parseCrigLine(line);
                if (!l.command) {
                    const std::vector<std::string> w = crigWords(line);
                    size_t k = !w.empty() && lower(w[0]) == "condition" ? 1 : 0;
                    // (a word with "_", as only commands have: a note's first word, "Kills to Cash", is no typo)
                    if (k >= w.size() || w[k].size() < 4 || w[k].find('_') == std::string::npos) continue;
                    const std::string first = lower(w[k]);
                    for (const CrigCommand& c : crigCommands())
                        if (oneOff(first, c.names.front())) {
                            problem("no command \"" + w[k] + "\" (" + c.names.front() + "?)");
                            break;
                        }
                    continue;
                }
            }
        }
    return out;
}

CrigMapInfo crigMapInfo(const Chk& chk, size_t unitTypes) {
    CrigMapInfo m;
    m.unitTypes = unitTypes;
    m.locations.insert("anywhere");
    const StringTable strings = StringTable::read(chk);
    if (const Section* s = chk.find("MRGN"))
        for (size_t i = 0; i + 20 <= s->data.size(); i += 20)
            if (const uint32_t id = uint32_t(s->data[i + 16] | s->data[i + 17] << 8); id && id <= strings.size()) {
                std::string n = strings.get(id);
                for (char& c : n) c = char(std::tolower(static_cast<unsigned char>(c)));
                m.locations.insert(n);
            }
    return m;
}

std::vector<std::string> orderNames(const GameData& data) { return labelLines(data, "orders"); }

std::string crigArgKind(const CrigArg& a) {
    if (a.kind != "number") return a.kind;
    if (a.name == "order_id") return "order";
    if (a.name == "secondary_unit_id") return "unit";
    if (a.name == "player" || a.name == "target_player" || a.name == "tyrannizing_player" || a.name == "infesting_player") return "player";
    if (a.name == "do_loop" || a.name == "do_shuffle" || a.name == "show" || a.name == "ignore_human_name" || a.name == "attach_to_nearest_resarea")
        return "flag";
    return a.kind;
}

std::string crigValueProblem(const CrigLine& l, size_t k, const CrigMapInfo& map) {
    // (the game reads values loosely: "*" for any, a location by its number or as "(x y ~w ~h)" (a position: "(x y)" or a
    // location, its middle), true and false as
    // numbers, values left out; the mod's 418 maps: only what none of them does is marked, but for
    // set_player_name's names with a space and no quotes (5 lines: the second word goes to ignore_human_name))
    if (!l.command || k >= l.command->args.size() || k >= l.values.size()) return {};
    const CrigArg& a = l.command->args[k];
    const std::string kind = crigArgKind(a);
    std::string v = l.values[k];
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
    std::string low = v;
    for (char& c : low) c = char(std::tolower(static_cast<unsigned char>(c)));
    const bool digits = !v.empty() && v.size() <= 9 && std::all_of(v.begin(), v.end(), [](char c) { return c >= '0' && c <= '9'; });
    if ((a.kind == "area" || a.kind == "position") && !v.empty() && v != "*" && v.front() != '(' && !digits && !map.locations.empty() && !map.locations.count(low))
        return "the map has no location \"" + v + "\"";
    if (kind == "unit" && digits && map.unitTypes && std::stoll(v) >= (long long)map.unitTypes) return "no unit has the id " + v;
    if (kind == "bool" && low != "true" && low != "false" && low != "1" && low != "0") return "\"" + v + "\" is not true or false";
    if (kind == "order" && digits && map.orders && std::stoll(v) >= (long long)map.orders) return "no order has the id " + v;
    if (kind == "flag" && low != "0" && low != "1" && low != "true" && low != "false") return "\"" + v + "\" is not 0 or 1";
    return {};
}

std::vector<std::string> crigValueProblems(const Chk& chk, const CrigMapInfo& map) {
    std::vector<std::string> out;
    const ClassicTriggers t = readClassicTriggers(chk);
    const StringTable strings = StringTable::read(chk);
    for (size_t ti = 0; ti < t.triggers.size(); ++ti)
        for (size_t ai = 0; ai < t.triggers[ti].actions.size(); ++ai) {
            const ClassicAction& a = t.triggers[ti].actions[ai];
            if (a.type != 47 || !a.text || (a.flags & 2) || a.text > strings.size()) continue;  // (Comments that are on)
            const std::string& text = strings.get(a.text);
            size_t start = 0;
            while (start <= text.size()) {
                size_t end = start;
                while (end < text.size() && text[end] != char(10) && text[end] != char(13)) ++end;
                const std::string line = text.substr(start, end - start);
                start = end + 1;
                const CrigLine l = parseCrigLine(line);
                if (!l.command || crigLooksLikeNote(line)) continue;
                for (size_t k = 0; k < l.command->args.size(); ++k)
                    if (const std::string p = crigValueProblem(l, k, map); !p.empty())
                        out.push_back("Trigger " + std::to_string(ti + 1) + ", action " + std::to_string(ai + 1) + ": \"" + line + "\": " + p);
            }
        }
    return out;
}

bool crigLooksLikeNote(const std::string& line) {
    const size_t i = line.find_first_not_of(" 	");
    return i != std::string::npos && line[i] >= 'A' && line[i] <= 'Z';
}

std::string crigHelp(const CrigCommand& c) {
    static const std::pair<const char*, const char*> kHelp[] = {
#include "cmbw_crig_docs.inc"
    };
    const std::string key = (c.kind == "condition" ? "condition " : "") + c.names.front();
    for (const auto& [name, help] : kHelp)
        if (key == name) return help;
    return {};
}

std::string crigTitle(const CrigCommand& c) {
    std::string s = c.names.front();
    for (char& ch : s)
        if (ch == '_') ch = ' ';
    if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') s[0] = char(s[0] - 'a' + 'A');
    return s;
}

std::string crigNewLine(const CrigCommand& c) {
    CrigLine l;
    l.command = &c, l.condition = c.kind == "condition";
    for (const CrigArg& a : c.args) {
        if (a.optional) break;
        const auto& words = a.kind == "word" ? crigWordChoices(c.names.front(), a.name) : std::vector<std::string>{};
        l.values.push_back(words.empty() ? crigDefaultValue(a.kind) : words.front());  // (a word: the first the mod lists)
    }
    return crigLineText(l);
}

namespace {

constexpr uint8_t kCommentType = 47, kDisabledFlag = 2;

// The rows of a trigger back into Comments. `strings` null: a trial (a text that is not the Comment's string gives the
// id 0xFFFFFFFF, so the trigger is not the same). `own`: the strings the page made (changed in place while one Comment
// uses them; the new ones are added). `actions`: how many actions it needs (more than 64 do not fit).
ClassicTrigger crigExpandOne(const ClassicTrigger& in, const StringTable& read, StringTable* strings, std::set<uint32_t>* own = nullptr,
                             size_t* actions = nullptr) {
    ClassicTrigger out = in;
    out.actions.assign(size_t(kClassicActions), ClassicAction{});
    out.conditions = {};
    struct Line {
        int64_t at;  // a condition's line: its place in its Comment (-1: before the others, a new or a moved one)
        std::string text;
    };
    struct Comment {
        int group;
        int slot;    // its place in the actions
        int depth;   // its rows' depth (rows of another depth are another Comment)
        std::vector<std::string> lines;  // the action rows' lines, in the rows' order
        std::vector<Line> conds;         // the condition rows' lines
        uint32_t text = 0;
        uint8_t flags = 0;
        bool crlf = true;
    };
    std::vector<Comment> comments;
    std::vector<ClassicAction> acts;
    int newGroup = 1 << 20, open = -1;  // open: the comment the last row went into (-1: none)
    // The Comments that held conditions only: back at their places among the action rows.
    std::set<int> withActions;
    for (int i = 0; i < in.actionCount(); ++i)
        if (in.actions[size_t(i)].type == kClassicCrigType && in.actions[size_t(i)].crig) withActions.insert(in.actions[size_t(i)].crig->group);
    std::map<int, std::vector<const ClassicCrig*>> onlyAt;  // place -> the first row of each such group
    for (int i = 0; i < in.conditionCount(); ++i) {
        const ClassicCondition& k = in.conditions[size_t(i)];
        if (k.type != kClassicCrigType || !k.crig || k.crig->group < 0 || withActions.count(k.crig->group) || (k.flags & kDisabledFlag)) continue;
        auto& g = onlyAt[k.crig->at];
        if (std::none_of(g.begin(), g.end(), [&](const ClassicCrig* c) { return c->group == k.crig->group; })) g.push_back(k.crig.p.get());
    }
    auto conditionsOnly = [&](int place) {
        const auto it = onlyAt.find(place);
        if (it == onlyAt.end()) return;
        for (const ClassicCrig* first : it->second) {
            ClassicAction slot;
            slot.type = kCommentType;
            comments.push_back(Comment{first->group, int(acts.size()), 0, {}, {}, first->text, first->flags, first->crlf});
            acts.push_back(slot);
        }
        onlyAt.erase(it), open = -1;
    };
    for (int i = 0; i < in.actionCount(); ++i) {
        conditionsOnly(i);
        const ClassicAction& a = in.actions[size_t(i)];
        if (a.type != kClassicCrigType || !a.crig) {
            acts.push_back(a), open = -1;
            continue;
        }
        const ClassicCrig& c = *a.crig.p;
        // A row turned off: a turned-off Comment of its own (a Comment's line cannot be off alone).
        const bool off = (a.flags & kDisabledFlag) != 0;
        const bool join = !off && open >= 0 && comments[size_t(open)].depth == a.depth && (c.group < 0 || comments[size_t(open)].group == c.group);
        if (!join) {
            ClassicAction slot;
            slot.type = kCommentType, slot.depth = a.depth;
            comments.push_back(Comment{off || c.group < 0 ? newGroup++ : c.group, int(acts.size()), a.depth, {}, {}, off ? 0u : c.text,
                                       uint8_t(off ? c.flags | kDisabledFlag : c.flags), c.crlf});
            acts.push_back(slot), open = int(comments.size()) - 1;
        }
        comments[size_t(open)].lines.push_back(c.line);
        if (off) open = -1;
    }
    while (!onlyAt.empty()) conditionsOnly(onlyAt.begin()->first);  // (those after the last row)
    std::vector<ClassicCondition> conds;
    for (int i = 0; i < in.conditionCount(); ++i) {
        const ClassicCondition& k = in.conditions[size_t(i)];
        if (k.type != kClassicCrigType || !k.crig) {
            conds.push_back(k);
            continue;
        }
        const ClassicCrig& c = *k.crig.p;
        const bool off = (k.flags & kDisabledFlag) != 0;  // (turned off: a turned-off Comment of its own at the top)
        int to = -1;
        for (size_t j = 0; j < comments.size() && to < 0 && !off; ++j)
            if (comments[j].group == c.group && comments[j].depth == 0 && !(comments[j].flags & kDisabledFlag)) to = int(j);
        for (size_t j = 0; j < comments.size() && to < 0 && !off; ++j)
            if (comments[j].depth == 0 && !(comments[j].flags & kDisabledFlag)) to = int(j);
        if (to < 0) {  // (no comment of commands: a new one at the top)
            ClassicAction slot;
            slot.type = kCommentType;
            acts.insert(acts.begin(), slot);
            for (Comment& cm : comments) ++cm.slot;
            comments.insert(comments.begin(), Comment{off || c.group < 0 ? newGroup++ : c.group, 0, 0, {}, {}, off ? 0u : c.text,
                                                      uint8_t(off ? c.flags | kDisabledFlag : c.flags), c.crlf});
            to = 0;
        }
        Comment& cm = comments[size_t(to)];
        cm.conds.push_back({c.group == cm.group ? int64_t(c.index) : -1, c.line});
    }
    std::set<uint32_t> used;  // (the strings of this trigger's Comments so far: one of them is not changed in place)
    for (Comment& cm : comments) {
        // The action rows' lines in their order; each condition's line back at its place (new ones first).
        std::vector<std::string> lines = cm.lines;
        std::stable_sort(cm.conds.begin(), cm.conds.end(), [](const Line& a, const Line& b) { return a.at < b.at; });
        size_t front = 0;
        for (const Line& l : cm.conds)
            if (l.at < 0) lines.insert(lines.begin() + std::ptrdiff_t(front++), l.text);
            else lines.insert(lines.begin() + std::ptrdiff_t(std::min<size_t>(size_t(l.at), lines.size())), l.text);
        std::string text;
        for (size_t i = 0; i < lines.size(); ++i) text += (i ? (cm.crlf ? "\r\n" : "\n") : "") + lines[i];
        ClassicAction& slot = acts[size_t(cm.slot)];
        if (cm.text && read.get(cm.text) == text) slot.text = cm.text;  // (the same text: its string, shared or not)
        else if (!strings) slot.text = 0xFFFFFFFFu;
        else if (cm.text && own && own->count(cm.text) && !used.count(cm.text)) strings->set(cm.text, text), slot.text = cm.text;  // (the page's own)
        else {
            const uint32_t before = strings->size();
            slot.text = classicStringId(*strings, text);
            if (own && slot.text > before) own->insert(slot.text);
            else if (own) own->erase(slot.text);  // (a string with the same text: shared now, never changed in place)
        }
        used.insert(slot.text);
        slot.flags = cm.flags;
    }
    if (actions) *actions = std::max(acts.size(), conds.size() > out.conditions.size() ? out.actions.size() + 1 : 0);
    for (size_t i = 0; i < acts.size() && i < out.actions.size(); ++i) out.actions[i] = acts[i];
    for (size_t i = 0; i < conds.size() && i < out.conditions.size(); ++i) out.conditions[i] = conds[i];
    return out;
}

bool hasCrigRows(const ClassicTrigger& t) {
    for (int i = 0; i < t.actionCount(); ++i)
        if (t.actions[size_t(i)].type == kClassicCrigType) return true;
    for (int i = 0; i < t.conditionCount(); ++i)
        if (t.conditions[size_t(i)].type == kClassicCrigType) return true;
    return false;
}

// A trigger's Comments of commands as rows (crigCollapse), or the trigger as it is.
ClassicTrigger crigCollapseOne(const ClassicTrigger& t, const StringTable& strings) {
    ClassicTrigger rows = t;
    rows.conditions = {};
    std::vector<ClassicAction> acts;
    std::vector<ClassicCondition> conds;
    for (int i = 0; i < t.conditionCount(); ++i) conds.push_back(t.conditions[size_t(i)]);
    int group = 0;
    for (int i = 0; i < t.actionCount(); ++i) {
        const ClassicAction& a = t.actions[size_t(i)];
        std::vector<std::string> lines;
        bool crlf = true, ok = a.type == kCommentType && a.text && !(a.flags & kDisabledFlag);
        if (ok) {
            const std::string text = strings.get(a.text);
            crlf = text.find("\r\n") != std::string::npos || text.find('\n') == std::string::npos;
            std::string cur;
            for (char ch : text)
                if (ch == '\n') lines.push_back(cur), cur.clear();
                else if (ch != '\r') cur += ch;
            lines.push_back(cur);
            bool command = false;  // (a Comment of text only stays a Comment)
            for (const std::string& l : lines) command = command || parseCrigLine(l).command;
            ok = command;
        }
        if (!ok) {
            acts.push_back(a);
            continue;
        }
        for (size_t k = 0; k < lines.size(); ++k) {
            auto c = std::make_shared<ClassicCrig>(ClassicCrig{lines[k], group, int(k), int(acts.size()), a.text, a.flags, crlf});
            if (parseCrigLine(lines[k]).condition) {
                ClassicCondition row;
                row.type = kClassicCrigType, row.crig.p = c;
                conds.push_back(row);
            } else {
                ClassicAction row;
                row.type = kClassicCrigType, row.depth = a.depth, row.crig.p = c;
                acts.push_back(row);
            }
        }
        ++group;
    }
    if (!group || conds.size() > rows.conditions.size()) return t;
    acts.resize(std::max(acts.size(), size_t(kClassicActions)));  // (more than 64 rows: a mod's lines, fewer Comments)
    rows.actions = std::move(acts);
    for (size_t i = 0; i < conds.size(); ++i) rows.conditions[i] = conds[i];
    return crigExpandOne(rows, strings, nullptr) == t ? rows : t;  // (else: comment mode)
}

}  // namespace

ClassicTriggers crigExpand(const ClassicTriggers& in, StringTable& strings, std::set<uint32_t>* own, ClassicTriggers* reread) {
    ClassicTriggers out = in;
    if (reread) *reread = in;
    for (size_t i = 0; i < out.triggers.size(); ++i) {
        if (!hasCrigRows(out.triggers[i])) continue;
        out.triggers[i] = crigExpandOne(out.triggers[i], strings, &strings, own);
        if (reread) reread->triggers[i] = crigCollapseOne(out.triggers[i], strings);
    }
    return out;
}

bool crigFits(const ClassicTriggers& t, const StringTable& strings) {
    for (const ClassicTrigger& tr : t.triggers) {
        if (!hasCrigRows(tr)) {  // (more than 64 rows: a trigger that had command rows)
            if (tr.actionCount() > kClassicActions) return false;
            continue;
        }
        size_t actions = 0;
        crigExpandOne(tr, strings, nullptr, nullptr, &actions);
        if (actions > size_t(kClassicActions)) return false;
    }
    return true;
}

int crigActionCount(const ClassicTrigger& t) {
    if (!hasCrigRows(t)) return t.actionCount();
    size_t actions = 0;
    crigExpandOne(t, StringTable{}, nullptr, nullptr, &actions);
    return int(actions);
}

ClassicTriggers crigCollapse(const ClassicTriggers& in, const StringTable& strings) {
    ClassicTriggers out = in;
    for (ClassicTrigger& t : out.triggers) t = crigCollapseOne(t, strings);
    return out;
}

namespace {
// The words of a line (as crigWords splits it): where each starts and how long it is.
std::vector<std::pair<size_t, size_t>> crigWordSpans(const std::string& line) {
    std::vector<std::pair<size_t, size_t>> out;
    size_t at = 0;
    for (const std::string& w : crigWords(line)) {
        at = line.find(w, at);
        out.push_back({at, w.size()});
        at += w.size();
    }
    return out;
}

// The words (their indexes in `w`) that a line's command takes as a location, from word k on; `condition`: the words are
// a condition's with no "condition" before them (the rest of exit_if, wait_until and the like).
void crigAreaWords(const std::vector<std::string>& w, size_t k, bool condition, std::vector<size_t>& out) {
    auto lower = [](std::string s) {
        for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    if (!condition && k < w.size() && lower(w[k]) == "condition") condition = true, ++k;
    if (k >= w.size()) return;
    const CrigCommand* command = nullptr;
    for (const CrigCommand& c : crigCommands())
        if ((c.kind == "condition") == condition && std::find(c.names.begin(), c.names.end(), lower(w[k])) != c.names.end()) command = &c;
    if (!command) return;
    ++k;
    for (const CrigArg& a : command->args) {
        if (k >= w.size()) return;
        const size_t at = k++;
        if ((a.kind == "quota" || a.kind == "timequota") && isQuotaOp(w[at]) && k < w.size()) ++k;  // (">= 5": two words)
        if (a.kind == "area") out.push_back(at);
    }
    // (a command that reads a condition after it: its locations too)
    static const std::set<std::string> kConditionAfter = {"wait_if", "wait_until", "exit_if", "exit_unless", "exit_preserve_if", "exit_preserve_unless"};
    if (command->readsMore && kConditionAfter.count(command->names.front())) crigAreaWords(w, k, true, out);
}
}  // namespace

std::string crigRenameLocation(const std::string& text, const std::string& from, const std::string& to) {
    auto lower = [](std::string s) {
        for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    auto unquote = [](const std::string& s) { return s.size() >= 2 && s.front() == '"' && s.back() == '"' ? s.substr(1, s.size() - 2) : s; };
    // (a name with a quote cannot be written in a line; one with a space or a tab is quoted)
    if (to.empty() || to.find('"') != std::string::npos) return text;
    const std::string old = lower(from), now = to.find_first_of(" \t") != std::string::npos ? "\"" + to + "\"" : to;
    std::string out;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = start;
        while (end < text.size() && text[end] != char(10) && text[end] != char(13)) ++end;
        std::string line = text.substr(start, end - start);
        if (!crigLooksLikeNote(line)) {
            // Only the location's words change, in place: the rest of the line keeps its bytes (aliases, spacing).
            const std::vector<std::string> w = crigWords(line);
            const auto spans = crigWordSpans(line);
            std::vector<size_t> areas;
            crigAreaWords(w, 0, false, areas);
            for (size_t n = areas.size(); n-- > 0;)
                if (lower(unquote(w[areas[n]])) == old) line.replace(spans[areas[n]].first, spans[areas[n]].second, now);
        }
        out += line;
        if (end < text.size()) out += text[end];  // (the line's end as it was: CR or LF)
        start = end + 1;
    }
    return out;
}

std::string crigLineText(const CrigLine& l) {
    if (!l.command) return l.rest;
    std::string out = (l.condition ? "condition " : "") + l.command->names.front();
    for (const std::string& v : l.values) out += " " + v;
    if (!l.rest.empty()) out += " " + l.rest;
    return out;
}

const std::vector<std::string>& crigWordChoices(const std::string& command, const std::string& arg) {
    // From the mod's scenario setup page (fraudsclub.com/cosmonarchy-bw/sites/scenario-setup) and its plugin's enums.
    static const std::vector<std::string> none;
    static const std::vector<std::string> visibility = {"visible", "explored", "unexplored"};
    static const std::vector<std::string> races = {"terran", "protoss", "zerg", "askosi"};
    static const std::map<std::string, std::vector<std::string>> byCommand = {
        {"spawn.type", {"None", "Augur", "AugurDamage", "Zerg"}},
        {"player_type.type", {"human", "computer", "witness", "human_defeated", "computer_defeated", "witness_defeated"}},
        {"set_faction.faction", {"orion_imperium", "hecaton_coalition", "futurist_federation", "golden_republic", "nydus_trinity", "upstart_terran",
                                 "divine_primacy", "order_of_the_sacred_saber", "khalendric_knights", "starbound", "astral_omen", "ataltreth",
                                 "imicren", "goszirith", "orkoti", "zorthos"}},
        {"set_unit_behavior.trigger_behavior", {"cloak_proximity", "cloak_passive", "hallucination", "disable_passive", "critter", "no_collision",
                                                "air_transport", "no_energy_overlay", "infrastructure_burrow", "conquerable"}},
        {"set_game_mode_race.omni_state", {"all", "terranprotoss", "protosszerg", "zergterran"}},
        {"unit_sound.animation", {"Init", "Death", "GroundAttackInit", "AirAttackInit", "Unused1", "GroundAttackRepeat", "AirAttackRepeat", "CastSpell",
                                  "GroundAttackToIdle", "AirAttackToIdle", "Unused2", "Walking", "WalkingToIdle", "SpecialState1", "SpecialState2",
                                  "AlmostBuilt", "Built", "Landing", "LiftOff", "IsWorking", "WorkingToIdle", "WarpIn", "Unused3", "StarEditInit",
                                  "Disable", "Burrow", "Unburrow", "Enable", "*"}},
    };
    if (auto it = byCommand.find(command + "." + arg); it != byCommand.end()) return it->second;
    if (arg == "min_visibility") return visibility;
    if (arg == "race") return races;
    return none;
}

std::string crigDefaultValue(const std::string& kind) {
    if (kind == "area") return "Anywhere";
    if (kind == "players" || kind == "player") return "1";
    if (kind == "units") return "*";
    if (kind == "unit") return "0";
    if (kind == "quota" || kind == "timequota") return ">= 1";
    if (kind == "equality") return "==";
    if (kind == "time") return "1s";
    if (kind == "seconds" || kind == "decimal") return "1";
    if (kind == "bool") return "true";
    if (kind == "text") return "\"text\"";
    if (kind == "position") return "(0 0)";
    if (kind == "number") return "1";
    return "name";
}

// --- The establishing shot ------------------------------------------------------------------------------------------

const std::vector<std::pair<std::string, std::string>>& establishingShotPlaces() {
    static const std::vector<std::pair<std::string, std::string>> places = {
        {"SCREENUPPERLEFT", "Upper left"}, {"SCREENTOP", "Top"},       {"SCREENUPPERRIGHT", "Upper right"},
        {"SCREENLEFT", "Left"},            {"SCREENCENTER", "Center"}, {"SCREENRIGHT", "Right"},
        {"SCREENLOWERLEFT", "Lower left"}, {"SCREENBOTTOM", "Bottom"}, {"SCREENLOWERRIGHT", "Lower right"}};
    return places;
}

EstablishingShot parseEstablishingShot(const std::string& text) {
    EstablishingShot s;
    s.fadeMs = -1;
    EstablishingShot::Page page;
    bool body = false;  // the page's place or text has come (its tags before it are done)
    auto upper = [](std::string t) {
        for (char& c : t) c = char(std::toupper(static_cast<unsigned char>(c)));
        return t;
    };
    auto take = [&](std::string l) {
        while (!l.empty() && (l.back() == '\r' || l.back() == '\n')) l.pop_back();
        if (l.size() >= 3 && l.compare(0, 2, "</") == 0 && l.back() == '>') {
            const std::string inner = l.substr(2, l.size() - 3);
            const size_t sp = inner.find(' ');
            const std::string tag = upper(inner.substr(0, sp)), arg = sp == std::string::npos ? "" : inner.substr(sp + 1);
            if (tag == "FADESPEED") return void(s.fadeMs = std::atoi(arg.c_str()));
            if (tag == "PAGE") {
                while (!page.lines.empty() && page.lines.back().empty()) page.lines.pop_back();  // (the blank lines before it)
                s.pages.push_back(std::move(page)), page = {}, body = false;
                return;
            }
            for (const auto& [p, name] : establishingShotPlaces())
                if (tag == p) {
                    if (body) break;  // (a second place in a page: kept as a line)
                    page.place = p, body = true;
                    return;
                }
            if (!body) {  // (the page's own tags, before its place)
                if (tag == "COMMENT") return void(page.comments.push_back(arg));
                if (tag == "BACKGROUND") return void(page.background = arg);
                if (tag == "FONTCOLOR") return void(page.font = arg);
                if (tag == "DISPLAYTIME") return void(page.displayMs = std::atoi(arg.c_str()));
                return void(page.tags.push_back(l));
            }
        }
        if (!body && l.empty()) return;  // (blank lines between the tags)
        page.lines.push_back(l), body = true;
    };
    std::string line;
    for (char c : text) {
        if (c == '\n') take(line), line.clear();
        else line += c;
    }
    if (!line.empty()) take(line);
    while (!page.lines.empty() && page.lines.back().empty()) page.lines.pop_back();
    if (body || !page.lines.empty()) s.pages.push_back(std::move(page));
    return s;
}

std::string establishingShotText(const EstablishingShot& s) {
    // (as the mod's example: FADESPEED once, then each page's comments, its display time, picture and font, its place,
    // its text and </PAGE>; a blank line between them)
    std::string out;
    auto put = [&](const std::string& l) { out += l + "\r\n"; };
    if (s.fadeMs >= 0) put("</FADESPEED " + std::to_string(s.fadeMs) + ">"), put("");
    for (const EstablishingShot::Page& p : s.pages) {
        for (const std::string& c : p.comments) put("</COMMENT " + c + ">");
        for (const std::string& t : p.tags) put(t);
        if (p.displayMs >= 0) put("</DISPLAYTIME " + std::to_string(p.displayMs) + ">");
        if (!p.background.empty()) put("</BACKGROUND " + p.background + ">");
        if (!p.font.empty()) put("</FONTCOLOR " + p.font + ">");
        put("</" + p.place + ">");
        for (const std::string& l : p.lines) put(l);
        put("</PAGE>");
        if (&p != &s.pages.back()) put("");
    }
    return out;
}

bool decodePcxRgba(const std::vector<uint8_t>& pcx, int& w, int& h, std::vector<uint8_t>& rgba) {
    if (pcx.size() < 128 + 769 || pcx[0] != 10 || pcx[2] != 1 || pcx[3] != 8 || pcx[65] != 1) return false;  // (8-bit, one plane, RLE)
    w = readU16(pcx, 8) - readU16(pcx, 4) + 1, h = readU16(pcx, 10) - readU16(pcx, 6) + 1;
    const int stride = readU16(pcx, 66);
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096 || stride < w) return false;
    const size_t palAt = pcx.size() - 768;
    if (pcx[palAt - 1] != 12) return false;  // (the 256-color palette's mark)
    std::vector<uint8_t> idx(size_t(stride) * size_t(h));
    size_t p = 128, o = 0;
    while (o < idx.size() && p < palAt - 1) {
        uint8_t b = pcx[p++];
        size_t run = 1;
        if ((b & 0xC0) == 0xC0) {
            run = b & 0x3F;
            if (p >= palAt - 1) break;
            b = pcx[p++];
        }
        for (; run && o < idx.size(); --run) idx[o++] = b;
    }
    rgba.assign(size_t(w) * size_t(h) * 4, 255);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const uint8_t i = idx[size_t(y) * size_t(stride) + size_t(x)];
            uint8_t* q = &rgba[(size_t(y) * size_t(w) + size_t(x)) * 4];
            q[0] = pcx[palAt + 3 * i], q[1] = pcx[palAt + 3 * i + 1], q[2] = pcx[palAt + 3 * i + 2];
        }
    return true;
}

std::vector<uint8_t> encodePcx8(const std::vector<uint8_t>& rgba, int width, int height, int outW, int outH) {
    if (width <= 0 || height <= 0 || rgba.size() < size_t(width) * size_t(height) * 4 || outW <= 0 || outH <= 0) return {};
    // The picture at the out size: each out pixel the mean of the source pixels it covers (or the nearest one, scaled up).
    std::vector<uint8_t> px(size_t(outW) * size_t(outH) * 3);
    for (int y = 0; y < outH; ++y)
        for (int x = 0; x < outW; ++x) {
            const int x0 = x * width / outW, x1 = std::max(x0 + 1, (x + 1) * width / outW);
            const int y0 = y * height / outH, y1 = std::max(y0 + 1, (y + 1) * height / outH);
            unsigned sum[3] = {}, n = 0;
            for (int sy = y0; sy < y1 && sy < height; ++sy)
                for (int sx = x0; sx < x1 && sx < width; ++sx, ++n)
                    for (int k = 0; k < 3; ++k) sum[k] += rgba[(size_t(sy) * size_t(width) + size_t(sx)) * 4 + size_t(k)];
            for (int k = 0; k < 3; ++k) px[(size_t(y) * size_t(outW) + size_t(x)) * 3 + size_t(k)] = uint8_t(n ? sum[k] / n : 0);
        }
    // Median cut: the colors (5 bits a channel, counted) in boxes; the box with the widest channel (weighted by its
    // pixels) is cut at its median, until 256 boxes; each box's color the mean of its pixels.
    std::vector<uint32_t> count(32768, 0);
    auto key = [&](size_t i) { return (px[i] >> 3) << 10 | (px[i + 1] >> 3) << 5 | (px[i + 2] >> 3); };
    for (size_t i = 0; i < px.size(); i += 3) ++count[key(i)];
    std::vector<uint16_t> colors;
    for (uint16_t c = 0; c < 32768; ++c)
        if (count[c]) colors.push_back(c);
    struct Box {
        size_t from, to;  // colors[from, to)
    };
    std::vector<Box> boxes{{0, colors.size()}};
    auto channel = [](uint16_t c, int k) { return (c >> (10 - 5 * k)) & 31; };
    while (boxes.size() < 256) {
        int best = -1, bestChannel = 0;
        double bestScore = 0;
        for (size_t b = 0; b < boxes.size(); ++b) {
            if (boxes[b].to - boxes[b].from < 2) continue;
            int lo[3] = {31, 31, 31}, hi[3] = {0, 0, 0};
            uint64_t pixels = 0;
            for (size_t i = boxes[b].from; i < boxes[b].to; ++i) {
                pixels += count[colors[i]];
                for (int k = 0; k < 3; ++k) lo[k] = std::min(lo[k], channel(colors[i], k)), hi[k] = std::max(hi[k], channel(colors[i], k));
            }
            for (int k = 0; k < 3; ++k)
                if (const double score = double(hi[k] - lo[k]) * std::sqrt(double(pixels)); score > bestScore) bestScore = score, best = int(b), bestChannel = k;
        }
        if (best < 0) break;
        Box& b = boxes[size_t(best)];
        std::sort(colors.begin() + long(b.from), colors.begin() + long(b.to),
                  [&](uint16_t a, uint16_t c) { return channel(a, bestChannel) < channel(c, bestChannel); });
        uint64_t total = 0, half = 0;
        for (size_t i = b.from; i < b.to; ++i) total += count[colors[i]];
        size_t cut = b.from + 1;
        for (size_t i = b.from; i + 1 < b.to; ++i) {
            half += count[colors[i]];
            if (half * 2 >= total) {
                cut = i + 1;
                break;
            }
            cut = i + 2;
        }
        cut = std::min(cut, b.to - 1);
        const Box second{cut, b.to};
        b.to = cut;
        boxes.push_back(second);
    }
    uint8_t palette[768] = {};
    std::vector<uint8_t> boxOf(32768, 0);
    for (size_t b = 0; b < boxes.size(); ++b) {
        uint64_t sum[3] = {}, n = 0;
        for (size_t i = boxes[b].from; i < boxes[b].to; ++i) {
            const uint32_t c = count[colors[i]];
            n += c;
            for (int k = 0; k < 3; ++k) sum[k] += uint64_t(channel(colors[i], k) * 8 + 4) * c;
            boxOf[colors[i]] = uint8_t(b);
        }
        for (int k = 0; k < 3; ++k) palette[b * 3 + size_t(k)] = uint8_t(n ? sum[k] / n : 0);
    }
    // The file: the header, each row run-length coded, then 12 and the palette.
    const int stride = (outW + 1) & ~1;
    std::vector<uint8_t> out(128, 0);
    auto put16 = [&](size_t at, int v) { out[at] = uint8_t(v), out[at + 1] = uint8_t(v >> 8); };
    out[0] = 10, out[1] = 5, out[2] = 1, out[3] = 8;
    put16(8, outW - 1), put16(10, outH - 1), put16(12, 72), put16(14, 72);
    out[65] = 1, put16(66, stride), put16(68, 1);
    for (int y = 0; y < outH; ++y) {
        std::vector<uint8_t> row(size_t(stride), 0);
        for (int x = 0; x < outW; ++x) row[size_t(x)] = boxOf[key((size_t(y) * size_t(outW) + size_t(x)) * 3)];
        for (size_t x = 0; x < row.size();) {
            size_t run = 1;
            while (x + run < row.size() && run < 63 && row[x + run] == row[x]) ++run;
            if (run > 1 || (row[x] & 0xC0) == 0xC0) out.push_back(uint8_t(0xC0 | run));
            out.push_back(row[x]);
            x += run;
        }
    }
    out.push_back(12);
    out.insert(out.end(), palette, palette + 768);
    return out;
}

fs::path gChosenInstall;  // (useInstall)
std::mutex gChosenInstallLock;  // (a worker thread can open the mod's data)

fs::path installIn(const fs::path& folder) {
    std::error_code ec;
    if (folder.empty()) return {};
    if (fs::is_regular_file(folder / "mpq" / "arr" / "units.datex", ec)) return folder;
    for (const char* channel : {"Prerelease", "Release"})
        if (fs::is_regular_file(folder / channel / "mpq" / "arr" / "units.datex", ec)) return folder / channel;
    return {};
}

void useInstall(const fs::path& install) {
    std::lock_guard<std::mutex> lock(gChosenInstallLock);
    gChosenInstall = install;
}

fs::path findInstall() {
    {
        std::lock_guard<std::mutex> lock(gChosenInstallLock);
        if (!gChosenInstall.empty()) return gChosenInstall;
    }
    for (const char* v : {"ProgramFiles(x86)", "ProgramFiles"})
        if (const char* p = std::getenv(v))
            if (fs::path in = installIn(fs::path(p) / "Cosmonarchy"); !in.empty()) return in;
    return {};
}

int brushEdgeScore(const Tileset& ts, const TerrainStamp& b, const TerrainLayer& map, int x0, int y0) {
    // A tile's pixel (x, y of its 32 x 32), from its megatile's minitiles (a flipped minitile: mirrored in x).
    auto pixel = [&](uint16_t value, int px, int py) -> const Rgb* {
        const int m = ts.megatileFor(value);
        if (m < 0 || size_t(m) >= ts.megatiles.size()) return nullptr;
        const uint32_t e = ts.megatiles[size_t(m)][size_t(py / 8 * 4 + px / 8)];
        if ((e >> 1) >= ts.minitiles.size()) return nullptr;
        return &ts.palette[ts.minitiles[e >> 1][size_t(py % 8 * 8 + ((e & 1) ? 7 - px % 8 : px % 8))]];
    };
    int64_t sum = 0, n = 0;
    const int dx[4] = {-1, 1, 0, 0}, dy[4] = {0, 0, -1, 1};
    for (int y = 0; y < b.height; ++y)
        for (int x = 0; x < b.width; ++x) {
            if (!b.has(x, y)) continue;
            const uint16_t v = b.tiles[size_t(y) * b.width + x];
            for (int d = 0; d < 4; ++d) {
                const int bx = x + dx[d], by = y + dy[d];
                if (bx >= 0 && by >= 0 && bx < b.width && by < b.height && b.has(bx, by)) continue;  // (its own tile)
                const int mx = x0 + bx, my = y0 + by;
                if (mx < 0 || my < 0 || mx >= map.width || my >= map.height) continue;
                const uint16_t w = map.tiles[size_t(my) * map.width + mx];
                for (int i = 0; i < 32; ++i) {  // (its edge on that side, and the neighbor's facing edge)
                    const int ax = d == 0 ? 0 : d == 1 ? 31 : i, ay = d == 2 ? 0 : d == 3 ? 31 : i;
                    const int cx = d == 0 ? 31 : d == 1 ? 0 : i, cy = d == 2 ? 31 : d == 3 ? 0 : i;
                    const Rgb *p = pixel(v, ax, ay), *q = pixel(w, cx, cy);
                    if (!p || !q) continue;
                    sum += std::abs(int(p->r) - q->r) + std::abs(int(p->g) - q->g) + std::abs(int(p->b) - q->b), n += 3;
                }
            }
        }
    return n ? int(sum / n) : -1;
}

std::vector<std::array<uint16_t, 4>> learnPieceNeighbors(const GameData& data, const fs::path& mapsFolder) {
    std::set<uint16_t> pieces[8];  // each era's mod terrains' edge and wall pieces (left tiles; a fill beside a fill needs no rule)
    for (int e = 0; e < 8; ++e) {
        Tileset t;
        std::string err;
        if (!t.load(data, uint16_t(e), err)) continue;
        const auto terrains = tileGroupTerrains(t);
        for (const auto& m : t.modTerrains)
            for (size_t i = 0; i + m.gapPieces < m.pieces.size(); ++i)  // (not the gaps' null tiles)
                if (const auto& pc = m.pieces[i]; pc[0] < terrains.size() && terrains[pc[0]][1]) pieces[e].insert(pc[1]);
    }
    std::set<std::array<uint16_t, 4>> seen;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(mapsFolder, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        const fs::path p = it->path();
        if (p.extension() != ".scx" && p.extension() != ".scm") continue;
        Bytes raw;
        std::string err;
        if (!readMapFile(p, raw, err)) continue;
        const Chk chk = Chk::parse(raw);
        const Section* era = chk.find("ERA ");
        if (!era || era->data.size() < 2) continue;
        const uint16_t e = uint16_t((era->data[0] | era->data[1] << 8) & 7);
        const TerrainLayer t = readTerrain(chk);
        auto piece = [&](int x, int y) -> int {
            if (x < 0 || y < 0 || x >= t.width || y >= t.height) return -1;
            const uint16_t v = t.tiles[size_t(y) * t.width + x];
            return pieces[e].count(v) ? v : -1;
        };
        for (int y = 0; y < t.height; ++y)
            for (int x = 0; x + 1 < t.width; x += 2) {
                const int a = piece(x, y);
                if (a < 0) continue;
                if (const int b = piece(x, y + 1); b >= 0) seen.insert({e, uint16_t(a), uint16_t(b), 0});
                if (const int b = piece(x + 2, y + 1); b >= 0) seen.insert({e, uint16_t(a), uint16_t(b), 1});
                if (const int b = piece(x + 2, y - 1); b >= 0) seen.insert({e, uint16_t(a), uint16_t(b), 2});
            }
    }
    return {seen.begin(), seen.end()};
}

bool open(GameData& data, const fs::path& install, std::string& error) {
    std::error_code ec;
    const fs::path loose = install / "mpq";
    if (!fs::is_regular_file(loose / "arr" / "units.datex", ec)) return error = "no Cosmonarchy data in " + install.string(), false;
    const fs::path sc = install.parent_path() / "Starcraft";
    std::vector<fs::path> mpqs;
    for (const char* m : {"patch_rt.mpq", "BrooDat.mpq", "StarDat.mpq"})
        if (fs::is_regular_file(sc / m, ec)) mpqs.push_back(sc / m);
    auto files = std::make_unique<LayeredFiles>(loose, mpqs);
    if (!files->archives()) return error = "no StarCraft 1.16.1 MPQs in " + sc.string(), false;
    gLoose = loose;
    data.open(std::move(files), &kLoader);
    return true;
}

// --- The mining simulation's units ----------------------------------------------------------------------------------

std::vector<uint8_t> simUnits(const std::vector<uint8_t>& in, const UnitTypes& types) {
    std::vector<uint8_t> out;
    for (size_t i = 0; i + 36 <= in.size(); i += 36) {
        uint16_t type = uint16_t(in[i + 8] | in[i + 9] << 8);
        if (type < types.size()) {
            const UnitType& t = types[type];
            const uint8_t race = t.groupFlags & (UnitGroup::Zerg | UnitGroup::Terran | UnitGroup::Protoss);
            auto byRace = [&](uint16_t terran, uint16_t zerg, uint16_t protoss) {
                return race == UnitGroup::Zerg ? zerg : race == UnitGroup::Protoss ? protoss : terran;  // (Askosi: as Terran)
            };
            if (t.is(UnitFlag::Worker) && !t.is(UnitFlag::Building)) type = byRace(7, 41, 64);
            else if (t.is(UnitFlag::ResourceDepot) && type != 214) type = byRace(106, 131, 154);  // (not the start location)
            else if (t.is(UnitFlag::ResourceContainer)) {
                if ((t.groupFlags & UnitGroup::Neutral) || !race)
                    type = t.placementWidth <= 64 && t.placementHeight <= 32 ? (type >= 176 && type <= 178 ? type : uint16_t(176)) : uint16_t(188);
                else
                    type = byRace(110, 149, 157);
            } else if (type >= 228)
                continue;
        } else if (type >= 228)
            continue;
        out.insert(out.end(), in.begin() + long(i), in.begin() + long(i) + 36);
        out[out.size() - 28] = uint8_t(type), out[out.size() - 27] = uint8_t(type >> 8);
    }
    return out;
}

void useTerrainOverride(const std::filesystem::path& file) {
    std::lock_guard<std::mutex> lock(gTerrainOverrideLock);
    gTerrainOverride = file;
}
void skipTerrainOverride(bool on) { tSkipOverride = on; }
void skipRepairedRules(bool on) { tSkipRepaired = on; }

}  // namespace stardraft::cmbw
