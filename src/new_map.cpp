#include "stardraft/new_map.h"

#include "byteio.h"
#include "stardraft/game_data.h"
#include "stardraft/isom.h"
#include "stardraft/locations.h"
#include "stardraft/strings.h"
#include "stardraft/tileset.h"

namespace stardraft {

namespace {

constexpr int kUnits = 228, kWeapons = 130, kUpgrades = 61, kTechs = 44;

// VCOD: the verification code that every map has, the same 1040 bytes (src/vcod.inc).
const uint8_t kVcod[1040] =
#include "vcod.inc"
    ;

void u8s(Bytes& d, int n, uint8_t v) { d.insert(d.end(), size_t(n), v); }

}  // namespace

bool createNewScenario(Chk& chk, const GameData& data, uint16_t width, uint16_t height, uint16_t era, uint16_t terrain,
                       std::string& error) {
    Bytes vcod(std::begin(kVcod), std::end(kVcod)), units, upgrades, techs, weapons;
    if (!data.readFile("arr\\units.dat", units, error) ||
        !data.readFile("arr\\upgrades.dat", upgrades, error) || !data.readFile("arr\\techdata.dat", techs, error) ||
        !data.readFile("arr\\weapons.dat", weapons, error))
        return false;
    if (units.size() != 19876 || upgrades.size() != 1281 || techs.size() != 836 || weapons.size() != 5460) {
        error = "unexpected .dat file sizes";
        return false;
    }

    chk = Chk{};
    auto put = [&](SectionName name, Bytes d) { chk.sections.push_back({name, std::move(d)}); };
    Bytes d;

    appendU32(d, 0x42574152);  // "RAWB": Brood War
    put("TYPE", std::move(d)), d.clear();
    appendU16(d, 206);  // Remastered, Brood War
    put("VER ", std::move(d)), d.clear();
    appendU16(d, 11);
    put("IVE2", std::move(d)), d.clear();
    put("VCOD", vcod);
    // Players 1-8 open human slots, 9-11 inactive, 12 neutral.
    Bytes owners = {6, 6, 6, 6, 6, 6, 6, 6, 0, 0, 0, 7};
    put("IOWN", owners);
    put("OWNR", owners);
    appendU16(d, era);
    put("ERA ", std::move(d)), d.clear();
    appendU16(d, width), appendU16(d, height);
    put("DIM ", std::move(d)), d.clear();
    put("SIDE", Bytes{5, 5, 5, 5, 5, 5, 5, 5, 7, 7, 7, 4});
    put("MTXM", Bytes(size_t(width) * height * 2, 0));

    // Unit availability: everything available, players use the defaults.
    u8s(d, 12 * kUnits, 1), u8s(d, kUnits, 1), u8s(d, 12 * kUnits, 1);
    put("PUNI", std::move(d)), d.clear();
    put("UNIT", {});
    put("ISOM", {});
    put("TILE", {});
    put("DD2 ", {});
    put("THG2", {});
    put("MASK", Bytes(size_t(width) * height, 0xFF));

    StringTable strings;
    uint32_t nameStr = strings.add("Untitled Scenario");
    uint32_t descStr = strings.add("Destroy all enemy buildings.");
    uint32_t anywhereStr = strings.add("Anywhere");
    std::vector<Location> locations(255);
    locations[kAnywhereLocation] = {0, 0, uint32_t(width) * 32, uint32_t(height) * 32, uint16_t(anywhereStr), 0};

    put("STR ", {});
    put("UPRP", Bytes(1280, 0));
    put("UPUS", Bytes(64, 0));
    put("MRGN", {});
    put("TRIG", {});
    put("MBRF", {});
    appendU16(d, uint16_t(nameStr)), appendU16(d, uint16_t(descStr));
    put("SPRP", std::move(d)), d.clear();
    // Forces: everyone in force 1, default names, no special properties.
    u8s(d, 8, 0), u8s(d, 8, 0), u8s(d, 4, 0);
    put("FORC", std::move(d)), d.clear();
    put("WAV ", Bytes(2048, 0));
    put("SWNM", Bytes(1024, 0));
    put("COLR", Bytes{0, 1, 2, 3, 4, 5, 6, 7});

    // Upgrade restrictions: per player max/start, global max (upgrades.dat max repeats) / start, use defaults.
    u8s(d, 12 * kUpgrades, 0), u8s(d, 12 * kUpgrades, 0);
    for (int i = 0; i < kUpgrades; ++i) d.push_back(upgrades[1159 + i]);
    u8s(d, kUpgrades, 0), u8s(d, 12 * kUpgrades, 1);
    put("PUPx", std::move(d)), d.clear();
    // Tech restrictions: available everywhere; techs that cost nothing to research start researched.
    u8s(d, 12 * kTechs, 1), u8s(d, 12 * kTechs, 0);
    u8s(d, kTechs, 1);
    for (int i = 0; i < kTechs; ++i) d.push_back(readU16(techs, 2 * i) == 0 && readU16(techs, 88 + 2 * i) == 0 ? 1 : 0);
    u8s(d, 12 * kTechs, 1);
    put("PTEx", std::move(d)), d.clear();

    // Unit settings: "use defaults" set, values copied from units.dat / weapons.dat.
    u8s(d, kUnits, 1);
    for (int i = 0; i < kUnits; ++i) appendU32(d, readU32(units, 3156 + 4 * i));        // hit points
    for (int i = 0; i < kUnits; ++i) appendU16(d, readU16(units, 2700 + 2 * i));        // shields
    for (int i = 0; i < kUnits; ++i) d.push_back(units[8856 + i]);                      // armor
    for (int i = 0; i < kUnits; ++i) appendU16(d, readU16(units, 15772 + 2 * i));       // build time
    for (int i = 0; i < kUnits; ++i) appendU16(d, readU16(units, 14860 + 2 * i));       // minerals
    for (int i = 0; i < kUnits; ++i) appendU16(d, readU16(units, 15316 + 2 * i));       // gas
    for (int i = 0; i < kUnits; ++i) appendU16(d, 0);                                   // name string (default)
    for (int i = 0; i < kWeapons; ++i) appendU16(d, readU16(weapons, 3640 + 2 * i));    // damage
    for (int i = 0; i < kWeapons; ++i) appendU16(d, readU16(weapons, 3900 + 2 * i));    // upgrade bonus
    put("UNIx", std::move(d)), d.clear();
    // Upgrade settings from upgrades.dat.
    u8s(d, kUpgrades, 1), d.push_back(0);
    for (size_t field : {0u, 122u, 244u, 366u, 488u, 610u})
        for (int i = 0; i < kUpgrades; ++i) appendU16(d, readU16(upgrades, field + 2 * i));
    put("UPGx", std::move(d)), d.clear();
    // Tech settings from techdata.dat: minerals, gas, research time, energy.
    u8s(d, kTechs, 1);
    for (size_t field : {0u, 88u, 176u, 264u})
        for (int i = 0; i < kTechs; ++i) appendU16(d, readU16(techs, field + 2 * i));
    put("TECx", std::move(d)), d.clear();

    strings.write(chk);
    writeLocations(chk, locations);

    // Terrain: ISOM filled with `terrain`, TILE/MTXM generated from it (StarEdit's new map, from the tileset's CV5).
    Tileset tileset;
    if (!tileset.load(data, era, error)) return false;
    IsomGrid grid;
    return grid.create(chk, width, height, era, terrain, tileset, error);
}

}  // namespace stardraft
