#include "stardraft/locations.h"

#include "byteio.h"

namespace stardraft {

std::vector<Location> readLocations(const Chk& chk) {
    std::vector<Location> out;
    const Section* s = chk.find("MRGN");
    if (!s) return out;
    for (size_t o = 0; o + Location::kSize <= s->data.size(); o += Location::kSize) {
        Location l;
        l.left = readU32(s->data, o);
        l.top = readU32(s->data, o + 4);
        l.right = readU32(s->data, o + 8);
        l.bottom = readU32(s->data, o + 12);
        l.nameString = readU16(s->data, o + 16);
        l.elevation = readU16(s->data, o + 18);
        out.push_back(l);
    }
    return out;
}

void writeLocations(Chk& chk, const std::vector<Location>& locations) {
    Bytes d;
    d.reserve(locations.size() * Location::kSize);
    for (const auto& l : locations) {
        appendU32(d, l.left);
        appendU32(d, l.top);
        appendU32(d, l.right);
        appendU32(d, l.bottom);
        appendU16(d, l.nameString);
        appendU16(d, l.elevation);
    }
    chk.set("MRGN", std::move(d));
}

}  // namespace stardraft
