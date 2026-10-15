// The Data form's Requirements section (Units, Upgrades, Tech Data, Orders), portable (core.inc): the names of the
// steps, the four ways a list is set (Default, Always, Disabled, Custom), the indent of a level's steps, and each
// change to a list (a step's opcode, its value, a new or removed step). The section's paint and clicks stay in
// main.cpp (data_extras.inc); they take the results from here, so every program edits a list the same way.

// The four ways, as EUD Editor has them (ReqModule: default, not used, always allow, custom).
enum RequirementWay { kReqDefault, kReqAlways, kReqDisabled, kReqCustom };

std::wstring requirementOpName(const RequirementOp& r) {
    if (r.op < 0x100) return L"Must have";
    if (r.op == 0xFFFF) return L"End of the level's list";
    const RequirementOpDef* d = Requirements::op(r.op);
    return d ? widen(d->name) : L"Step 0x" + std::to_wstring(r.op);
}

// The kind of value a step takes: "units" for a unit the player must have, the opcode's .dat, or nullptr.
const char* requirementOpArgKind(const RequirementOp& r) {
    if (r.op < 0x100) return "units";
    const RequirementOpDef* d = Requirements::op(r.op);
    return d ? d->arg : nullptr;
}

// The value a step shows: the unit (a unit step) or its argument.
uint16_t requirementOpValue(const RequirementOp& r) { return r.op < 0x100 ? r.op : r.arg; }

// The way a list is set to: Default (not changed), Disabled (no list), Always (an empty list), Custom (its steps).
int requirementWay(bool changed, const RequirementList& list) {
    return !changed ? kReqDefault : !list.used ? kReqDisabled : list.ops.empty() ? kReqAlways : kReqCustom;
}

// The list a clicked way gives, from the game's list `stock` and the list's current way `mode`. False when the click
// changes nothing (Custom while the list is Custom already).
bool requirementListForWay(int way, int mode, const RequirementList& stock, RequirementList& out) {
    if (way == kReqDefault) {
        out = stock;
        return true;
    }
    if (way == kReqAlways) {
        out = RequirementList{true, {}};
        return true;
    }
    if (way == kReqDisabled) {
        out = RequirementList{false, {}};
        return true;
    }
    if (mode == kReqCustom) return false;
    // Custom: from the game's list (an empty one when it has none)
    out = stock.used && !stock.ops.empty() ? stock : RequirementList{true, {}};
    return true;
}

// Each step's level: the steps after a level's start are indented; a level's end (0xFFFF) returns to the top.
std::vector<int> requirementLevels(const std::vector<RequirementOp>& ops) {
    std::vector<int> levels(ops.size());
    int level = 0;
    for (size_t i = 0; i < ops.size(); ++i) {
        if (ops[i].op == 0xFFFF) level = 0;
        levels[i] = level;
        if (ops[i].op == 0xFF1F || ops[i].op == 0xFF20 || ops[i].op == 0xFF21) level = 1;  // a level's list follows
    }
    return levels;
}

// The opcode a step picker starts from: a unit step starts at "must have" with no value.
uint16_t requirementPickerCurrent(uint16_t op) { return op < 0x100 ? 0xFF03 : op; }

// Step `i` takes the opcode `n`. A value of the same kind is kept.
RequirementList requirementWithOp(RequirementList list, size_t i, RequirementOp n) {
    const RequirementOp old = list.ops[i];
    const char* nk = requirementOpArgKind(n);
    const char* ok = requirementOpArgKind(old);
    if (nk && ok && std::string(nk) == ok) n.arg = old.op < 0x100 ? old.op : old.arg;
    list.ops[i] = n;
    return list;
}

// Step `i` takes the value `picked` (a "must have" value when it was a unit step).
RequirementList requirementWithArg(RequirementList list, size_t i, uint16_t picked) {
    RequirementOp& n = list.ops[i];
    if (n.op < 0x100) n.op = 0xFF03;  // "must have" with its value
    n.arg = picked;
    return list;
}

RequirementList requirementWithoutStep(RequirementList list, size_t i) {
    list.ops.erase(list.ops.begin() + long(i));
    return list;
}

RequirementList requirementWithNewStep(RequirementList list, RequirementOp n) {
    list.ops.push_back(n);
    return list;
}
