#pragma once

// The game's string tables (.tbl): a u16 count, then u16 offsets from the file start, each to a NUL-terminated
// string. Reads never go past the end of the file.

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace stardraft {

struct TblStrings {
    std::vector<std::string> strings;
    size_t bad = 0;  // entries whose offset slot or string start is past the end of the file (each gives "")
};

// One string per declared entry. A file of less than 2 bytes is one bad entry. A string with no NUL reads to the end of
// the file.
inline TblStrings parseTbl(std::span<const uint8_t> tbl) {
    TblStrings out;
    if (tbl.size() < 2) {
        out.bad = 1;
        return out;
    }
    const size_t n = size_t(tbl[0] | tbl[1] << 8);
    out.strings.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const size_t slot = 2 + 2 * i;
        if (slot + 1 >= tbl.size()) {
            out.strings.emplace_back();
            ++out.bad;
            continue;
        }
        const size_t o = size_t(tbl[slot] | tbl[slot + 1] << 8);
        if (o >= tbl.size()) {
            out.strings.emplace_back();
            ++out.bad;
            continue;
        }
        size_t e = o;
        while (e < tbl.size() && tbl[e]) ++e;
        out.strings.emplace_back(reinterpret_cast<const char*>(tbl.data() + o), e - o);
    }
    return out;
}

// The error text for a table with bad entries.
inline std::string tblDamaged(const std::string& file, size_t bad) {
    return file + " is damaged: " + std::to_string(bad) + (bad == 1 ? " string starts" : " strings start") +
           " past the end of the file";
}

}  // namespace stardraft
