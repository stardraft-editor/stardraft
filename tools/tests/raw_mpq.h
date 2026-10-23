// Tests: the scenario.chk of a map that euddraft wrote (StormLib does not read it). From classic_trigger_test.
#pragma once

#include <StormLib.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include "stardraft/chk.h"

// euddraft protects the maps it writes (the block table at the archive's start, one sector as large as the file), and
// StormLib does not read their scenario.chk then. The game reads them as this does: the hash table finds the file,
// the block table gives where it is; a file of one sector is decompressed whole.
namespace rawmpq {
using stardraft::Bytes;
namespace fs = std::filesystem;
inline uint32_t crypt[0x500];
inline void init() {
    uint32_t seed = 0x00100001;
    for (uint32_t i = 0; i < 0x100; ++i)
        for (uint32_t j = 0, idx = i; j < 5; ++j, idx += 0x100) {
            seed = (seed * 125 + 3) % 0x2AAAAB;
            const uint32_t a = (seed & 0xFFFF) << 16;
            seed = (seed * 125 + 3) % 0x2AAAAB;
            crypt[idx] = a | (seed & 0xFFFF);
        }
}
inline uint32_t hash(const char* s, uint32_t type) {
    uint32_t s1 = 0x7FED7FED, s2 = 0xEEEEEEEE;
    for (; *s; ++s) {
        const uint32_t c = uint32_t(std::toupper(static_cast<unsigned char>(*s)));
        s1 = crypt[(type << 8) + c] ^ (s1 + s2);
        s2 = c + s1 + s2 + (s2 << 5) + 3;
    }
    return s1;
}
inline std::vector<uint32_t> decrypt(const Bytes& d, size_t at, size_t words, uint32_t key) {
    std::vector<uint32_t> out;
    uint32_t s2 = 0xEEEEEEEE;
    for (size_t i = 0; i < words && at + 4 * i + 4 <= d.size(); ++i) {
        s2 += crypt[0x400 + (key & 0xFF)];
        uint32_t v = 0;
        std::memcpy(&v, &d[at + 4 * i], 4);
        const uint32_t c = v ^ (key + s2);
        key = ((~key << 0x15) + 0x11111111) | (key >> 0x0B);
        s2 = c + s2 + (s2 << 5) + 3;
        out.push_back(c);
    }
    return out;
}
inline bool readChk(const fs::path& file, Bytes& out) {
    init();
    std::ifstream f(file, std::ios::binary);
    const Bytes d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (d.size() < 32 || std::memcmp(d.data(), "MPQ\x1a", 4)) return false;
    uint32_t h[7];
    std::memcpy(h, &d[4], 28);
    const uint32_t hashPos = h[3], blockPos = h[4], hashN = h[5], blockN = h[6];
    const auto ht = decrypt(d, hashPos, size_t(hashN) * 4, hash("(hash table)", 3));
    const auto bt = decrypt(d, blockPos, size_t(blockN) * 4, hash("(block table)", 3));
    const char* name = "staredit\\scenario.chk";
    const uint32_t a = hash(name, 1), b = hash(name, 2), start = hashN ? hash(name, 0) % hashN : 0;
    for (uint32_t k = 0; k < hashN; ++k) {
        const size_t i = size_t((start + k) % hashN) * 4;
        if (i + 4 > ht.size() || ht[i + 3] == 0xFFFFFFFF) return false;
        if (ht[i] != a || ht[i + 1] != b) continue;
        const size_t bi = size_t(ht[i + 3]) * 4;
        if (bi + 4 > bt.size()) return false;
        const uint32_t at = bt[bi], csize = bt[bi + 1], fsize = bt[bi + 2], flags = bt[bi + 3];
        if ((flags & 0x00010000) || at + 8 > d.size()) return false;  // encrypted files: not needed here
        out.assign(fsize, 0);
        if (!(flags & 0x00000300)) return std::memcpy(out.data(), &d[at], std::min<size_t>(fsize, d.size() - at)), true;
        uint32_t s0 = 0, s1 = 0;  // one sector: its offsets
        std::memcpy(&s0, &d[at], 4), std::memcpy(&s1, &d[at + 4], 4);
        if (at + s1 > d.size() || s1 <= s0 || (uint64_t(512) << (h[2] >> 16)) < fsize) return false;
        Bytes in(d.begin() + at + s0, d.begin() + at + s1);
        int n = int(fsize);
        if (s1 - s0 == fsize) return std::memcpy(out.data(), in.data(), fsize), true;
        const bool ok = (flags & 0x100) ? SCompExplode(out.data(), &n, in.data(), int(in.size())) : SCompDecompress(out.data(), &n, in.data(), int(in.size()));
        (void)csize;
        return ok && n == int(fsize);
    }
    return false;
}
}  // namespace rawmpq
