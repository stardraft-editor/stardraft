// The game data the simulation reads: units.dat, weapons.dat, flingy.dat, sprites.dat, images.dat, iscript.bin, and
// the map's size, terrain (the tileset's CV5 and VF4: which minitiles can be walked on) and placed units.
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "stardraft/chk.h"
#include "world.h"

namespace sim {
namespace {

struct Reader {
    const std::vector<uint8_t>& d;
    int u8(size_t o) const { return o < d.size() ? d[o] : 0; }
    int u16(size_t o) const { return u8(o) | u8(o + 1) << 8; }
    int u32(size_t o) const { return u16(o) | u16(o + 2) << 16; }
};

bool readFile(const FileReader& read, const std::string& name, size_t minSize, std::vector<uint8_t>& out,
              std::string& error) {
    if (!read(name, out) || out.size() < minSize) {
        error = "The game file " + name + " cannot be read";
        return false;
    }
    return true;
}

// Animations in an iscript header, by its type.
int animCount(int type) {
    static const int counts[30] = {2,  2,  4,  0,  0,  0, 0,  0,  0,  0,  0,  0,  14, 14, 16,
                                   16, 0,  0,  0,  0,  22, 22, 0,  24, 26, 0,  28, 28, 30, 30};
    return type >= 0 && type < 30 ? counts[type] : 0;
}

}  // namespace

bool Tables::load(const FileReader& reader, std::string& error) {
    read = reader;
    std::vector<uint8_t> u, w, f, s, im;
    if (!readFile(read, "arr/units.dat", 19876, u, error) || !readFile(read, "arr/weapons.dat", 5460, w, error) ||
        !readFile(read, "arr/flingy.dat", 3135, f, error) || !readFile(read, "arr/sprites.dat", 1034, s, error) ||
        !readFile(read, "arr/images.dat", 9990 + 999 * 4, im, error) ||
        !readFile(read, "scripts/iscript.bin", 2, iscript, error))
        return false;
    Reader ru{u}, rw{w}, rf{f}, rs{s}, ri{im};
    units.resize(228);
    for (int i = 0; i < 228; ++i) {
        UnitData& d = units[i];
        d.flingy = ru.u8(i);
        d.direction = ru.u8(0x8C4 + i);
        d.sight = ru.u8(0x1FEC + i);
        d.subunit = ru.u16(0xE4 + i * 2);
        d.hasShields = ru.u8(0x9A8 + i) != 0;
        d.shields = ru.u16(0xA8C + i * 2);
        d.hp = ru.u32(0xC54 + i * 4);
        d.groundWeapon = ru.u8(0x1704 + i);
        d.airWeapon = ru.u8(0x18CC + i);
        d.flags = uint32_t(ru.u32(0x1B78 + i * 4));
        d.acquire = ru.u8(0x1F08 + i);
        d.armorUpgrade = ru.u8(0x20D0 + i);
        d.size = ru.u8(0x21B4 + i);
        d.armor = ru.u8(0x2298 + i);
        d.placeW = ru.u16(0x2C14 + i * 4);
        d.placeH = ru.u16(0x2C16 + i * 4);
        d.left = ru.u16(0x3124 + i * 8);
        d.up = ru.u16(0x3126 + i * 8);
        d.right = ru.u16(0x3128 + i * 8);
        d.down = ru.u16(0x312A + i * 8);
        d.minerals = ru.u16(0x3A0C + i * 2);
        d.gas = ru.u16(0x3BD4 + i * 2);
        d.supply = ru.u8(0x42F4 + i);
        d.group = ru.u8(0x412C + i);
        d.idleOrder = ru.u8(0x1290 + i);
        d.attackOrder = ru.u8(0x153C + i);
    }
    weapons.resize(130);
    for (int i = 0; i < 130; ++i) {
        WeaponData& d = weapons[i];
        d.flingy = rw.u32(0x104 + i * 4);
        d.targetFlags = rw.u16(0x38E + i * 2);
        d.minRange = rw.u32(0x492 + i * 4);
        d.maxRange = rw.u32(0x69A + i * 4);
        d.damageType = rw.u8(0x924 + i);
        d.behaviour = rw.u8(0x9A6 + i);
        d.removeAfter = rw.u8(0xA28 + i);
        d.explosion = rw.u8(0xAAA + i);
        d.inner = rw.u16(0xB2C + i * 2);
        d.medium = rw.u16(0xC30 + i * 2);
        d.outer = rw.u16(0xD34 + i * 2);
        d.upgrade = rw.u8(0x8A2 + i);
        d.damage = rw.u16(0xE38 + i * 2);
        d.bonus = rw.u16(0xF3C + i * 2);
        d.cooldown = rw.u8(0x1040 + i);
        d.factor = rw.u8(0x10C2 + i);
        d.angle = rw.u8(0x1144 + i);
        d.spin = rw.u8(0x11C6 + i);
        d.forward = rw.u8(0x1248 + i);
        d.upward = rw.u8(0x12CA + i);
    }
    flingy.resize(209);
    for (int i = 0; i < 209; ++i) {
        FlingyData& d = flingy[i];
        d.sprite = rf.u16(i * 2);
        d.topSpeed = rf.u32(0x1A2 + i * 4);
        d.accel = rf.u16(0x4E6 + i * 2);
        d.halt = rf.u32(0x688 + i * 4);
        d.turn = rf.u8(0x9CC + i);
        d.control = rf.u8(0xB6E + i);
    }
    spriteImage.resize(517);
    spriteVisible.resize(517);
    // (sprites.dat: the image of each sprite; then, after the health bars and an unknown byte, whether the game draws
    // it at all: an invisible sprite, such as some weapons' projectiles, is never drawn)
    for (int i = 0; i < 517; ++i) {
        spriteImage[i] = rs.u16(i * 2);
        spriteVisible[i] = s.size() >= 2455 ? rs.u8(1938 + i) != 0 : true;
    }
    imageTurns.resize(999);
    imageDraw.resize(999);
    imageIscript.resize(999);
    for (int i = 0; i < 999; ++i) {
        imageTurns[i] = ri.u8(3996 + i);
        imageDraw[i] = ri.u8(0x1F38 + i);
        imageIscript[i] = ri.u32(9990 + i * 4);
    }
    // The carry offsets: the workers' main images' special overlay lists. Not there: none.
    std::vector<uint8_t> tbl, lo;
    if (reader("arr/images.tbl", tbl) && tbl.size() >= 2) {
        Reader rt{tbl};
        for (int worker = 0; worker < int(units.size()); ++worker) {  // (the workers: SCV, Drone, Probe)
            if (!(units[size_t(worker)].flags & kWorker)) continue;
            int image = flingyImage(units[size_t(worker)].flingy);
            int entry = image >= 0 ? ri.u32(0x6576 + size_t(image) * 4) : 0;  // (Special Overlay: an images.tbl entry)
            if (entry <= 0 || entry > rt.u16(0)) continue;
            std::string name = "unit/";
            for (size_t o = size_t(rt.u16(size_t(entry) * 2)); o < tbl.size() && tbl[o]; ++o)
                name += tbl[o] == '\\' ? '/' : char(tbl[o]);
            if (!reader(name, lo) || lo.size() < 8) continue;
            Reader rl{lo};
            std::vector<std::pair<int, int>> frames;
            for (int frame = 0; frame < std::min(rl.u32(0), int(lo.size() - 8) / 4); ++frame) {
                size_t at = size_t(rl.u32(8 + size_t(frame) * 4));
                frames.push_back({int8_t(rl.u8(at)), int8_t(rl.u8(at + 1))});
            }
            carryOffsets.push_back({image, frames});
        }
    }
    Reader rsc{iscript};
    for (size_t o = size_t(rsc.u16(0)); o + 4 <= iscript.size(); o += 4) {
        int id = rsc.u16(o);
        if (id == 0xFFFF) break;
        scripts.push_back({id, rsc.u16(o + 2)});
    }
    deriveRoles(*this);
    return true;
}

bool Tables::firesFromScript(int header, int anim) const {
    int pc = animOffset(header, anim);
    for (int n = 0; pc > 0 && pc + 2 < int(iscript.size()) && n < 200; ++n) {
        int op = iscript[size_t(pc)], len = opLength(pc);
        if (op == 0x1C || op == 0x25 || op == 0x26 || op == 0x28 || op == 0x31) return true;
        if (len < 0 || op == 0x16 || op == 0x30) return false;
        // (goto and call: followed)
        pc = op == 0x07 || op == 0x35 ? (iscript[size_t(pc) + 1] | iscript[size_t(pc) + 2] << 8) : pc + len;
    }
    return pc == 0;  // (no animation: not this rule)
}

bool Tables::flingyVisible(int flingyId) const {
    if (flingyId < 0 || flingyId >= int(flingy.size())) return false;
    int sprite = flingy[flingyId].sprite;
    return sprite < int(spriteVisible.size()) && spriteVisible[size_t(sprite)];
}

int Tables::flingyImage(int flingyId) const {
    if (flingyId < 0 || flingyId >= int(flingy.size())) return -1;
    int sprite = flingy[flingyId].sprite;
    return sprite < int(spriteImage.size()) ? spriteImage[sprite] : -1;
}

int Tables::scriptHeader(int image) const {
    if (image < 0 || image >= int(imageIscript.size())) return -1;
    for (auto& [id, offset] : scripts)
        if (id == imageIscript[image]) return offset;
    return -1;
}

int Tables::animOffset(int header, int anim) const {
    if (header < 0 || header + 8 > int(iscript.size())) return 0;
    if (anim >= animCount(iscript[header + 4])) return 0;
    Reader r{iscript};
    return r.u16(header + 8 + anim * 2);
}

int Tables::opLength(int pc) const {
    // The argument bytes of each opcode (iscript.bin's public layout); -1: a variable list (a count, then u16s).
    static const int8_t args[0x45] = {
        2, 2, 1, 1, 2, 1, 2, 2, 4, 4, 2, 2, 0, 4, 4, 4, 4, 4, 2, 4, 4, 3, 0, 1, 2, -1, 4, 0, -1, 0, 3, 1, 1, 0, 1,
        1, 1, 1, 0, 0, 1, 1, 0, 1, 1, 0, 0, 0, 0, 1, 0, 0, 1, 2, 0, 2, 1, 2, 4, 6, 6, 2, 0, 2, 2, 1, 4, 0, 0};
    if (pc < 0 || pc >= int(iscript.size()) || iscript[pc] >= 0x45) return -1;
    int a = args[iscript[pc]];
    if (a < 0) a = pc + 1 < int(iscript.size()) ? 1 + iscript[pc + 1] * 2 : 0;
    return 1 + a;
}

bool MapInfo::load(const std::vector<uint8_t>& bytes, const Tables& t, std::string& error) {
    stardraft::Chk chk = stardraft::Chk::parse(bytes);
    stardraft::Bytes dim = chk.effective("DIM "), era = chk.effective("ERA "), mtxm = chk.effective("MTXM"),
                     unit = chk.effective("UNIT");
    if (dim.size() < 4) {
        error = "The map has no size (DIM)";
        return false;
    }
    Reader rd{dim};
    width = rd.u16(0);
    height = rd.u16(2);
    tileset = era.size() >= 2 ? (era[0] & 7) : 0;
    static const char* names[8] = {"badlands", "platform", "install", "ashworld",
                                   "jungle",   "desert",   "ice",     "twilight"};
    std::vector<uint8_t> cv5, vf4;
    std::string base = std::string("tileset/") + names[tileset];
    if (!readFile(t.read, base + ".cv5", 52, cv5, error) || !readFile(t.read, base + ".vf4", 32, vf4, error))
        return false;
    Reader rc{cv5}, rv{vf4}, rm{mtxm};
    walk.assign(size_t(width) * 4 * height * 4, 0);
    std::vector<int> cls(size_t(width) * height, 0);  // (the tile's class from its 16 minitiles)
    for (int ty = 0; ty < height; ++ty)
        for (int tx = 0; tx < width; ++tx) {
            int v = rm.u16(size_t(ty * width + tx) * 2);
            int mega = rc.u16(size_t(v >> 4) * 52 + 20 + (v & 15) * 2);
            int walkable = 0, middle = 0, high = 0;
            for (int my = 0; my < 4; ++my)
                for (int mx = 0; mx < 4; ++mx) {
                    int f = rv.u16(size_t(mega) * 32 + (my * 4 + mx) * 2);
                    walk[size_t(ty * 4 + my) * width * 4 + tx * 4 + mx] = f & 1;
                    walkable += f & 1;
                    middle += (f >> 1) & 1;
                    high += (f >> 2) & 1;
                }
            cls[size_t(ty * width + tx)] = walkable <= 12 ? 0 : high >= 12 ? 3 : middle + high >= 12 ? 2 : 1;
        }
    // Regions: from the top left, each unassigned tile starts a rectangle of its class, up to 8 tiles wide and 7 tall
    // (3:1 at most); then the walkable neighbours (orthogonal) and the groups (a flood fill).
    tileRegion.assign(size_t(width) * height, -1);
    regions.clear();
    auto free = [&](int x, int y, int c) {
        return x < width && y < height && tileRegion[size_t(y * width + x)] < 0 && cls[size_t(y * width + x)] == c;
    };
    for (int ty = 0; ty < height; ++ty)
        for (int tx = 0; tx < width; ++tx) {
            int c = cls[size_t(ty * width + tx)];
            if (tileRegion[size_t(ty * width + tx)] >= 0) continue;
            int w = 1, h = 1;
            while (w < 8 && free(tx + w, ty, c)) ++w;
            for (bool more = true; more && h < 7; h += more ? 1 : 0)
                for (int x = tx; x < tx + w && more; ++x) more = free(x, ty + h, c);
            w = std::min(w, 3 * h);
            h = std::min(h, 3 * w);
            for (int y = ty; y < ty + h; ++y)
                for (int x = tx; x < tx + w; ++x) tileRegion[size_t(y * width + x)] = int(regions.size());
            Region g;
            g.x = (tx + w / 2) * 32 + 16;
            g.y = (ty + h / 2) * 32 + 16;
            g.cls = c;
            regions.push_back(g);
        }
    for (int ty = 0; ty < height; ++ty)
        for (int tx = 0; tx < width; ++tx) {
            int a = tileRegion[size_t(ty * width + tx)];
            for (int k = 0; k < 2; ++k) {
                int x = tx + (k == 0), y = ty + (k == 1);
                if (x >= width || y >= height) continue;
                int b = tileRegion[size_t(y * width + x)];
                if (a == b || !regions[size_t(a)].cls || !regions[size_t(b)].cls) continue;
                auto& na = regions[size_t(a)].neighbours;
                if (std::find(na.begin(), na.end(), b) != na.end()) continue;
                na.push_back(b);
                regions[size_t(b)].neighbours.push_back(a);
            }
        }
    for (size_t i = 0, group = 0; i < regions.size(); ++i) {
        if (regions[i].group || !regions[i].cls) continue;
        std::vector<int> stack = {int(i)};
        regions[i].group = int(++group);
        while (!stack.empty()) {
            int g = stack.back();
            stack.pop_back();
            for (int n : regions[size_t(g)].neighbours)
                if (!regions[size_t(n)].group) {
                    regions[size_t(n)].group = int(group);
                    stack.push_back(n);
                }
        }
    }
    // Each player's start levels and researched techs (public CHK layouts): per player maximum (or available), per
    // player start level (or researched), the defaults, then per player whether the defaults apply. The Brood War
    // sections (PUPx, PTEx) come first; maps without them have the original ones (UPGR, PTEC) with fewer entries.
    auto starts = [&](const stardraft::Bytes& sec, int n, int stride, std::vector<uint8_t>& out) {
        out.assign(size_t(12 * stride), 0);
        if (sec.size() < size_t(12 * n * 3 + 2 * n)) return false;
        for (int p = 0; p < 12; ++p)
            for (int k = 0; k < n; ++k) {
                bool useDefault = sec[size_t(24 * n + 2 * n + p * n + k)] != 0;
                out[size_t(p * stride + k)] =
                    useDefault ? sec[size_t(24 * n + n + k)] : sec[size_t(12 * n + p * n + k)];
            }
        return true;
    };
    if (!starts(chk.effective("PUPx"), 61, 61, upgrades)) starts(chk.effective("UPGR"), 46, 61, upgrades);
    if (!starts(chk.effective("PTEx"), 44, 44, techs)) starts(chk.effective("PTEC"), 24, 44, techs);
    stardraft::Bytes side = chk.effective("SIDE");
    sides.assign(side.begin(), side.end());
    units.clear();
    Reader ru{unit};
    for (size_t o = 0; o + 36 <= unit.size(); o += 36) {
        Placed p;
        p.x = ru.u16(o + 4);
        p.y = ru.u16(o + 6);
        p.type = ru.u16(o + 8);
        p.owner = ru.u8(o + 16);
        p.resources = ru.u32(o + 20);
        if (p.type < 228) units.push_back(p);
    }
    return true;
}

bool MapInfo::walkable(int px, int py) const {
    if (px < 0 || py < 0 || px >= width * 32 || py >= height * 32) return false;
    return walk[size_t(py / 8) * width * 4 + px / 8] != 0;
}

bool MapInfo::boxWalkable(int x0, int y0, int x1, int y1) const {
    for (int y = y0 & ~7; y <= y1; y += 8)
        for (int x = x0 & ~7; x <= x1; x += 8)
            if (!walkable(x, y)) return false;
    return true;
}

}  // namespace sim
