// Experimental: the game's data files as a map plays them, for the simulation (the editor's Tools > Simulate and the
// command line's simulate-* commands give them to Simulator::init).
#pragma once

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "stardraft/chk.h"
#include "stardraft/classic_settings.h"
#include "stardraft/dat_edit.h"
#include "stardraft/game_data.h"

namespace sim {

// The .dat files that differ from the game's (by CASC name, "arr/units.dat"): first `project` (a project's edited
// .dat files, by the same names), then the map's unit settings (hit points, shields, armor, costs, build time, weapon
// damage and bonus) where the project does not change the value. The game takes the map's settings when it loads the
// map; a project's EUD writes its own values over them at the start.
inline std::map<std::string, stardraft::Bytes> dataFilesForMap(const stardraft::GameData& data, const stardraft::Chk& chk,
                                                               std::map<std::string, stardraft::Bytes> project = {}) {
    using namespace stardraft;
    std::map<std::string, Bytes> files = std::move(project);
    std::string e;
    static SettingDefaults defaults;
    static bool defaultsRead = false;
    if (!defaultsRead) defaultsRead = defaults.load(data, e);
    if (!defaultsRead) return files;
    const UnitSettings map = readUnitSettings(chk, defaults);
    std::map<std::string, Bytes> stock;
    // One value of a .dat: where it is in the file (these settings' fields have one value per entry).
    auto put = [&](const char* file, const char* field, int entry, uint32_t value) {
        const DatSchema* schema = DatSchema::find(file);
        const DatField* f = nullptr;
        if (schema)
            for (const DatField& d : schema->fields)
                if (d.name == field) f = &d;
        if (!f || !f->covers(entry)) return;
        const std::string name = "arr/" + std::string(file) + ".dat";
        Bytes& original = stock[name];
        if (original.empty() && !data.readFile(name, original, e)) return;
        Bytes& b = files[name];
        if (b.empty()) b = original;
        const size_t at = f->fileOffset + size_t(entry - f->first) * f->stride();
        if (at + size_t(f->size) > b.size() || at + size_t(f->size) > original.size()) return;
        if (std::memcmp(&b[at], &original[at], size_t(f->size)) != 0) return;  // (the project changes it: the project's value)
        for (int k = 0; k < f->size; ++k) b[at + size_t(k)] = uint8_t(value >> (8 * k));
    };
    for (int u = 0; u < kSettingUnits; ++u) {
        const UnitSettings::Unit& s = map.units[size_t(u)];
        if (s.useDefault) continue;
        put("units", "Hit Points", u, s.hitPoints), put("units", "Shield Amount", u, s.shields), put("units", "Armor", u, s.armor);
        put("units", "Mineral Cost", u, s.minerals), put("units", "Vespene Cost", u, s.gas), put("units", "Build Time", u, s.buildTime);
    }
    for (int w = 0; w < kSettingWeapons; ++w) {
        const UnitSettings::Weapon& s = map.weapons[size_t(w)];
        if (s == defaults.units.weapons[size_t(w)]) continue;
        put("weapons", "Damage Amount", w, s.damage), put("weapons", "Damage Bonus", w, s.bonus);
    }
    return files;
}

}  // namespace sim
