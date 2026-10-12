#pragma once

// Map string table (STR / STRx sections). String ids are 1-based; 0 means "no string".

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "stardraft/chk.h"

namespace stardraft {

class StringTable {
public:
    // Reads STRx if present, else STR. Text bytes are kept as-is (encoding is unspecified).
    static StringTable read(const Chk& chk);
    // Writes STR when everything fits its 16-bit offsets, else STRx; removes the other section.
    void write(Chk& chk) const;

    size_t size() const { return strings_.size(); }
    const std::string& get(uint32_t id) const;
    void set(uint32_t id, std::string text);
    // Store `text` in the first empty slot (or a new one); returns its id.
    uint32_t add(std::string_view text);

private:
    std::vector<std::string> strings_;  // strings_[i] is string id i + 1
    bool extended_ = false;
};

}  // namespace stardraft
