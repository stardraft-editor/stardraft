#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace stardraft {

using Bytes = std::vector<uint8_t>;

// 4-character section name, e.g. "VER ", "UNIT". Stored exactly as in the file.
struct SectionName {
    std::array<char, 4> c{};

    constexpr SectionName() = default;
    constexpr SectionName(const char (&s)[5]) : c{s[0], s[1], s[2], s[3]} {}

    std::string str() const { return std::string(c.data(), 4); }
    friend bool operator==(const SectionName&, const SectionName&) = default;
};

struct Section {
    SectionName name;
    Bytes data;
};

// Non-fatal oddities found while parsing. StarCraft tolerates all of these.
struct ParseReport {
    bool truncatedSection = false;  // last section claims more bytes than remain
    bool trailingBytes = false;     // < 8 bytes left over after the last section
    bool negativeSize = false;      // a section used a negative size ("section stacking")
    bool loopDetected = false;      // a negative size jumped back to an already visited offset
};

// A scenario.chk file: an ordered list of raw sections.
//
// Sections are kept in file order, duplicates included, so that unmodified data
// re-serializes to identical bytes. Duplicate/split sections follow StarCraft's
// rules: for most sections the last one wins, while UNIT, THG2, TRIG and MBRF
// accumulate (see effective()).
class Chk {
public:
    std::vector<Section> sections;

    static Chk parse(std::span<const uint8_t> bytes, ParseReport* report = nullptr);
    Bytes serialize() const;

    // Last section with this name, or nullptr.
    const Section* find(SectionName name) const;
    Section* find(SectionName name);

    // Data StarCraft would see for this section name: concatenation for
    // UNIT/THG2/TRIG/MBRF, otherwise the last occurrence. Empty if absent.
    Bytes effective(SectionName name) const;

    // Replace all occurrences of `name` with a single section holding `data`,
    // at the position of the first occurrence (or appended if absent).
    void set(SectionName name, Bytes data);
    void remove(SectionName name);
    bool has(SectionName name) const { return find(name) != nullptr; }
};

}  // namespace stardraft
