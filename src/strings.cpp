#include "stardraft/strings.h"

#include <algorithm>

#include "byteio.h"
#include "stardraft/classic_settings.h"
#include "stardraft/classic_triggers.h"

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

bool StringTable::reorder(const std::vector<uint32_t>& order) {
    if (order.size() != strings_.size()) return false;
    std::vector<bool> seen(strings_.size(), false);
    for (uint32_t id : order) {
        if (id < 1 || id > strings_.size() || seen[id - 1]) return false;
        seen[id - 1] = true;
    }
    std::vector<std::string> out(strings_.size());
    for (size_t k = 0; k < order.size(); ++k) out[k] = std::move(strings_[order[k] - 1]);
    strings_ = std::move(out);
    return true;
}

size_t StringTable::strBytes() const {
    // The count, the offsets, the shared empty string and the texts: 2 bytes each in STR, 4 in STRx (write() makes STRx
    // when the table is STRx, or when STR has no room).
    auto bytes = [&](size_t w) {
        size_t n = w + std::max<size_t>(strings_.size(), 1024) * w + 1;
        for (const std::string& s : strings_)
            if (!s.empty()) n += s.size() + 1;
        return n;
    };
    const size_t str = bytes(2);
    return extended_ || str > 0xFFFF ? bytes(4) : str;
}

// --- Where the map uses its strings ---------------------------------------------------------------------------------

namespace {

// One field that holds a string id: in the effective bytes of `section`, at `offset`, `width` bytes (2 or 4).
// `counts`: false for a field that gets the new ids but is not a use (the unit names of the section the game does not
// read).
struct StringField {
    SectionName section;
    size_t offset = 0;
    uint8_t width = 2;
    bool counts = true;
    StringUse use;
};

constexpr size_t kTrig = 2400, kTrigConditions = 16 * 20, kTrigAction = 32, kTrigActions = 64;
constexpr size_t kUnitNames = 3192, kUnitTypes = 228;

// Every string field of the map, with the effective bytes of each section that has one (`data`).
std::vector<StringField> stringFields(const Chk& chk, std::vector<std::pair<SectionName, Bytes>>& data) {
    std::vector<StringField> out;
    auto bytesOf = [&](SectionName n) -> const Bytes& {
        data.emplace_back(n, chk.effective(n));
        return data.back().second;
    };
    auto add = [&](SectionName sec, size_t off, uint8_t w, StringUseKind kind, int index, int action = -1, bool counts = true) {
        StringField f;
        f.section = sec, f.offset = off, f.width = w, f.counts = counts;
        f.use.kind = kind, f.use.index = index, f.use.action = action;
        out.push_back(f);
    };
    if (chk.has("SPRP")) {
        const Bytes& d = bytesOf("SPRP");
        if (d.size() >= 2) add("SPRP", 0, 2, StringUseKind::ScenarioName, 0);
        if (d.size() >= 4) add("SPRP", 2, 2, StringUseKind::ScenarioDescription, 0);
    }
    if (chk.has("FORC")) {
        const Bytes& d = bytesOf("FORC");
        for (size_t f = 0; f < 4 && 8 + 2 * f + 2 <= d.size(); ++f) add("FORC", 8 + 2 * f, 2, StringUseKind::ForceName, int(f));
    }
    // The unit names: the game reads UNIx in a Brood War map (UNIS when the map has only that), else UNIS.
    const bool hasX = chk.has("UNIx"), hasS = chk.has("UNIS"), bw = settingsBroodWar(chk);
    const bool readX = bw ? hasX || !hasS : !hasS && hasX;
    for (const SectionName n : {SectionName("UNIx"), SectionName("UNIS")}) {
        if (!chk.has(n)) continue;
        const Bytes& d = bytesOf(n);
        const bool counts = (n == SectionName("UNIx")) == readX;
        for (size_t u = 0; u < kUnitTypes && kUnitNames + 2 * u + 2 <= d.size(); ++u)
            add(n, kUnitNames + 2 * u, 2, StringUseKind::UnitName, int(u), -1, counts);
    }
    if (chk.has("MRGN")) {
        const Bytes& d = bytesOf("MRGN");
        for (size_t i = 0; i * 20 + 20 <= d.size(); ++i) add("MRGN", i * 20 + 16, 2, StringUseKind::LocationName, int(i));
    }
    if (chk.has("SWNM")) {
        const Bytes& d = bytesOf("SWNM");
        for (size_t i = 0; i < 256 && i * 4 + 4 <= d.size(); ++i) add("SWNM", i * 4, 4, StringUseKind::SwitchName, int(i));
    }
    if (chk.has("WAV ")) {
        const Bytes& d = bytesOf("WAV ");
        for (size_t i = 0; i < 512 && i * 4 + 4 <= d.size(); ++i) add("WAV ", i * 4, 4, StringUseKind::SoundFile, int(i));
    }
    if (chk.has("TRIG")) {
        const Bytes& d = bytesOf("TRIG");
        for (size_t t = 0; (t + 1) * kTrig <= d.size(); ++t)
            for (size_t a = 0; a < kTrigActions; ++a) {
                const size_t at = t * kTrig + kTrigConditions + a * kTrigAction;
                const ClassicDef* def = classicActionDef(d[at + 26]);
                if (!def) continue;
                for (const ClassicParam& p : def->params) {
                    if (p.field == ClassicField::Text) add("TRIG", at + 4, 4, StringUseKind::TriggerText, int(t), int(a));
                    if (p.field == ClassicField::Wav) add("TRIG", at + 8, 4, StringUseKind::TriggerSound, int(t), int(a));
                }
            }
    }
    if (chk.has("MBRF")) {  // (briefings.cpp reads the text and the sound of each action that is not None)
        const Bytes& d = bytesOf("MBRF");
        for (size_t t = 0; (t + 1) * kTrig <= d.size(); ++t)
            for (size_t a = 0; a < kTrigActions; ++a) {
                const size_t at = t * kTrig + kTrigConditions + a * kTrigAction;
                if (!d[at + 26]) continue;
                add("MBRF", at + 4, 4, StringUseKind::BriefingText, int(t), int(a));
                add("MBRF", at + 8, 4, StringUseKind::BriefingSound, int(t), int(a));
            }
    }
    return out;
}

Bytes* bytesIn(std::vector<std::pair<SectionName, Bytes>>& data, SectionName n) {
    for (auto& [name, d] : data)
        if (name == n) return &d;
    return nullptr;
}
uint32_t fieldValue(const Bytes& d, const StringField& f) { return f.width == 2 ? readU16(d, f.offset) : readU32(d, f.offset); }

}  // namespace

std::vector<StringUse> mapStringUses(const Chk& chk) {
    std::vector<std::pair<SectionName, Bytes>> data;
    data.reserve(12);  // (one entry per section: the references into it stay)
    const std::vector<StringField> fields = stringFields(chk, data);
    std::vector<StringUse> out;
    for (const StringField& f : fields) {
        if (!f.counts) continue;
        const uint32_t id = fieldValue(*bytesIn(data, f.section), f);
        if (!id) continue;
        StringUse u = f.use;
        u.id = id;
        out.push_back(u);
    }
    return out;
}

bool renumberMapStrings(Chk& chk, const std::vector<uint32_t>& newId) {
    std::vector<std::pair<SectionName, Bytes>> data;
    data.reserve(12);
    const std::vector<StringField> fields = stringFields(chk, data);
    std::vector<bool> touched(data.size(), false);
    for (int pass = 0; pass < 2; ++pass)  // (first the check that every new id fits, then the change)
        for (const StringField& f : fields) {
            Bytes& d = *bytesIn(data, f.section);
            const uint32_t id = fieldValue(d, f);
            if (!id || id >= newId.size() || newId[id] == id) continue;
            const uint32_t to = newId[id];
            if (!pass) {
                if (f.width == 2 && to > 0xFFFF) return false;
                continue;
            }
            for (size_t b = 0; b < f.width; ++b) d[f.offset + b] = uint8_t(to >> (8 * b));
            for (size_t k = 0; k < data.size(); ++k)
                if (data[k].first == f.section) touched[k] = true;
        }
    for (size_t k = 0; k < data.size(); ++k)
        if (touched[k]) chk.set(data[k].first, std::move(data[k].second));
    return true;
}

}  // namespace stardraft
