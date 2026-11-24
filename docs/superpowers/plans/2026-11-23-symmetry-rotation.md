# Symmetry Rotation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add Isometric/Tile rotation, Stable/Precise rotation, a per-tool Rotate brush with a hole-free reverse
method, and two 2:1 mirrors to the editor's symmetry.

**Architecture:** The library (`Editor`, `src/editor.cpp`) gets the two rotation options, a 2 x 2 matrix on each
symmetric place, the 2:1 mirrors, and `symmetryCells` (the reverse method). The app (`apps/editor/core/*.inc`) keeps
Rotate brush per tool in `AppState`, saves the options in the panel state, and shows three buttons in the tool strip.

**Tech Stack:** C++20, CMake, Visual Studio 2022 (Windows), the editor's own UI kit (`draw::Canvas`).

**Spec:** `docs/superpowers/specs/2026-11-23-symmetry-rotation-design.md`

## Global Constraints

- Defaults: Tile rotation, Precise rotation, Rotate brush on. With the defaults and no new mirror, every existing
  symmetry test result stays the same.
- Stable/Precise changes only turns (`turns() > 1`). The mirrors alone behave as before.
- UI text follows `docs/dev/PRINCIPLES.md` ("Text"); `python -I tools/ui_text_lint.py` must exit with 0.
- Do not commit `art/` or `docs/superpowers/specs/2026-11-01-bridge-ramp-tool/`.
- Commit messages have no co-author line.

## Build and test commands

- Build: `cmake --build build --config Release --target editor_test stardraft_editor`
- Tests: `build\Release\editor_test.exe <install> "<install>\Maps\BroodWar\(2)Binary Burghs.scx" <empty dir> mirror`
  (`<install>`: the folder with `.build.info`). The `mirror` argument runs only the symmetry tests.
- UI text: `python -I tools/ui_text_lint.py`

---

### Task 1: Rotation options, matrices and 2:1 mirrors in the library

**Files:**
- Modify: `include/stardraft/editor.h` (namespace `Mirror`; the public symmetry section; the private `SymmetricPlace`)
- Modify: `src/editor.cpp` (`mirror`, `turnPoint`, `symmetricPlaces`, `symmetricPoints`, `terrainSymmetricPoints`,
  `fillTerrain`, `pasteTerrain`)
- Test: `tools/tests/editor_test.cpp` (new `symmetryRotationTest`, called after `turnCountTest` in both places)

**Interfaces:**
- Produces: `Mirror::IsoDiagonal = 16`, `Mirror::IsoAntiDiagonal = 32`, `Mirror::All = 63`;
  `void Editor::setIsoTurns(bool)`, `bool Editor::isoTurns() const`, `void Editor::setStableTurns(bool)`,
  `bool Editor::stableTurns() const`; `enum class Editor::SymmetryCell { None, Isom, Tile, TilePair }`;
  `static std::pair<int, int> Editor::cellMiddle(int px, int py, SymmetryCell cell)`;
  `std::vector<std::pair<int, int>> Editor::symmetricPoints(int px, int py, SymmetryCell cell = SymmetryCell::None) const`.

- [ ] **Step 1: Write the failing test** (in `editor_test.cpp`, after `turnCountTest`)

```cpp
// Isometric / Tile rotation, Stable / Precise rotation and the 2:1 mirrors
// (docs/superpowers/specs/2026-11-23-symmetry-rotation-design.md).
static void symmetryRotationTest(const GameData& data) {
    Editor ed(data);
    std::string err;
    if (!ed.newMap(64, 64, 0, 1, err)) {
        CHECK(false, "rotation: a new 64 x 64 map");
        return;
    }
    const int cx = 64 * 16, cy = 64 * 16;  // the middle: a point of the diamonds' grid
    auto has = [](const std::vector<std::pair<int, int>>& v, int x, int y) { return std::find(v.begin(), v.end(), std::pair{x, y}) != v.end(); };
    auto onLattice = [](int x, int y) { return x % 64 == 0 && y % 32 == 0 && ((x / 64 + y / 32) & 1) == 1; };
    CHECK(!ed.isoTurns() && !ed.stableTurns(), "rotation: Tile rotation and Precise rotation at first");
    // Isometric rotation: a quarter turn moves (64 i, 32 j) to (-64 j, 32 i); every copy of a corner is a corner.
    ed.setTurns(4);
    ed.setIsoTurns(true);
    const int px = cx + 64 * 3, py = cy + 32 * 6;  // (i + j odd from the middle: a corner)
    const auto iso = ed.symmetricPoints(px, py);
    bool corners = iso.size() == 4;
    for (auto [x, y] : iso) corners = corners && onLattice(x, y);
    CHECK(corners && onLattice(px, py), "rotation: Isometric quarter turns keep a corner on the corners");
    CHECK(has(iso, cx - 64 * 6, cy + 32 * 3) && has(iso, cx - 64 * 3, cy - 32 * 6) && has(iso, cx + 64 * 6, cy - 32 * 3),
          "rotation: Isometric quarter turns give (-64 j, 32 i), (-64 i, -32 j) and (64 j, -32 i)");
    ed.setIsoTurns(false);
    CHECK(has(ed.symmetricPoints(px, py), cx - 32 * 6, cy + 64 * 3), "rotation: Tile rotation is the turn in pixels on a square map");
    // Stable: two points in one tile give the same copies; Precise: their copies differ.
    ed.setTurns(3);
    ed.setStableTurns(true);
    auto a = ed.symmetricPoints(cx + 300 + 1, cy + 3, Editor::SymmetryCell::Tile), b = ed.symmetricPoints(cx + 300 + 30, cy + 28, Editor::SymmetryCell::Tile);
    CHECK(a.size() == 3 && b.size() == 3 && std::equal(a.begin() + 1, a.end(), b.begin() + 1), "rotation: Stable turns the middle of the tile");
    CHECK(a[0] == std::pair{cx + 301, cy + 3}, "rotation: Stable keeps the point itself first");
    ed.setStableTurns(false);
    a = ed.symmetricPoints(cx + 300 + 1, cy + 3, Editor::SymmetryCell::Tile), b = ed.symmetricPoints(cx + 300 + 30, cy + 28, Editor::SymmetryCell::Tile);
    CHECK(a.size() == 3 && b.size() == 3 && a[1] != b[1], "rotation: Precise turns the point as it is");
    CHECK(Editor::cellMiddle(cx + 10, cy + 40, Editor::SymmetryCell::Isom) == std::pair{cx, cy + 32} &&
              Editor::cellMiddle(33, 70, Editor::SymmetryCell::Tile) == std::pair{48, 80} &&
              Editor::cellMiddle(65, 70, Editor::SymmetryCell::TilePair) == std::pair{96, 80},
          "rotation: the middles of a diamond, a tile and a pair of tiles");
    // The 2:1 mirrors: (dx, dy) -> (2 dy, dx / 2) and (-2 dy, -dx / 2); a corner goes to a corner.
    ed.setTurns(1);
    ed.setMirrors(Mirror::IsoDiagonal);
    const auto m1 = ed.symmetricPoints(cx + 64 * 3, cy + 32 * 2);
    CHECK(m1.size() == 2 && has(m1, cx + 64 * 2, cy + 32 * 3), "rotation: the 2:1 mirror down to the right");
    ed.setMirrors(Mirror::IsoAntiDiagonal);
    const auto m2 = ed.symmetricPoints(cx + 64 * 3, cy + 32 * 2);
    CHECK(m2.size() == 2 && has(m2, cx - 64 * 2, cy - 32 * 3), "rotation: the 2:1 mirror up to the right");
    ed.setMirrors(Mirror::All);
    CHECK(ed.mirrors() == 63, "rotation: Mirror::All has the six mirrors");
    ed.setMirrors(0);
    // A painted map: StarEdit's diamonds with the 2:1 mirror are symmetric where the image is on the map.
    {
        Editor pe(data);
        pe.newMap(128, 128, 0, 1, err);
        const uint16_t water = terrainId(pe, "Water");
        pe.setMirrors(Mirror::IsoDiagonal);
        pe.beginTerrainStroke();
        for (auto [x, y] : {std::pair{1500, 1900}, {2600, 2200}, {1800, 2500}}) pe.paintBrush(x, y, water, Brush{Brush::Diamond, 3});
        pe.endTerrainStroke();
        const int U = 64, V = 128, ocx = 32, ocy = 64;  // the corner grid; the middle at (32, 64)
        int diff = 0, painted = 0;
        for (int y = 0; y <= V; ++y)
            for (int x = 0; x <= U; ++x) {
                if (!((x + y) & 1)) continue;
                const int mx = ocx + (y - ocy), my = ocy + (x - ocx);  // (p, q) -> (q, p) from the middle
                if (mx < 0 || mx > U || my < 0 || my > V) continue;
                const uint16_t t = pe.terrainAt(x * 64, y * 32);
                painted += t == water;
                diff += t != pe.terrainAt(mx * 64, my * 32);
            }
        std::printf("      2:1 mirror: %d corners differ from their image (%d painted)\n", diff, painted);
        CHECK(diff == 0 && painted > 0, "rotation: the 2:1 mirror paints symmetric corners");
    }
}
```

`terrainId` is the helper that `mirrorTest` uses to find Water. When `mirrorTest` uses another name, use that name.
Add `symmetryRotationTest(data);` after each `turnCountTest(data);` call (the `mirror` branch and the full run).

- [ ] **Step 2: Run the test and see it fail**

Run: `cmake --build build --config Release --target editor_test`
Expected: compile errors (`setIsoTurns`, `SymmetryCell`, `Mirror::IsoDiagonal` are not declared).

- [ ] **Step 3: Implement**

`include/stardraft/editor.h`:

```cpp
namespace Mirror {
// IsoDiagonal, IsoAntiDiagonal: across the 2:1 lines through the center (down to the right, up to the right), the
// lines of the diamonds' edges: a diamond's image is a diamond.
constexpr int LeftRight = 1, TopBottom = 2, Diagonal = 4, AntiDiagonal = 8, IsoDiagonal = 16, IsoAntiDiagonal = 32, All = 63;
}
```

In the public symmetry section, after `turns()`:

```cpp
    // How the turns go (the tool strip's Isometric / Tile rotation and Stable / Precise rotation;
    // docs/superpowers/specs/2026-11-23-symmetry-rotation-design.md). Isometric: the offset from the center turns with
    // its height counted twice, so a diamond (64 x 32) goes to a diamond; Tile: in the map's proportions (as before).
    void setIsoTurns(bool on) { isoTurns_ = on; }
    bool isoTurns() const { return isoTurns_; }
    // Stable: with turns, the point first goes to the middle of its cell (the caller's `cell`), then turns; Precise: the
    // point turns as it is. Each copy then uses the cell under it. The point itself stays first, as it is.
    void setStableTurns(bool on) { stableTurns_ = on; }
    bool stableTurns() const { return stableTurns_; }
    // The cell that Stable puts a point in the middle of: none, a diamond (the nearest corner: (64 x, 32 y), x + y odd), a
    // tile (32 x 32), a pair of tiles (64 x 32, as Rectangle's).
    enum class SymmetryCell { None, Isom, Tile, TilePair };
    static std::pair<int, int> cellMiddle(int px, int py, SymmetryCell cell);
```

Replace the `symmetricPoints` declaration:

```cpp
    // The point itself followed by its mirror images (duplicates removed); `cell`: Stable's cell (see setStableTurns).
    std::vector<std::pair<int, int>> symmetricPoints(int px, int py, SymmetryCell cell = SymmetryCell::None) const;
```

Private: after `turns_`:

```cpp
    bool isoTurns_ = false, stableTurns_ = false;  // setIsoTurns, setStableTurns
```

Private `SymmetricPlace` and `symmetricPlaces`, and two matrix helpers:

```cpp
    struct SymmetricPlace {
        int x, y;
        bool flipX, flipY;  // a mirror image across a vertical (horizontal) axis: a pasted copy flips that way
        double m[4] = {1, 0, 0, 1};  // the copy's turns and mirrors, for offsets in pixels: (dx, dy) -> (m0 dx + m1 dy, m2 dx + m3 dy)
    };
    // margin: how far past the map's edges a place may be (terrain: a brush or a copy there still reaches the map).
    // cell: Stable's cell (setStableTurns).
    std::vector<SymmetricPlace> symmetricPlaces(int px, int py, int margin = 0, SymmetryCell cell = SymmetryCell::None) const;
    void turnMatrix(int k, double m[4]) const;
    void mirrorMatrix(int which, double m[4]) const;
```

`src/editor.cpp`, `mirror`: add two cases before the closing brace of the switch:

```cpp
    case 5: return {int(std::lround(cx + 2.0 * (py - cy))), int(std::lround(cy + (px - cx) / 2.0))};  // 2:1, down to the right
    case 6: return {int(std::lround(cx - 2.0 * (py - cy))), int(std::lround(cy - (px - cx) / 2.0))};  // 2:1, up to the right
```

`turnPoint`: after `const auto [cx, cy] = center;`:

```cpp
    if (isoTurns_) {  // Isometric: the height counts twice (a diamond turns to a diamond)
        const double u = px - cx, v = 2.0 * (py - cy);
        return {int(std::lround(cx + u * c - v * s)), int(std::lround(cy + (u * s + v * c) / 2))};
    }
```

New functions after `turnPoint`:

```cpp
void Editor::turnMatrix(int k, double m[4]) const {
    constexpr double kTau = 6.283185307179586;
    const double a = kTau * k / turns_, c = std::cos(a), s = std::sin(a), r = double(width_) / height_;
    if (isoTurns_) m[0] = c, m[1] = -2 * s, m[2] = s / 2, m[3] = c;
    else m[0] = c, m[1] = -s * r, m[2] = s / r, m[3] = c;
}

void Editor::mirrorMatrix(int which, double m[4]) const {
    const double r = double(width_) / height_;
    static const double id[4] = {1, 0, 0, 1};
    std::copy(id, id + 4, m);
    switch (which) {
    case 0: m[0] = -1; break;
    case 1: m[3] = -1; break;
    case 3: m[0] = 0, m[1] = r, m[2] = 1 / r, m[3] = 0; break;
    case 4: m[0] = 0, m[1] = -r, m[2] = -1 / r, m[3] = 0; break;
    case 5: m[0] = 0, m[1] = 2, m[2] = 0.5, m[3] = 0; break;
    case 6: m[0] = 0, m[1] = -2, m[2] = -0.5, m[3] = 0; break;
    }
}

std::pair<int, int> Editor::cellMiddle(int px, int py, SymmetryCell cell) {
    const auto fl = [](int v, int d) { return int(std::floor(double(v) / d)); };
    switch (cell) {
    case SymmetryCell::Tile: return {fl(px, 32) * 32 + 16, fl(py, 32) * 32 + 16};
    case SymmetryCell::TilePair: return {fl(px, 64) * 64 + 32, fl(py, 32) * 32 + 16};
    case SymmetryCell::Isom: {  // (the diamond round a corner: |x/64 - X| + |y/32 - Y| <= 1, so u and v round to odd numbers)
        const double u = px / 64.0 + py / 32.0, v = px / 64.0 - py / 32.0;
        const int U = 2 * int(std::floor(u / 2)) + 1, V = 2 * int(std::floor(v / 2)) + 1;
        return {(U + V) / 2 * 64, (U - V) / 2 * 32};
    }
    default: return {px, py};
    }
}
```

`symmetricPlaces` (replace the whole function):

```cpp
std::vector<Editor::SymmetricPlace> Editor::symmetricPlaces(int px, int py, int margin, SymmetryCell cell) const {
    std::vector<SymmetricPlace> out{{px, py, false, false}};
    if (!mirrors_ && turns_ <= 1) return out;  // (symmetry off: the point alone, also with more anchors)
    if (stableTurns_ && turns_ > 1) std::tie(px, py) = cellMiddle(px, py, cell);  // (Stable: the cell's middle turns; the point stays first)
    const int W = width_ * 32, H = height_ * 32;
    // A place is added when it is inside the map and no place is there yet (1 px).
    auto add = [&](const SymmetricPlace& p) {
        if (p.x < -margin || p.y < -margin || p.x > W + margin || p.y > H + margin || out.size() >= kMaxSymmetricPlaces) return;
        for (const SymmetricPlace& o : out)
            if (std::abs(o.x - p.x) <= 1 && std::abs(o.y - p.y) <= 1) return;
        out.push_back(p);
    };
    // How each mirror flips a pasted copy.
    static const struct {
        int flag, which;
        bool fx, fy;
    } mirrorsOf[] = {{Mirror::LeftRight, 0, true, false},     {Mirror::TopBottom, 1, false, true},
                     {Mirror::Diagonal, 3, false, false},     {Mirror::AntiDiagonal, 4, false, false},
                     {Mirror::IsoDiagonal, 5, false, false},  {Mirror::IsoAntiDiagonal, 6, false, false}};
    const auto mul = [](const double a[4], const double b[4], double c[4]) {
        c[0] = a[0] * b[0] + a[1] * b[2], c[1] = a[0] * b[1] + a[1] * b[3], c[2] = a[2] * b[0] + a[3] * b[2], c[3] = a[2] * b[1] + a[3] * b[3];
    };
    // The pattern round the main anchor (the mouse is relative to it), then the same pattern at the other anchors.
    const std::vector<std::pair<int, int>> anchors = symmetryAnchors();
    const std::pair<int, int> center = anchors.front();
    std::vector<SymmetricPlace> own{{px, py, false, false}};
    {
        auto addOwn = [&](const SymmetricPlace& p) {
            if (own.size() >= kMaxSymmetricPlaces) return;
            for (const SymmetricPlace& o : own)
                if (std::abs(o.x - p.x) <= 1 && std::abs(o.y - p.y) <= 1) return;
            own.push_back(p);
            add(p);
        };
        // The turns: the point turned k steps, each from the point itself (so the copies do not drift apart). A half
        // turn flips a pasted copy both ways.
        for (int k = 1; k < turns_; ++k) {
            const auto [x, y] = turnPoint(px, py, k, center);
            SymmetricPlace p{x, y, 2 * k == turns_, 2 * k == turns_};
            turnMatrix(k, p.m);
            addOwn(p);
        }
        // The mirrors: each place mirrored across the chosen axes, and the mirror images in turn, until no place is new.
        for (size_t k = 0; k < own.size(); ++k)
            for (const auto& m : mirrorsOf) {
                if (!(mirrors_ & m.flag)) continue;
                const SymmetricPlace from = own[k];
                const auto [x, y] = mirror(from.x, from.y, m.which, center);
                SymmetricPlace p{x, y, from.flipX != m.fx, from.flipY != m.fy};
                double mm[4];
                mirrorMatrix(m.which, mm);
                mul(mm, from.m, p.m);
                addOwn(p);
            }
    }
    // The other anchors: the pattern moved by each one's offset from the main anchor (as it is: not mirrored again).
    for (size_t a = 1; a < anchors.size(); ++a) {
        const int dx = anchors[a].first - center.first, dy = anchors[a].second - center.second;
        for (SymmetricPlace p : own) p.x += dx, p.y += dy, add(p);
    }
    return out;
}
```

`symmetricPoints`:

```cpp
std::vector<std::pair<int, int>> Editor::symmetricPoints(int px, int py, SymmetryCell cell) const {
    std::vector<std::pair<int, int>> out;
    for (const SymmetricPlace& p : symmetricPlaces(px, py, 0, cell)) out.push_back({p.x, p.y});
    return out;
}
```

`terrainSymmetricPoints`: StarEdit's brush snaps first only with Stable or with no turns (the mirrors stay exact);
another brush gives Stable the diamond:

```cpp
std::vector<std::pair<int, int>> Editor::terrainSymmetricPoints(int px, int py, const Brush& brush) const {
    const bool snapped = brush.starEdit() && (stableTurns_ || turns_ <= 1);
    if (snapped) {
        const auto [x, y] = starEditCenter(px, py, int(brush.size));
        px = x * 64, py = y * 32;
    }
    // (a place off the map still paints what reaches the map, with the center away from the middle)
    std::vector<std::pair<int, int>> out;
    for (const SymmetricPlace& p : symmetricPlaces(px, py, int(std::max(brush.size, brush.height())) * 64, snapped ? SymmetryCell::None : SymmetryCell::Isom))
        out.push_back({p.x, p.y});
    return out;
}
```

`fillTerrain`: `symmetricPoints(px, py)` becomes `symmetricPoints(px, py, SymmetryCell::Isom)`.
`pasteTerrain`: `symmetricPlaces(px, py, std::max(...))` becomes
`symmetricPlaces(px, py, std::max(terrainClip_.width, terrainClip_.height), SymmetryCell::Isom)`.

- [ ] **Step 4: Run the tests**

Run: `cmake --build build --config Release --target editor_test` then the `mirror` test command.
Expected: every `mirror:`, `turns:` and `rotation:` check passes ("0 failures").

- [ ] **Step 5: Commit**

```bash
git add include/stardraft/editor.h src/editor.cpp tools/tests/editor_test.cpp
git commit -m "Symmetry: Isometric rotation, Stable rotation and the 2:1 mirrors in the editor library"
```

### Task 2: The reverse method (`Editor::symmetryCells`)

**Files:**
- Modify: `include/stardraft/editor.h`, `src/editor.cpp`
- Test: `tools/tests/editor_test.cpp` (`symmetryCellsTest`, called after `symmetryRotationTest`)

**Interfaces:**
- Consumes: Task 1's `SymmetricPlace::m`, `symmetricPlaces(px, py, margin, cell)`, `cellMiddle`.
- Produces: `std::vector<TileAt> Editor::symmetryCells(const std::vector<TileAt>& cells, int cw, int ch, int cols, int rows, bool rotate, SymmetryCell cell) const`.

- [ ] **Step 1: Write the failing test**

```cpp
// Rotate brush's reverse method (Editor::symmetryCells): 8 turns of a 12 x 12 block of tiles leave no holes, and a
// quarter turn of a block of 2-tile pairs has no empty rows.
static void symmetryCellsTest(const GameData& data) {
    Editor ed(data);
    std::string err;
    if (!ed.newMap(128, 128, 0, 1, err)) {
        CHECK(false, "rotate brush: a new 128 x 128 map");
        return;
    }
    const double cx = 128 * 16, cy = 128 * 16;
    // Every cell whose middle, turned back, is well inside the source block must be in the result or the block.
    auto holes = [&](const std::vector<Editor::TileAt>& src, const std::vector<Editor::TileAt>& got, int cw, int ch, int cols, int rows, int n) {
        std::set<std::pair<int, int>> in;
        for (const auto& c : src) in.insert({c.x, c.y});
        for (const auto& c : got) in.insert({c.x, c.y});
        int x0 = INT_MAX, y0 = INT_MAX, x1 = INT_MIN, y1 = INT_MIN;
        for (const auto& c : src) x0 = std::min(x0, c.x), y0 = std::min(y0, c.y), x1 = std::max(x1, c.x), y1 = std::max(y1, c.y);
        const double l = x0 * cw + cw * 0.75, t = y0 * ch + ch * 0.75, r = (x1 + 1) * cw - cw * 0.75, b = (y1 + 1) * ch - ch * 0.75;
        int miss = 0;
        for (int k = 1; k < n; ++k) {
            const double a = -6.283185307179586 * k / n, c = std::cos(a), s = std::sin(a);
            for (int y = 0; y < rows; ++y)
                for (int x = 0; x < cols; ++x) {
                    const double dx = x * cw + cw / 2.0 - cx, dy = y * ch + ch / 2.0 - cy;
                    const double bx = cx + dx * c - dy * s, by = cy + dx * s + dy * c;
                    if (bx > l && bx < r && by > t && by < b && !in.count({x, y})) ++miss;
                }
        }
        return miss;
    };
    ed.setTurns(8);
    std::vector<Editor::TileAt> block;
    for (int y = 70; y < 82; ++y)
        for (int x = 40; x < 52; ++x) block.push_back({x, y, 7});
    const auto turned = ed.symmetryCells(block, 32, 32, 128, 128, true, Editor::SymmetryCell::Tile);
    const int m8 = holes(block, turned, 32, 32, 128, 128, 8);
    std::printf("      8 turns of 12 x 12 tiles: %d holes, %zu cells\n", m8, turned.size());
    CHECK(m8 == 0 && turned.size() >= 7 * 120, "rotate brush: 8 turns of a 12 x 12 block have no holes");
    bool values = true;
    for (const auto& c : turned) values = values && c.value == 7;
    CHECK(values, "rotate brush: each copy cell has the value of its source cell");
    const auto moved = ed.symmetryCells(block, 32, 32, 128, 128, false, Editor::SymmetryCell::Tile);
    CHECK(moved.size() == 7 * 144, "rotate brush off: each copy is the 12 x 12 block moved");
    // Pairs (Rectangle): 6 x 12 pairs, a quarter turn.
    ed.setTurns(4);
    std::vector<Editor::TileAt> pairs;
    for (int y = 70; y < 82; ++y)
        for (int x = 20; x < 26; ++x) pairs.push_back({x, y, 1});
    const auto pq = ed.symmetryCells(pairs, 64, 32, 64, 128, true, Editor::SymmetryCell::TilePair);
    const int m4 = holes(pairs, pq, 64, 32, 64, 128, 4);
    std::printf("      quarter turns of 6 x 12 pairs: %d holes, %zu pairs\n", m4, pq.size());
    CHECK(m4 == 0, "rotate brush: a quarter turn of pairs has no empty rows");
    ed.setTurns(1);
}
```

Add `#include <climits>` and `#include <set>` at the top of `editor_test.cpp` when they are not there.

- [ ] **Step 2: Run the test and see it fail**

Run: `cmake --build build --config Release --target editor_test`
Expected: compile error (`symmetryCells` is not a member of `Editor`).

- [ ] **Step 3: Implement**

`editor.h`, public, after `terrainSymmetricPoints`:

```cpp
    // Rotate brush (the Tile, Custom and Rectangle tools): the copies of `cells` (cells of cw x ch pixels, on a map of
    // cols x rows of them) at each copy of the symmetry, round the middle of the cells' box (Stable: the middle of its
    // cell). rotate: each copy turned and mirrored, each of its cells reading the cell it comes from (no holes); else
    // moved only. The cells themselves are not in the result; each cell is in it once at most.
    std::vector<TileAt> symmetryCells(const std::vector<TileAt>& cells, int cw, int ch, int cols, int rows, bool rotate, SymmetryCell cell) const;
```

`editor.cpp`, after `terrainSymmetricPoints`:

```cpp
std::vector<Editor::TileAt> Editor::symmetryCells(const std::vector<TileAt>& cells, int cw, int ch, int cols, int rows, bool rotate, SymmetryCell cell) const {
    std::vector<TileAt> out;
    if (cells.empty() || (!mirrors_ && turns_ <= 1)) return out;
    std::map<std::pair<int, int>, uint16_t> src;
    int x0 = INT_MAX, y0 = INT_MAX, x1 = INT_MIN, y1 = INT_MIN;
    for (const TileAt& c : cells) {
        src.emplace(std::pair{c.x, c.y}, c.value);
        x0 = std::min(x0, c.x), y0 = std::min(y0, c.y), x1 = std::max(x1, c.x), y1 = std::max(y1, c.y);
    }
    const int ax = (x0 + x1 + 1) * cw / 2, ay = (y0 + y1 + 1) * ch / 2;  // the middle of the box (pixels)
    const bool stable = stableTurns_ && turns_ > 1;
    const auto [sx, sy] = stable ? cellMiddle(ax, ay, cell) : std::pair{ax, ay};
    const int margin = std::max((x1 - x0 + 1) * cw, (y1 - y0 + 1) * ch);
    const auto places = symmetricPlaces(ax, ay, margin, cell);
    std::set<std::pair<int, int>> taken;
    for (const TileAt& c : cells) taken.insert({c.x, c.y});
    auto put = [&](int x, int y, uint16_t v) {
        if (x >= 0 && y >= 0 && x < cols && y < rows && taken.insert({x, y}).second) out.push_back({x, y, v});
    };
    for (size_t k = 1; k < places.size(); ++k) {  // (place 0: the cells themselves)
        const SymmetricPlace& p = places[k];
        const auto [qx, qy] = stable ? cellMiddle(p.x, p.y, cell) : std::pair{p.x, p.y};  // (Stable: the copy's middle on its cell too)
        if (!rotate) {  // moved only: each cell's middle by the place's offset, to the cell under it
            for (const TileAt& c : cells)
                put(int(std::floor((c.x * cw + cw / 2.0 + qx - sx) / cw)), int(std::floor((c.y * ch + ch / 2.0 + qy - sy) / ch)), c.value);
            continue;
        }
        const double* m = p.m;
        const double det = m[0] * m[3] - m[1] * m[2];
        if (std::abs(det) < 1e-9) continue;
        const double i0 = m[3] / det, i1 = -m[1] / det, i2 = -m[2] / det, i3 = m[0] / det;
        // The copy's box: the source box's corners turned.
        double bx0 = 1e18, by0 = 1e18, bx1 = -1e18, by1 = -1e18;
        for (const auto& [qx0, qy0] : {std::pair{x0 * cw, y0 * ch}, {(x1 + 1) * cw, y0 * ch}, {x0 * cw, (y1 + 1) * ch}, {(x1 + 1) * cw, (y1 + 1) * ch}}) {
            const double dx = qx0 - sx, dy = qy0 - sy, tx = qx + m[0] * dx + m[1] * dy, ty = qy + m[2] * dx + m[3] * dy;
            bx0 = std::min(bx0, tx), by0 = std::min(by0, ty), bx1 = std::max(bx1, tx), by1 = std::max(by1, ty);
        }
        // Each cell of that box reads the cell that its middle comes from.
        for (int ty = int(std::floor(by0 / ch)); ty <= int(std::floor(by1 / ch)); ++ty)
            for (int tx = int(std::floor(bx0 / cw)); tx <= int(std::floor(bx1 / cw)); ++tx) {
                const double dx = tx * cw + cw / 2.0 - qx, dy = ty * ch + ch / 2.0 - qy;
                const double bx = sx + i0 * dx + i1 * dy, by = sy + i2 * dx + i3 * dy;
                const auto it = src.find({int(std::floor(bx / cw)), int(std::floor(by / ch))});
                if (it != src.end()) put(tx, ty, it->second);
            }
    }
    return out;
}
```

Add `#include <climits>` to `src/editor.cpp` when it is not there (`<map>` and `<set>` are there).

- [ ] **Step 4: Run the tests**

Run the build and the `mirror` test command. Expected: "0 failures".

- [ ] **Step 5: Commit**

```bash
git add include/stardraft/editor.h src/editor.cpp tools/tests/editor_test.cpp
git commit -m "Symmetry: the reverse method for turned copies of tiles (no holes)"
```

### Task 3: Rotate brush in the tile tools, and the options in the panel state

**Files:**
- Modify: `apps/editor/core/app_state.inc:36` (new field)
- Modify: `apps/editor/core/tiles_edit.inc:287-317` (`tilesMirrored`, `solidMirrored`, two new helpers)
- Modify: `apps/editor/core/panel_state.inc` (fields, store, load, text, read)
- Modify: `apps/editor/core/map_scene.inc:1137` (the paste preview: `SymmetryCell::Isom`)

**Interfaces:**
- Consumes: Task 1 (`setIsoTurns`, `setStableTurns`, `SymmetryCell`), Task 2 (`symmetryCells`).
- Produces: `int AppState::rotateBrush` (bits: 1 Tile, 2 Custom, 4 Rectangle; default 7);
  `int rotateBrushBit()`, `bool rotateBrushOn()` (in `tiles_edit.inc`).

- [ ] **Step 1: AppState**

After `bool terrainRect = false;`:

```cpp
    int rotateBrush = 7;  // Rotate brush, each tool's (bits: 1 Tile, 2 Custom, 4 Rectangle): turned copies of tiles turn (Editor::symmetryCells)
```

- [ ] **Step 2: tiles_edit.inc** (replace lines 287-317)

```cpp
// Rotate brush (the tool strip, after the symmetry): each of Tile, Custom and Rectangle has its own.
int rotateBrushBit() { return gTiles && gTiles->solid ? 4 : gTiles && gTiles->stamps ? 2 : 1; }
bool rotateBrushOn() { return (appState().rotateBrush & rotateBrushBit()) != 0; }

// The tool strip's mirrors and turns in the Tile and Custom tools: the tiles' copies round the symmetry (Editor::symmetryCells:
// with Rotate brush, turned and mirrored with no holes; else moved). The tiles themselves first, then their copies where no
// tile of the stroke is.
std::vector<Editor::TileAt> tilesMirrored(const std::vector<Editor::TileAt>& cells) {
    const Editor& ed = *appState().ed;
    if (!ed.mirrors() && ed.turns() == 1) return cells;
    std::vector<Editor::TileAt> out = cells;
    const auto more = ed.symmetryCells(cells, 32, 32, ed.width(), ed.height(), rotateBrushOn(), Editor::SymmetryCell::Tile);
    out.insert(out.end(), more.begin(), more.end());
    return out;
}
// Rectangle's mirrors and turns: the same with rects of 2 x 1 tiles as the cells; a rect keeps its two tiles in their order.
std::vector<Editor::TileAt> solidMirrored(const std::vector<Editor::TileAt>& cells) {
    const Editor& ed = *appState().ed;
    if (!ed.mirrors() && ed.turns() == 1) return cells;
    std::map<std::pair<int, int>, size_t> at;
    std::vector<std::array<int, 2>> pairValue;  // (each rect's left and right tile, -1: not in the stroke)
    std::vector<Editor::TileAt> rects;
    for (const auto& c : cells) {
        auto [it, added] = at.try_emplace({c.x >> 1, c.y}, pairValue.size());
        if (added) pairValue.push_back({-1, -1}), rects.push_back({c.x >> 1, c.y, uint16_t(it->second)});
        pairValue[it->second][size_t(c.x & 1)] = c.value;
    }
    std::vector<Editor::TileAt> out = cells;
    for (const auto& r : ed.symmetryCells(rects, 64, 32, (ed.width() + 1) / 2, ed.height(), rotateBrushOn(), Editor::SymmetryCell::TilePair))
        for (int b = 0; b < 2; ++b)
            if (const int v = pairValue[r.value][size_t(b)]; v >= 0 && 2 * r.x + b < ed.width()) out.push_back({2 * r.x + b, r.y, uint16_t(v)});
    return out;
}
```

The old functions kept the stroke's own tiles out of the copies with `taken`. `symmetryCells` does this for the
cells it gets. A rect's other tile can be on a stroke tile: the old code allowed it too.

- [ ] **Step 3: panel_state.inc**

`PanelState`, after `int turns = 1;`:

```cpp
    bool isoTurns = false, stableTurns = false;  // (the Editor's: Isometric / Tile rotation, Stable / Precise rotation)
    int rotateBrush = 7;                         // (AppState's)
```

`panelStore`: `if (a.ed) s.mirrors = a.ed->mirrors(), s.turns = a.ed->turns(), s.isoTurns = a.ed->isoTurns(), s.stableTurns = a.ed->stableTurns();`
and `s.rotateBrush = a.rotateBrush;`.
`panelLoad`: `if (a.ed) a.ed->setMirrors(s.mirrors), a.ed->setTurns(s.turns), a.ed->setIsoTurns(s.isoTurns), a.ed->setStableTurns(s.stableTurns);`
and `a.rotateBrush = s.rotateBrush;`.
`panelStateText`: add `"\t%d\t%d\t%d"` with `int(s.isoTurns), int(s.stableTurns), s.rotateBrush` at the end (`char b[240]`).
`panelStateRead`, before `return true;`:

```cpp
    if (part.size() >= at + 18)  // (Isometric / Tile rotation, Stable / Precise rotation, Rotate brush; an older file: the defaults)
        s.isoTurns = num(15) != 0, s.stableTurns = num(16) != 0, s.rotateBrush = num(17) & 7;
```

- [ ] **Step 4: The paste preview** (`map_scene.inc:1137`)

`ed.symmetricPoints(int(wx), int(wy))` becomes `ed.symmetricPoints(int(wx), int(wy), Editor::SymmetryCell::Isom)`, as
`pasteTerrain` does.

- [ ] **Step 5: Build and run**

Run: `cmake --build build --config Release --target stardraft_editor editor_test` and the `mirror` test command.
Expected: the build succeeds; "0 failures".

- [ ] **Step 6: Commit**

```bash
git add apps/editor/core/app_state.inc apps/editor/core/tiles_edit.inc apps/editor/core/panel_state.inc apps/editor/core/map_scene.inc
git commit -m "Terrain: Rotate brush for Tile, Custom and Rectangle; the rotation options in the panel state"
```

### Task 4: Tool strip controls, the 2:1 mirrors in the UI, keys, map axes, CLI

**Files:**
- Modify: `apps/editor/core/views_toolstrip.inc` (`mirrorIcon`, `kMirrorKinds`, the symmetry row)
- Modify: `apps/editor/core/map_edit_commands.inc:54-55, 549-560`, `apps/editor/sdl_menus.inc:320, 337`,
  `apps/editor/core/ids.inc:99`
- Modify: `apps/editor/core/map_scene.inc:917-920` (the 2:1 axes)
- Modify: `apps/cli/main.cpp:1545-1550` (names `iso_diagonal`, `iso_anti_diagonal`; options `iso_turns`, `stable_turns`)

**Interfaces:**
- Consumes: Task 1, Task 3 (`rotateBrushBit`, `rotateBrushOn`, `AppState::rotateBrush`).

- [ ] **Step 1: `mirrorIcon`**: after the `AntiDiagonal` line:

```cpp
    if (kind & Mirror::IsoDiagonal) c.smoothLine(P{x0, y0 + s / 4}, P{x0 + s, y0 + s * 3 / 4}, col, aw);
    if (kind & Mirror::IsoAntiDiagonal) c.smoothLine(P{x0, y0 + s * 3 / 4}, P{x0 + s, y0 + s / 4}, col, aw);
```

- [ ] **Step 2: `kMirrorKinds`**: add after the AntiDiagonal entry:

```cpp
                                       {Mirror::IsoDiagonal, L"Mirror across the 2:1 line from top left to bottom right (the diamonds' edges)"},
                                       {Mirror::IsoAntiDiagonal, L"Mirror across the 2:1 line from bottom left to top right (the diamonds' edges)"}};
```

The symmetry button's tip: `Alt+0 to Alt+6`.

- [ ] **Step 3: Keys**: `map_edit_commands.inc` adds `{"Alt+5", ID_MIRROR_KEY + 5, true}, {"Alt+6", ID_MIRROR_KEY + 6, true},`;
the handler uses `id < ID_MIRROR_KEY + 7`, `kFlags[7] = {..., Mirror::IsoDiagonal, Mirror::IsoAntiDiagonal}` and
`kNames[7] = {..., L"the 2:1 line from top left to bottom right", L"the 2:1 line from bottom left to top right"}`.
`sdl_menus.inc` lines 320 and 337: `ID_MIRROR_KEY + 7`. `ids.inc:99`: the comment names 5 and 6.

- [ ] **Step 4: The three buttons** (in the symmetry row of `views_toolstrip.inc`, after the `spinner(kNumTurns, ...)` call)

```cpp
            // Isometric / Tile rotation, Stable / Precise rotation, and Rotate brush (Tile, Custom, Rectangle): square
            // buttons after the brushes, in the row when there is space, else in the next row
            // (docs/superpowers/specs/2026-11-23-symmetry-rotation-design.md).
            {
                Editor& ed = *app.ed;
                const bool brushOpt = app.mode == Mode::Terrain && app.terrainRect;
                const int n = brushOpt ? 3 : 2, og = scale(4);
                int ox = joined.right + og, oy = y;
                if (ox + n * (mb + og) > W) ox = 0, oy = y + mb + gap;
                using P = Canvas::PointF;
                auto button = [&](int i, bool on, auto&& icon, std::wstring tip, auto&& act) {
                    const draw::Rect r{ox + i * (mb + og), oy, ox + i * (mb + og) + mb, oy + mb};
                    const bool over = hot(r);
                    c.roundBox(r, on ? kSel : over ? kCtlHot : kCtl, on ? kAccentLine : kBorder, float(scale(kModeBoxRadius)));
                    const float s = float(scale(14)), x0 = (r.left + r.right - s) / 2.0f, y0 = (r.top + r.bottom - s) / 2.0f;
                    icon(x0, y0, s, on ? kAccentBright : over ? kText : kDim);
                    addHit(r, [act] {
                        act();
                        shellToolsChanged();
                        appState().dirty = true;
                    }, tip);
                };
                const float lw = float(std::max(1, scale(2))) * 0.75f;
                const bool iso = ed.isoTurns();
                button(0, false, [&](float x0, float y0, float s, auto col) {
                    if (iso) {  // a diamond, 2:1
                        const P l{x0, y0 + s / 2}, t{x0 + s / 2, y0 + s / 4}, r{x0 + s, y0 + s / 2}, b{x0 + s / 2, y0 + s * 3 / 4};
                        c.smoothLine(l, t, col, lw), c.smoothLine(t, r, col, lw), c.smoothLine(r, b, col, lw), c.smoothLine(b, l, col, lw);
                    } else {
                        c.smoothFrame(P{x0 + s / 8, y0 + s / 8}, s * 3 / 4, s * 3 / 4, col, 1.0f, 110);
                    }
                }, tr(iso ? L"Isometric rotation: the copies turn as the diamonds do (2:1). Click for Tile rotation."
                          : L"Tile rotation: the copies turn in the map's proportions. Click for Isometric rotation."),
                       [] {
                           Editor& e = *appState().ed;
                           e.setIsoTurns(!e.isoTurns());
                           setMessage(e.isoTurns() ? L"Isometric rotation" : L"Tile rotation");
                       });
                const bool stable = ed.stableTurns();
                button(1, false, [&](float x0, float y0, float s, auto col) {
                    if (stable) {  // a cell with a dot in its middle
                        c.smoothFrame(P{x0 + s / 8, y0 + s / 8}, s * 3 / 4, s * 3 / 4, col, 1.0f, 110);
                        c.smoothEllipse(P{x0 + s / 2 - s / 8, y0 + s / 2 - s / 8}, s / 4, s / 4, col);
                    } else {  // the pointer's cross
                        c.smoothLine(P{x0 + s / 2, y0}, P{x0 + s / 2, y0 + s}, col, lw), c.smoothLine(P{x0, y0 + s / 2}, P{x0 + s, y0 + s / 2}, col, lw);
                    }
                }, tr(stable ? L"Stable rotation: the middle of the cell under the pointer turns. Click for Precise rotation."
                             : L"Precise rotation: the exact pointer position turns. Click for Stable rotation."),
                       [] {
                           Editor& e = *appState().ed;
                           e.setStableTurns(!e.stableTurns());
                           setMessage(e.stableTurns() ? L"Stable rotation" : L"Precise rotation");
                       });
                if (brushOpt) {
                    const bool rot = rotateBrushOn();
                    button(2, rot, [&](float x0, float y0, float s, auto col) {
                        if (rot) {  // a square turned
                            const P l{x0, y0 + s / 2}, t{x0 + s / 2, y0}, r{x0 + s, y0 + s / 2}, b{x0 + s / 2, y0 + s};
                            c.smoothLine(l, t, col, lw), c.smoothLine(t, r, col, lw), c.smoothLine(r, b, col, lw), c.smoothLine(b, l, col, lw);
                        } else {
                            c.smoothFrame(P{x0 + s / 8, y0 + s / 8}, s * 3 / 4, s * 3 / 4, col, 1.0f, 110);
                        }
                    }, tr(rot ? L"Rotate brush: on. The turned copies turn their tiles. Click to keep the tiles as they are."
                              : L"Rotate brush: off. The turned copies move and keep their tiles as they are. Click to turn them."),
                           [] {
                               appState().rotateBrush ^= rotateBrushBit();
                               setMessage(rotateBrushOn() ? L"Rotate brush: on" : L"Rotate brush: off");
                           });
                }
                if (oy != y) y += mb + gap;  // (the buttons' own row)
            }
```

Use the same color type that `mirrorIcon` takes for `col` (`auto` in the lambda). When `Canvas::PointF` or
`smoothFrame` have other names in this file, use the names that `mirrorIcon` uses.

- [ ] **Step 5: Map axes** (`map_scene.inc`, after `if (mm & Mirror::AntiDiagonal) diag(true);`)

```cpp
            auto slope = [&](float k) {  // (a 2:1 line through the anchor: y - cy = k (x - cx), cut at the map's edges)
                float xa = 0, xb = W;
                const float xt = cx + (0 - cy) / k, xbt = cx + (H - cy) / k;
                xa = std::max(xa, std::min(xt, xbt)), xb = std::min(xb, std::max(xt, xbt));
                if (xa >= xb) return;
                render::RectDraw l;
                l.x0 = xa, l.y0 = cy + k * (xa - cx), l.x1 = xb, l.y1 = cy + k * (xb - cx);
                l.rgba = c, l.thickness = 3, l.line = true;
                s.rects.push_back(l);
            };
            if (mm & Mirror::IsoDiagonal) slope(0.5f);
            if (mm & Mirror::IsoAntiDiagonal) slope(-0.5f);
```

- [ ] **Step 6: CLI** (`apps/cli/main.cpp`)

```cpp
                else if (n == "iso_diagonal") mirrors |= Mirror::IsoDiagonal;
                else if (n == "iso_anti_diagonal") mirrors |= Mirror::IsoAntiDiagonal;
                else fail("options.mirrors: left_right, top_bottom, diagonal, anti_diagonal, iso_diagonal or iso_anti_diagonal");
```

After `ed.setTurns(...)`: `ed.setIsoTurns(opt->boolean("iso_turns", false)), ed.setStableTurns(opt->boolean("stable_turns", false));`
(use the `Json` accessor for booleans that the file has).

- [ ] **Step 7: Build, run, lint**

Run: `cmake --build build --config Release --target stardraft_editor editor_test stardraft_cli` (the CLI target's name
from `CMakeLists.txt`), the `mirror` test command, and `python -I tools/ui_text_lint.py`.
Expected: the build succeeds; "0 failures"; the lint exits with 0.

- [ ] **Step 8: Snapshot**

Run: `"build\Release\StarDraft 2.exe" --mode terrain --snapshot %TEMP%\strip.png` (add a map argument when the
snapshot needs one) and look at the tool strip: the three buttons are after the Brushes number, or in the next row.

- [ ] **Step 9: Commit**

```bash
git add apps/editor/core/views_toolstrip.inc apps/editor/core/map_edit_commands.inc apps/editor/sdl_menus.inc apps/editor/core/ids.inc apps/editor/core/map_scene.inc apps/cli/main.cpp
git commit -m "Tool strip: Isometric/Tile rotation, Stable/Precise rotation, Rotate brush, and the 2:1 mirrors (Alt+5, Alt+6)"
```

### Task 5: User docs

**Files:**
- Modify: `docs/user/CHANGELOG.md` (the top, unreleased section)
- Modify: `docs/help/keyboard-shortcuts.md` (Alt+5, Alt+6), the help page that tells about the symmetry (find it
  with `grep -ril "symmetry" docs/help`)
- Modify: `docs/dev/CLI.md` (the new mirror names and options)

- [ ] **Step 1:** Write the entries in ASD-STE100 (one sentence for each control, and the keys).
- [ ] **Step 2:** Run `python -I tools/help_build.py` and `python -I tools/ui_text_lint.py`. Expected: both exit with 0.
- [ ] **Step 3: Commit**

```bash
git add docs/user/CHANGELOG.md docs/help docs/dev/CLI.md
git commit -m "Docs: symmetry rotation options and the 2:1 mirrors"
```
