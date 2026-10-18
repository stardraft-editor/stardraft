#pragma once
// A campaign project (roadmap R25): the maps of a campaign in their order, in campaign.json in the campaign's folder.
// The maps are files of their own (classic maps); the campaign keeps their paths (relative to its folder when they are
// in it), so a map opens from the campaign, and data and terrain can be copied from one map to another
// (Editor::copyMapData, Editor::copyTerrain).

#include <filesystem>
#include <string>
#include <vector>

namespace stardraft {

struct Campaign {
    static constexpr const char* kFileName = "campaign.json";
    std::filesystem::path folder;              // the campaign's folder (campaign.json is in it)
    std::string name;
    std::vector<std::filesystem::path> maps;   // in the campaign's order; relative to `folder` when inside it

    // Makes a new campaign in `folder` (made when it does not exist; it must not hold a campaign already).
    bool create(const std::filesystem::path& folder, const std::string& name, std::string& error);
    bool load(const std::filesystem::path& folderOrFile, std::string& error);
    bool save(std::string& error) const;
    // The path of map i as a file on the disk.
    std::filesystem::path mapFile(size_t i) const;
    // Adds a map file at the end (relative to the folder when inside it). False when it is in the campaign already.
    bool add(const std::filesystem::path& file);
};

}  // namespace stardraft
