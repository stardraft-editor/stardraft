#pragma once

// The units of the .dat fields: what a field's raw value counts (frames, 1/256 pixels, tiles, ...) and the value in the
// unit that a user reads more easily (seconds at Fastest, pixels per second, pixels). The Data form shows the converted
// value next to the raw value; the help pages (docs/help/*-dat.md) say the same for each field.
//
// Fastest: the game's fastest speed runs 24 frames each second (a frame is 42 ms).

#include <cstdint>
#include <string>

#include "stardraft/dat_edit.h"

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

// The shown value of a field, which can differ from the stored one: the shown value is raw / scale. Hit points (units.dat,
// no flags) are shown in whole hit points: scale 256 (the stored value is 1/256 of a hit point). 1: shown as stored.
uint32_t datShownScale(const DatField& field);
// The shown value as text: a whole number, or with its part (at most 3 decimals, no zeros at the end).
std::string datShownText(uint32_t scale, uint32_t raw);
// The stored value of a shown value (`shown` in the shown unit, decimals too), rounded to the stored unit and at most
// `maxValue`.
uint32_t datRawFromShown(double shown, uint32_t scale, uint32_t maxValue);

}  // namespace stardraft
