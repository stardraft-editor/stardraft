// A Cosmonarchy unit's graphics as the editor reads them: its image, iscript ID, facing set, the Init script's frames
// and overlays, the code of its Init and Walking animations, and a picture of its frame in each of the 32 directions
// (the editor's defaultFrame) with its subunit.
// usage: cmbw_unit_script <Cosmonarchy install> <map> <unit name part> <out .png>
//        cmbw_unit_script <Cosmonarchy install> <map> --all   (each unit's image and subunit: no script, a frame that
//        its Init plays or a facing needs past the GRP's frames)
#include <algorithm>
#include <map>
#include <set>
#include <cstdio>
#include <string>
#include <vector>

#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/sprites.h"
#include "stardraft/terrain.h"

using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 4 || (argc < 5 && std::string(argv[3]) != "--all" && std::string(argv[3]) != "--ops" && std::string(argv[3]) != "--air" && std::string(argv[3]) != "--dirs")) return 2;
    std::string err;
    GameData data;
    if (!cmbw::open(data, argv[1], err)) return 1;
    Editor ed(data);
    if (!ed.open(argv[2], err)) return 1;
    UnitSprites& sp = ed.unitSprites();
    const Tileset& ts = ed.tileset();
    auto lower = [](std::string s) {
        for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    if (std::string(argv[3]) == "--dirs") {  // how many unit types units.dat gives a random direction, and a fixed one
        int random = 0, fixed = 0;
        for (size_t i = 0; i < ed.unitTypes().size(); ++i)
            if (!ed.unitTypes()[uint16_t(i)].name.empty()) (sp.randomDirection(uint16_t(i)) ? random : fixed)++;
        std::printf("random %d, fixed %d\n", random, fixed);
        return 0;
    }
    if (std::string(argv[3]) == "--air") {  // each air unit: whether its idle (ImageAnim, 30 s) moves its image up or down
        for (size_t i = 0; i < ed.unitTypes().size(); ++i) {
            const uint16_t u = uint16_t(i);
            const UnitType& t = ed.unitTypes()[u];
            if (t.name.empty() || !t.is(UnitFlag::Flyer) || t.is(UnitFlag::Building)) continue;
            const int img = sp.imageFor(u);
            ImageAnim a(sp, img, 7);
            a.play(sp, img, 12);  // (as the map view: map_scene.inc unitAnim)
            std::set<int> ys;
            int ticks = 0;
            for (; ticks < 720 && a.running(); ++ticks, a.tick()) ys.insert(a.y());
            std::string where;  // (the animations with a setvertpos, and the overlays' images that have one)
            for (int anim = 0; anim < 28; ++anim)
                if (const size_t at = sp.animOffset(img, anim))
                    for (const IscriptLine& l : disassembleIscript(sp.scriptCode(img), at, 200))
                        if (l.op == "setvertpos" && where.find(" " + std::to_string(anim) + ",") == std::string::npos) where += " " + std::to_string(anim) + ",";
            std::printf("%3zu %-32s image %4d iscript %6d: %s%s; setvertpos in anims%s\n", i, t.name.c_str(), img, sp.scriptId(img),
                        ys.size() > 1 ? "bobs" : "still", a.running() ? "" : (" (stops at tick " + std::to_string(ticks) + ")").c_str(), where.c_str());
        }
        return 0;
    }
    if (std::string(argv[3]) == "--ops") {  // the commands in the units' Init scripts: how many units use each
        std::map<std::string, std::set<std::string>> users;
        for (size_t i = 0; i < ed.unitTypes().size(); ++i) {
            const uint16_t u = uint16_t(i);
            if (ed.unitTypes()[u].name.empty()) continue;
            for (int img : {sp.imageFor(u), sp.subunitImage(u)})
                if (img >= 0 && sp.animOffset(img, 0))
                    for (const IscriptLine& l : disassembleIscript(sp.scriptCode(img), sp.animOffset(img, 0), 200))
                        users[l.op].insert(ed.unitTypes()[u].name);
        }
        for (const auto& [op, names] : users) std::printf("%-20s %4zu  %s\n", op.c_str(), names.size(), names.begin()->c_str());
        return 0;
    }
    if (std::string(argv[3]) == "--all") {
        int units = 0, bad = 0;
        for (size_t i = 0; i < ed.unitTypes().size(); ++i) {
            const uint16_t u = uint16_t(i);
            if (ed.unitTypes()[u].name.empty()) continue;
            ++units;
            std::string why;
            for (int img : {sp.imageFor(u), sp.subunitImage(u)}) {
                if (img < 0) continue;
                const Grp* g = sp.grp(img);
                const int frames = g ? int(g->frames.size()) : 0;
                if (!frames) { why += " image " + std::to_string(img) + ": no GRP;"; continue; }
                if (!sp.animOffset(img, 0)) { why += " image " + std::to_string(img) + ": no Init (iscript " + std::to_string(sp.scriptId(img)) + ");"; continue; }
                for (const IscriptLine& l : disassembleIscript(sp.scriptCode(img), sp.animOffset(img, 0), 60))
                    if (l.op == "playfram" && !l.argv.empty() && l.argv[0].value >= frames)
                        why += " image " + std::to_string(img) + ": Init plays frame " + std::to_string(l.argv[0].value) + " of " + std::to_string(frames) + ";";
                for (int dir = 0; dir < 32; ++dir) {
                    bool flip = false;
                    const int f = sp.defaultFrame(u, img, flip, dir);
                    if (f >= frames) { why += " image " + std::to_string(img) + ": facing " + std::to_string(dir) + " is frame " + std::to_string(f) + " of " + std::to_string(frames) + ";"; break; }
                }
            }
            if (!why.empty()) ++bad, std::printf("%3zu %-32s%s\n", i, ed.unitTypes()[u].name.c_str(), why.c_str());
        }
        std::printf("%d of %d units with a problem\n", bad, units);
        return 0;
    }
    const std::string want = lower(argv[3]);
    int type = -1;
    for (size_t i = 0; i < ed.unitTypes().size() && type < 0; ++i)
        if (lower(ed.unitTypes()[uint16_t(i)].name).find(want) != std::string::npos) type = int(i);
    if (type < 0) return std::printf("no unit named %s\n", argv[3]), 1;
    const uint16_t u = uint16_t(type);
    for (const Unit& m : ed.units())
        if (m.type == u) std::printf("on the map at %d,%d (player %d)\n", m.x, m.y, m.owner + 1);
    const int image = sp.imageFor(u), sub = sp.subunitImage(u);
    std::printf("unit %d %s: image %d (iscript %d, facing set %d, turns %d), subunit image %d (iscript %d), random facing %d\n", type,
                ed.unitTypes()[u].name.c_str(), image, sp.scriptId(image), sp.facingSet(image), 0, sub, sp.scriptId(sub), sp.randomDirection(u));
    std::vector<int> shown{image, sub};  // (and the images its Init puts over and under it)
    if (image >= 0)
        for (const auto* list : {&sp.script(image).over, &sp.script(image).under})
            for (const auto& o : *list) shown.push_back(o.image);
    for (int img : shown) {
        if (img < 0) continue;
        const ImageScript& s = sp.script(img);
        std::printf("image %d: init frame %d, start frame %d, %zu under, %zu over\n", img, s.initFrame, s.startFrame, s.under.size(), s.over.size());
        for (const auto& o : s.under) std::printf("  under image %d at %d,%d\n", o.image, o.x, o.y);
        for (const auto& o : s.over) std::printf("  over image %d at %d,%d\n", o.image, o.x, o.y);
        for (int anim : {0, 1, 11, 12}) {  // Init, Death, Walking
            const size_t at = sp.animOffset(img, anim);
            if (!at) continue;
            std::printf("  anim %d at 0x%zx:\n", anim, at);
            for (const IscriptLine& l : disassembleIscript(sp.scriptCode(img), at, 40))
                std::printf("    %s%s %s\n", l.label ? "L: " : "", l.op.c_str(), l.args.c_str());
        }
        const Grp* g = sp.grp(img);
        std::printf("  grp: %d frames, box %dx%d\n", g ? int(g->frames.size()) : -1, g ? g->width : 0, g ? g->height : 0);
        // The map view's idle (ImageAnim) over 30 s: each change of the frame shown, the image's place and direction.
        ImageAnim a(sp, img, 7);
        std::string last;
        for (int tick = 0; tick < 720 && a.running(); ++tick, a.tick()) {
            bool flip = false;
            const int dir = a.direction(sp.facingDirection(u, -1));
            const int f = sp.animatedFrame(u, img, a.frame(), flip, -1, dir);
            const std::string now = "frame " + std::to_string(f) + (flip ? " flipped" : "") + ", at " + std::to_string(a.x()) + "," +
                                    std::to_string(a.y()) + ", direction " + std::to_string(dir);
            if (now != last) std::printf("  tick %3d: %s\n", tick, now.c_str()), last = now;
        }
    }
    // The 32 directions: each cell the image's frame (and the subunit's at its special offset), its frame number under.
    const Grp* g = sp.grp(image);
    if (!g) return 1;
    const int cw = std::max<int>(g->width, 64) + 8, ch = std::max<int>(g->height, 64) + 8;
    Image out{8 * cw, 4 * ch, {}};
    out.rgba.assign(size_t(out.width) * out.height * 4, 48);
    for (size_t i = 3; i < out.rgba.size(); i += 4) out.rgba[i] = 255;
    auto blit = [&](int img, int frame, bool flip, int cx, int cy) {
        const Grp* gg = sp.grp(img);
        if (!gg || frame < 0 || frame >= int(gg->frames.size())) return;
        const Grp::Frame& f = gg->frames[size_t(frame)];
        for (int y = 0; y < f.height; ++y)
            for (int x = 0; x < f.width; ++x) {
                const uint8_t idx = f.pixels[size_t(y) * f.width + x];
                if (!idx) continue;
                const int fx = flip ? gg->width - 1 - (f.x + x) : f.x + x;
                const int X = cx - gg->width / 2 + fx, Y = cy - gg->height / 2 + f.y + y;
                if (X < 0 || Y < 0 || X >= out.width || Y >= out.height) continue;
                const Rgb& c = ts.palette[sp.remap(idx, 0)];
                uint8_t* d = &out.rgba[(size_t(Y) * out.width + X) * 4];
                d[0] = c.r, d[1] = c.g, d[2] = c.b;
            }
    };
    for (int dir = 0; dir < 32; ++dir) {
        const int cx = (dir % 8) * cw + cw / 2, cy = (dir / 8) * ch + ch / 2;
        bool flip = false;
        const int frame = sp.defaultFrame(u, image, flip, dir);
        blit(image, frame, flip, cx, cy);
        if (sub >= 0) {
            bool sflip = false;
            const int sframe = sp.defaultFrame(u, sub, sflip, dir);
            auto [ox, oy] = sp.specialOffset(image, frame);
            blit(sub, sframe, sflip, cx + (flip ? -ox : ox), cy + oy);
        }
        std::printf("dir %2d: frame %d%s\n", dir, frame, flip ? " flipped" : "");
    }
    return writePng(argv[4], out) ? 0 : 1;
}
