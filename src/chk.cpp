#include "stardraft/chk.h"

#include <cstring>
#include <set>

#include "byteio.h"

namespace stardraft {

namespace {

bool isAccumulating(SectionName n) {
    return n == SectionName("UNIT") || n == SectionName("THG2") || n == SectionName("TRIG") ||
           n == SectionName("MBRF");
}

}  // namespace

Chk Chk::parse(std::span<const uint8_t> bytes, ParseReport* report) {
    ParseReport local;
    ParseReport& r = report ? *report : local;
    Chk chk;

    std::set<size_t> visited;
    size_t pos = 0;
    while (pos + 8 <= bytes.size()) {
        if (!visited.insert(pos).second) {
            r.loopDetected = true;
            break;
        }
        Section s;
        std::memcpy(s.name.c.data(), bytes.data() + pos, 4);
        int32_t size = static_cast<int32_t>(readU32(bytes, pos + 4));
        size_t start = pos + 8;

        if (size < 0) {
            // StarCraft continues reading at start + size; the section itself is invalid.
            r.negativeSize = true;
            int64_t next = static_cast<int64_t>(start) + size;
            if (next < 0) break;
            pos = static_cast<size_t>(next);
            continue;
        }

        size_t end = start + static_cast<size_t>(size);
        if (end > bytes.size()) {
            r.truncatedSection = true;
            end = bytes.size();
        }
        s.data.assign(bytes.begin() + start, bytes.begin() + end);
        chk.sections.push_back(std::move(s));
        pos = end;
    }
    if (pos < bytes.size() && !r.loopDetected && !r.negativeSize) r.trailingBytes = true;
    return chk;
}

Bytes Chk::serialize() const {
    size_t total = 0;
    for (const auto& s : sections) total += 8 + s.data.size();
    Bytes out;
    out.reserve(total);
    for (const auto& s : sections) {
        out.insert(out.end(), s.name.c.begin(), s.name.c.end());
        appendU32(out, static_cast<uint32_t>(s.data.size()));
        out.insert(out.end(), s.data.begin(), s.data.end());
    }
    return out;
}

const Section* Chk::find(SectionName name) const {
    for (auto it = sections.rbegin(); it != sections.rend(); ++it)
        if (it->name == name) return &*it;
    return nullptr;
}

Section* Chk::find(SectionName name) {
    return const_cast<Section*>(static_cast<const Chk*>(this)->find(name));
}

Bytes Chk::effective(SectionName name) const {
    if (!isAccumulating(name)) {
        const Section* s = find(name);
        return s ? s->data : Bytes{};
    }
    Bytes out;
    for (const auto& s : sections)
        if (s.name == name) out.insert(out.end(), s.data.begin(), s.data.end());
    return out;
}

void Chk::set(SectionName name, Bytes data) {
    bool placed = false;
    for (auto it = sections.begin(); it != sections.end();) {
        if (it->name != name) {
            ++it;
        } else if (!placed) {
            it->data = std::move(data);
            placed = true;
            ++it;
        } else {
            it = sections.erase(it);
        }
    }
    if (!placed) sections.push_back({name, std::move(data)});
}

void Chk::remove(SectionName name) {
    std::erase_if(sections, [&](const Section& s) { return s.name == name; });
}

}  // namespace stardraft
