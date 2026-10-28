// How a mod's tilesets (Cosmonarchy BW: <install>\mpq\tileset\*.cv5) differ from StarCraft 1.16.1's: whether the game's
// tile groups stay as they are, and what the mod's further groups hold (their terrain types and edge ids), for the
// isometric terrain of the mod's tilesets.
// usage: cmbw_tiles_probe <Starcraft folder (StarDat.mpq, BrooDat.mpq)> <mod tileset folder>
#include <StormLib.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Bytes = std::vector<unsigned char>;

static bool readMpq(const fs::path& mpq, const char* name, Bytes& out) {
    HANDLE a = nullptr, f = nullptr;
    if (!SFileOpenArchive(mpq.c_str(), 0, MPQ_OPEN_READ_ONLY, &a)) return false;
    bool ok = false;
    if (SFileOpenFileEx(a, name, SFILE_OPEN_FROM_MPQ, &f)) {
        DWORD high = 0, size = SFileGetFileSize(f, &high), read = 0;
        out.resize(size);
        ok = SFileReadFile(f, out.data(), size, &read, nullptr) && read == size;
        SFileCloseFile(f);
    }
    SFileCloseArchive(a);
    return ok;
}
static unsigned u16(const Bytes& b, size_t o) { return b[o] | b[o + 1] << 8; }

int main(int argc, char** argv) {
    if (argc < 3) return std::fprintf(stderr, "usage: cmbw_tiles_probe <Starcraft folder> <mod tileset folder>\n"), 2;
    const char* sets[] = {"badlands", "platform", "install", "AshWorld", "Jungle", "Desert", "Ice", "Twilight"};
    for (const char* s : sets) {
        Bytes stock, mod;
        const std::string name = std::string("tileset\\") + s + ".cv5";
        if (!readMpq(fs::path(argv[1]) / "StarDat.mpq", name.c_str(), stock) && !readMpq(fs::path(argv[1]) / "BrooDat.mpq", name.c_str(), stock)) {
            std::printf("%s: no stock cv5\n", s);
            continue;
        }
        std::ifstream in(fs::path(argv[2]) / (std::string(s) + ".cv5"), std::ios::binary);
        mod.assign(std::istreambuf_iterator<char>(in), {});
        const size_t ns = stock.size() / 52, nm = mod.size() / 52;
        // CMBW_DUMP=<tileset>: that tileset's stock and mod header (type, edges) of each changed group of the game's.
        if (const char* only = std::getenv("CMBW_DUMP"); only && std::string(only) == s)
            for (size_t g = 0; g < ns && g < nm; ++g)
                if (!std::equal(stock.begin() + g * 52, stock.begin() + g * 52 + 20, mod.begin() + g * 52))
                    std::printf("group %4zu stock type %2u edges %3u %3u %3u %3u, mod type %2u edges %3u %3u %3u %3u\n", g, u16(stock, g * 52),
                                u16(stock, g * 52 + 4), u16(stock, g * 52 + 6), u16(stock, g * 52 + 8), u16(stock, g * 52 + 10), u16(mod, g * 52),
                                u16(mod, g * 52 + 4), u16(mod, g * 52 + 6), u16(mod, g * 52 + 8), u16(mod, g * 52 + 10));
        size_t same = 0, sameHead = 0, sameTiles = 0;
        for (size_t g = 0; g < ns && g < nm; ++g) {
            const bool head = std::equal(stock.begin() + g * 52, stock.begin() + g * 52 + 20, mod.begin() + g * 52);
            const bool tiles = std::equal(stock.begin() + g * 52 + 20, stock.begin() + g * 52 + 52, mod.begin() + g * 52 + 20);
            same += head && tiles, sameHead += head, sameTiles += tiles;
        }
        std::set<unsigned> stockTypes, modTypes, stockEdges, newEdges;
        for (size_t g = 0; g < ns; ++g) {
            stockTypes.insert(u16(stock, g * 52));
            for (int k = 0; k < 4; ++k) stockEdges.insert(u16(stock, g * 52 + 4 + 2 * k));
        }
        std::map<unsigned, int> newTypeCount;
        size_t emptyNew = 0;
        for (size_t g = ns; g < nm; ++g) {
            const unsigned t = u16(mod, g * 52);
            bool empty = true;
            for (int k = 0; k < 16; ++k) empty &= u16(mod, g * 52 + 20 + 2 * k) == 0;
            if (empty) {
                ++emptyNew;
                continue;
            }
            modTypes.insert(t);
            ++newTypeCount[t];
            for (int k = 0; k < 4; ++k) newEdges.insert(u16(mod, g * 52 + 4 + 2 * k));
        }
        size_t unknownTypes = 0, unknownEdges = 0;
        for (unsigned t : modTypes) unknownTypes += !stockTypes.count(t);
        for (unsigned e : newEdges) unknownEdges += !stockEdges.count(e);
        std::printf("%-9s stock %zu groups, mod %zu: first %zu same %zu (header %zu, tiles %zu); new groups %zu (empty %zu), their types %zu "
                    "(%zu not in the stock set of %zu), edge ids %zu (%zu new)\n",
                    s, ns, nm, std::min(ns, nm), same, sameHead, sameTiles, nm - ns, emptyNew, modTypes.size(), unknownTypes, stockTypes.size(),
                    newEdges.size(), unknownEdges);
        std::printf("          new types (count):");
        int shown = 0;
        for (auto [t, c] : newTypeCount)
            if (shown++ < 24) std::printf(" %u(%d)", t, c);
        std::printf("\n");
    }
    return 0;
}
