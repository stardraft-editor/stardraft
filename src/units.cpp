#include "stardraft/units.h"

#include <algorithm>
#include <cctype>

#include "byteio.h"
#include "stardraft/game_data.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"

namespace stardraft {

// --- UNIT section ------------------------------------------------------------------------------

Unit Unit::decode(std::span<const uint8_t> b) {
    Unit u;
    u.serial = readU32(b, 0);
    u.x = readU16(b, 4);
    u.y = readU16(b, 6);
    u.type = readU16(b, 8);
    u.links = readU16(b, 10);
    u.validState = readU16(b, 12);
    u.validProps = readU16(b, 14);
    u.owner = b[16];
    u.hitPoints = b[17];
    u.shields = b[18];
    u.energy = b[19];
    u.resources = readU32(b, 20);
    u.hangar = readU16(b, 24);
    u.state = readU16(b, 26);
    u.unused = readU32(b, 28);
    u.relatedSerial = readU32(b, 32);
    return u;
}

void Unit::encode(Bytes& out) const {
    appendU32(out, serial);
    appendU16(out, x);
    appendU16(out, y);
    appendU16(out, type);
    appendU16(out, links);
    appendU16(out, validState);
    appendU16(out, validProps);
    out.push_back(owner);
    out.push_back(hitPoints);
    out.push_back(shields);
    out.push_back(energy);
    appendU32(out, resources);
    appendU16(out, hangar);
    appendU16(out, state);
    appendU32(out, unused);
    appendU32(out, relatedSerial);
}

std::vector<Unit> readUnits(const Chk& chk) {
    std::vector<Unit> units;
    for (const auto& s : chk.sections) {
        if (s.name != SectionName("UNIT")) continue;
        // StarCraft rejects a UNIT section that is not a multiple of 36 bytes.
        if (s.data.size() % Unit::kSize) continue;
        for (size_t o = 0; o + Unit::kSize <= s.data.size(); o += Unit::kSize)
            units.push_back(Unit::decode(std::span(s.data).subspan(o, Unit::kSize)));
    }
    return units;
}

void writeUnits(Chk& chk, const std::vector<Unit>& units) {
    Bytes data;
    data.reserve(units.size() * Unit::kSize);
    for (const auto& u : units) u.encode(data);
    chk.set("UNIT", std::move(data));
}

uint32_t nextSerial(const std::vector<Unit>& units) {
    // The answer is at most units.size(), so larger serials do not matter.
    std::vector<bool> used(units.size() + 1, false);
    for (const auto& u : units)
        if (u.serial < used.size()) used[u.serial] = true;
    uint32_t s = 0;
    while (used[s]) ++s;
    return s;
}

SerialCounter::SerialCounter(const std::vector<Unit>& units) : used_(units.size() + 1, false) {
    for (const auto& u : units)
        if (u.serial < used_.size()) used_[u.serial] = true;
        else high_.push_back(u.serial);
    std::sort(high_.begin(), high_.end());
}

uint32_t SerialCounter::take() {
    while (next_ < used_.size() ? bool(used_[next_]) : std::binary_search(high_.begin(), high_.end(), next_)) ++next_;
    return next_++;  // the serials given are not used again
}

// --- units.dat / stat_txt.tbl --------------------------------------------------------------------

namespace {

// Offsets of the arrays in arr\units.dat (19876 bytes, 228 units).
constexpr size_t kUnitsDatSize = 19876;
constexpr size_t kAttackUnitOrder = 5436, kShieldEnable = 2472, kShieldAmount = 2700, kHitPoints = 3156, kElevation = 4068,
                 kSpecialFlags = 7032, kPlacementBox = 11284, kUnitSize = 12580, kGroupFlags = 16684,
                 kAvailability = 19420;

std::vector<std::string> readTbl(const Bytes& tbl) {
    std::vector<std::string> out;
    if (tbl.size() < 2) return out;
    uint16_t n = readU16(tbl, 0);
    for (uint16_t i = 0; i < n && 2 + 2 * i + 1 < tbl.size(); ++i) {
        size_t o = readU16(tbl, 2 + 2 * i), e = o;
        while (e < tbl.size() && tbl[e]) ++e;
        out.emplace_back(tbl.begin() + std::min(o, tbl.size()), tbl.begin() + e);
    }
    return out;
}

bool iequals(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

}  // namespace

bool UnitTypes::load(const GameData& data, std::string& error) {
    Bytes dat, tbl;
    if (!data.readFile("arr\\units.dat", dat, error)) return false;
    if (dat.size() != kUnitsDatSize) {
        error = "unexpected units.dat size " + std::to_string(dat.size());
        return false;
    }
    std::vector<std::string> strings;
    if (data.readFile("rez\\stat_txt.tbl", tbl, error)) strings = readTbl(tbl);
    error.clear();
    statTxt_ = strings;

    types_.resize(kCount);
    for (uint16_t i = 0; i < kCount; ++i) {
        UnitType& t = types_[i];
        t.id = i;
        t.name = i < strings.size() ? strings[i] : "Unit " + std::to_string(i);
        if (2 + 2 * size_t(i) + 1 < tbl.size()) {
            // The two strings after the name, each NUL-terminated.
            size_t o = readU16(tbl, 2 + 2 * size_t(i)) + t.name.size() + 1;
            auto next = [&]() {
                size_t e = o;
                while (e < tbl.size() && tbl[e]) ++e;
                std::string part(tbl.begin() + long(std::min(o, tbl.size())), tbl.begin() + long(e));
                o = e + 1;
                return part == "*" ? std::string() : part;
            };
            t.subtitle = next();
            t.editorGroup = next();
        }
        t.hasShields = dat[kShieldEnable + i] != 0;
        t.shields = readU16(dat, kShieldAmount + 2 * i);
        t.hitPoints = readU32(dat, kHitPoints + 4 * i);
        t.elevation = dat[kElevation + i];
        t.flags = readU32(dat, kSpecialFlags + 4 * i);
        t.placementWidth = readU16(dat, kPlacementBox + 4 * i);
        t.placementHeight = readU16(dat, kPlacementBox + 4 * i + 2);
        t.sizeLeft = readU16(dat, kUnitSize + 8 * i);
        t.sizeUp = readU16(dat, kUnitSize + 8 * i + 2);
        t.sizeRight = readU16(dat, kUnitSize + 8 * i + 4);
        t.sizeDown = readU16(dat, kUnitSize + 8 * i + 6);
        t.groupFlags = dat[kGroupFlags + i];
        t.availability = readU16(dat, kAvailability + 2 * i);
        t.attackUnitOrder = dat[kAttackUnitOrder + i];
    }
    return true;
}

const std::string& UnitTypes::statText(uint32_t index) const {
    static const std::string empty;
    return index < statTxt_.size() ? statTxt_[index] : empty;
}

std::optional<uint16_t> UnitTypes::findByName(std::string_view name) const {
    for (const auto& t : types_)
        if (iequals(t.name, name)) return t.id;
    return std::nullopt;
}

// --- Defaults ------------------------------------------------------------------------------------

Unit makeUnit(const UnitType& type, uint8_t owner, uint16_t x, uint16_t y) {
    // orders.dat ids of the Carrier and Reaver attack orders; units using them carry a hangar.
    constexpr uint8_t kCarrierAttack = 53, kReaverAttack = 59;

    Unit u;
    u.type = type.id;
    u.owner = owner;
    u.x = x;
    u.y = y;
    // Types not in the editor palette (e.g. Start Location) have no editable properties.
    if (!(type.availability & UnitAvail::Palette)) return u;

    u.validProps = UnitProp::HitPoints;
    if (type.availability & UnitAvail::NonNeutral) u.validProps |= UnitProp::Owner;
    if (type.hasShields) u.validProps |= UnitProp::Shields;
    if (type.is(UnitFlag::Spellcaster)) u.validProps |= UnitProp::Energy;
    if (type.is(UnitFlag::ResourceContainer)) u.validProps |= UnitProp::Resources;
    if (type.attackUnitOrder == kCarrierAttack || type.attackUnitOrder == kReaverAttack) u.validProps |= UnitProp::Hangar;

    u.validState = UnitState::Invincible;
    if (!type.is(UnitFlag::Building)) u.validState |= UnitState::Hallucinated;
    if (type.is(UnitFlag::Cloakable)) u.validState |= UnitState::Cloaked;
    if (type.is(UnitFlag::Burrowable)) u.validState |= UnitState::Burrowed;
    if (type.is(UnitFlag::FlyingBuilding)) u.validState |= UnitState::InTransit;

    // StarEdit defaults: mineral fields (64x32 box) 1500, geysers and gas buildings 5000.
    if (type.is(UnitFlag::ResourceContainer)) u.resources = type.placementHeight <= 32 ? 1500 : 5000;
    return u;
}

void snapToGrid(const UnitType& type, uint16_t& x, uint16_t& y) {
    if (!type.is(UnitFlag::Building)) return;
    auto snap = [](int c, int size) {
        int left = ((c - size / 2) + 16) / 32 * 32;  // nearest tile boundary for the box's left/top
        return uint16_t(std::max(0, left) + size / 2);
    };
    x = snap(x, type.placementWidth);
    y = snap(y, type.placementHeight);
}

// --- Placement -----------------------------------------------------------------------------------

const char* placementIssueName(PlacementIssue issue) {
    switch (issue) {
    case PlacementIssue::OutOfBounds: return "out_of_bounds";
    case PlacementIssue::BadType: return "bad_type";
    case PlacementIssue::Unwalkable: return "unwalkable";
    case PlacementIssue::Unbuildable: return "unbuildable";
    case PlacementIssue::OverlapsBuilding: return "overlaps_building";
    case PlacementIssue::NearResources: return "near_resources";
    case PlacementIssue::OverlapsUnit: return "overlaps_unit";
    case PlacementIssue::UnitLimit: return "unit_limit";
    }
    return "unknown";
}

namespace {

struct Box {
    int left, top, right, bottom;  // pixels, right/bottom exclusive
    bool overlaps(const Box& o) const { return left < o.right && o.left < right && top < o.bottom && o.top < bottom; }
};

Box placementBox(const UnitType& t, const Unit& u) {
    int l = u.x - t.placementWidth / 2, tp = u.y - t.placementHeight / 2;
    return {l, tp, l + t.placementWidth, tp + t.placementHeight};
}

Box collisionBox(const UnitType& t, const Unit& u) {
    return {u.x - t.sizeLeft, u.y - t.sizeUp, u.x + t.sizeRight + 1, u.y + t.sizeDown + 1};
}

// An air unit: a flyer, or a Terran building that is lifted off (in transit). Air and ground do not block each other.
bool isAir(const UnitType& t, const Unit& u) {
    if (!t.is(UnitFlag::Building)) return t.is(UnitFlag::Flyer);
    return t.is(UnitFlag::FlyingBuilding) && (u.state & UnitState::InTransit);
}

// The placement rules. With `first`, it stops at the first issue (for a yes-or-no answer).
void collectIssues(const PlacementContext& ctx, const Unit& unit, int self, bool first, std::vector<PlacementIssue>& issues) {
    if (!ctx.types || unit.type >= ctx.types->size()) {
        issues.push_back(PlacementIssue::BadType);
        return;
    }
    const UnitType& t = (*ctx.types)[unit.type];
    const bool flyer = isAir(t, unit);
    const bool building = t.is(UnitFlag::Building) && !flyer;  // a lifted building is an air unit
    const Box box = building ? placementBox(t, unit) : collisionBox(t, unit);
    const int mapPxW = ctx.mapWidth * 32, mapPxH = ctx.mapHeight * 32;

    if (box.left < 0 || box.top < 0 || box.right > mapPxW || box.bottom > mapPxH)
        issues.push_back(PlacementIssue::OutOfBounds);
    if (first && !issues.empty()) return;

    if (ctx.terrain && ctx.tileset && !flyer) {
        const Box c{std::max(box.left, 0), std::max(box.top, 0), std::min(box.right, mapPxW), std::min(box.bottom, mapPxH)};
        bool walkable = true, buildable = true, stop = false;
        for (int py = c.top / 8; !stop && py * 8 < c.bottom; ++py)
            for (int px = c.left / 8; !stop && px * 8 < c.right; ++px) {
                uint16_t tile = ctx.terrain->tiles[size_t(py / 4) * ctx.terrain->width + px / 4];
                int mega = ctx.tileset->megatileFor(tile);
                const TileGroup* g = ctx.tileset->groupFor(tile);
                if (mega < 0 || size_t(mega) >= ctx.tileset->minitileFlags.size() || !g) {
                    walkable = buildable = false;
                    stop = first;
                    continue;
                }
                if (!(ctx.tileset->minitileFlags[mega][(py % 4) * 4 + px % 4] & Tileset::kWalkable)) walkable = false;
                if (g->buildability & Tileset::kUnbuildable) buildable = false;
                stop = first && !(building ? buildable : walkable);
            }
        if (building && !buildable) issues.push_back(PlacementIssue::Unbuildable);
        if (!building && !walkable) issues.push_back(PlacementIssue::Unwalkable);
        if (first && !issues.empty()) return;
    }

    if (ctx.units) {
        for (size_t i = 0; i < ctx.units->size(); ++i) {
            if (int(i) == self) continue;
            const Unit& o = (*ctx.units)[i];
            if (o.type >= ctx.types->size()) continue;
            const UnitType& ot = (*ctx.types)[o.type];
            const bool otherFlyer = isAir(ot, o);
            const bool otherBuilding = ot.is(UnitFlag::Building) && !otherFlyer;
            if (ctx.stacking && !(building && otherBuilding)) continue;
            if (flyer && otherFlyer) {
                // Air units keep out of each other unless stacking is allowed.
                if (box.overlaps(collisionBox(ot, o))) issues.push_back(PlacementIssue::OverlapsUnit);
            } else if (flyer || otherFlyer) {
                // Air and ground units do not block each other.
            } else if (building && !otherBuilding) {
                // Buildings may not be placed on top of ground units.
                if (box.overlaps(collisionBox(ot, o))) issues.push_back(PlacementIssue::OverlapsUnit);
            } else if (building && otherBuilding) {
                Box ob = placementBox(ot, o);
                if (box.overlaps(ob)) issues.push_back(PlacementIssue::OverlapsBuilding);
                // Town halls must keep 3 tiles between their box and resources.
                if (t.is(UnitFlag::ResourceDepot) && ot.is(UnitFlag::ResourceContainer)) {
                    Box grown{ob.left - 96, ob.top - 96, ob.right + 96, ob.bottom + 96};
                    if (box.overlaps(grown)) issues.push_back(PlacementIssue::NearResources);
                }
            } else if (!building) {
                // Ground units may not stand inside other ground units or buildings.
                Box ob = otherBuilding ? placementBox(ot, o) : collisionBox(ot, o);
                if (box.overlaps(ob)) issues.push_back(PlacementIssue::OverlapsUnit);
            }
            if (first && !issues.empty()) return;
        }
        std::sort(issues.begin(), issues.end());
        issues.erase(std::unique(issues.begin(), issues.end()), issues.end());
    }
}

}  // namespace

std::vector<PlacementIssue> checkPlacement(const PlacementContext& ctx, const Unit& unit, int self) {
    std::vector<PlacementIssue> issues;
    collectIssues(ctx, unit, self, false, issues);
    return issues;
}

bool placementOk(const PlacementContext& ctx, const Unit& unit, int self) {
    std::vector<PlacementIssue> issues;
    collectIssues(ctx, unit, self, true, issues);
    return issues.empty();
}

PlaceResult placeUnit(std::vector<Unit>& units, const PlacementContext& ctx, uint16_t type, uint8_t owner,
                      uint16_t x, uint16_t y, bool force) {
    PlaceResult r;
    if (!ctx.types || type >= ctx.types->size()) {
        r.issues.push_back(PlacementIssue::BadType);
        return r;
    }
    const UnitType& t = (*ctx.types)[type];
    snapToGrid(t, x, y);
    r.unit = makeUnit(t, owner, x, y);
    r.unit.serial = nextSerial(units);
    r.issues = checkPlacement(ctx, r.unit);
    if (!r.issues.empty() && !force) return r;
    units.push_back(r.unit);
    r.index = int(units.size()) - 1;
    r.placed = true;
    return r;
}

}  // namespace stardraft
