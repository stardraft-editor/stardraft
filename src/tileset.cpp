#include "stardraft/tileset.h"

#include <cstring>

#include "byteio.h"
#include "stardraft/game_data.h"

namespace stardraft {

const char* tilesetName(uint16_t era) {
    static const char* names[8] = {"badlands", "platform", "install", "ashworld",
                                   "jungle",   "desert",   "ice",     "twilight"};
    return names[era & 7];
}

bool Tileset::load(const GameData& data, uint16_t era, std::string& error) {
    this->era = era;
    groups.clear();
    megatiles.clear();
    minitiles.clear();
    minitileFlags.clear();
    std::string base = std::string("TileSet\\") + tilesetName(era);
    Bytes cv5, vx4, vr4, wpe, vf4;
    if (!data.readFile(base + ".cv5", cv5, error) || !data.readFile(base + ".vx4ex", vx4, error) ||
        !data.readFile(base + ".vr4", vr4, error) || !data.readFile(base + ".wpe", wpe, error) ||
        !data.readFile(base + ".vf4", vf4, error))
        return false;
    if (wpe.size() < 1024) {
        error = base + ".wpe is too small";
        return false;
    }
    // Whole records only: a partial record is a damaged file.
    const struct {
        const Bytes& file;
        const char* ext;
        size_t record;
    } records[] = {{cv5, ".cv5", 52}, {vx4, ".vx4ex", 64}, {vr4, ".vr4", 64}, {vf4, ".vf4", 32}};
    for (const auto& r : records)
        if (r.file.size() % r.record) {
            error = base + r.ext + " is " + std::to_string(r.file.size()) + " bytes, not a multiple of " +
                    std::to_string(r.record);
            return false;
        }

    groups.resize(cv5.size() / 52);
    for (size_t i = 0; i < groups.size(); ++i) {
        size_t o = i * 52;
        TileGroup& g = groups[i];
        g.type = readU16(cv5, o);
        g.buildability = cv5[o + 2];
        g.groundHeight = cv5[o + 3];
        for (int k = 0; k < 4; ++k) g.edges[k] = readU16(cv5, o + 4 + 2 * k);
        for (int k = 0; k < 4; ++k) g.extra[k] = readU16(cv5, o + 12 + 2 * k);
        for (int k = 0; k < 16; ++k) g.megatiles[k] = readU16(cv5, o + 20 + 2 * k);
    }

    megatiles.resize(vx4.size() / 64);
    for (size_t i = 0; i < megatiles.size(); ++i)
        for (int k = 0; k < 16; ++k) megatiles[i][k] = readU32(vx4, i * 64 + 4 * k);

    minitiles.resize(vr4.size() / 64);
    for (size_t i = 0; i < minitiles.size(); ++i) std::memcpy(minitiles[i].data(), vr4.data() + i * 64, 64);

    minitileFlags.resize(vf4.size() / 32);
    for (size_t i = 0; i < minitileFlags.size(); ++i)
        for (int k = 0; k < 16; ++k) minitileFlags[i][k] = readU16(vf4, i * 32 + 2 * k);

    for (int i = 0; i < 256; ++i) palette[i] = {wpe[i * 4], wpe[i * 4 + 1], wpe[i * 4 + 2]};
    return true;
}

int Tileset::megatileFor(uint16_t tileValue) const {
    size_t group = tileValue >> 4;
    if (group >= groups.size()) return -1;
    uint16_t mega = groups[group].megatiles[tileValue & 15];
    return mega < megatiles.size() ? mega : -1;
}

const TileGroup* Tileset::groupFor(uint16_t tileValue) const {
    size_t group = tileValue >> 4;
    return group < groups.size() ? &groups[group] : nullptr;
}

}  // namespace stardraft
