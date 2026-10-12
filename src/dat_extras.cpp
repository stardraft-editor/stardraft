#include "stardraft/dat_extras.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "json.h"
#include "stardraft/game_data.h"

namespace stardraft {
namespace fs = std::filesystem;

namespace {
#include "data/dat_extras.inc"

uint16_t u16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t u32(const uint8_t* p) { return uint32_t(p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24); }

bool readText(const fs::path& file, std::string& out) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}
bool writeText(const fs::path& file, const std::string& text, std::string& error) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary);
    if (!out) return error = "cannot write " + file.string(), false;
    out << text;
    return true;
}
// Parses edits text (a JSON object of version 1). Empty text is no edits (true, `j` empty).
bool parseJson(const std::string& text, const char* what, Json& j, std::string& error) {
    j = {};
    if (text.empty()) return true;
    JsonParser p{text};
    j = p.value();
    if (!p.ok || j.type != Json::Type::Object) return error = std::string(what) + " is not valid", false;
    if (j.integer("version", 1) > 1) return error = std::string(what) + " is from a newer StarDraft", false;
    return true;
}
// A missing file is no edits.
bool readEdits(const fs::path& file, std::string& text, std::string& error) {
    text.clear();
    if (!fs::exists(file)) return true;
    if (!readText(file, text)) return error = "cannot read " + file.string(), false;
    return true;
}
bool writeEdits(const fs::path& file, const std::string& text, std::string& error) {
    std::error_code ec;
    if (text.empty()) return fs::remove(file, ec), true;
    return writeText(file, text, error);
}
Json number(double n) {
    Json j;
    j.type = Json::Type::Number, j.n = n;
    return j;
}
Json array(std::initializer_list<double> ns) {
    Json j;
    j.type = Json::Type::Array;
    for (double n : ns) j.items.push_back(number(n));
    return j;
}

// Words of memory as masked SetMemoryX lines (only the bytes that differ from `stock`).
void writeDiff(std::ostringstream& o, uint32_t address, const Bytes& stock, const Bytes& now) {
    char line[96];
    for (size_t a = 0; a < now.size(); a += 4) {
        uint32_t v = 0, mask = 0;
        for (size_t k = 0; k < 4 && a + k < now.size(); ++k)
            if (a + k >= stock.size() || stock[a + k] != now[a + k]) v |= uint32_t(now[a + k]) << (8 * k), mask |= 0xFFu << (8 * k);
        if (!mask) continue;
        if (mask == 0xFFFFFFFFu) std::snprintf(line, sizeof line, "    SetMemory(0x%06X, SetTo, 0x%08X);\n", unsigned(address + a), v);
        else std::snprintf(line, sizeof line, "    SetMemoryX(0x%06X, SetTo, 0x%08X, 0x%08X);\n", unsigned(address + a), v, mask);
        o << line;
    }
}
}  // namespace

// --- Strings ---------------------------------------------------------------------------------------------------

bool StatTxt::load(const GameData& data, std::string& error) {
    Bytes t;
    if (!data.readFile("rez\\stat_txt.tbl", t, error)) return false;
    if (t.size() < 2) return error = "stat_txt.tbl is empty", false;
    const size_t n = u16(t.data());
    if (2 + 2 * n > t.size()) return error = "stat_txt.tbl is not valid", false;
    std::vector<size_t> offs(n), sorted;
    for (size_t i = 0; i < n; ++i) offs[i] = std::min<size_t>(u16(&t[2 + 2 * i]), t.size());
    sorted = offs;
    sorted.push_back(t.size());
    std::sort(sorted.begin(), sorted.end());
    first_.assign(n, {}), rest_.assign(n, {});
    for (size_t i = 0; i < n; ++i) {
        const size_t o = offs[i], end = *std::upper_bound(sorted.begin(), sorted.end(), o);
        // A button text starts with its hotkey and its type (0-5); type 0 is a zero byte, so the text starts after both.
        size_t z = o + 2 <= end && t[o] >= 0x20 && t[o + 1] <= 5 ? o + 2 : o;
        while (z < end && t[z]) ++z;
        first_[i].assign(t.begin() + long(o), t.begin() + long(z));
        if (z < end) rest_[i].assign(t.begin() + long(z) + 1, t.begin() + long(end));
    }
    stockSize_ = t.size();
    edits_.clear();
    return true;
}

const std::string& StatTxt::text(int i) const {
    auto it = edits_.find(i);
    return it != edits_.end() ? it->second : first_[size_t(i)];
}

void StatTxt::set(int i, const std::string& text) {
    if (i < 0 || i >= count()) return;
    if (text == first_[size_t(i)]) edits_.erase(i);
    else edits_[i] = text;
}

void StatTxt::setEdits(std::map<int, std::string> e) {
    edits_.clear();
    for (auto& [i, s] : e) set(i, s);
}

Bytes StatTxt::build() const {
    const size_t n = first_.size();
    Bytes out(2 + 2 * n);
    out[0] = uint8_t(n), out[1] = uint8_t(n >> 8);
    std::map<std::string, size_t> placed;  // entries with the same bytes share them, as in the game's file
    for (size_t i = 0; i < n; ++i) {
        std::string raw = text(int(i));
        raw += '\0';
        raw += rest_[i];
        auto [it, fresh] = placed.try_emplace(raw, out.size());
        if (fresh) out.insert(out.end(), raw.begin(), raw.end());
        out[2 + 2 * i] = uint8_t(it->second), out[3 + 2 * i] = uint8_t(it->second >> 8);
    }
    return out;
}

bool StatTxt::setEditsText(const std::string& text, std::string& error) {
    Json j;
    edits_.clear();
    if (!parseJson(text, "strings.json", j, error)) return false;
    if (const Json* list = j.get("strings"))
        for (const Json& e : list->items) set(e.integer("id", -1), e.str("text"));
    return true;
}

std::string StatTxt::editsText() const {
    if (edits_.empty()) return {};
    std::string o = "{\"version\": 1, \"strings\": [\n";
    for (auto it = edits_.begin(); it != edits_.end(); ++it)
        o += "  {\"id\": " + std::to_string(it->first) + ", \"text\": " + quote(it->second) + "}" + (std::next(it) != edits_.end() ? ",\n" : "\n");
    return o + "]}\n";
}

bool StatTxt::loadEdits(const fs::path& file, std::string& error) {
    std::string text;
    return readEdits(file, text, error) && setEditsText(text, error);
}
bool StatTxt::saveEdits(const fs::path& file, std::string& error) const { return writeEdits(file, editsText(), error); }

std::string StatTxt::editsScript(std::string& error) const {
    if (edits_.empty()) return {};
    Bytes t = build();
    if (t.size() > stockSize_) {
        error = "The strings take " + std::to_string(t.size()) + " bytes; the game has room for " + std::to_string(stockSize_) + ".";
        return {};
    }
    while (t.size() % 4) t.push_back(0);
    std::ostringstream o;
    o << "// Generated by StarDraft from the project's string edits (dat/strings.json). Do not edit: it is rewritten on\n"
         "// every compile. The game's stat_txt.tbl, rebuilt, copied over the game's copy (0x6D5A30 points to it).\n"
         "// In epScript an EUDArray is its EPD number already.\n\n"
         "const statTxt = EUDArray(list(";
    for (size_t a = 0; a < t.size(); a += 4) {
        if (a) o << (a % 64 ? ", " : ",\n    ");
        o << "0x" << std::hex << u32(&t[a]) << std::dec;
    }
    o << "));\n\nfunction onPluginStart() {\n    f_repmovsd_epd(f_epdread_epd(EPD(0x6D5A30)), statTxt," << t.size() / 4 << ");\n}\n";
    return o.str();
}

std::string StatTxt::escape(const std::string& raw) {
    std::string o;
    char b[8];
    for (size_t i = 0; i < raw.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(raw[i]);
        if (c == '\r' && i + 1 < raw.size() && raw[i + 1] == '\n') continue;  // a CR LF line end: a line end
        if (c == '<') o += "\\<";
        else if (c == '\\') o += "\\\\";
        else if (c < 0x20 && c != '\n') std::snprintf(b, sizeof b, "<%02X>", c), o += b;
        else o += char(c);
    }
    return o;
}

std::string StatTxt::unescape(const std::string& text) {
    std::string o;
    auto hex = [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; };
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\\' && i + 1 < text.size() && (text[i + 1] == '<' || text[i + 1] == '\\')) {
            o += text[++i];
        } else if (c == '<' && i + 3 < text.size() && hex(text[i + 1]) && hex(text[i + 2]) && text[i + 3] == '>') {
            o += char(std::stoi(text.substr(i + 1, 2), nullptr, 16));
            i += 3;
        } else if (c == '<' && i + 2 < text.size() && hex(text[i + 1]) && text[i + 2] == '>') {
            o += char(std::stoi(text.substr(i + 1, 1), nullptr, 16));
            i += 2;
        } else if (c != '\r') {
            o += c;
        }
    }
    return o;
}

// --- Requirements ----------------------------------------------------------------------------------------------

namespace {
const Requirements::Kind kKindTable[Requirements::kKinds] = {
    {"units", "units", 228, 0x514178, 1096, 0x660A70},
    {"upgrades", "upgrades", 61, 0x5145C0, 840, 0x6558C0},
    {"research", "techdata", 44, 0x514908, 320, 0x656198},
    {"tech use", "techdata", 44, 0x514A48, 688, 0x6562F8},
    {"orders", "orders", 189, 0x514CF8, 1316, 0x665580},
};
constexpr uint32_t kBlock = 0x514178, kBlockEnd = 0x514CF8 + 1316;
bool takesArg(uint16_t op) { return op == 0xFF02 || op == 0xFF03 || op == 0xFF04 || op == 0xFF25; }

// The game's block and offset tables (require.dat: the five offset tables, then the block).
void stockImage(Bytes& block, std::vector<std::vector<uint16_t>>& offsets) {
    size_t pos = 0;
    offsets.assign(Requirements::kKinds, {});
    for (int k = 0; k < Requirements::kKinds; ++k)
        for (int e = 0; e < kKindTable[k].entries; ++e, pos += 2) offsets[size_t(k)].push_back(u16(&kRequireDat[pos]));
    block.assign(kRequireDat + pos, kRequireDat + sizeof kRequireDat);
}
}  // namespace

const Requirements::Kind& Requirements::kind(int k) { return kKindTable[std::clamp(k, 0, kKinds - 1)]; }
std::span<const RequirementOpDef> Requirements::ops() { return kRequirementOps; }
const RequirementOpDef* Requirements::op(uint16_t op) {
    for (const auto& d : kRequirementOps)
        if (d.op == op) return &d;
    return nullptr;
}
size_t Requirements::capacity() { return kBlockEnd - kBlock; }

Requirements::Requirements() {
    Bytes block;
    std::vector<std::vector<uint16_t>> offsets;
    stockImage(block, offsets);
    stock_.assign(kKinds, {});
    for (int k = 0; k < kKinds; ++k) {
        const size_t base = (kKindTable[k].address - kBlock) / 2, words = block.size() / 2;
        for (int e = 0; e < kKindTable[k].entries; ++e) {
            RequirementList l;
            const uint16_t off = offsets[size_t(k)][size_t(e)];
            if (off) {
                l.used = true;
                bool sub = false;
                for (size_t w = base + off; w < words;) {
                    const uint16_t op = u16(&block[2 * w++]);
                    if (op == 0xFFFF && !sub) break;
                    if (op == 0xFFFF) sub = false;
                    if (op == 0xFF1F || op == 0xFF20) sub = true;
                    RequirementOp r{op, 0};
                    if (takesArg(op) && w < words) r.arg = u16(&block[2 * w++]);
                    l.ops.push_back(r);
                }
            }
            stock_[size_t(k)].push_back(std::move(l));
        }
    }
    lists_ = stock_;
}

int Requirements::changedCount() const {
    int n = 0;
    for (int k = 0; k < kKinds; ++k)
        for (int e = 0; e < kKindTable[k].entries; ++e) n += changed(k, e);
    return n;
}

bool Requirements::layout(Bytes& block, std::vector<std::vector<uint16_t>>& offsets) const {
    // Kind by kind, at its own start or after the kind before it (an offset counts words from the kind's start, so
    // a list cannot sit before it): a zero word, then per entry its id and its steps, ended by 0xFFFF; then 0xFFFF.
    std::vector<uint16_t> words;
    bool fits = true;
    offsets.assign(kKinds, {});
    auto put = [&](size_t at, uint16_t w) {
        if (words.size() <= at) words.resize(at + 1, 0);
        words[at] = w;
    };
    size_t cur = 0;
    for (int k = 0; k < kKinds; ++k) {
        const size_t base = (kKindTable[k].address - kBlock) / 2;
        if (cur <= base) cur = base, put(cur++, 0);
        for (int e = 0; e < kKindTable[k].entries; ++e) {
            const RequirementList& l = list(k, e);
            if (!l.used) {
                offsets[size_t(k)].push_back(0);
                continue;
            }
            put(cur++, uint16_t(e));
            offsets[size_t(k)].push_back(uint16_t(cur - base));
            for (const RequirementOp& r : l.ops) {
                put(cur++, r.op);
                if (takesArg(r.op)) put(cur++, r.arg);
            }
            put(cur++, 0xFFFF);
        }
        put(cur++, 0xFFFF);
    }
    if (cur * 2 > capacity()) fits = false;
    block.assign(std::max(capacity(), words.size() * 2), 0);
    for (size_t i = 0; i < words.size(); ++i) block[2 * i] = uint8_t(words[i]), block[2 * i + 1] = uint8_t(words[i] >> 8);
    return fits;
}

size_t Requirements::used() const {
    size_t n = 0;
    for (int k = 0; k < kKinds; ++k) {
        n += 4;  // the leading zero and the closing 0xFFFF
        for (int e = 0; e < kKindTable[k].entries; ++e) {
            const RequirementList& l = list(k, e);
            if (!l.used) continue;
            n += 4;
            for (const RequirementOp& r : l.ops) n += takesArg(r.op) ? 4 : 2;
        }
    }
    return n;
}

bool Requirements::loadEdits(const fs::path& file, std::string& error) {
    std::string text;
    return readEdits(file, text, error) && setEditsText(text, error);
}
bool Requirements::saveEdits(const fs::path& file, std::string& error) const { return writeEdits(file, editsText(), error); }

bool Requirements::setEditsText(const std::string& text, std::string& error) {
    Json j;
    lists_ = stock_;
    if (!parseJson(text, "requirements.json", j, error)) return false;
    if (const Json* lists = j.get("lists"))
        for (const Json& e : lists->items) {
            int k = -1;
            for (int i = 0; i < kKinds; ++i)
                if (e.str("kind") == kKindTable[i].name) k = i;
            const int entry = e.integer("entry", -1);
            if (k < 0 || entry < 0 || entry >= kKindTable[k].entries) continue;
            RequirementList l;
            l.used = e.boolean("used", true);
            if (const Json* ops = e.get("ops"))
                for (const Json& op : ops->items)
                    if (!op.items.empty()) l.ops.push_back({uint16_t(op.items[0].n), uint16_t(op.items.size() > 1 ? op.items[1].n : 0)});
            set(k, entry, std::move(l));
        }
    return true;
}

std::string Requirements::editsText() const {
    if (!changedCount()) return {};
    std::string o = "{\"version\": 1, \"lists\": [";
    bool first = true;
    for (int k = 0; k < kKinds; ++k)
        for (int e = 0; e < kKindTable[k].entries; ++e) {
            if (!changed(k, e)) continue;
            const RequirementList& l = list(k, e);
            Json ops;
            ops.type = Json::Type::Array;
            for (const RequirementOp& r : l.ops) ops.items.push_back(takesArg(r.op) ? array({double(r.op), double(r.arg)}) : array({double(r.op)}));
            o += std::string(first ? "\n" : ",\n") + "  {\"kind\": " + quote(kKindTable[k].name) + ", \"entry\": " + std::to_string(e) +
                 ", \"used\": " + (l.used ? "true" : "false") + ", \"ops\": " + jsonText(ops) + "}";
            first = false;
        }
    return o + "\n]}\n";
}

std::string Requirements::editsScript(std::string& error) const {
    if (!changedCount()) return {};
    Bytes block, stockBlock;
    std::vector<std::vector<uint16_t>> offsets, stockOffsets;
    if (!layout(block, offsets)) {
        error = "The requirements take " + std::to_string(used()) + " bytes; the game has room for " + std::to_string(capacity()) + ".";
        return {};
    }
    stockImage(stockBlock, stockOffsets);
    std::ostringstream o;
    o << "// Generated by StarDraft from the project's requirement edits (dat/requirements.json). Do not edit: it is\n"
         "// rewritten on every compile. The requirement lists (one block from 0x514178) and each entry's offset.\n\n"
         "function onPluginStart() {\n";
    writeDiff(o, kBlock, stockBlock, block);
    for (int k = 0; k < kKinds; ++k) {
        Bytes now, was;
        for (int e = 0; e < kKindTable[k].entries; ++e) {
            for (int b = 0; b < 2; ++b) now.push_back(uint8_t(offsets[size_t(k)][size_t(e)] >> (8 * b)));
            for (int b = 0; b < 2; ++b) was.push_back(uint8_t(stockOffsets[size_t(k)][size_t(e)] >> (8 * b)));
        }
        writeDiff(o, kKindTable[k].offsets, was, now);
    }
    o << "}\n";
    return o.str();
}

// --- Button sets -----------------------------------------------------------------------------------------------

std::span<const ButtonFunctionDef> ButtonSets::conditions() { return kButtonConditions; }
std::span<const ButtonFunctionDef> ButtonSets::actions() { return kButtonActions; }
const ButtonFunctionDef* ButtonSets::condition(uint32_t address) {
    for (const auto& f : kButtonConditions)
        if (f.address == address) return &f;
    return nullptr;
}
const ButtonFunctionDef* ButtonSets::action(uint32_t address) {
    for (const auto& f : kButtonActions)
        if (f.address == address) return &f;
    return nullptr;
}

ButtonSets::ButtonSets() {
    // btnset.dat: per set {u32 count, u32 address}, then its 20-byte buttons.
    stock_.assign(kSets, {});
    size_t pos = 0;
    for (int s = 0; s < kSets && pos + 8 <= sizeof kButtonSetDat; ++s) {
        const uint32_t n = u32(&kButtonSetDat[pos]);
        pos += 8;
        for (uint32_t i = 0; i < n && pos + 20 <= sizeof kButtonSetDat; ++i, pos += 20) {
            const uint8_t* p = &kButtonSetDat[pos];
            stock_[size_t(s)].push_back({u16(p), u16(p + 2), u32(p + 4), u32(p + 8), u16(p + 12), u16(p + 14), u16(p + 16), u16(p + 18)});
        }
    }
    sets_ = stock_;
    unitSets_.resize(kUnits);
    for (int u = 0; u < kUnits; ++u) unitSets_[size_t(u)] = u;
}

int ButtonSets::changedCount() const {
    int n = 0;
    for (int s = 0; s < kSets; ++s) n += changed(s);
    for (int u = 0; u < kUnits; ++u) n += unitSets_[size_t(u)] != u;
    return n;
}

bool ButtonSets::loadEdits(const fs::path& file, std::string& error) {
    std::string text;
    return readEdits(file, text, error) && setEditsText(text, error);
}
bool ButtonSets::saveEdits(const fs::path& file, std::string& error) const { return writeEdits(file, editsText(), error); }

bool ButtonSets::setEditsText(const std::string& text, std::string& error) {
    Json j;
    sets_ = stock_;
    for (int u = 0; u < kUnits; ++u) unitSets_[size_t(u)] = u;
    if (text.empty()) return true;
    if (!parseJson(text, "buttons.json", j, error)) return false;
    if (const Json* units = j.get("units"))
        for (const Json& e : units->items) {
            const int u = e.integer("unit", -1), s = e.integer("set", -1);
            if (u >= 0 && u < kUnits && s >= 0 && s < kSets) unitSets_[size_t(u)] = s;
        }
    if (const Json* sets = j.get("sets"))
        for (const Json& e : sets->items) {
            const int s = e.integer("set", -1);
            if (s < 0 || s >= kSets) continue;
            std::vector<Button> bs;
            if (const Json* list = e.get("buttons"))
                for (const Json& b : list->items) {
                    if (b.items.size() < 8) continue;
                    auto v = [&](size_t i) { return b.items[i].n; };
                    bs.push_back({uint16_t(v(0)), uint16_t(v(1)), uint32_t(v(2)), uint32_t(v(3)), uint16_t(v(4)), uint16_t(v(5)), uint16_t(v(6)), uint16_t(v(7))});
                }
            set(s, std::move(bs));
        }
    return true;
}

std::string ButtonSets::editsText() const {
    if (!changedCount()) return {};
    std::string o = "{\"version\": 1, \"sets\": [";
    bool first = true;
    for (int s = 0; s < kSets; ++s) {
        if (!changed(s)) continue;
        o += std::string(first ? "\n" : ",\n") + "  {\"set\": " + std::to_string(s) + ", \"buttons\": [";
        for (size_t i = 0; i < sets_[size_t(s)].size(); ++i) {
            const Button& b = sets_[size_t(s)][i];
            o += (i ? ", " : "") + jsonText(array({double(b.pos), double(b.icon), double(b.condition), double(b.action), double(b.conditionArg),
                                                   double(b.actionArg), double(b.enabledString), double(b.disabledString)}));
        }
        o += "]}";
        first = false;
    }
    o += "\n]";
    std::string units;
    for (int u = 0; u < kUnits; ++u)
        if (unitSets_[size_t(u)] != u) units += std::string(units.empty() ? "\n" : ",\n") + "  {\"unit\": " + std::to_string(u) + ", \"set\": " + std::to_string(unitSets_[size_t(u)]) + "}";
    if (!units.empty()) o += ", \"units\": [" + units + "\n]";
    return o + "}\n";
}

std::string ButtonSets::editsScript() const {
    if (!changedCount()) return {};
    std::ostringstream o, start;
    o << "// Generated by StarDraft from the project's button set edits (dat/buttons.json). Do not edit: it is rewritten\n"
         "// on every compile. Each changed set is a new array; the set's entry (0x5187E8 + 12 x set) gets its count and\n"
         "// address. In epScript an EUDArray is its EPD number: its address is 0x58A364 + 4 x EPD.\n\n";
    char line[128];
    for (int s = 0; s < kSets; ++s) {
        if (!changed(s)) continue;
        const auto& bs = sets_[size_t(s)];
        if (!bs.empty()) {
            o << "const buttonSet" << s << " = EUDArray(list(";
            for (size_t i = 0; i < bs.size(); ++i) {
                const Button& b = bs[i];
                std::snprintf(line, sizeof line, "%s0x%08X, 0x%08X, 0x%08X, 0x%08X, 0x%08X", i ? ",\n    " : "", unsigned(b.pos | b.icon << 16),
                              unsigned(b.condition), unsigned(b.action), unsigned(b.conditionArg | b.actionArg << 16),
                              unsigned(b.enabledString | b.disabledString << 16));
                o << line;
            }
            o << "));\n";
        }
        std::snprintf(line, sizeof line, "    SetMemory(0x%06X, SetTo, %u);\n", unsigned(kTable + 12 * s), unsigned(bs.size()));
        start << line;
        if (!bs.empty()) std::snprintf(line, sizeof line, "    SetMemory(0x%06X, SetTo, 0x58A364 + 4 * buttonSet%d);\n", unsigned(kTable + 12 * s + 4), s), start << line;
    }
    // Units that use another set: that set's count and pointer (read first, so one moved set does not move another).
    std::string reads, writes;
    for (int u = 0; u < kUnits; ++u) {
        const int s = unitSets_[size_t(u)];
        if (s == u) continue;
        std::snprintf(line, sizeof line, "    const unitSetCount%d = dwread(0x%06X);\n    const unitSetButtons%d = dwread(0x%06X);\n", u, unsigned(kTable + 12 * s), u,
                      unsigned(kTable + 12 * s + 4));
        reads += line;
        std::snprintf(line, sizeof line, "    SetMemory(0x%06X, SetTo, unitSetCount%d);\n    SetMemory(0x%06X, SetTo, unitSetButtons%d);\n", unsigned(kTable + 12 * u), u,
                      unsigned(kTable + 12 * u + 4), u);
        writes += line;
    }
    o << "\nfunction onPluginStart() {\n" << start.str() << reads << writes << "}\n";
    return o.str();
}

// --- Units: status panel and wireframes ----------------------------------------------------------------------------

std::span<const UnitPanelKind> UnitLooks::kinds() {
    static const UnitPanelKind k[] = {
        {"Building", 0x425180, 0x427890},
        {"Unit", 0x424980, 0x426F50},
        {"Unit with a hangar (Carrier, Reaver)", 0x424B50, 0x4274A0},
        {"Transport", 0x424F10, 0x427C90},
        {"Transport that gives supply (Overlord)", 0x425900, 0x427D30},
        {"Power-up (flag, crystal)", 0x424520, 0x426EE0},
        {"Morphing unit (egg)", 0x424AC0, 0x427260},
        {"Merging unit (Archon)", 0x424AF0, 0x4273E0},
        {"Not selectable (Nuclear Missile, Scanner Sweep)", 0x424500, 0x425EE0},
        {"Scarab, Map Revealer", 0x424980, 0x425EE0},
        {"Unused Protoss building", 0x425180, 0x425EE0},
    };
    return k;
}
const UnitPanelKind* UnitLooks::kindOf(uint32_t status, uint32_t display) {
    for (const auto& k : kinds())
        if (k.status == status && k.display == display) return &k;
    return nullptr;
}

UnitLooks::UnitLooks() {
    stockStatus_.resize(kUnits), stockDisplay_.resize(kUnits), units_.resize(kUnits);
    for (int u = 0; u < kUnits && size_t(12 * u + 12) <= sizeof kStatusInforDat; ++u)
        stockStatus_[size_t(u)] = u32(&kStatusInforDat[12 * size_t(u) + 4]), stockDisplay_[size_t(u)] = u32(&kStatusInforDat[12 * size_t(u) + 8]);
}

int UnitLooks::changedCount() const {
    int n = 0;
    for (int u = 0; u < kUnits; ++u) n += changed(u);
    return n;
}

bool UnitLooks::loadEdits(const fs::path& file, std::string& error) {
    std::string text;
    return readEdits(file, text, error) && setEditsText(text, error);
}
bool UnitLooks::saveEdits(const fs::path& file, std::string& error) const { return writeEdits(file, editsText(), error); }

std::string UnitLooks::editsText() const {
    if (!changedCount()) return {};
    std::string o = "{\"version\": 1, \"units\": [";
    bool first = true;
    for (int u = 0; u < kUnits; ++u) {
        if (!changed(u)) continue;
        const Unit& v = units_[size_t(u)];
        o += std::string(first ? "\n" : ",\n") + "  {\"unit\": " + std::to_string(u);
        if (v.status) o += ", \"status\": " + std::to_string(v.status);
        if (v.display) o += ", \"display\": " + std::to_string(v.display);
        if (v.wireframe >= 0) o += ", \"wireframe\": " + std::to_string(v.wireframe);
        if (v.groupWireframe >= 0) o += ", \"groupWireframe\": " + std::to_string(v.groupWireframe);
        if (v.transportWireframe >= 0) o += ", \"transportWireframe\": " + std::to_string(v.transportWireframe);
        o += "}";
        first = false;
    }
    return o + "\n]}\n";
}

bool UnitLooks::setEditsText(const std::string& text, std::string& error) {
    units_.assign(kUnits, {});
    if (text.empty()) return true;
    Json j;
    if (!parseJson(text, "unit_display.json", j, error)) return false;
    if (const Json* list = j.get("units"))
        for (const Json& e : list->items) {
            const int u = e.integer("unit", -1);
            if (u < 0 || u >= kUnits) continue;
            Unit& v = units_[size_t(u)];
            v.status = uint32_t(e.integer("status", 0)), v.display = uint32_t(e.integer("display", 0));
            v.wireframe = e.integer("wireframe", -1), v.groupWireframe = e.integer("groupWireframe", -1);
            v.transportWireframe = e.integer("transportWireframe", -1);
        }
    return true;
}

std::string UnitLooks::editsScript() const {
    if (!changedCount()) return {};
    std::ostringstream o;
    o << "// Generated by StarDraft from the project's unit display edits (dat/unit_display.json). Do not edit: it is rewritten on\n"
         "// every compile. The status panel's functions: 0x5193A0 + 12 x unit (+4 status, +8 display). Wireframes: eudplib's\n"
         "// SetWirefram / SetGrpWire / SetTranWire (they use the 32- or 64-bit StarCraft's tables, as it runs).\n\n"
         "function onPluginStart() {\n";
    char line[128];
    for (int u = 0; u < kUnits; ++u) {
        const Unit& v = units_[size_t(u)];
        if (v.status) std::snprintf(line, sizeof line, "    SetMemory(0x%06X, SetTo, 0x%06X);\n", unsigned(kPanelTable + 12 * u + 4), unsigned(v.status)), o << line;
        if (v.display) std::snprintf(line, sizeof line, "    SetMemory(0x%06X, SetTo, 0x%06X);\n", unsigned(kPanelTable + 12 * u + 8), unsigned(v.display)), o << line;
        if (v.wireframe >= 0 && v.wireframe < kWireframes && u < kWireframes) o << "    SetWirefram(" << u << ", " << v.wireframe << ");\n";
        if (v.groupWireframe >= 0 && v.groupWireframe < kGroupWireframes && u < kGroupWireframes) o << "    SetGrpWire(" << u << ", " << v.groupWireframe << ");\n";
        if (v.transportWireframe >= 0 && v.transportWireframe < kTransportWireframes && u < kTransportWireframes)
            o << "    SetTranWire(" << u << ", " << v.transportWireframe << ");\n";
    }
    o << "}\n";
    return o.str();
}

}  // namespace stardraft
