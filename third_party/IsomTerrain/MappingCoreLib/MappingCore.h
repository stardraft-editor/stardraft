#pragma once
// StarDraft's stand-in for Chkdraft's MappingCoreLib/MappingCore.h, as much as IsomTerrain's IsomApi.h uses: Span,
// the integer names, Sc::Rect, Sc::Terrain (the tilesets) and declarations for the archive loading that IsomApi.h has
// and StarDraft does not call (it gives the tileset's CV5 itself: stardraft/isom_core.h). From Chkdraft (MIT,
// Copyright (c) Justin F), cut down.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef _MSC_VER
#define __declspec(x)  // (packing comes from #pragma pack; align(1) adds nothing to it)
#endif

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using s32 = std::int32_t;

template <typename T>
class Span {
    const T* data = nullptr;
    size_t length = 0;

public:
    constexpr Span() {}
    template <size_t N>
    constexpr Span(T const (&data)[N]) : data(&data[0]), length(N) {}
    constexpr Span(const T* data, size_t length) : data(data), length(length) {}
    constexpr size_t size() const { return length; }
    constexpr const T* begin() const { return data; }
    constexpr const T* end() const { return data + length; }
    constexpr const T& operator[](size_t index) const {
        if (index < length) return data[index];
        throw std::out_of_range("Span index out of bounds");
    }
};

#define CHKD_ERR(...) ((void)0)
#define REFLECT(...)  // (RareCpp's reflection: IsomApi.h does not use it)

class ArchiveFile;
using ArchiveFilePtr = std::shared_ptr<ArchiveFile>;
template <typename T>
class FileBrowser;
template <typename T>
using FileBrowserPtr = std::shared_ptr<FileBrowser<T>>;
inline std::string makeMpqFilePath(const std::string& dir, const std::string& name) { return dir + "\\" + name; }
inline std::string makeExtMpqFilePath(const std::string& path, const std::string& ext) { return path + "." + ext; }

namespace Sc {
#pragma pack(push, 1)
struct Rect {
    uint16_t left, top, right, bottom;
};
#pragma pack(pop)

struct Terrain {
    static constexpr size_t NumTilesets = 8;
    static inline const std::vector<std::string> TilesetNames{"badlands", "platform", "install", "AshWorld", "Jungle", "Desert", "Ice", "Twilight"};
    struct Tileset_ {
        enum u16_ : u16 { Badlands = 0, SpacePlatform = 1, Installation = 2, Ashworld = 3, Jungle = 4, Desert = 5, Arctic = 6, Twilight = 7 };
    };
    using Tileset = Tileset_::u16_;
    static constexpr uint16_t getTileGroup(uint16_t tileValue) { return tileValue / 16; }
    static constexpr uint16_t getSubtileValue(uint16_t tileValue) { return tileValue % 16; }
};

namespace Data {
inline std::optional<std::vector<u8>> GetAsset(const std::vector<ArchiveFilePtr>&, const std::string&) { return std::nullopt; }
}

namespace DataFile {
struct Descriptor {};
class Browser {
public:
    std::vector<ArchiveFilePtr> openScDataFiles(const std::vector<Descriptor>&, const std::string&, const FileBrowserPtr<u32>&) { return {}; }
    static FileBrowserPtr<u32> getDefaultStarCraftBrowser() { return nullptr; }
};
using BrowserPtr = std::shared_ptr<Browser>;
inline const std::vector<Descriptor>& getDefaultDataFiles() {
    static const std::vector<Descriptor> none;
    return none;
}
}  // namespace DataFile
}  // namespace Sc
