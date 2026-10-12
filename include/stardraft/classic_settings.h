#pragma once

// The settings sections of a classic map (roadmap L.3), as StarEdit's Unit, Upgrade and Tech settings edit them:
//   units:    UNIx (Brood War, 130 weapons) and UNIS (original, 100 weapons), PUNI (who can build each unit)
//   upgrades: UPGx (61 upgrades) and UPGS (46), PUPx (61) and UPGR (46) (levels per player)
//   techs:    TECx (44 techs) and TECS (24), PTEx (44) and PTEC (24) (available and researched per player)
// The layouts are the staredit.net wiki's (docs/scenario.chk.wiki), checked against a corpus of maps.
//
// Each kind is read from the Brood War section when the map has it (else the original one; else the game's values),
// and written into each section of the pair that the map has (the main one of the map's version is made when it is
// not there). Only the fields that changed are written; every other byte stays as it is.

#include <array>
#include <cstdint>
#include <string>

#include "stardraft/chk.h"

namespace stardraft {

class GameData;

constexpr int kSettingUnits = 228, kSettingWeapons = 130, kSettingUpgrades = 61, kSettingTechs = 44;
constexpr int kOriginalWeapons = 100, kOriginalUpgrades = 46, kOriginalTechs = 24;
constexpr int kSettingPlayers = 12;

struct UnitSettings {
    struct Unit {
        bool useDefault = true;
        uint32_t hitPoints = 0;  // 1/256 hit points (the low byte is the fraction)
        uint16_t shields = 0;
        uint8_t armor = 0;
        uint16_t buildTime = 0;  // frames
        uint16_t minerals = 0, gas = 0;
        uint16_t nameString = 0;  // map string id; 0: the game's name
        friend bool operator==(const Unit&, const Unit&) = default;
    };
    struct Weapon {
        uint16_t damage = 0, bonus = 0;  // base damage, damage added by each upgrade level
        friend bool operator==(const Weapon&, const Weapon&) = default;
    };
    // PUNI: who can build the unit.
    struct Availability {
        bool global = true;                          // the default for all players
        std::array<bool, kSettingPlayers> player{};  // when the player does not use the default
        std::array<bool, kSettingPlayers> usesDefault{};
        friend bool operator==(const Availability&, const Availability&) = default;
    };
    std::array<Unit, kSettingUnits> units{};
    std::array<Weapon, kSettingWeapons> weapons{};
    std::array<Availability, kSettingUnits> availability{};
    friend bool operator==(const UnitSettings&, const UnitSettings&) = default;
};

struct UpgradeSettings {
    struct Upgrade {
        bool useDefault = true;
        uint16_t minerals = 0, mineralsPerLevel = 0, gas = 0, gasPerLevel = 0, time = 0, timePerLevel = 0;
        friend bool operator==(const Upgrade&, const Upgrade&) = default;
    };
    // PUPx / UPGR: the levels.
    struct Levels {
        uint8_t defaultMax = 0, defaultStart = 0;
        std::array<uint8_t, kSettingPlayers> max{}, start{};
        std::array<bool, kSettingPlayers> usesDefault{};
        friend bool operator==(const Levels&, const Levels&) = default;
    };
    std::array<Upgrade, kSettingUpgrades> upgrades{};
    std::array<Levels, kSettingUpgrades> levels{};
    friend bool operator==(const UpgradeSettings&, const UpgradeSettings&) = default;
};

struct TechSettings {
    struct Tech {
        bool useDefault = true;
        uint16_t minerals = 0, gas = 0, time = 0, energy = 0;
        friend bool operator==(const Tech&, const Tech&) = default;
    };
    // PTEx / PTEC: available and researched at the start.
    struct Players {
        bool defaultAvailable = true, defaultResearched = false;
        std::array<bool, kSettingPlayers> available{}, researched{}, usesDefault{};
        friend bool operator==(const Players&, const Players&) = default;
    };
    std::array<Tech, kSettingTechs> techs{};
    std::array<Players, kSettingTechs> players{};
    friend bool operator==(const TechSettings&, const TechSettings&) = default;
};

// The game's values (units.dat, weapons.dat, upgrades.dat, techdata.dat): what "use default" gives.
struct SettingDefaults {
    UnitSettings units;        // every unit "use default", the .dat values, every unit available to all
    UpgradeSettings upgrades;  // max level from upgrades.dat, start level 0
    TechSettings techs;        // available; researched when it costs nothing (as StarEdit's new maps)
    // The weapons of each unit (units.dat ground and air weapon, and those of its turret, subunit 1); 130: none.
    std::array<std::array<uint8_t, 4>, kSettingUnits> unitWeapons{};
    std::array<uint16_t, kSettingUpgrades> upgradeIcon{};  // cmdicons frame
    std::array<uint16_t, kSettingTechs> techIcon{};
    bool load(const GameData& data, std::string& error);
};

// Is the map a Brood War map (VER 63 and later)? Then the Brood War sections are the ones the game reads.
bool settingsBroodWar(const Chk& chk);

UnitSettings readUnitSettings(const Chk& chk, const SettingDefaults& defaults);
UpgradeSettings readUpgradeSettings(const Chk& chk, const SettingDefaults& defaults);
TechSettings readTechSettings(const Chk& chk, const SettingDefaults& defaults);

// Write `to` where it differs from `from` (what read*Settings gave for this chk). Returns false when nothing changed.
bool writeUnitSettings(Chk& chk, const UnitSettings& from, const UnitSettings& to);
bool writeUpgradeSettings(Chk& chk, const UpgradeSettings& from, const UpgradeSettings& to);
bool writeTechSettings(Chk& chk, const TechSettings& from, const TechSettings& to);

}  // namespace stardraft
