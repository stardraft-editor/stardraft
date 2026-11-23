// View > Overlays > Psi power (SCMDraft shows it too): each building that needs power (units.dat: Requires Psi) gets a
// dashed ellipse, light blue (the color of a Pylon's energy) when a Pylon of the same player powers it, red when none does.
// The game's rule: a building has power when its center is in the psi field of one of its owner's Pylons: 16 x 10
// tiles round the Pylon's center, with the corners cut (the mask that the Psi field range also draws, map_scene.inc).

bool gPsiPower = false;
bool cmbwMode();  // (core/cmbw.inc: in Mod support mode other units give power too: no marks there)

namespace psipower {
constexpr uint32_t kRequiresPsi = 0x80000;  // (units.dat special ability flags)
constexpr uint16_t kPylon = 156;            // (the game gives power from Protoss Pylons only)
constexpr const char* kMask[10] = {"0000011111100000", "0011111111111100", "0111111111111110", "1111111111111111", "1111111111111111",
                                   "1111111111111111", "1111111111111111", "0111111111111110", "0011111111111100", "0000011111100000"};

// Whether the point (x, y) is in the psi field of a Pylon centered at (px, py).
inline bool inField(int x, int y, int px, int py) {
    const int dx = x - px + 8 * 32, dy = y - py + 5 * 32;
    return dx >= 0 && dy >= 0 && dx < 16 * 32 && dy < 10 * 32 && kMask[dy / 32][dx / 32] == '1';
}

struct Cache {
    const Editor* editor = nullptr;
    uint64_t revision = ~0ull;
    struct Mark {
        float x0, y0, x1, y1;  // (the ellipse's box, map pixels)
        bool powered;
    };
    std::vector<Mark> marks;
} gCache;
}  // namespace psipower

void addPsiPower(render::Scene& s) {
    using namespace psipower;
    const AppState& app = appState();
    if (!gPsiPower || !app.ed || !app.ed->isOpen() || cmbwMode()) return;
    const Editor& ed = *app.ed;
    Cache& c = gCache;
    if (c.editor != &ed || c.revision != uint64_t(ed.revision())) {
        c.editor = &ed, c.revision = uint64_t(ed.revision());
        c.marks.clear();
        std::vector<const Unit*> pylons;
        for (const Unit& u : ed.units())
            if (u.type == kPylon) pylons.push_back(&u);
        for (const Unit& u : ed.units()) {
            if (u.type >= ed.unitTypes().size()) continue;
            const UnitType& t = ed.unitTypes()[u.type];
            if (!t.is(UnitFlag::Building) || !(t.flags & kRequiresPsi)) continue;
            bool powered = false;
            for (const Unit* p : pylons)
                if (p->owner == u.owner && inField(u.x, u.y, p->x, p->y)) {
                    powered = true;
                    break;
                }
            // (a little wider than the building's base, flat as the map's view)
            const float rx = std::max(16.0f, t.placementWidth * 0.5f + 8), ry = std::max(10.0f, t.placementHeight * 0.5f + 2);
            c.marks.push_back({u.x - rx, u.y - ry, u.x + rx, u.y + ry, powered});
        }
    }
    int vw, vh;
    viewSize(vw, vh);
    const float vx0 = app.camX, vy0 = app.camY, vx1 = app.camX + vw / app.zoom, vy1 = app.camY + vh / app.zoom;
    for (const Cache::Mark& m : c.marks) {
        if (m.x1 < vx0 || m.x0 > vx1 || m.y1 < vy0 || m.y0 > vy1) continue;
        const float cx = (m.x0 + m.x1) / 2, cy = (m.y0 + m.y1) / 2, rx = (m.x1 - m.x0) / 2, ry = (m.y1 - m.y0) / 2;
        // (dashes of about 6 screen pixels: a multiple of 2 segments round the ellipse)
        const int steps = std::clamp(int(6.2832f * std::max(rx, ry) * app.zoom / 6) / 2 * 2, 16, 160);
        const uint32_t color = m.powered ? 0x60D0FFF0u : 0xF04040E0u;
        for (int k = 0; k < steps; k += 2) {
            const float a0 = 6.2832f * k / steps, a1 = 6.2832f * (k + 1) / steps;
            render::RectDraw l;
            l.x0 = cx + rx * std::cos(a0), l.y0 = cy + ry * std::sin(a0), l.x1 = cx + rx * std::cos(a1), l.y1 = cy + ry * std::sin(a1);
            l.rgba = color, l.thickness = 1.5f, l.line = true;
            s.rects.push_back(l);
        }
    }
}

void psiPowerToggle() {
    gPsiPower = !gPsiPower;
    shellMenusChanged();
    appState().dirty = true;
    setMessage(std::wstring(tr(!gPsiPower   ? L"Psi power is hidden"
                               : cmbwMode() ? L"Psi power shows the game's rule only: in Mod support mode other units give power too, so it shows nothing"
                                            : L"Psi power: light blue buildings have power from a Pylon of their owner, red buildings have no power")));
}
