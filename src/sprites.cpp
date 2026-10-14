#include "stardraft/sprites.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <set>

#include "byteio.h"
#include "tbl.h"
#include "stardraft/game_data.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"
#include "stardraft/units.h"

namespace stardraft {

// --- GRP ---------------------------------------------------------------------------------------

bool Grp::parse(std::span<const uint8_t> d) {
    if (d.size() < 6) return false;
    uint16_t count = readU16(d, 0);
    width = readU16(d, 2);
    height = readU16(d, 4);
    if (d.size() < 6 + size_t(count) * 8) return false;
    frames.assign(count, {});
    for (uint16_t f = 0; f < count; ++f) {
        size_t h = 6 + size_t(f) * 8;
        Frame& fr = frames[f];
        fr.x = d[h], fr.y = d[h + 1], fr.width = d[h + 2], fr.height = d[h + 3];
        size_t base = readU32(d, h + 4);
        fr.pixels.assign(size_t(fr.width) * fr.height, 0);
        if (base + size_t(fr.height) * 2 > d.size()) return false;
        // Each row: RLE of skip (0x80|n), repeat (0x40|n, value), or copy (n, n bytes).
        for (int row = 0; row < fr.height; ++row) {
            size_t p = base + readU16(d, base + row * 2);
            int x = 0;
            while (x < fr.width && p < d.size()) {
                uint8_t c = d[p++];
                if (c & 0x80) {
                    x += c & 0x7f;
                } else if (c & 0x40) {
                    if (p >= d.size()) return false;
                    uint8_t v = d[p++];
                    for (int n = c & 0x3f; n > 0 && x < fr.width; --n) fr.pixels[size_t(row) * fr.width + x++] = v;
                } else {
                    for (int n = c; n > 0 && x < fr.width && p < d.size(); --n) fr.pixels[size_t(row) * fr.width + x++] = d[p++];
                }
            }
        }
    }
    return true;
}

// --- Data tables --------------------------------------------------------------------------------

namespace {

constexpr size_t kUnits = 228, kFlingies = 209, kSprites = 517, kImages = 999;
constexpr uint16_t kNoUnit = 228;

// Decode an 8-bit RLE PCX; returns palette indices.
std::vector<uint8_t> decodePcx(const Bytes& pcx, int& w, int& h) {
    std::vector<uint8_t> out;
    if (pcx.size() < 128) return out;
    w = readU16(pcx, 8) - readU16(pcx, 4) + 1;
    h = readU16(pcx, 10) - readU16(pcx, 6) + 1;
    int bytesPerLine = readU16(pcx, 66);
    std::vector<uint8_t> raw;
    size_t p = 128;
    while (raw.size() < size_t(bytesPerLine) * h && p < pcx.size()) {
        uint8_t c = pcx[p++];
        if ((c & 0xC0) == 0xC0 && p < pcx.size()) raw.insert(raw.end(), c & 0x3F, pcx[p++]);
        else raw.push_back(c);
    }
    out.resize(size_t(w) * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            size_t i = size_t(y) * bytesPerLine + x;
            out[size_t(y) * w + x] = i < raw.size() ? raw[i] : 0;
        }
    return out;
}

}  // namespace

namespace {

// Argument layout of each iscript opcode (community-documented opcode table); 0xFF = variable/unknown.
const char* kOpArgs[0x45] = {
    "H",   "H",   "b",   "b",   "bb",  "B",   "BB",  "H",   "Hbb", "Hbb", "H",   "H",   "",    "Hbb", "Hbb", "Hbb",
    "Hbb", "Hbb", "H",   "Hbb", "Hbb", "HB",  "",    "B",   "H",   nullptr, "HH", "",    nullptr, "", "BH",  "B",
    "B",   "",    "B",   "B",   "B",   "B",   "",    "",    "B",   "B",   "",    "B",   "B",   "",    "",    "",
    "",    "B",   "",    "",    "B",   "H",   "",    "H",   "B",   "H",   "HH",  "HHH", "HHH", "bb",  "",    "H",
    "H",   "B",   "Hbb", "",    ""};

size_t argSize(const char* a) {
    size_t n = 0;
    for (; *a; ++a) n += *a == 'H' ? 2 : 1;
    return n;
}

}  // namespace

std::vector<IscriptLine> disassembleIscript(const Bytes& d, size_t start, size_t maxLines) {
    static const char* kNames[0x45] = {
        "playfram", "playframtile", "sethorpos", "setvertpos", "setpos", "wait", "waitrand", "goto", "imgol", "imgul", "imgolorig", "switchul", "__0c",
        "imgoluselo", "imguluselo", "sprol", "highsprol", "lowsprul", "uflunstable", "spruluselo", "sprul", "sproluselo", "end", "setflipstate",
        "playsnd", "playsndrand", "playsndbtwn", "domissiledmg", "attackmelee", "followmaingraphic", "randcondjmp", "turnccwise", "turncwise",
        "turn1cwise", "turnrand", "setspawnframe", "sigorder", "attackwith", "attack", "castspell", "useweapon", "move", "gotorepeatattk",
        "engframe", "engset", "__2d", "nobrkcodestart", "nobrkcodeend", "ignorerest", "attkshiftproj", "tmprmgraphicstart", "tmprmgraphicend",
        "setfldirect", "call", "return", "setflspeed", "creategasoverlays", "pwrupcondjmp", "trgtrangecondjmp", "trgtarccondjmp",
        "curdirectcondjmp", "imgulnextid", "__3e", "liftoffcondjmp", "warpoverlay", "orderdone", "grdsprol", "__43", "dogrddamage"};
    // Which argument is a place in the script (a jump or a call), -1 if none.
    auto labelArg = [](uint8_t op) {
        switch (op) {
        case 0x07: case 0x35: case 0x39: case 0x3F: return 0;
        case 0x1E: case 0x3A: return 1;
        case 0x3B: case 0x3C: return 2;
        default: return -1;
        }
    };
    auto hex = [](unsigned v, int digits) {
        char b[16];
        std::snprintf(b, sizeof b, "%0*X", digits, v);
        return std::string(b);
    };
    std::map<size_t, IscriptLine> code;
    std::set<size_t> labels, seen;
    std::vector<size_t> todo{start};
    while (!todo.empty() && code.size() < maxLines) {
        size_t pc = todo.back();
        todo.pop_back();
        while (pc < d.size() && code.size() < maxLines && seen.insert(pc).second) {
            const uint8_t op = d[pc];
            size_t len = 0;
            std::string args;
            std::vector<IscriptArg> argv;
            if (op == 0x19 || op == 0x1C) {  // a count, then that many sounds
                if (pc + 1 >= d.size()) break;
                len = 1 + 2 * size_t(d[pc + 1]);
                if (pc + 1 + len > d.size()) break;
                argv.push_back({std::to_string(int(d[pc + 1])), "", int(d[pc + 1])});
                for (size_t k = 0; k < d[pc + 1]; ++k) {
                    const int v = readU16(d, pc + 2 + 2 * k);
                    args += (k ? " " : "") + std::to_string(v);
                    argv.push_back({std::to_string(v), "sfxdata", v});
                }
            } else {
                if (op >= 0x45 || !kOpArgs[op]) {
                    code[pc] = {pc, false, "??", hex(op, 2), op, {}};
                    break;
                }
                const char* fmt = kOpArgs[op];
                len = argSize(fmt);
                if (pc + 1 + len > d.size()) break;
                size_t a = pc + 1;
                for (int k = 0; fmt[k]; ++k) {
                    if (!args.empty()) args += ' ';
                    IscriptArg arg;
                    if (fmt[k] == 'H') {
                        const uint16_t v = readU16(d, a);
                        arg.value = v;
                        if (k == labelArg(op)) arg.text = "L_" + hex(v, 4), arg.ref = "label", labels.insert(v), todo.push_back(v);
                        else arg.text = op <= 0x01 ? "0x" + hex(v, 2) : std::to_string(v);
                        if (k == 0) {  // what the first argument points at
                            if (op >= 0x08 && op <= 0x0B) arg.ref = "images";  // imgol, imgul, imgolorig, switchul
                            if (op == 0x0D || op == 0x0E) arg.ref = "images";  // imgoluselo, imguluselo
                            if ((op >= 0x0F && op <= 0x11) || (op >= 0x13 && op <= 0x15) || op == 0x42) arg.ref = "sprites";
                            if (op == 0x18 || op == 0x1A) arg.ref = "sfxdata";  // playsnd, playsndbtwn
                        }
                        if (k == 1 && op == 0x1A) arg.ref = "sfxdata";
                        a += 2;
                    } else if (fmt[k] == 'b') {
                        arg.value = int(int8_t(d[a])), arg.text = std::to_string(arg.value), ++a;
                    } else {
                        arg.value = int(d[a]), arg.text = std::to_string(arg.value), ++a;
                        if (op == 0x28) arg.ref = "weapons";                                       // useweapon
                        if (op == 0x38) arg.ref = "images", arg.value = 430 + arg.value;           // creategasoverlays: a smoke
                    }
                    args += arg.text;
                    argv.push_back(arg);
                }
            }
            code[pc] = {pc, false, kNames[op], args, op, std::move(argv)};
            pc += 1 + len;
            if (op == 0x07 || op == 0x16 || op == 0x36 || op == 0x30) break;  // goto, end, return, ignorerest: the code goes no further
        }
    }
    std::vector<IscriptLine> out;
    for (const auto& [at, line] : code) {
        if (labels.count(at) || at == start) out.push_back({at, true, "L_" + hex(unsigned(at), 4), "", 0, {}});
        out.push_back(line);
    }
    return out;
}

void UnitSprites::applyEdits(const Bytes& sprites, const Bytes& images) {
    if (sprites.size() >= kSprites * 2)
        for (size_t i = 0; i < kSprites; ++i) spriteImage_[i] = readU16(sprites, 2 * i);
    if (images.size() >= 3996 + kImages) imageTurns_.assign(images.begin() + 3996, images.begin() + 3996 + kImages);
    if (images.size() >= 7992 + kImages) imageDrawFn_.assign(images.begin() + 7992, images.begin() + 7992 + kImages);
    if (images.size() >= 8991 + kImages) imageRemap_.assign(images.begin() + 8991, images.begin() + 8991 + kImages);
    if (data_) loadScripts(*data_, images);  // each image's script, by its edited Iscript ID
}

void UnitSprites::applyUnitEdits(const Bytes& units, const Bytes& flingy) {
    if (units.size() >= 228 + kUnits * 2) {
        unitFlingy_.assign(units.begin(), units.begin() + kUnits);
        for (size_t i = 0; i < kUnits; ++i) unitSubunit_[i] = readU16(units, 228 + 2 * i);
    }
    if (flingy.size() >= kFlingies * 2)
        for (size_t i = 0; i < kFlingies; ++i) flingySprite_[i] = readU16(flingy, 2 * i);
    sprites_.clear();  // the pictures of the unit types
}

std::vector<uint16_t> UnitSprites::changedUnitGraphics(const UnitSprites& game, const UnitSprites& edited) {
    auto look = [](const UnitSprites& s, int image) {
        const size_t i = size_t(image);
        return std::array<int, 5>{image, s.scriptId(image), image >= 0 && i < s.imageTurns_.size() ? s.imageTurns_[i] : 0, s.drawFunction(image),
                                  image >= 0 && i < s.imageRemap_.size() ? s.imageRemap_[i] : 0};
    };
    std::set<uint16_t> subunits;
    for (const UnitSprites* s : {&game, &edited})
        for (uint16_t u : s->unitSubunit_)
            if (u != kNoUnit) subunits.insert(u);
    std::vector<uint16_t> out;
    for (uint16_t t = 0; t < kUnits; ++t) {
        if (subunits.count(t)) continue;
        if (look(game, game.imageFor(t)) != look(edited, edited.imageFor(t)) ||
            look(game, game.subunitImage(t)) != look(edited, edited.subunitImage(t)))
            out.push_back(t);
    }
    return out;
}

void UnitSprites::loadScripts(const GameData& data, const Bytes& images) {
    scripts_.assign(kImages, {});
    scriptHeader_.assign(kImages, 0);
    imageScriptId_.assign(kImages, -1);
    Bytes& d = iscript_;
    std::string err;
    // iscript.bin does not change: read it once.
    if (d.empty() && !data.readFile("scripts\\iscript.bin", d, err)) return;
    if (d.size() < 2 || images.size() < 9990 + kImages * 4) return;
    // Table of (iscript id, offset) pairs, terminated by id 0xFFFF.
    std::map<uint16_t, uint16_t>& entries = scriptEntries_;
    entries.clear();
    for (size_t p = readU16(d, 0); p + 4 <= d.size(); p += 4) {
        uint16_t id = readU16(d, p);
        if (id == 0xFFFF) break;
        entries[id] = readU16(d, p + 2);
    }
    for (size_t img = 0; img < kImages; ++img) {
        imageScriptId_[img] = int(readU32(images, 9990 + img * 4));
        auto it = entries.find(uint16_t(imageScriptId_[img]));
        if (it == entries.end() || size_t(it->second) + 10 > d.size()) continue;
        scriptHeader_[img] = it->second;
    }
    auto spriteImage = [&](int sprite) { return sprite >= 0 && size_t(sprite) < spriteImage_.size() ? int(spriteImage_[size_t(sprite)]) : -1; };
    for (size_t img = 0; img < kImages; ++img) {
        if (!scriptHeader_[img]) continue;
        ImageScript& sc = scripts_[img];
        // Init, then Built (what a finished building plays), each read straight through waits to the
        // first jump or end: the frame it rests on and every overlay it creates.
        for (int anim : {0, 16}) {
            size_t q = animOffset(int(img), anim);
            if (!q) continue;
            bool waited = anim != 0;  // past Init's first wait: startFrame is set
            for (int n = 0; n < 96 && q < d.size(); ++n) {
                uint8_t op = d[q++];
                if (op >= 0x45 || !kOpArgs[op]) break;
                size_t len = argSize(kOpArgs[op]);
                if (q + len > d.size()) break;
                auto s8 = [&](size_t o) { return int(int8_t(d[q + o])); };
                const int arg = len >= 2 ? readU16(d, q) : 0;
                switch (op) {
                case 0x00:  // playfram
                    if (anim == 0) sc.initFrame = arg;
                    if (!waited) sc.startFrame = arg, sc.startFrameTile = false;
                    break;
                case 0x01:  // playframtile
                    if (!waited) sc.startFrame = arg, sc.startFrameTile = true;
                    break;
                case 0x05: waited = waited || d[q]; break;                      // wait (0 ticks: goes on)
                case 0x06: waited = waited || d[q] || d[q + 1]; break;          // waitrand
                case 0x08: case 0x0D: sc.over.push_back({arg, s8(2), s8(3)}); break;   // imgol, imgoluselo
                case 0x0A: sc.over.push_back({arg, 0, 0}); break;                      // imgolorig
                case 0x09: case 0x0E: sc.under.push_back({arg, s8(2), s8(3)}); break;  // imgul, imguluselo
                case 0x3D: sc.under.push_back({int(img) + 1, s8(0), s8(1)}); break;    // imgulnextid
                case 0x0F: case 0x10: case 0x15: sc.over.push_back({spriteImage(arg), len >= 4 ? s8(2) : 0, len >= 4 ? s8(3) : 0}); break;
                case 0x11: case 0x13: case 0x14: sc.under.push_back({spriteImage(arg), s8(2), s8(3)}); break;
                }
                q += len;
                if (op == 0x07 || op == 0x16 || op == 0x30 || op == 0x36) {  // goto, end, ignorerest, return
                    waited = true;
                    break;
                }
            }
        }
        std::erase_if(sc.under, [](const ImageScript::Overlay& o) { return o.image < 0 || o.image >= int(kImages); });
        std::erase_if(sc.over, [&](const ImageScript::Overlay& o) { return o.image < 0 || o.image >= int(kImages) || o.image == int(img); });
    }
}

std::vector<uint16_t> UnitSprites::scriptIds() const {
    std::vector<uint16_t> out;
    for (const auto& [id, at] : scriptEntries_) out.push_back(id);
    return out;
}

size_t UnitSprites::animOffset(int image, int anim) const {
    if (image < 0 || size_t(image) >= scriptHeader_.size() || !scriptHeader_[size_t(image)]) return 0;
    return headerAnimOffset(scriptHeader_[size_t(image)], anim);
}

size_t UnitSprites::scriptAnimOffset(int scriptId, int anim) const {
    auto it = scriptEntries_.find(uint16_t(scriptId));
    return scriptId >= 0 && it != scriptEntries_.end() ? headerAnimOffset(it->second, anim) : 0;
}

size_t UnitSprites::headerAnimOffset(size_t h, int anim) const {
    // Header: "SCPE", type, 3 bytes, then one u16 offset per animation; the type sets how many.
    if (h + 8 > iscript_.size()) return 0;
    static const std::map<int, int> counts = {{0, 2},   {1, 2},   {2, 4},   {12, 14}, {13, 14}, {14, 16}, {15, 16}, {20, 22},
                                              {21, 22}, {23, 24}, {24, 26}, {26, 28}, {27, 28}, {28, 30}, {29, 30}};
    auto c = counts.find(iscript_[h + 4]);
    if (c == counts.end() || anim < 0 || anim >= c->second || h + 10 + 2 * size_t(anim) > iscript_.size()) return 0;
    return readU16(iscript_, h + 8 + 2 * size_t(anim));
}

ImageAnim::ImageAnim(const UnitSprites& sprites, int image, uint32_t seed, int anim) {
    const size_t start = sprites.animOffset(image, anim);
    if (!start) return;
    code_ = &sprites.iscript();
    pc_ = start;
    rng_ = seed * 2654435761u + 1;
    stopped_ = false;
    frame_ = sprites.script(image).initFrame;
    tick();  // the first tick runs up to the first wait
}

bool ImageAnim::tick() {
    if (stopped_) return false;
    if (wait_ > 0 && --wait_ > 0) return false;
    const Bytes& d = *code_;
    const int before = frame_;
    auto rand = [&]() {
        rng_ = rng_ * 1103515245u + 12345u;
        return (rng_ >> 16) & 0x7FFF;
    };
    // Up to the next wait; a loop without one stops the script.
    for (int n = 0; n < 256; ++n) {
        if (pc_ >= d.size()) break;
        const uint8_t op = d[pc_];
        size_t len = 0;
        if (op == 0x19 || op == 0x1C) {  // playsndrand, attackmelee: a count, then that many sounds
            if (pc_ + 1 >= d.size()) break;
            len = 1 + 2 * size_t(d[pc_ + 1]);
        } else {
            if (op >= 0x45 || !kOpArgs[op]) break;
            len = argSize(kOpArgs[op]);
        }
        if (pc_ + 1 + len > d.size()) break;
        const size_t a = pc_ + 1;
        pc_ = a + len;
        switch (op) {
        case 0x1B: case 0x25: case 0x26: case 0x27: case 0x28: attacked_ = true; break;  // the attacks
        case 0x18: sounds_.push_back(readU16(d, a)); break;  // playsnd
        case 0x19:                                           // playsndrand
        case 0x1C:                                           // attackmelee
            if (op == 0x1C) attacked_ = true;
            if (d[a]) sounds_.push_back(readU16(d, a + 1 + 2 * (rand() % d[a])));
            break;
        case 0x1A: {  // playsndbtwn
            const int lo = readU16(d, a), hi = std::max<int>(lo, readU16(d, a + 2));
            sounds_.push_back(lo + int(rand() % uint32_t(hi - lo + 1)));
            break;
        }
        case 0x00:  // playfram
        case 0x01:  // playframtile
            frame_ = readU16(d, a);
            break;
        case 0x05:  // wait
            if (d[a]) {
                wait_ = d[a];
                return frame_ != before;
            }
            break;
        case 0x06: {  // waitrand
            const int lo = d[a], hi = std::max<int>(d[a], d[a + 1]);
            wait_ = lo + int(rand() % uint32_t(hi - lo + 1));
            if (wait_) return frame_ != before;
            break;
        }
        case 0x07: pc_ = readU16(d, a); break;  // goto
        case 0x1E:                              // randcondjmp
            if (int(rand() & 255) <= d[a]) pc_ = readU16(d, a + 1);
            break;
        case 0x35: ret_ = pc_, pc_ = readU16(d, a); break;  // call
        case 0x36: pc_ = ret_; break;                       // return
        case 0x16:                                          // end
        case 0x30:                                          // ignorerest
            stopped_ = true;
            return frame_ != before;
        default: break;
        }
    }
    stopped_ = true;
    return frame_ != before;
}

int UnitSprites::animatedFrame(uint16_t unitType, int image, int scriptFrame, bool& flip) const {
    const int facing = defaultFrame(unitType, image, flip);
    if (image < 0 || size_t(image) >= imageTurns_.size() || !imageTurns_[image]) return scriptFrame;
    const int base = size_t(image) < scripts_.size() ? scripts_[image].initFrame / 17 * 17 : 0;
    return scriptFrame / 17 * 17 + (facing - base);
}

const ImageScript& UnitSprites::script(int image) const {
    static const ImageScript none;
    return image >= 0 && size_t(image) < scripts_.size() ? scripts_[image] : none;
}

bool UnitSprites::isShadow(int image) const {
    return image >= 0 && size_t(image) < imageDrawFn_.size() && imageDrawFn_[image] == 10;
}

UnitSprites::UnitSprites() = default;
UnitSprites::~UnitSprites() = default;

bool UnitSprites::load(const GameData& data, std::string& error) {
    data_ = &data;
    sprites_.clear();  // loaded again: the game's graphics, without edits
    Bytes units, flingy, sprites, images, tbl, tunit;
    if (!data.readFile("arr\\units.dat", units, error) || !data.readFile("arr\\flingy.dat", flingy, error) ||
        !data.readFile("arr\\sprites.dat", sprites, error) || !data.readFile("arr\\images.dat", images, error) ||
        !data.readFile("arr\\images.tbl", tbl, error))
        return false;
    if (units.size() < 2472 || flingy.size() < kFlingies * 2 || sprites.size() < kSprites * 2 ||
        images.size() < 3996 + kImages) {
        error = "unexpected .dat sizes";
        return false;
    }
    TblStrings paths = parseTbl(tbl);
    if (paths.bad) {
        error = tblDamaged("arr\\images.tbl", paths.bad);
        return false;
    }
    unitFlingy_.assign(units.begin(), units.begin() + kUnits);
    unitSubunit_.resize(kUnits);
    unitDirection_.assign(units.begin() + 2244, units.begin() + 2244 + kUnits);
    for (size_t i = 0; i < kUnits; ++i) unitSubunit_[i] = readU16(units, 228 + 2 * i);
    flingySprite_.resize(kFlingies);
    for (size_t i = 0; i < kFlingies; ++i) flingySprite_[i] = readU16(flingy, 2 * i);
    spriteImage_.resize(kSprites);
    for (size_t i = 0; i < kSprites; ++i) spriteImage_[i] = readU16(sprites, 2 * i);
    // sprites.dat after the images (u16 x 517): health bar u8 x 387, unknown u8 x 517, visible u8 x 517,
    // selection circle image u8 x 387, selection circle vertical offset u8 x 387 (sprites 130-516).
    {
        const size_t n = kSprites - 130, circle = kSprites * 2 + n + kSprites * 2, offset = circle + n;
        if (sprites.size() >= offset + n) {
            spriteSelCircle_.assign(sprites.begin() + long(circle), sprites.begin() + long(circle + n));
            spriteSelOffset_.assign(sprites.begin() + long(offset), sprites.begin() + long(offset + n));
        }
    }
    imageGrp_.resize(kImages);
    for (size_t i = 0; i < kImages; ++i) imageGrp_[i] = readU32(images, 4 * i);
    imageTurns_.assign(images.begin() + 3996, images.begin() + 3996 + kImages);
    imageDrawFn_.assign(images.begin() + 7992, images.begin() + 7992 + kImages);
    // After the u32 iscript ids (9990): shield, attack, damage, special, landing dust, lift-off overlays.
    imageSpecial_.assign(kImages, 0);
    imageRemap_.assign(images.begin() + 8991, images.begin() + 8991 + kImages);
    if (images.size() >= 25974 + kImages * 4)
        for (size_t i = 0; i < kImages; ++i) imageSpecial_[i] = readU32(images, 25974 + 4 * i);
    {
        // All six lo files: attack, damage, special, landing dust, lift-off (in images.dat order after the shield's), shield.
        const size_t at[6] = {17982, 21978, 25974, 29970, 33966, 13986};
        for (int k = 0; k < 6; ++k) {
            imageOverlays_[k].assign(kImages, 0);
            if (images.size() >= at[k] + kImages * 4)
                for (size_t i = 0; i < kImages; ++i) imageOverlays_[k][i] = readU32(images, at[k] + 4 * i);
        }
    }
    imagePaths_ = std::move(paths.strings);
    loadScripts(data, images);

    // Player colors: tunit.pcx holds 8 palette indices per player, side by side.
    if (data.readFile("game\\tunit.pcx", tunit, error)) {
        int w = 0, h = 0;
        std::vector<uint8_t> px = decodePcx(tunit, w, h);
        playerColors_.assign(px.begin(), px.begin() + std::min<size_t>(px.size(), size_t(w)));
    }
    error.clear();
    return true;
}

int UnitSprites::imageFor(uint16_t unitType) const {
    if (unitType >= unitFlingy_.size()) return -1;
    uint8_t f = unitFlingy_[unitType];
    if (f >= flingySprite_.size()) return -1;
    uint16_t s = flingySprite_[f];
    if (s >= spriteImage_.size()) return -1;
    uint16_t img = spriteImage_[s];
    return img < kImages ? img : -1;
}

int UnitSprites::imageForSprite(uint16_t sprite) const {
    if (sprite >= spriteImage_.size()) return -1;
    uint16_t img = spriteImage_[sprite];
    return img < kImages ? img : -1;
}

bool UnitSprites::selectionCircle(uint16_t unitType, int& image, int& offsetY) const {
    if (unitType >= unitFlingy_.size()) return false;
    uint8_t f = unitFlingy_[unitType];
    if (f >= flingySprite_.size()) return false;
    uint16_t s = flingySprite_[f];
    if (s < 130 || size_t(s - 130) >= spriteSelCircle_.size()) return false;
    // Selection circle images are images.dat 561 onwards (22 to 146 pixels), indexed by this field.
    image = 561 + spriteSelCircle_[s - 130];
    offsetY = spriteSelOffset_[s - 130];
    return image < int(kImages);
}

int UnitSprites::subunitImage(uint16_t unitType) const {
    if (unitType >= unitSubunit_.size() || unitSubunit_[unitType] == kNoUnit) return -1;
    return imageFor(unitSubunit_[unitType]);
}

int UnitSprites::defaultFrame(uint16_t unitType, int image, bool& flip) const {
    flip = false;
    if (image < 0 || size_t(image) >= imageTurns_.size()) return 0;
    // An image that does not turn: the frame its Init rests on (a sieged tank's base: deployed).
    if (!imageTurns_[image]) return size_t(image) < scripts_.size() ? scripts_[image].initFrame : 0;
    // Turning graphics hold 17 frames (up .. down via right); the left side is mirrored.
    // Face down-right (direction 12 of 32) unless units.dat fixes a direction.
    int dir = unitType < unitDirection_.size() && unitDirection_[unitType] < 32 ? unitDirection_[unitType] : 12;
    if (dir > 16) flip = true, dir = 32 - dir;
    // Facing sets are 17 frames; start from the set the image's Init animation shows.
    int base = size_t(image) < scripts_.size() ? scripts_[image].initFrame / 17 * 17 : 0;
    return base + dir;
}

uint8_t UnitSprites::remap(uint8_t idx, uint8_t owner) const {
    if (idx >= 8 && idx < 16 && size_t(owner) * 8 + 7 < playerColors_.size()) return playerColors_[size_t(owner) * 8 + (idx - 8)];
    return idx;
}

const Grp* UnitSprites::grp(int image) {
    if (image < 0 || size_t(image) >= imageGrp_.size()) return nullptr;
    auto it = grps_.find(image);
    if (it != grps_.end()) return it->second.get();
    std::unique_ptr<Grp> g;
    uint32_t idx = imageGrp_[image];
    if (idx && idx - 1 < imagePaths_.size()) {
        Bytes raw;
        std::string err;
        g = std::make_unique<Grp>();
        if (!data_->readFile("unit\\" + imagePaths_[idx - 1], raw, err) || !g->parse(raw)) g.reset();
    }
    return (grps_[image] = std::move(g)).get();
}

// --- SpriteSim: a sprite run as OpenBW's iscript_execute runs it --------------------------------------------------

SpriteSim::SpriteSim(UnitSprites& sprites, int image, int anim, int dir32, uint32_t seed, int tilesetIndex)
    : sp_(&sprites), heading_((dir32 & 31) * 8), tileset_(tilesetIndex), rng_(seed * 2654435761u + 1) {
    if (image < 0 || !sprites.initOffset(image)) return;
    main_ = addImage(image, 0, 0, true, -1, false);
    if (anim != 0) playAnim(anim);  // its overlays too (a shadow's Death ends it)
}

int SpriteSim::rand() {
    rng_ = rng_ * 22695477u + 1;
    return int((rng_ >> 16) & 0x7FFF);
}

void SpriteSim::setImageDirection(Image& im, int dir32) {
    if (!im.directional) return;
    const int d = ((dir32 % 32) + 32) % 32;
    im.dirIndex = d > 16 ? 32 - d : d;
    im.flipped = d > 16;
}

void SpriteSim::setDirection(int dir32) { setHeading(dir32 * 8); }

// set_unit_heading: every image faces the new heading ((heading + 4) / 8 of 32 directions).
void SpriteSim::setHeading(int dir256) {
    heading_ = dir256 & 255;
    for (auto& im : images_) setImageDirection(im, direction());
}

void SpriteSim::setTarget(int dir256, int distance) {
    target_ = dir256 < 0 ? -1 : dir256 & 255;
    targetDistance_ = distance;
    if (target_ >= 0 && turnRate_ <= 0) setHeading(target_);
}

// replace_sprite_images: every image goes, and the sprite's image comes again (it runs its Init).
void SpriteSim::replaceImages() {
    if (!running()) return;
    const int id = images_[size_t(main_)].id;
    images_.clear(), children_.clear();
    main_ = 0;
    main_ = addImage(id, 0, 0, true, -1, false);
}

void SpriteSim::warpIn() {
    if (!running()) return;
    replaceImages();
    const size_t start = sp_->scriptAnimOffset(193, 0);
    if (!running() || !start) return;
    Image& im = images_[size_t(main_)];
    im.pc = start, im.wait = 0;
    execute(main_);
    setModifier(images_[size_t(main_)], 12);
    warp_ = 1, handoff_ = 0;  // the order has the sprite now
}

bool SpriteSim::running() const { return main_ >= 0 && main_ < int(images_.size()) && images_[size_t(main_)].alive; }

int SpriteSim::mainFrame() const { return running() ? images_[size_t(main_)].base + images_[size_t(main_)].dirIndex : 0; }

// A new image above or below `rel` (the main image when -1); it runs its Init at once, as create_image does.
void SpriteSim::setModifier(Image& im, int mod) {
    im.mod = mod;
    if (mod == 17) im.mod1 = 48, im.mod2 = 2;  // warp flash
}

int SpriteSim::addImage(int id, int ox, int oy, bool above, int rel, bool special) {
    if (id < 0 || id >= sp_->imageCount() || images_.size() > 64) return -1;
    Image im;
    im.id = id, im.ox = ox, im.oy = oy, im.special = special, im.directional = sp_->turns(id);
    if (rel < 0) rel = main_;
    setModifier(im, sp_->drawFunction(id));
    // An image made by a cloaking or cloaked image cloaks as it does.
    if (!im.mod && rel >= 0 && rel < int(images_.size()) && images_[size_t(rel)].mod >= 2 && images_[size_t(rel)].mod <= 7) {
        const Image& r = images_[size_t(rel)];
        im.mod = r.mod, im.mod1 = r.mod1, im.mod2 = r.mod2;
    }
    int at = 0;
    if (!images_.empty() && rel >= 0 && rel < int(images_.size())) at = above ? rel : rel + 1;
    // The new image faces as the image that made it.
    if (im.directional && rel >= 0 && rel < int(images_.size())) {
        const Image& r = images_[size_t(rel)];
        const int index = r.flipped ? 32 - r.dirIndex : r.dirIndex;
        setImageDirection(im, index);
    } else setImageDirection(im, direction());
    images_.insert(images_.begin() + at, im);
    if (main_ >= at && images_.size() > 1) ++main_;
    if (images_.size() == 1) main_ = 0;
    if (special) {  // the main image's special point
        auto [x, y] = sp_->loOffset(images_[size_t(main_)].id, 2, mainFrame(), 0);
        images_[size_t(at)].ox = images_[size_t(main_)].flipped ? -x : x, images_[size_t(at)].oy = y;
    }
    runAnim(at, 0);
    return at;
}

void SpriteSim::addChild(int spriteId, int x, int y, bool above, const Image& parent) {
    if (children_.size() > 32) return;
    const int image = sp_->spriteImage(spriteId);
    if (image < 0) return;
    Child c;
    const int index = parent.flipped ? 32 - parent.dirIndex : parent.dirIndex;
    c.sim = std::make_shared<SpriteSim>(*sp_, image, 0, index, rng_ + uint32_t(children_.size()), tileset_);
    c.x = parent.ox + x + int(std::lround(px_)), c.y = parent.oy + y + int(std::lround(py_)), c.above = above;
    children_.push_back(std::move(c));
}

void SpriteSim::runAnim(int index, int anim) {
    Image& im = images_[size_t(index)];
    const size_t start = sp_->animOffset(im.id, anim);
    if (!start) return;
    im.pc = start, im.wait = 0;
    execute(index);
}

void SpriteSim::playAnim(int anim) {
    for (size_t k = 0; k < images_.size(); ++k)
        if (images_[k].alive && sp_->animOffset(images_[k].id, anim)) runAnim(int(k), anim);
}

void SpriteSim::tick() {
    if (!sp_) return;
    // While the main image waits, what it ran last stays: the listing keeps its place.
    std::vector<size_t> before = std::move(executed_);
    executed_.clear();
    // order_IncompleteWarping runs before the scripts: at the last tick's signal, the images again with the Warp in
    // animation; at the next one (the flash ended), done.
    if (warp_ && (signals_ & 1)) {
        signals_ &= ~1;
        if (warp_ == 1) replaceImages(), playAnim(21), warp_ = 2;
        else if (warp_ == 2) warp_ = 3;
    }
    // update_unit_heading: toward the target by the turn rate at most.
    if (target_ >= 0 && heading_ != target_) {
        int turn = int(int8_t(uint8_t(target_ - heading_)));
        if (turnRate_ > 0) turn = std::clamp(turn, -turnRate_, turnRate_);
        setHeading(heading_ + turn);
    }
    // Each image, front to back (an image added while this runs ran its Init already).
    for (size_t k = 0; k < images_.size(); ++k) {
        const int before = int(images_.size());
        // The draw function's own clock (OpenBW iscript_execute_sprite): cloaking takes 8 steps of 4 ticks,
        // decloaking the same back; the warp flash brightens to 63 in steps of 3 ticks, then plays Death.
        if (Image& im = images_[k]; im.alive) {
            if (im.mod == 2 || im.mod == 5) {
                if (im.mod2) --im.mod2;
                else if (im.mod2 = 3; ++im.mod1 >= 8) setModifier(im, im.mod + 1);
            } else if (im.mod == 4 || im.mod == 7) {
                if (im.mod2) --im.mod2;
                else if (im.mod2 = 3; im.mod1) --im.mod1;
                else setModifier(im, 0);
            } else if (im.mod == 17) {
                if (im.mod2) --im.mod2;
                else if (im.mod2 = 2; im.mod1 < 63) ++im.mod1;
                else runAnim(int(k), 1), signals_ |= 1;  // image_update_warpin: Death, and the order's signal
            }
        }
        if (images_[k].alive) execute(int(k));
        if (int(images_.size()) > before) k += size_t(int(images_.size()) - before);  // skip what was just added
    }
    for (auto& c : children_)
        if (c.sim) c.sim->tick();
    children_.erase(std::remove_if(children_.begin(), children_.end(), [](const Child& c) { return !c.sim || !c.sim->running(); }), children_.end());
    // Images that ended are gone (OpenBW destroys them); room for the ones scripts make later.
    for (size_t k = images_.size(); k-- > 0;)
        if (!images_[k].alive && int(k) != main_) {
            images_.erase(images_.begin() + long(k));
            if (main_ > int(k)) --main_;
        }
    if (executed_.empty()) executed_ = std::move(before);
}

bool SpriteSim::execute(int index) {
    Image* im = &images_[size_t(index)];
    if (!im->alive) return false;
    if (im->wait) {
        --im->wait;
        return true;
    }
    const Bytes& d = sp_->iscript();
    const bool isMain = index == main_;
    auto u16 = [&](size_t a) { return int(d[a] | d[a + 1] << 8); };
    for (int n = 0; n < 512; ++n) {
        im = &images_[size_t(index)];
        if (im->pc >= d.size()) break;
        const size_t at = im->pc;
        const uint8_t op = d[at];
        size_t len = 0;
        if (op == 0x19 || op == 0x1C) {
            if (at + 1 >= d.size()) break;
            len = 1 + 2 * size_t(d[at + 1]);
        } else {
            if (op >= 0x45 || !kOpArgs[op]) break;
            len = argSize(kOpArgs[op]);
        }
        if (at + 1 + len > d.size()) break;
        const size_t a = at + 1;
        im->pc = a + len;
        if (isMain) executed_.push_back(at);
        const int8_t sb = int8_t(d[a]);
        switch (op) {
        case 0x00: im->base = u16(a); break;                                                  // playfram
        case 0x01: im->base = u16(a) + tileset_; break;                                       // playframtile
        case 0x02: im->ox = sb; break;                                                        // sethorpos
        case 0x03: im->oy = sb; break;                                                        // setvertpos
        case 0x04: im->ox = sb, im->oy = int8_t(d[a + 1]); break;                             // setpos
        case 0x05:                                                                            // wait
            im->wait = d[a] ? d[a] - 1 : 0;
            if (isMain && d[a] >= 60) handoff_ = 3;  // an idle loop: the order decides what comes next
            return true;
        case 0x06: {                                                                          // waitrand
            const int lo = d[a], hi = std::max<int>(d[a], d[a + 1]);
            im->wait = lo + (rand() & 0xFF) % (hi - lo + 1) - 1;
            if (im->wait < 0) im->wait = 0;
            return true;
        }
        case 0x07: im->pc = size_t(u16(a)); break;                                            // goto
        case 0x08: case 0x09: {                                                               // imgol, imgul
            const int ox = im->ox + int8_t(d[a + 2]), oy = im->oy + int8_t(d[a + 3]);
            if (const int pos = addImage(u16(a), ox, oy, op == 0x08, index, false); pos >= 0 && pos <= index) ++index;
            break;
        }
        case 0x0A: case 0x0B:
            if (const int pos = addImage(u16(a), 0, 0, op == 0x0A, index, true); pos >= 0 && pos <= index) ++index;
            break;         // imgolorig, switchul
        case 0x0D: case 0x0E: {                                                               // imgoluselo, imguluselo
            auto [x, y] = sp_->loOffset(im->id, d[a + 2], im->base + im->dirIndex, d[a + 3]);
            if (const int pos = addImage(u16(a), im->flipped ? -x : x, y, op == 0x0D, index, false); pos >= 0 && pos <= index) ++index;
            break;
        }
        case 0x0F: case 0x10: case 0x42: addChild(u16(a), int8_t(d[a + 2]), int8_t(d[a + 3]), true, *im); break;   // sprol, highsprol, grdsprol
        case 0x11: case 0x14: addChild(u16(a), int8_t(d[a + 2]), int8_t(d[a + 3]), false, *im); break;             // lowsprul, sprul
        case 0x13: addChild(u16(a), int8_t(d[a + 2]), int8_t(d[a + 3]), false, *im); break;                        // spruluselo
        case 0x15: {                                                                          // sproluselo
            auto [x, y] = sp_->loOffset(im->id, d[a + 2], im->base + im->dirIndex, 0);
            addChild(u16(a), im->flipped ? -x : x, y, true, *im);
            break;
        }
        case 0x3D:
            if (const int pos = addImage(im->id + 1, int8_t(d[a]), int8_t(d[a + 1]), false, index, false); pos >= 0 && pos <= index) ++index;
            break;              // imgulnextid
        case 0x16:                                                                            // end
            images_[size_t(index)].alive = false;
            return false;
        case 0x17: im->flipped = d[a] != 0; break;                                            // setflipstate
        case 0x18: sounds_.push_back(u16(a)); break;                                          // playsnd
        case 0x19:                                                                            // playsndrand
            if (d[a]) sounds_.push_back(u16(a + 1 + 2 * size_t(rand() % d[a])));
            break;
        case 0x1A: {                                                                          // playsndbtwn
            const int lo = u16(a), hi = std::max(lo, u16(a + 2));
            sounds_.push_back(lo + rand() % (hi - lo + 1));
            break;
        }
        case 0x1C:                                                                            // attackmelee
            attacked_ = true;
            if (d[a]) sounds_.push_back(u16(a + 1 + 2 * size_t(rand() % d[a])));
            break;
        case 0x1B: case 0x25: case 0x26: case 0x27: case 0x28: case 0x31: case 0x44: attacked_ = true; break;  // the attacks
        case 0x1D:                                                                            // followmaingraphic
            if (main_ >= 0 && main_ < int(images_.size())) {
                const Image& m = images_[size_t(main_)];
                im->base = m.base, im->dirIndex = m.dirIndex, im->flipped = m.flipped;
            }
            break;
        case 0x1E: if ((rand() & 0xFF) <= d[a]) im->pc = size_t(u16(a + 1)); break;          // randcondjmp
        case 0x1F: setHeading(heading_ - 8 * d[a]); break;                                    // turnccwise
        case 0x20: setHeading(heading_ + 8 * d[a]); break;                                    // turncwise
        case 0x21: if (target_ < 0) setHeading(heading_ + 8); break;                          // turn1cwise: not with a target
        case 0x22: setHeading(rand() % 4 == 1 ? heading_ - 8 * d[a] : heading_ + 8 * d[a]); break;  // turnrand
        case 0x34: setHeading(8 * d[a]); break;                                               // setfldirect
        case 0x29: {                                                                          // move: that many pixels ahead
            const double r = heading_ * 3.14159265358979 / 128;
            px_ += d[a] * std::sin(r), py_ -= d[a] * std::cos(r);
            break;
        }
        case 0x24: signals_ |= d[a]; break;                                                   // sigorder
        case 0x40: im->mod1 = d[a], im->mod2 = d[a + 1]; break;                               // warpoverlay: the warp texture's frame
        case 0x3A:                                                                            // trgtrangecondjmp
            if (target_ >= 0 && targetDistance_ <= u16(a)) im->pc = size_t(u16(a + 2));
            break;
        case 0x3B:                                                                            // trgtarccondjmp
            if (target_ >= 0 && std::abs(int(int8_t(uint8_t(u16(a) - target_)))) < u16(a + 2)) im->pc = size_t(u16(a + 4));
            break;
        case 0x2B:                                                                            // engframe
            if (main_ >= 0) im->base = d[a], im->dirIndex = images_[size_t(main_)].dirIndex, im->flipped = images_[size_t(main_)].flipped;
            break;
        case 0x2C:                                                                            // engset
            if (main_ >= 0) {
                const Image& m = images_[size_t(main_)];
                const Grp* g = sp_->grp(m.id);
                im->base = m.base + int(g ? g->frames.size() & 0x7FFF : 0) * d[a], im->dirIndex = m.dirIndex, im->flipped = m.flipped;
            }
            break;
        case 0x32: im->hidden = true; break;                                                  // tmprmgraphicstart
        case 0x33: im->hidden = false; break;                                                 // tmprmgraphicend
        case 0x35: im->ret = im->pc, im->pc = size_t(u16(a)); break;                          // call
        case 0x36: im->pc = im->ret; break;                                                   // return
        case 0x39: if (index != main_) im->pc = size_t(u16(a)); break;                        // pwrupcondjmp
        case 0x3C: {  // curdirectcondjmp: the heading (in 256ths) less than `range` from `dir`
            const int dir = u16(a), range = u16(a + 2);
            const int diff = std::abs(int(int8_t(uint8_t(heading_ - dir))));
            if (diff < range) im->pc = size_t(u16(a + 4));
            break;
        }
        case 0x38: {  // creategasoverlays: a smoke (images 430 and up) at the image's special overlay point
            auto [x, y] = sp_->loOffset(im->id, 2, im->base + im->dirIndex, d[a]);
            if (const int pos = addImage(430 + d[a], im->flipped ? -x : x, y, true, index, false); pos >= 0 && pos <= index) ++index;
            break;
        }
        case 0x30:                                                                            // ignorerest: waits for its order
            im->wait = 10, im->pc = at;
            if (isMain) handoff_ = 2;
            return true;
        case 0x2A: if (isMain) handoff_ = 1; break;                                            // gotorepeatattk
        default: break;  // orders, movement, targets: nothing to run here (no target, so no condition jumps)
        }
    }
    return true;
}

std::vector<SpriteSim::Draw> SpriteSim::draws() const {
    std::vector<Draw> out;
    for (const auto& c : children_)  // below: the sprites made under this one
        if (!c.above && c.sim)
            for (auto dd : c.sim->draws()) dd.x += c.x, dd.y += c.y, out.push_back(dd);
    for (size_t k = images_.size(); k-- > 0;) {  // back to front
        const Image& im = images_[k];
        if (!im.alive || im.hidden) continue;
        out.push_back({im.id, im.base + im.dirIndex, im.flipped, im.ox + int(std::lround(px_)), im.oy + int(std::lround(py_)), im.mod, im.mod1});
    }
    for (const auto& c : children_)
        if (c.above && c.sim)
            for (auto dd : c.sim->draws()) dd.x += c.x, dd.y += c.y, out.push_back(dd);
    return out;
}

std::pair<int, int> UnitSprites::loOffset(int image, int lo, int frame, int index) const {
    if (image < 0 || lo < 0 || lo > 5 || size_t(image) >= imageOverlays_[lo].size() || !data_) return {0, 0};
    const uint32_t tbl = imageOverlays_[lo][size_t(image)];
    if (!tbl) return {0, 0};
    auto [it, fresh] = lo_.try_emplace({image, lo});
    if (fresh) {
        // .lol: u32 frames, u32 points per frame, then per frame a u32 file offset to (s8 x, s8 y) pairs.
        Bytes f;
        std::string err;
        if (tbl <= imagePaths_.size() && data_->readFile("unit\\" + imagePaths_[tbl - 1], f, err) && f.size() >= 8) {
            const uint32_t frames = readU32(f, 0), per = readU32(f, 4);
            for (uint32_t fr = 0; fr < frames && 8 + 4 * size_t(fr) + 4 <= f.size(); ++fr) {
                const size_t o = readU32(f, 8 + 4 * size_t(fr));
                std::vector<std::pair<int8_t, int8_t>> pts;
                for (uint32_t p = 0; p < per && o + 2 * p + 1 < f.size(); ++p) pts.push_back({int8_t(f[o + 2 * p]), int8_t(f[o + 2 * p + 1])});
                it->second.push_back(std::move(pts));
            }
        }
    }
    const auto& v = it->second;
    if (v.empty()) return {0, 0};
    const auto& pts = v[size_t(frame) < v.size() ? size_t(frame) : size_t(frame) % v.size()];
    if (size_t(index) >= pts.size()) return {0, 0};
    return {pts[size_t(index)].first, pts[size_t(index)].second};
}

std::pair<int, int> UnitSprites::specialOffset(int image, int frame) const {
    if (image < 0 || size_t(image) >= imageSpecial_.size() || !imageSpecial_[size_t(image)] || !data_) return {0, 0};
    auto [it, fresh] = specialLo_.try_emplace(image);
    if (fresh) {
        // .lol: u32 frames, u32 offsets per frame, then per frame a u32 file offset to (s8 x, s8 y) pairs.
        const uint32_t tbl = imageSpecial_[size_t(image)];
        Bytes lo;
        std::string err;
        if (tbl <= imagePaths_.size() && data_->readFile("unit\\" + imagePaths_[tbl - 1], lo, err) && lo.size() >= 8) {
            const uint32_t frames = readU32(lo, 0), per = readU32(lo, 4);
            for (uint32_t f = 0; per && f < frames && 8 + 4 * size_t(f) + 4 <= lo.size(); ++f) {
                const size_t o = readU32(lo, 8 + 4 * size_t(f));
                it->second.push_back(o + 1 < lo.size() ? std::pair{int8_t(lo[o]), int8_t(lo[o + 1])} : std::pair<int8_t, int8_t>{0, 0});
            }
        }
    }
    const auto& v = it->second;
    if (v.empty()) return {0, 0};
    const auto& p = v[size_t(frame) < v.size() ? size_t(frame) : size_t(frame) % v.size()];
    return {p.first, p.second};
}

const std::vector<uint8_t>& UnitSprites::tilesetTable(const Tileset& ts, const std::string& name) const {
    auto [it, fresh] = tilesetTables_.try_emplace({ts.era, name});
    if (fresh && data_) {
        Bytes pcx;
        std::string err;
        int w = 0, h = 0;
        if (data_->readFile(std::string("TileSet\\") + tilesetName(ts.era) + "\\" + name + ".pcx", pcx, err)) it->second = decodePcx(pcx, w, h);
        if (w != 256) it->second.clear();
    }
    return it->second;
}

bool UnitSprites::glowColor(int image, uint8_t index, const Tileset& ts, Rgb& out) const {
    if (drawFunction(image) != 9 || size_t(image) >= imageRemap_.size() || !data_) return false;
    const int remap = imageRemap_[size_t(image)];
    static const char* names[] = {nullptr, "ofire", "gfire", "bfire", "bexpl"};
    if (remap < 1 || remap > 4) return false;
    auto [it, fresh] = remapTables_.try_emplace({ts.era, remap});
    if (fresh) {
        Bytes pcx;
        std::string err;
        int w = 0, h = 0;
        if (data_->readFile(std::string("TileSet\\") + tilesetName(ts.era) + "\\" + names[remap] + ".pcx", pcx, err))
            it->second = decodePcx(pcx, w, h);
    }
    // 256 columns (the color underneath) per row; a glow index n uses row n - 1. Over black: column 0.
    const auto& t = it->second;
    const size_t row = size_t(std::max(1, int(index)) - 1), rows = t.size() / 256;
    if (!rows) return false;
    out = ts.palette[t[std::min(row, rows - 1) * 256]];
    return true;
}

void UnitSprites::drawImage(Sprite& s, int image, int frame, bool flip, uint8_t owner, const Tileset& ts, int dx, int dy, Blend blend) {
    const bool shadow = blend == Blend::Shadow;
    const Grp* g = grp(image);
    if (!g || g->frames.empty()) return;
    const Grp::Frame& f = g->frames[size_t(frame) < g->frames.size() ? frame : 0];
    for (int y = 0; y < f.height; ++y)
        for (int x = 0; x < f.width; ++x) {
            uint8_t idx = f.pixels[size_t(y) * f.width + x];
            if (!idx) continue;
            if (shadow) {
                // Shadows darken what is below: half-transparent black where nothing is drawn yet.
                int bx = flip ? g->width - 1 - (f.x + x) : f.x + x;
                int px = s.originX - g->width / 2 + bx + dx, py = s.originY - g->height / 2 + f.y + y + dy;
                if (px < 0 || py < 0 || px >= s.width || py >= s.height) continue;
                uint8_t* d = &s.rgba[(size_t(py) * s.width + px) * 4];
                if (!d[3]) d[0] = d[1] = d[2] = 0, d[3] = 128;
                continue;
            }
            const uint8_t raw = idx;
            // Palette indices 8-15 are the player color ramp.
            if (idx >= 8 && idx < 16 && size_t(owner) * 8 + 7 < playerColors_.size())
                idx = playerColors_[size_t(owner) * 8 + (idx - 8)];
            int bx = flip ? g->width - 1 - (f.x + x) : f.x + x;
            int px = s.originX - g->width / 2 + bx + dx, py = s.originY - g->height / 2 + f.y + y + dy;
            if (px < 0 || py < 0 || px >= s.width || py >= s.height) continue;
            if (blend == Blend::Index) {
                uint8_t* d = &s.rgba[(size_t(py) * s.width + px) * 4];
                d[0] = raw, d[1] = idx, d[2] = 0, d[3] = 255;
                continue;
            }
            if (blend == Blend::Glow) {
                // Additive: brightens what is under it.
                Rgb c{};
                if (!glowColor(image, idx, ts, c)) continue;
                uint8_t* d = &s.rgba[(size_t(py) * s.width + px) * 4];
                d[0] = uint8_t(std::min(255, d[0] + c.r)), d[1] = uint8_t(std::min(255, d[1] + c.g)), d[2] = uint8_t(std::min(255, d[2] + c.b));
                d[3] = std::max<uint8_t>(d[3], std::max({c.r, c.g, c.b}));
                continue;
            }
            const Rgb& c = ts.palette[idx];
            uint8_t* d = &s.rgba[(size_t(py) * s.width + px) * 4];
            d[0] = c.r, d[1] = c.g, d[2] = c.b, d[3] = 255;
        }
}

const Sprite* UnitSprites::commandIcon(uint16_t unitType) {
    if (!iconsTried_ && data_) {
        iconsTried_ = true;
        Bytes grp, ticon;
        std::string err;
        int w = 0, h = 0;
        if (data_->readFile("unit\\cmdicons\\cmdicons.grp", grp, err) && data_->readFile("game\\ticon.pcx", ticon, err) &&
            ticon.size() > 128 + 769 && ticon[ticon.size() - 769] == 12) {
            std::vector<uint8_t> row = decodePcx(ticon, w, h);
            auto g = std::make_unique<Grp>();
            g->parse(grp);  // the last frames run past the end of the file; the unit icons come first
            if (row.size() >= 17 && g->frames.size() > 227) {
                const uint8_t* pal = ticon.data() + ticon.size() - 768;
                for (int i = 0; i < 17; ++i)
                    for (int c = 0; c < 3; ++c) iconRgb_.push_back(pal[row[std::min<size_t>(i, row.size() - 1)] * 3 + c]);
                // Units without an icon share one placeholder frame ("BLANK"): the frame repeated most
                // often among the unit icons.
                std::map<std::vector<uint8_t>, int> seen;
                int most = 0;
                for (int i = 0; i < 228; ++i) most = std::max(most, ++seen[g->frames[size_t(i)].pixels]);
                for (int i = 0; i < 228; ++i)
                    if (most > 1 && seen[g->frames[size_t(i)].pixels] == most) iconBlank_.push_back(uint16_t(i));
                cmdIcons_ = std::move(g);
            }
        }
    }
    if (!cmdIcons_ || unitType >= cmdIcons_->frames.size()) return nullptr;
    return iconFrame(unitType);
}

const Sprite* UnitSprites::blankIcon() {
    commandIcon(0);  // loads the icons
    return iconBlank_.empty() ? nullptr : iconFrame(iconBlank_.front());
}

const Sprite* UnitSprites::iconFrame(uint16_t unitType) {
    auto& slot = icons_[unitType];
    if (!slot) {
        const Grp::Frame& f = cmdIcons_->frames[unitType];
        auto s = std::make_unique<Sprite>();
        s->width = cmdIcons_->width, s->height = cmdIcons_->height;
        s->originX = s->width / 2, s->originY = s->height / 2;
        s->rgba.assign(size_t(s->width) * s->height * 4, 0);
        for (int y = 0; y < f.height; ++y)
            for (int x = 0; x < f.width; ++x) {
                int v = f.pixels[size_t(y) * f.width + x];
                if (!v || v > 16 || f.x + x >= s->width || f.y + y >= s->height) continue;
                uint8_t* o = &s->rgba[(size_t(f.y + y) * s->width + f.x + x) * 4];
                o[0] = iconRgb_[v * 3], o[1] = iconRgb_[v * 3 + 1], o[2] = iconRgb_[v * 3 + 2], o[3] = 255;
            }
        slot = std::move(s);
    }
    return slot.get();
}

const Sprite* UnitSprites::unitSprite(uint16_t unitType, uint8_t owner, const Tileset& ts) {
    if (spriteTileset_ != &ts) sprites_.clear(), spriteTileset_ = &ts;
    owner = std::min<uint8_t>(owner, 11);
    auto key = std::make_pair(unitType, owner);
    auto it = sprites_.find(key);
    if (it != sprites_.end()) return it->second.get();

    std::unique_ptr<Sprite> s;
    int image = imageFor(unitType);
    if (const Grp* g = grp(image)) {
        s = std::make_unique<Sprite>();
        int sub = unitType < unitSubunit_.size() && unitSubunit_[unitType] != kNoUnit ? imageFor(unitSubunit_[unitType]) : -1;
        const Grp* sg = grp(sub);
        // Room for the turret's offset, the shadow's and the overlays' (each up to a box away).
        int w = g->width, h = g->height;
        for (const auto* list : {&script(image).under, &script(image).over})
            for (const auto& o : *list)
                if (const Grp* og = grp(o.image)) w = std::max(w, og->width + 2 * std::abs(o.x)), h = std::max(h, og->height + 2 * std::abs(o.y));
        if (sg) w = std::max(w, sg->width + 64), h = std::max(h, sg->height + 64);
        s->width = w, s->height = h;
        s->originX = s->width / 2, s->originY = s->height / 2;
        s->rgba.assign(size_t(s->width) * s->height * 4, 0);
        // Turning graphics hold 17 frames per facing set (up .. down via right); others use frame 0.
        // Face down-right (direction 12 of 32) unless units.dat fixes a direction.
        auto frameFor = [&](int img, bool& flip) {
            flip = false;
            if (!imageTurns_[img]) return 0;
            int dir = unitDirection_[unitType] < 32 ? unitDirection_[unitType] : 12;
            if (dir > 16) flip = true, dir = 32 - dir;
            return dir;
        };
        bool flip = false;
        int frame = frameFor(image, flip);
        if (imageTurns_[image]) frame += scripts_[image].initFrame / 17 * 17;
        else frame = scripts_[image].initFrame;
        // Painter's order: the unit, its turret at the base's special-overlay offset, the overlays on top
        // (glows added to what is under them), then shadows, which only fill empty pixels.
        auto at = [&](const ImageScript::Overlay& o) { return std::pair{flip ? -o.x : o.x, o.y}; };
        drawImage(*s, image, frame, flip, owner, ts);
        if (sg) {
            bool sflip = false;
            int sframe = frameFor(sub, sflip) + (imageTurns_[sub] ? scripts_[sub].initFrame / 17 * 17 : 0);
            if (!imageTurns_[sub]) sframe = scripts_[sub].initFrame;
            auto [tx, ty] = specialOffset(image, frame);
            drawImage(*s, sub, sframe, sflip, owner, ts, flip ? -tx : tx, ty);
        }
        for (const auto& o : script(image).over) {
            auto [ox, oy] = at(o);
            const int of = imageTurns_[o.image] ? scripts_[o.image].initFrame / 17 * 17 + frame % 17 : scripts_[o.image].initFrame;
            drawImage(*s, o.image, of, imageTurns_[o.image] && flip, owner, ts, ox, oy, drawFunction(o.image) == 9 ? Blend::Glow : Blend::Normal);
        }
        for (const auto& o : script(image).under) {
            if (!isShadow(o.image)) continue;
            auto [ox, oy] = at(o);
            drawImage(*s, o.image, frame, flip, owner, ts, ox, oy, Blend::Shadow);
        }
    }
    return (sprites_[key] = std::move(s)).get();
}

// --- Map rendering --------------------------------------------------------------------------------

void drawUnits(Image& image, const std::vector<Unit>& units, UnitSprites& sprites, const Tileset& ts, int downscale) {
    downscale = std::max(downscale, 1);
    std::vector<const Unit*> order;
    for (const auto& u : units) order.push_back(&u);
    std::stable_sort(order.begin(), order.end(), [](const Unit* a, const Unit* b) { return a->y < b->y; });
    for (const Unit* u : order) {
        const Sprite* s = sprites.unitSprite(u->type, u->owner, ts);
        if (!s) continue;
        int left = u->x - s->originX, top = u->y - s->originY;
        for (int y = 0; y < s->height; ++y)
            for (int x = 0; x < s->width; ++x) {
                const uint8_t* src = &s->rgba[(size_t(y) * s->width + x) * 4];
                if (!src[3]) continue;
                int px = (left + x) / downscale, py = (top + y) / downscale;
                if (left + x < 0 || top + y < 0 || px >= image.width || py >= image.height) continue;
                uint8_t* d = &image.rgba[(size_t(py) * image.width + px) * 4];
                for (int k = 0; k < 3; ++k) d[k] = uint8_t((src[k] * src[3] + d[k] * (255 - src[3])) / 255);
                d[3] = 255;
            }
    }
}

}  // namespace stardraft
