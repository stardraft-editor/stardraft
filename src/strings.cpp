#include "stardraft/strings.h"

#include "byteio.h"

namespace stardraft {

StringTable StringTable::read(const Chk& chk) {
    StringTable t;
    const Section* s = chk.find("STRx");
    t.extended_ = s != nullptr;
    if (!s) s = chk.find("STR ");
    if (!s || s->data.size() < (t.extended_ ? 4u : 2u)) return t;
    const Bytes& d = s->data;
    const size_t w = t.extended_ ? 4 : 2;
    size_t count = t.extended_ ? readU32(d, 0) : readU16(d, 0);
    count = std::min(count, (d.size() - w) / w);
    t.strings_.resize(count);
    for (size_t i = 0; i < count; ++i) {
        size_t off = t.extended_ ? readU32(d, w + i * w) : readU16(d, w + i * w);
        size_t end = off;
        while (end < d.size() && d[end]) ++end;
        if (off < d.size()) t.strings_[i].assign(d.begin() + off, d.begin() + end);
    }
    return t;
}

void StringTable::write(Chk& chk) const {
    // StarEdit keeps at least 1024 slots; unused slots point at one shared NUL.
    size_t count = std::max<size_t>(strings_.size(), 1024);
    auto build = [&](bool ext) {
        const size_t w = ext ? 4 : 2;
        Bytes out;
        size_t dataStart = w + count * w;
        out.resize(dataStart, 0);
        out.push_back(0);  // shared empty string
        std::vector<size_t> offs(count, dataStart);
        for (size_t i = 0; i < strings_.size(); ++i) {
            if (strings_[i].empty()) continue;
            offs[i] = out.size();
            out.insert(out.end(), strings_[i].begin(), strings_[i].end());
            out.push_back(0);
        }
        if (!ext && out.size() > 0xFFFF) return Bytes{};
        for (size_t i = 0; i < w; ++i) out[i] = uint8_t(count >> (8 * i));
        for (size_t i = 0; i < count; ++i)
            for (size_t b = 0; b < w; ++b) out[w + i * w + b] = uint8_t(offs[i] >> (8 * b));
        return out;
    };
    Bytes data = extended_ ? Bytes{} : build(false);
    if (!data.empty() && count <= 0xFFFF) {
        chk.set("STR ", std::move(data));
        chk.remove("STRx");
    } else {
        chk.set("STRx", build(true));
        chk.remove("STR ");
    }
}

const std::string& StringTable::get(uint32_t id) const {
    static const std::string empty;
    return id >= 1 && id <= strings_.size() ? strings_[id - 1] : empty;
}

void StringTable::set(uint32_t id, std::string text) {
    if (id == 0) return;
    if (id > strings_.size()) strings_.resize(id);
    strings_[id - 1] = std::move(text);
}

uint32_t StringTable::add(std::string_view text) {
    for (size_t i = 0; i < strings_.size(); ++i)
        if (strings_[i].empty()) {
            strings_[i] = std::string(text);
            return uint32_t(i + 1);
        }
    strings_.emplace_back(text);
    return uint32_t(strings_.size());
}

}  // namespace stardraft
