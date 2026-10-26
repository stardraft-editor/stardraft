#include "stardraft/classic_settings.h"

#include <algorithm>
#include <functional>

#include "byteio.h"
#include "stardraft/game_data.h"

namespace stardraft {

namespace {

constexpr int U = kSettingUnits, P = kSettingPlayers;

// The size of each section (the wiki's validation sizes; the corpus has no other).
size_t unitSectionSize(int weapons) { return size_t(U) * 16 + size_t(weapons) * 4; }       // UNIS 4048, UNIx 4168
size_t upgradeSectionSize(int n) { return size_t(n) + (n == kSettingUpgrades) + size_t(n) * 12; }  // UPGS 598, UPGx 794
size_t techSectionSize(int n) { return size_t(n) * 9; }                                   // TECS 216, TECx 396
size_t levelSectionSize(int n) { return size_t(n) * 38; }                                 // UPGR 1748, PUPx 2318; PTEC 912, PTEx 1672
constexpr size_t kPuniSize = size_t(U) * 25;                                              // 5700

// Reads that give `fallback` past the end of a short section (protected maps).
struct Reader {
    const Bytes& d;
    uint8_t u8(size_t o, uint8_t fallback) const { return o < d.size() ? d[o] : fallback; }
    uint16_t u16(size_t o, uint16_t fallback) const { return o + 2 <= d.size() ? readU16(d, o) : fallback; }
    uint32_t u32(size_t o, uint32_t fallback) const { return o + 4 <= d.size() ? readU32(d, o) : fallback; }
    bool flag(size_t o, bool fallback) const { return o < d.size() ? d[o] != 0 : fallback; }
};

// Writes into a section: only where the value changes (a flag keeps its byte while its meaning stays: a 3 stays 3).
struct Writer {
    Bytes& d;
    void u8(size_t o, uint8_t v) { d[o] = v; }
    void u16(size_t o, uint16_t v) { d[o] = uint8_t(v), d[o + 1] = uint8_t(v >> 8); }
    void u32(size_t o, uint32_t v) {
        for (int i = 0; i < 4; ++i) d[o + size_t(i)] = uint8_t(v >> (8 * i));
    }
    void flag(size_t o, bool v) {
        if ((d[o] != 0) != v) d[o] = v ? 1 : 0;
    }
};

// The section the game reads for a pair: by the map's version, else the one the map has.
const Section* pick(const Chk& chk, SectionName bw, SectionName original, bool& isBw) {
    const Section* x = chk.find(bw);
    const Section* o = chk.find(original);
    const bool wantBw = settingsBroodWar(chk);
    isBw = wantBw ? x != nullptr || !o : !o && x;
    return isBw ? x : o;
}

// Each section of a pair that the map has gets `patch` (made big enough); when the map has neither the main one for
// its version is made (all zero, then `patch` with `full` set writes every field).
void eachSection(Chk& chk, std::initializer_list<std::pair<SectionName, int>> pair, size_t (*size)(int),
                 const std::function<void(Bytes&, int, bool full)>& patch) {
    const bool bw = settingsBroodWar(chk);
    bool any = false;
    for (const auto& [name, n] : pair)
        if (Section* s = chk.find(name)) {
            if (s->data.size() < size(n)) s->data.resize(size(n), 0);
            patch(s->data, n, false);
            any = true;
        }
    if (any) return;
    const auto& [name, n] = bw ? *pair.begin() : *(pair.begin() + 1);
    Bytes d(size(n), 0);
    patch(d, n, true);
    chk.set(name, std::move(d));
}

}  // namespace

bool settingsBroodWar(const Chk& chk) {
    const Section* ver = chk.find("VER ");
    return ver && ver->data.size() >= 2 && readU16(ver->data, 0) >= 63;
}

// --- Defaults ---------------------------------------------------------------------------------------------------

bool SettingDefaults::load(const GameData& data, std::string& error) {
    if (const UnitDataLoader* l = data.unitDataLoader(); l && l->settings) return l->settings(data, *this, error);
    Bytes ud, wd, gd, td;
    if (!data.readFile("arr\\units.dat", ud, error) || !data.readFile("arr\\weapons.dat", wd, error) ||
        !data.readFile("arr\\upgrades.dat", gd, error) || !data.readFile("arr\\techdata.dat", td, error))
        return false;
    if (ud.size() != 19876 || wd.size() != 5460 || gd.size() != 1281 || td.size() != 836) {
        error = "unexpected .dat file sizes";
        return false;
    }
    // units.dat arrays: the same offsets new maps take their settings from.
    for (int i = 0; i < U; ++i) {
        UnitSettings::Unit& u = units.units[size_t(i)];
        u.useDefault = true;
        u.hitPoints = readU32(ud, 3156 + 4 * size_t(i));
        u.shields = readU16(ud, 2700 + 2 * size_t(i));
        u.armor = ud[8856 + size_t(i)];
        u.buildTime = readU16(ud, 15772 + 2 * size_t(i));
        u.minerals = readU16(ud, 14860 + 2 * size_t(i));
        u.gas = readU16(ud, 15316 + 2 * size_t(i));
        u.nameString = 0;
        UnitSettings::Availability& a = units.availability[size_t(i)];
        a.global = true;
        a.player.fill(true), a.usesDefault.fill(true);
        const uint16_t sub = readU16(ud, 228 + 2 * size_t(i));
        auto& w = unitWeapons[size_t(i)];
        w = {ud[5892 + size_t(i)], ud[6348 + size_t(i)], uint8_t(kSettingWeapons), uint8_t(kSettingWeapons)};
        if (sub < U) w[2] = ud[5892 + sub], w[3] = ud[6348 + sub];
    }
    for (int i = 0; i < kSettingWeapons; ++i)
        units.weapons[size_t(i)] = {readU16(wd, 3640 + 2 * size_t(i)), readU16(wd, 3900 + 2 * size_t(i))};
    // upgrades.dat: minerals, minerals factor, gas, gas factor, time, time factor (u16 each), icon u16 @854, max repeats u8 @1159.
    for (int i = 0; i < kSettingUpgrades; ++i) {
        UpgradeSettings::Upgrade& g = upgrades.upgrades[size_t(i)];
        const size_t o = 2 * size_t(i);
        g = {true, readU16(gd, 0 + o), readU16(gd, 122 + o), readU16(gd, 244 + o), readU16(gd, 366 + o), readU16(gd, 488 + o), readU16(gd, 610 + o)};
        UpgradeSettings::Levels& l = upgrades.levels[size_t(i)];
        l.defaultMax = gd[1159 + size_t(i)], l.defaultStart = 0;
        l.max.fill(l.defaultMax), l.start.fill(0), l.usesDefault.fill(true);
        upgradeIcon[size_t(i)] = readU16(gd, 854 + o);
    }
    // techdata.dat: minerals, gas, time, energy (u16 each), icon u16 @528.
    for (int i = 0; i < kSettingTechs; ++i) {
        TechSettings::Tech& t = techs.techs[size_t(i)];
        const size_t o = 2 * size_t(i);
        t = {true, readU16(td, 0 + o), readU16(td, 88 + o), readU16(td, 176 + o), readU16(td, 264 + o)};
        TechSettings::Players& p = techs.players[size_t(i)];
        p.defaultAvailable = true, p.defaultResearched = t.minerals == 0 && t.gas == 0;
        p.available.fill(true), p.researched.fill(p.defaultResearched), p.usesDefault.fill(true);
        techIcon[size_t(i)] = readU16(td, 528 + o);
    }
    return true;
}

// --- Units: UNIx / UNIS, PUNI -----------------------------------------------------------------------------------
// u8 use default[228], u32 hit points[228], u16 shields[228], u8 armor[228], u16 build time[228], u16 minerals[228],
// u16 gas[228], u16 name string[228], u16 damage[weapons], u16 bonus[weapons]. PUNI: u8 player[12][228],
// u8 global[228], u8 player uses the global value[12][228].

namespace {
struct UnitOffsets {
    static constexpr size_t use = 0, hp = 228, shields = 1140, armor = 1596, time = 1824, minerals = 2280, gas = 2736, name = 3192, damage = 3648;
};
}  // namespace

UnitSettings readUnitSettings(const Chk& chk, const SettingDefaults& defaults) {
    UnitSettings s = defaults.units;
    bool bw = false;
    if (const Section* sec = pick(chk, "UNIx", "UNIS", bw)) {
        const Reader r{sec->data};
        const int nw = bw ? kSettingWeapons : kOriginalWeapons;
        using O = UnitOffsets;
        for (size_t i = 0; i < size_t(U); ++i) {
            UnitSettings::Unit& u = s.units[i];
            u.useDefault = r.flag(O::use + i, u.useDefault);
            u.hitPoints = r.u32(O::hp + 4 * i, u.hitPoints);
            u.shields = r.u16(O::shields + 2 * i, u.shields);
            u.armor = r.u8(O::armor + i, u.armor);
            u.buildTime = r.u16(O::time + 2 * i, u.buildTime);
            u.minerals = r.u16(O::minerals + 2 * i, u.minerals);
            u.gas = r.u16(O::gas + 2 * i, u.gas);
            u.nameString = r.u16(O::name + 2 * i, u.nameString);
        }
        for (size_t w = 0; w < size_t(nw); ++w) {
            s.weapons[w].damage = r.u16(O::damage + 2 * w, s.weapons[w].damage);
            s.weapons[w].bonus = r.u16(O::damage + 2 * size_t(nw) + 2 * w, s.weapons[w].bonus);
        }
    }
    if (const Section* sec = chk.find("PUNI")) {
        const Reader r{sec->data};
        for (size_t i = 0; i < size_t(U); ++i) {
            UnitSettings::Availability& a = s.availability[i];
            for (size_t p = 0; p < size_t(P); ++p) {
                a.player[p] = r.flag(p * U + i, a.player[p]);
                a.usesDefault[p] = r.flag(size_t(U) * 13 + p * U + i, a.usesDefault[p]);
            }
            a.global = r.flag(size_t(U) * 12 + i, a.global);
        }
    }
    return s;
}

bool writeUnitSettings(Chk& chk, const UnitSettings& from, const UnitSettings& to) {
    bool changed = false;
    if (from.units != to.units || from.weapons != to.weapons) {
        changed = true;
        eachSection(chk, {{"UNIx", kSettingWeapons}, {"UNIS", kOriginalWeapons}}, unitSectionSize, [&](Bytes& d, int nw, bool full) {
            Writer w{d};
            using O = UnitOffsets;
            for (size_t i = 0; i < size_t(U); ++i) {
                const UnitSettings::Unit &a = from.units[i], &b = to.units[i];
                if (full || a.useDefault != b.useDefault) full ? w.u8(O::use + i, b.useDefault) : w.flag(O::use + i, b.useDefault);
                if (full || a.hitPoints != b.hitPoints) w.u32(O::hp + 4 * i, b.hitPoints);
                if (full || a.shields != b.shields) w.u16(O::shields + 2 * i, b.shields);
                if (full || a.armor != b.armor) w.u8(O::armor + i, b.armor);
                if (full || a.buildTime != b.buildTime) w.u16(O::time + 2 * i, b.buildTime);
                if (full || a.minerals != b.minerals) w.u16(O::minerals + 2 * i, b.minerals);
                if (full || a.gas != b.gas) w.u16(O::gas + 2 * i, b.gas);
                if (full || a.nameString != b.nameString) w.u16(O::name + 2 * i, b.nameString);
            }
            for (size_t k = 0; k < size_t(nw); ++k) {
                if (full || from.weapons[k].damage != to.weapons[k].damage) w.u16(O::damage + 2 * k, to.weapons[k].damage);
                if (full || from.weapons[k].bonus != to.weapons[k].bonus) w.u16(O::damage + 2 * size_t(nw) + 2 * k, to.weapons[k].bonus);
            }
        });
    }
    if (from.availability != to.availability) {
        changed = true;
        Section* sec = chk.find("PUNI");
        const bool full = !sec;
        if (!sec) chk.set("PUNI", Bytes(kPuniSize, 0)), sec = chk.find("PUNI");
        else if (sec->data.size() < kPuniSize) sec->data.resize(kPuniSize, 0);
        Writer w{sec->data};
        auto flag = [&](size_t o, bool was, bool now) {
            if (full) w.u8(o, now);
            else if (was != now) w.flag(o, now);
        };
        for (size_t i = 0; i < size_t(U); ++i) {
            const UnitSettings::Availability &a = from.availability[i], &b = to.availability[i];
            for (size_t p = 0; p < size_t(P); ++p) {
                flag(p * U + i, a.player[p], b.player[p]);
                flag(size_t(U) * 13 + p * U + i, a.usesDefault[p], b.usesDefault[p]);
            }
            flag(size_t(U) * 12 + i, a.global, b.global);
        }
    }
    return changed;
}

// --- Upgrades: UPGx / UPGS, PUPx / UPGR -------------------------------------------------------------------------
// UPGx: u8 use default[61], u8 unused, then u16[61] each: minerals, minerals per level, gas, gas per level, time, time
// per level. UPGS: the same for 46 upgrades without the unused byte. PUPx / UPGR: u8 max level[12][n], u8 start
// level[12][n], u8 default max[n], u8 default start[n], u8 player uses the defaults[12][n].

namespace {
uint16_t& upgradeField(UpgradeSettings::Upgrade& g, int k) {
    uint16_t* f[6] = {&g.minerals, &g.mineralsPerLevel, &g.gas, &g.gasPerLevel, &g.time, &g.timePerLevel};
    return *f[k];
}
uint16_t upgradeField(const UpgradeSettings::Upgrade& g, int k) { return upgradeField(const_cast<UpgradeSettings::Upgrade&>(g), k); }
size_t upgradeOffset(int n, int k, size_t i) { return size_t(n) + (n == kSettingUpgrades) + size_t(k) * 2 * size_t(n) + 2 * i; }
}  // namespace

UpgradeSettings readUpgradeSettings(const Chk& chk, const SettingDefaults& defaults) {
    UpgradeSettings s = defaults.upgrades;
    bool bw = false;
    if (const Section* sec = pick(chk, "UPGx", "UPGS", bw)) {
        const Reader r{sec->data};
        const int n = bw ? kSettingUpgrades : kOriginalUpgrades;
        for (size_t i = 0; i < size_t(n); ++i) {
            UpgradeSettings::Upgrade& g = s.upgrades[i];
            g.useDefault = r.flag(i, g.useDefault);
            for (int k = 0; k < 6; ++k) upgradeField(g, k) = r.u16(upgradeOffset(n, k, i), upgradeField(g, k));
        }
    }
    if (const Section* sec = pick(chk, "PUPx", "UPGR", bw)) {
        const Reader r{sec->data};
        const size_t n = size_t(bw ? kSettingUpgrades : kOriginalUpgrades);
        for (size_t i = 0; i < n; ++i) {
            UpgradeSettings::Levels& l = s.levels[i];
            for (size_t p = 0; p < size_t(P); ++p) {
                l.max[p] = r.u8(p * n + i, l.max[p]);
                l.start[p] = r.u8(12 * n + p * n + i, l.start[p]);
                l.usesDefault[p] = r.flag(26 * n + p * n + i, l.usesDefault[p]);
            }
            l.defaultMax = r.u8(24 * n + i, l.defaultMax);
            l.defaultStart = r.u8(25 * n + i, l.defaultStart);
        }
    }
    return s;
}

bool writeUpgradeSettings(Chk& chk, const UpgradeSettings& from, const UpgradeSettings& to) {
    bool changed = false;
    if (from.upgrades != to.upgrades) {
        changed = true;
        eachSection(chk, {{"UPGx", kSettingUpgrades}, {"UPGS", kOriginalUpgrades}}, upgradeSectionSize, [&](Bytes& d, int n, bool full) {
            Writer w{d};
            for (size_t i = 0; i < size_t(n); ++i) {
                const UpgradeSettings::Upgrade &a = from.upgrades[i], &b = to.upgrades[i];
                if (full) w.u8(i, b.useDefault);
                else if (a.useDefault != b.useDefault) w.flag(i, b.useDefault);
                for (int k = 0; k < 6; ++k)
                    if (full || upgradeField(a, k) != upgradeField(b, k)) w.u16(upgradeOffset(n, k, i), upgradeField(b, k));
            }
        });
    }
    if (from.levels != to.levels) {
        changed = true;
        eachSection(chk, {{"PUPx", kSettingUpgrades}, {"UPGR", kOriginalUpgrades}}, levelSectionSize, [&](Bytes& d, int ni, bool full) {
            Writer w{d};
            const size_t n = size_t(ni);
            for (size_t i = 0; i < n; ++i) {
                const UpgradeSettings::Levels &a = from.levels[i], &b = to.levels[i];
                for (size_t p = 0; p < size_t(P); ++p) {
                    if (full || a.max[p] != b.max[p]) w.u8(p * n + i, b.max[p]);
                    if (full || a.start[p] != b.start[p]) w.u8(12 * n + p * n + i, b.start[p]);
                    if (full) w.u8(26 * n + p * n + i, b.usesDefault[p]);
                    else if (a.usesDefault[p] != b.usesDefault[p]) w.flag(26 * n + p * n + i, b.usesDefault[p]);
                }
                if (full || a.defaultMax != b.defaultMax) w.u8(24 * n + i, b.defaultMax);
                if (full || a.defaultStart != b.defaultStart) w.u8(25 * n + i, b.defaultStart);
            }
        });
    }
    return changed;
}

// --- Techs: TECx / TECS, PTEx / PTEC ----------------------------------------------------------------------------
// TECx / TECS: u8 use default[n], then u16[n] each: minerals, gas, time, energy. PTEx / PTEC: u8 available[12][n],
// u8 researched[12][n], u8 default available[n], u8 default researched[n], u8 player uses the defaults[12][n].

namespace {
uint16_t& techField(TechSettings::Tech& t, int k) {
    uint16_t* f[4] = {&t.minerals, &t.gas, &t.time, &t.energy};
    return *f[k];
}
uint16_t techField(const TechSettings::Tech& t, int k) { return techField(const_cast<TechSettings::Tech&>(t), k); }
}  // namespace

TechSettings readTechSettings(const Chk& chk, const SettingDefaults& defaults) {
    TechSettings s = defaults.techs;
    bool bw = false;
    if (const Section* sec = pick(chk, "TECx", "TECS", bw)) {
        const Reader r{sec->data};
        const size_t n = size_t(bw ? kSettingTechs : kOriginalTechs);
        for (size_t i = 0; i < n; ++i) {
            TechSettings::Tech& t = s.techs[i];
            t.useDefault = r.flag(i, t.useDefault);
            for (int k = 0; k < 4; ++k) techField(t, k) = r.u16(n + size_t(k) * 2 * n + 2 * i, techField(t, k));
        }
    }
    if (const Section* sec = pick(chk, "PTEx", "PTEC", bw)) {
        const Reader r{sec->data};
        const size_t n = size_t(bw ? kSettingTechs : kOriginalTechs);
        for (size_t i = 0; i < n; ++i) {
            TechSettings::Players& t = s.players[i];
            for (size_t p = 0; p < size_t(P); ++p) {
                t.available[p] = r.flag(p * n + i, t.available[p]);
                t.researched[p] = r.flag(12 * n + p * n + i, t.researched[p]);
                t.usesDefault[p] = r.flag(26 * n + p * n + i, t.usesDefault[p]);
            }
            t.defaultAvailable = r.flag(24 * n + i, t.defaultAvailable);
            t.defaultResearched = r.flag(25 * n + i, t.defaultResearched);
        }
    }
    return s;
}

bool writeTechSettings(Chk& chk, const TechSettings& from, const TechSettings& to) {
    bool changed = false;
    if (from.techs != to.techs) {
        changed = true;
        eachSection(chk, {{"TECx", kSettingTechs}, {"TECS", kOriginalTechs}}, techSectionSize, [&](Bytes& d, int ni, bool full) {
            Writer w{d};
            const size_t n = size_t(ni);
            for (size_t i = 0; i < n; ++i) {
                const TechSettings::Tech &a = from.techs[i], &b = to.techs[i];
                if (full) w.u8(i, b.useDefault);
                else if (a.useDefault != b.useDefault) w.flag(i, b.useDefault);
                for (int k = 0; k < 4; ++k)
                    if (full || techField(a, k) != techField(b, k)) w.u16(n + size_t(k) * 2 * n + 2 * i, techField(b, k));
            }
        });
    }
    if (from.players != to.players) {
        changed = true;
        eachSection(chk, {{"PTEx", kSettingTechs}, {"PTEC", kOriginalTechs}}, levelSectionSize, [&](Bytes& d, int ni, bool full) {
            Writer w{d};
            const size_t n = size_t(ni);
            auto flag = [&](size_t o, bool was, bool now) {
                if (full) w.u8(o, now);
                else if (was != now) w.flag(o, now);
            };
            for (size_t i = 0; i < n; ++i) {
                const TechSettings::Players &a = from.players[i], &b = to.players[i];
                for (size_t p = 0; p < size_t(P); ++p) {
                    flag(p * n + i, a.available[p], b.available[p]);
                    flag(12 * n + p * n + i, a.researched[p], b.researched[p]);
                    flag(26 * n + p * n + i, a.usesDefault[p], b.usesDefault[p]);
                }
                flag(24 * n + i, a.defaultAvailable, b.defaultAvailable);
                flag(25 * n + i, a.defaultResearched, b.defaultResearched);
            }
        });
    }
    return changed;
}

}  // namespace stardraft
