#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "stardraft/chk.h"

namespace stardraft {

// Read-only access to StarCraft: Remastered game files stored in CASC
// (the install folder containing .build.info, e.g. "...\Starcraft Remastered\StarCraft").
class GameData {
public:
    GameData();
    ~GameData();
    GameData(const GameData&) = delete;
    GameData& operator=(const GameData&) = delete;

    bool open(const std::filesystem::path& installDir, std::string& error);
    bool isOpen() const;

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
