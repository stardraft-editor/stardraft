#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "stardraft/chk.h"

namespace stardraft {

class GameData;
class UnitTypes;
class UnitSprites;
struct SettingDefaults;
struct Tileset;

// Game files from somewhere other than a CASC storage: a StarCraft 1.16.1 game or a mod (src/mods).
class FileSource {
public:
    virtual ~FileSource() = default;
    virtual bool readFile(const std::string& name, Bytes& out, std::string& error) const = 0;
    virtual bool hasFile(const std::string& name, uint64_t* size) const = 0;
    virtual std::vector<std::string> listFiles() const = 0;
};

// A game whose unit data is not in the classic .dat files (a mod's .datex) loads it with these: UnitTypes::load and
// UnitSprites::load call them.
struct UnitDataLoader {
    bool (*types)(const GameData& data, UnitTypes& out, std::string& error) = nullptr;
    bool (*sprites)(const GameData& data, UnitSprites& out, std::string& error) = nullptr;
    bool (*settings)(const GameData& data, SettingDefaults& out, std::string& error) = nullptr;  // SettingDefaults::load
    void (*tileset)(Tileset& loaded) = nullptr;  // Tileset::load, after it read the files: what the mod adds
    // The player colors by their number in a map's COLR, when the game has more than StarCraft's (a mod's tunit.pcx rows
    // and their names); nullptr: StarCraft's.
    const std::vector<std::string>& (*colorNames)() = nullptr;
    // A trigger Comment's text with a location's name changed where the game's script in it uses the location (a mod's
    // trigger commands in Comments: Cosmonarchy's CRIG); nullptr: Comments are only text.
    std::string (*renameInComment)(const std::string& text, const std::string& from, const std::string& to) = nullptr;
};

// Read-only access to StarCraft: Remastered game files stored in CASC
// (the install folder containing .build.info, e.g. "...\Starcraft Remastered\StarCraft").
class GameData {
public:
    GameData();
    ~GameData();
    GameData(const GameData&) = delete;
    GameData& operator=(const GameData&) = delete;

    bool open(const std::filesystem::path& installDir, std::string& error);
    // Files from `source` in place of a CASC storage, and the game's own unit data loader (none: the .dat files).
    void open(std::unique_ptr<FileSource> source, const UnitDataLoader* loader = nullptr);
    bool isOpen() const;
    const UnitDataLoader* unitDataLoader() const;

    // Read a file by its CASC name, e.g. "tileset/badlands.cv5".
    bool readFile(const std::string& name, Bytes& out, std::string& error) const;
    // Whether the storage has the file (and its size), without reading it.
    bool hasFile(const std::string& name, uint64_t* size = nullptr) const;

    // All file names in the storage (from its root manifest).
    std::vector<std::string> listFiles() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace stardraft
