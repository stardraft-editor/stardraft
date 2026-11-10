// The unit selection of the Editor: what is selected, and its edits (delete, move, snap to the grid, arrange, copy and
// paste). The other parts of the Editor are in editor.cpp.

#include "stardraft/editor.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>

#include "stardraft/game_data.h"

namespace stardraft {

bool Editor::isSelected(int unit) const { return std::find(selection_.begin(), selection_.end(), unit) != selection_.end(); }
void Editor::clearSelection() {
    selection_.clear();
    revision_ = nextRevision();
}
void Editor::select(int unit, bool add) {
    if (!add) selection_.clear();
    if (unit >= 0 && unit < int(units_.size()) && !isSelected(unit)) selection_.push_back(unit);
    revision_ = nextRevision();
}
void Editor::toggleSelected(int unit) {
    auto it = std::find(selection_.begin(), selection_.end(), unit);
    if (it != selection_.end()) selection_.erase(it);
    else if (unit >= 0 && unit < int(units_.size())) selection_.push_back(unit);
    revision_ = nextRevision();
}
void Editor::selectRect(const Rect& rect, bool add) {
    Rect r = rect.normalized();
    if (!add) selection_.clear();
    for (int i = 0; i < int(units_.size()); ++i)
        if (pickable(i) && r.intersects(unitBox(units_[i])) && !isSelected(i)) selection_.push_back(i);
    revision_ = nextRevision();
}
void Editor::selectAll() {
    selection_.clear();
    for (int i = 0; i < int(units_.size()); ++i)
        if (pickable(i)) selection_.push_back(i);
    revision_ = nextRevision();
}

int Editor::selectUnitsOfType(uint16_t type, int owner) {
    selection_.clear();
    for (int i = 0; i < int(units_.size()); ++i)
        if (units_[size_t(i)].type == type && (owner < 0 || units_[size_t(i)].owner == owner)) selection_.push_back(i);
    revision_ = nextRevision();
    return int(selection_.size());
}

void Editor::deleteSelection() {
    if (selection_.empty()) return;
    beginEdit();
    std::vector<int> idx = selection_;
    std::sort(idx.rbegin(), idx.rend());
    for (int i : idx) units_.erase(units_.begin() + i);
    selection_.clear();
    fixAddonLinks();  // a deleted building's add-on (or a deleted add-on's building) is not linked any more
    writeUnits(chk_, units_);
    commit(false);
}

std::vector<int> Editor::withLinkedAddons(std::vector<int> sel) const {
    const size_t n = sel.size();
    for (size_t k = 0; k < n; ++k) {
        const Unit& b = units_[size_t(sel[k])];
        if (!(b.links & UnitLink::Addon) || addonParent(b.type) >= 0) continue;  // a building with an add-on
        for (int i = 0; i < int(units_.size()); ++i) {
            const Unit& a = units_[size_t(i)];
            if (a.serial == b.relatedSerial && (a.links & UnitLink::Addon) && addonParent(a.type) >= 0 &&
                std::find(sel.begin(), sel.end(), i) == sel.end())
                sel.push_back(i);
        }
    }
    return sel;
}

void Editor::fixAddonLinks() {
    auto find = [&](uint32_t serial) -> Unit* {
        for (Unit& u : units_)
            if (u.serial == serial) return &u;
        return nullptr;
    };
    for (Unit& a : units_) {
        if (!(a.links & UnitLink::Addon) || addonParent(a.type) < 0) continue;  // the add-ons
        Unit* b = find(a.relatedSerial);
        const bool ok = b && b->type == addonParent(a.type) && b->type < types_.size() && a.type < types_.size() &&
                        addonSpot(types_[b->type], *b, types_[a.type]) == std::pair<int, int>{a.x, a.y};
        if (ok) continue;
        a.links &= uint16_t(~UnitLink::Addon), a.relatedSerial = 0;
        if (b && b->relatedSerial == a.serial) b->links &= uint16_t(~UnitLink::Addon), b->relatedSerial = 0;
    }
    for (Unit& b : units_) {  // a building whose add-on is gone
        if (!(b.links & UnitLink::Addon) || addonParent(b.type) >= 0) continue;
        const Unit* a = find(b.relatedSerial);
        if (!a || !(a->links & UnitLink::Addon) || a->relatedSerial != b.serial) b.links &= uint16_t(~UnitLink::Addon), b.relatedSerial = 0;
    }
}

int Editor::moveSelection(int dx, int dy, bool correct) {
    if (selection_.empty() || (!dx && !dy)) return 0;
    std::vector<int> sel = withLinkedAddons(selection_);  // a building's add-on goes with it
    std::sort(sel.begin(), sel.end());
    std::vector<bool> selected(units_.size(), false);
    for (int i : sel) selected[i] = true;
    std::vector<Unit> others;  // the units that do not move
    for (size_t i = 0; i < units_.size(); ++i)
        if (!selected[i]) others.push_back(units_[i]);
    std::vector<Unit> moved;
    for (int i : sel) {
        Unit u = units_[i];
        u.x = uint16_t(std::clamp(int(u.x) + dx, 0, width_ * 32 - 1));
        u.y = uint16_t(std::clamp(int(u.y) + dy, 0, height_ * 32 - 1));
        if (u.type < types_.size()) snapToGrid(types_[u.type], u.x, u.y);
        moved.push_back(u);
    }
    // A linked add-on moving with its building: on the building's add-on spot (the building may have stopped at the map's
    // edge, where the add-on alone would have gone on).
    for (size_t k = 0; k < sel.size(); ++k) {
        const Unit& a = units_[size_t(sel[k])];
        if (!(a.links & UnitLink::Addon) || addonParent(a.type) < 0) continue;
        for (size_t j = 0; j < sel.size(); ++j)
            if (units_[size_t(sel[j])].serial == a.relatedSerial && moved[j].type < types_.size() && a.type < types_.size()) {
                const auto [ax, ay] = addonSpot(types_[moved[j].type], moved[j], types_[a.type]);
                moved[k].x = uint16_t(std::clamp(ax, 0, width_ * 32 - 1)), moved[k].y = uint16_t(std::clamp(ay, 0, height_ * 32 - 1));
            }
    }
    // As a group: the units keep their places to each other (as they were, also when they overlap).
    bool fits = true;
    if (!allowIllegalUnits_) {
        const PlacementContext ctx = placementContext(&others);
        for (const Unit& u : moved) fits = fits && placementOk(ctx, u);
    }
    if (!fits) {
        if (!correct) return 0;  // refused: the units stay where they were
        // Each unit to the nearest spot where it fits, checked against the others and the units already done.
        std::vector<Unit> against = others;
        for (size_t k = 0; k < sel.size(); ++k) {
            std::optional<Unit> f = fitted(moved[k], true, against);
            moved[k] = f ? *f : units_[sel[k]];  // no spot: it stays
            against.push_back(moved[k]);
        }
    }
    int count = 0;
    for (size_t k = 0; k < sel.size(); ++k) count += moved[k].x != units_[sel[k]].x || moved[k].y != units_[sel[k]].y;
    if (!count) return 0;
    beginEdit();
    for (size_t k = 0; k < sel.size(); ++k) units_[sel[k]] = moved[k];
    fixAddonLinks();  // an add-on that did not keep its place on its building is not linked any more
    writeUnits(chk_, units_);
    commit(false);
    return count;
}

int Editor::snapSelection(int gw, int gh, int ox, int oy) {
    if (selection_.empty() || gw <= 0 || gh <= 0) return 0;
    std::vector<int> sel = withLinkedAddons(selection_);  // a building's add-on goes with it
    std::sort(sel.begin(), sel.end());
    std::vector<Unit> to;
    for (int i : sel) to.push_back(units_[size_t(i)]);
    for (size_t k = 0; k < sel.size(); ++k) {
        Unit& u = to[k];
        if ((u.links & UnitLink::Addon) && addonParent(u.type) >= 0) continue;  // (it goes with its building: moveUnitsTo)
        if (u.type >= types_.size() || !types_[u.type].is(UnitFlag::Building)) {
            u.x = uint16_t(gridCrossing(u.x, gw, ox, width_ * 32)), u.y = uint16_t(gridCrossing(u.y, gh, oy, height_ * 32));
            continue;
        }
        // A building: the edges of its box on the grid's lines, on the tiles; only when that brings them nearer to a line
        // (a grid between the tiles has no place for it: it stays, also when snapped again).
        const int halfW = types_[u.type].placementWidth / 2, halfH = types_[u.type].placementHeight / 2;
        Unit t = u;
        t.x = uint16_t(std::clamp(gridCrossing(u.x - halfW, gw, ox, width_ * 32) + halfW, 0, width_ * 32 - 1));
        t.y = uint16_t(std::clamp(gridCrossing(u.y - halfH, gh, oy, height_ * 32) + halfH, 0, height_ * 32 - 1));
        snapToGrid(types_[u.type], t.x, t.y);
        if (gridDistance(t.x - halfW, gw, ox) < gridDistance(u.x - halfW, gw, ox)) u.x = t.x;
        if (gridDistance(t.y - halfH, gh, oy) < gridDistance(u.y - halfH, gh, oy)) u.y = t.y;
    }
    return moveUnitsTo(sel, std::move(to));
}

int Editor::moveUnitsTo(const std::vector<int>& sel, std::vector<Unit> to) {
    std::vector<bool> selected(units_.size(), false);
    for (int i : sel) selected[size_t(i)] = true;
    std::vector<Unit> others;  // the units that do not move
    for (size_t i = 0; i < units_.size(); ++i)
        if (!selected[i]) others.push_back(units_[i]);
    std::vector<Unit> now;
    for (int i : sel) now.push_back(units_[size_t(i)]);
    std::vector<std::vector<size_t>> groups;  // (a building and its linked add-on go together, or stay together)
    std::vector<size_t> addons;
    for (size_t k = 0; k < sel.size(); ++k) {
        if ((now[k].links & UnitLink::Addon) && addonParent(now[k].type) >= 0) addons.push_back(k), to[k] = now[k];
        else groups.push_back({k});
    }
    for (size_t k : addons)  // (with its building in the selection; alone it stays, and it stays linked)
        for (size_t j = 0; j < sel.size(); ++j)
            if (now[j].serial == now[k].relatedSerial && to[j].type < types_.size() && now[k].type < types_.size()) {
                const auto [ax, ay] = addonSpot(types_[to[j].type], to[j], types_[now[k].type]);
                to[k].x = uint16_t(std::clamp(ax, 0, width_ * 32 - 1)), to[k].y = uint16_t(std::clamp(ay, 0, height_ * 32 - 1));
                for (std::vector<size_t>& g : groups)
                    if (g[0] == j) g.push_back(k);
            }
    // All of them at once when they fit at their new places (units that change places with each other).
    bool all = true;
    if (!allowIllegalUnits_) {
        std::vector<Unit> against = others;
        for (size_t k = 0; k < to.size() && all; ++k) {
            all = placementOk(placementContext(&against), to[k]);
            against.push_back(to[k]);
        }
    }
    if (all) now = to;
    // Else each one goes when it fits among all the others as they are then, so that no move makes units overlap. Twice:
    // one may leave the place that another one wants.
    for (int pass = 0; pass < 2 && !all; ++pass)
        for (const std::vector<size_t>& g : groups) {
            bool differs = false;
            for (size_t k : g) differs = differs || to[k].x != now[k].x || to[k].y != now[k].y;
            if (!differs) continue;
            bool fits = true;
            if (!allowIllegalUnits_) {
                std::vector<Unit> against = others;
                for (size_t k = 0; k < now.size(); ++k)
                    if (std::find(g.begin(), g.end(), k) == g.end()) against.push_back(now[k]);
                for (size_t k : g) {
                    fits = fits && placementOk(placementContext(&against), to[k]);
                    against.push_back(to[k]);
                }
            }
            if (fits)
                for (size_t k : g) now[k] = to[k];
        }
    int count = 0;
    for (size_t k = 0; k < sel.size(); ++k) count += now[k].x != units_[size_t(sel[k])].x || now[k].y != units_[size_t(sel[k])].y;
    if (!count) return 0;
    beginEdit();
    for (size_t k = 0; k < sel.size(); ++k) units_[size_t(sel[k])] = now[k];
    fixAddonLinks();  // an add-on that did not keep its place on its building is not linked any more
    writeUnits(chk_, units_);
    commit(false);
    return count;
}

int Editor::arrangeSelection(Arrange how) {
    if (selection_.size() < 2) return 0;
    std::vector<int> sel = withLinkedAddons(selection_);  // a building's add-on goes with it
    std::sort(sel.begin(), sel.end());
    std::vector<Unit> to;
    for (int i : sel) to.push_back(units_[size_t(i)]);
    // The units to arrange, each with its box (a building's with its linked add-on's). A linked add-on is not one of
    // them: it goes with its building, or stays.
    struct Item {
        size_t k;
        Rect box;
        bool building;
        int w() const { return box.right - box.left; }
        int h() const { return box.bottom - box.top; }
        double cx() const { return (box.left + box.right) / 2.0; }
        double cy() const { return (box.top + box.bottom) / 2.0; }
    };
    std::vector<Item> items;
    bool buildings = false;
    for (size_t k = 0; k < sel.size(); ++k) {
        const Unit& u = to[k];
        if ((u.links & UnitLink::Addon) && addonParent(u.type) >= 0) continue;
        Item it{k, unitBox(u), u.type < types_.size() && types_[u.type].is(UnitFlag::Building)};
        if (u.links & UnitLink::Addon)
            for (const Unit& a : to)
                if (a.serial == u.relatedSerial && (a.links & UnitLink::Addon) && a.relatedSerial == u.serial) {
                    const Rect b = unitBox(a);
                    it.box = {std::min(it.box.left, b.left), std::min(it.box.top, b.top), std::max(it.box.right, b.right), std::max(it.box.bottom, b.bottom)};
                }
        buildings = buildings || it.building;
        items.push_back(it);
    }
    const int n = int(items.size());
    if (n < 2 || ((how == Arrange::SpreadH || how == Arrange::SpreadV) && n < 3)) return 0;
    Rect all = items[0].box;
    int maxW = 0, maxH = 0;
    for (const Item& it : items) {
        all = {std::min(all.left, it.box.left), std::min(all.top, it.box.top), std::max(all.right, it.box.right), std::max(all.bottom, it.box.bottom)};
        maxW = std::max(maxW, it.w()), maxH = std::max(maxH, it.h());
    }
    // An item's box with its corner at (left, top), as near as the map and a building's grid let it.
    auto put = [&](Item& it, int left, int top) {
        Unit& u = to[it.k];
        const int x0 = u.x, y0 = u.y;
        u.x = uint16_t(std::clamp(x0 + left - it.box.left, 0, width_ * 32 - 1));
        u.y = uint16_t(std::clamp(y0 + top - it.box.top, 0, height_ * 32 - 1));
        if (u.type < types_.size()) snapToGrid(types_[u.type], u.x, u.y);
        const int dx = int(u.x) - x0, dy = int(u.y) - y0;
        it.box = {it.box.left + dx, it.box.top + dy, it.box.right + dx, it.box.bottom + dy};
    };
    // (a building that its grid moved back over the one before it: one tile on)
    auto putAfter = [&](Item& it, int left, int top, bool across) {
        put(it, left, top);
        if (across && it.box.left < left) put(it, it.box.left + 32, it.box.top);
        if (!across && it.box.top < top) put(it, it.box.left, it.box.top + 32);
    };
    const int gap = buildings ? 0 : 8;  // the space between units side by side (buildings: wall to wall, on the tiles)
    const int left0 = buildings ? all.left / 32 * 32 : all.left, top0 = buildings ? all.top / 32 * 32 : all.top;
    // A shape's start for a shape of `size` pixels: moved back so that the whole shape is on the map (`most` pixels).
    const auto inside = [&](int at, int size, int most) {
        at = std::max(0, std::min(at, most - size));
        return buildings ? at / 32 * 32 : at;
    };
    const int mapW = width_ * 32, mapH = height_ * 32;
    const auto byX = [](const Item& a, const Item& b) { return a.cx() < b.cx(); };
    const auto byY = [](const Item& a, const Item& b) { return a.cy() < b.cy(); };
    switch (how) {
    case Arrange::Left:
        for (Item& it : items) put(it, all.left, it.box.top);
        break;
    case Arrange::Center:
        for (Item& it : items) put(it, (all.left + all.right - it.w()) / 2, it.box.top);
        break;
    case Arrange::Right:
        for (Item& it : items) put(it, all.right - it.w(), it.box.top);
        break;
    case Arrange::Top:
        for (Item& it : items) put(it, it.box.left, all.top);
        break;
    case Arrange::Middle:
        for (Item& it : items) put(it, it.box.left, (all.top + all.bottom - it.h()) / 2);
        break;
    case Arrange::Bottom:
        for (Item& it : items) put(it, it.box.left, all.bottom - it.h());
        break;
    case Arrange::SpreadH:
    case Arrange::SpreadV: {
        const bool across = how == Arrange::SpreadH;
        std::stable_sort(items.begin(), items.end(), across ? byX : byY);
        int sum = 0;
        for (const Item& it : items) sum += across ? it.w() : it.h();
        const double space = ((across ? all.right - all.left : all.bottom - all.top) - sum) / double(n - 1);
        double at = across ? all.left : all.top;
        for (Item& it : items) {
            const int size = across ? it.w() : it.h();
            if (across) put(it, int(std::lround(at)), it.box.top);
            else put(it, it.box.left, int(std::lround(at)));
            at += size + space;
        }
        break;
    }
    case Arrange::Row:
    case Arrange::Column: {
        const bool across = how == Arrange::Row;
        std::stable_sort(items.begin(), items.end(), across ? byX : byY);
        int total = gap * (n - 1);
        for (const Item& it : items) total += across ? it.w() : it.h();
        int at = across ? inside(left0, total, mapW) : inside(top0, total, mapH);
        for (Item& it : items) {
            if (across) putAfter(it, at, (all.top + all.bottom - it.h()) / 2, true);
            else putAfter(it, (all.left + all.right - it.w()) / 2, at, false);
            at = (across ? it.box.right : it.box.bottom) + gap;
        }
        break;
    }
    case Arrange::Grid: {
        // As near to a square as the count lets it be; the units keep their order (the rows from the top, each row
        // from the left). A cell holds the largest unit; a building is at its cell's corner, on the tiles.
        const int cols = int(std::ceil(std::sqrt(double(n))));
        std::stable_sort(items.begin(), items.end(), byY);
        for (int r = 0; r * cols < n; ++r) std::stable_sort(items.begin() + r * cols, items.begin() + std::min(n, (r + 1) * cols), byX);
        const int cellW = buildings ? (maxW + 31) / 32 * 32 : maxW + gap, cellH = buildings ? (maxH + 31) / 32 * 32 : maxH + gap;
        const int gridLeft = inside(left0, cols * cellW, mapW), gridTop = inside(top0, (n + cols - 1) / cols * cellH, mapH);
        for (int i = 0; i < n; ++i) {
            Item& it = items[size_t(i)];
            const int cl = gridLeft + i % cols * cellW, ct = gridTop + i / cols * cellH;
            if (it.building) put(it, cl, ct);
            else put(it, cl + (cellW - gap - it.w()) / 2, ct + (cellH - gap - it.h()) / 2);
        }
        break;
    }
    case Arrange::Circle: {
        // Round the middle of the selection, in the order that the units have round it now, the first one at its
        // angle. The circle is as large as the units are far from the middle, and large enough for them side by side.
        double cx = (all.left + all.right) / 2.0, cy = (all.top + all.bottom) / 2.0;
        const double turn = 6.283185307179586;
        const auto angle = [&](const Item& it) { return std::atan2(it.cy() - cy, it.cx() - cx); };
        std::stable_sort(items.begin(), items.end(), [&](const Item& a, const Item& b) { return angle(a) < angle(b); });
        double far = 0, round = 0;
        for (const Item& it : items) far += std::hypot(it.cx() - cx, it.cy() - cy), round += std::hypot(double(it.w()), double(it.h())) + gap;
        const double radius = std::max(far / n, round / turn), from = angle(items[0]);
        // (a circle that goes off the map: its middle moves in, when the map is large enough for it)
        const double halfW = radius + maxW / 2.0, halfH = radius + maxH / 2.0;
        if (2 * halfW <= mapW) cx = std::clamp(cx, halfW, mapW - halfW);
        if (2 * halfH <= mapH) cy = std::clamp(cy, halfH, mapH - halfH);
        for (int i = 0; i < n; ++i) {
            Item& it = items[size_t(i)];
            const double a = from + turn * i / n;
            put(it, int(std::lround(cx + radius * std::cos(a) - it.w() / 2.0)), int(std::lround(cy + radius * std::sin(a) - it.h() / 2.0)));
        }
        break;
    }
    }
    return moveUnitsTo(sel, std::move(to));
}

void Editor::copySelection() {
    if (selection_.empty()) return;
    clipboard_.clear();
    long long cx = 0, cy = 0;
    for (int i : selection_) cx += units_[i].x, cy += units_[i].y;
    cx /= static_cast<long long>(selection_.size());
    cy /= static_cast<long long>(selection_.size());
    for (int i : selection_) {
        Unit u = units_[i];
        // Store offsets from the group center in x/y (as signed values packed into the fields).
        u.x = uint16_t(int(u.x) - int(cx));
        u.y = uint16_t(int(u.y) - int(cy));
        u.relatedSerial = 0;
        u.links = 0;
        clipboard_.push_back(u);
    }
}

void Editor::turnUnitClipboard(int turn, bool upDown) {
    for (Unit& u : clipboard_) {
        const int x = int16_t(u.x), y = int16_t(u.y);
        const int nx = turn > 0 ? -y : turn < 0 ? y : upDown ? x : -x, ny = turn > 0 ? x : turn < 0 ? -x : upDown ? -y : y;
        u.x = uint16_t(int16_t(nx)), u.y = uint16_t(int16_t(ny));
    }
}

std::vector<Unit> Editor::pasteUnits(int x, int y) const {
    std::vector<Unit> out, copies;
    for (Unit u : clipboard_) {
        const int nx = x + int16_t(u.x), ny = y + int16_t(u.y);
        // (one start location for each player: a start location gets no symmetric copies, as placing gives it none)
        const bool oneStart = u.type == 214 /* Start Location */ && !manyStartLocations_;
        const auto pts = symmetricPoints(nx, ny);
        for (size_t k = 0; k < (oneStart ? std::min<size_t>(pts.size(), 1) : pts.size()); ++k) {
            Unit c = u;
            c.x = uint16_t(std::clamp(pts[k].first, 0, width_ * 32 - 1));
            c.y = uint16_t(std::clamp(pts[k].second, 0, height_ * 32 - 1));
            if (c.type < types_.size()) snapToGrid(types_[c.type], c.x, c.y);
            (k ? copies : out).push_back(c);
        }
    }
    out.insert(out.end(), copies.begin(), copies.end());  // (the copy first: it gets the spots where it fits)
    return out;
}

int Editor::paste(int x, int y, bool correct) {
    if (clipboard_.empty()) return 0;
    std::vector<Unit> all = units_;
    SerialCounter serials(all);
    for (const Unit& u : pasteUnits(x, y)) {
        if (unitLimit_ > 0 && int(all.size()) >= unitLimit_) break;
        std::optional<Unit> f = fitted(u, correct, all);
        if (!f) continue;  // no spot where it fits: not pasted, as placing refuses
        f->serial = serials.take();
        all.push_back(*f);
    }
    const int count = int(all.size() - units_.size());
    if (!count) {
        selection_.clear();
        revision_ = nextRevision();
        return 0;
    }
    beginEdit();
    selection_.clear();
    for (size_t i = units_.size(); i < all.size(); ++i) selection_.push_back(int(i));
    units_ = std::move(all);
    writeUnits(chk_, units_);
    commit(false);
    return count;
}

}  // namespace stardraft
