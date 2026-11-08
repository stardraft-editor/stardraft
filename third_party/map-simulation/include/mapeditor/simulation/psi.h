// SPDX-License-Identifier: MIT
#pragma once
#include "geometry.h"
namespace mapeditor::simulation {
// Pixel coverage reconstructed from direct public-API boundary observations.
inline bool withinPsiField(Point offset) {
  if (offset.x <= -256 || offset.x >= 256 || offset.y <= -160 ||
      offset.y >= 160)
    return false;
  if (offset.y >= -64 && offset.y < 64)
    return true;
  if (offset.y >= -96 && offset.y < 96)
    return offset.x >= -224 && offset.x < 224;
  if (offset.y >= -128 && offset.y < 128)
    return offset.x >= -192 && offset.x < 192;
  return offset.x >= -96 && offset.x < 96;
}
} // namespace mapeditor::simulation
