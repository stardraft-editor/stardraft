#pragma once

// The units of the .dat fields: what a field's raw value counts (frames, 1/256 pixels, tiles, ...) and the value in the
// unit that a user reads more easily (seconds at Fastest, pixels per second, pixels). The Data form shows the converted
// value next to the raw value; the help pages (docs/help/*-dat.md) say the same for each field.
//
// Fastest: the game's fastest speed runs 24 frames each second (a frame is 42 ms).

#include <cstdint>
#include <string>

namespace stardraft {

// The conversion of a field: shown = raw * factor, in `unit` ("" unit: the field has no conversion).
struct DatUnit {
    const char* raw = "";   // what the raw value counts: "frames", "1/256 pixels per frame"
    const char* unit = "";  // the converted value's unit: "s (fastest)", "pixels/s (fastest)"
    double factor = 0;
};

// The conversion of field `field` (its name in the schema: "Hit Points") of `dat` ("units"); unit "" when it has none.
DatUnit datFieldUnit(const std::string& dat, const std::string& field);

// The converted value as the Data form shows it after the raw value: "= 96 pixels/s (fastest)"; "" when the field has
// no conversion. At most 3 decimals, with no zeros at the end.
std::string datConvertedText(const std::string& dat, const std::string& field, uint32_t raw);

}  // namespace stardraft
