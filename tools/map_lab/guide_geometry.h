// SPDX-License-Identifier: MIT
#pragma once
#include "mining.h"
#include "stardraft/scene.h"
#include <algorithm>
#include <cmath>

namespace maplab {
// Retention concerns completed measurements. A runner still crossing the map
// must keep its whole trail, regardless of how many runners launched after it.
inline bool showRushTrail(const RushPath &path, const RushFrame &frame, int keep) {
    if (!path.finished) return true;
    return std::count_if(frame.paths.begin(), frame.paths.end(), [&](const auto &other) {
        return other.finished && other.id > path.id;
    }) < std::clamp(keep, 1, 50);
}
inline std::uint32_t miningGuideColor(int worker) {
    constexpr std::uint32_t colors[]{0xFFB54CFF,0xFFEF75FF,0xFF91D0FF,
                                    0xC7F56BFF,0xFFA88BFF,0xE5ABFFFF};
    return colors[unsigned(worker)%6];
}
inline std::uint32_t workerGuideColor(int worker,int saturation=110,int alpha=150) {
    const float hue = float((worker * 137) % 360) / 60.f;
    const float c = (245.f / 255.f) * (float(saturation) / 255.f);
    const float x = c * (1 - std::abs(std::fmod(hue, 2.f) - 1));
    const float m = (245.f / 255.f) - c;
    const int sector = int(hue);
    const float r = sector == 0 || sector == 5 ? c : sector == 1 || sector == 4 ? x : 0;
    const float g = sector == 1 || sector == 2 ? c : sector == 0 || sector == 3 ? x : 0;
    const float b = sector == 3 || sector == 4 ? c : sector == 2 || sector == 5 ? x : 0;
    return (std::uint32_t((r+m)*255) << 24) | (std::uint32_t((g+m)*255) << 16) |
           (std::uint32_t((b+m)*255) << 8) | std::uint32_t(alpha);
}
// Matches the source editor's display contract: ghosts for all three kinds,
// return markers for cargo, per-worker colors and numbered arrows for cycles.
// Ghost rendering is supplied by the editor so its own unit image data is used.
template <class Ghost>
void appendMiningGuides(stardraft::render::Scene &scene,
                        const MiningFrame &frame, unsigned mask, float zoom,
                        Ghost ghost, bool smoothLabels = false) {
  zoom = std::max(.01f, zoom);
  auto line = [&](float ax, float ay, float bx, float by, std::uint32_t color) {
    stardraft::render::RectDraw d;
    d.x0 = ax;
    d.y0 = ay;
    d.x1 = bx;
    d.y1 = by;
    d.line = true;
    d.rgba = color;
    d.thickness = 4;
    d.rgba = 0x101014F0;
    scene.rects.push_back(d);
    d.thickness = 2;
    d.rgba = color;
    scene.rects.push_back(d);
  };
  auto label = [&](float x, float y, const std::string &text,
                   std::uint32_t color) {
    if (smoothLabels) {
      scene.texts.push_back({x, y, text, color, true, 1.5f, true});
      return;
    }
    stardraft::render::RectDraw plate;
    // Fixed screen-size backplate keeps text legible on bright minerals,
    // snow, terrain and workers at any map zoom.
    plate.x0 = x - 2 / zoom;
    plate.y0 = y - 2 / zoom;
    plate.x1 = x + (text.size() * 10.f + 2) / zoom;
    plate.y1 = y + 14 / zoom;
    plate.fill = true;
    plate.rgba = 0x101014E8;
    scene.rects.push_back(plate);
    scene.texts.push_back({x, y, text, color});
  };
  for (const auto &guide : frame.guides) {
    if (guide.kind < 0 || guide.kind > 2 || !(mask & (1u << guide.kind)))
      continue;
    ghost(guide);
    const auto color = miningGuideColor(guide.worker);
    const float x = float(guide.x), y = float(guide.y);
    if (guide.kind == 0) {
      stardraft::render::RectDraw ring;
      ring.x0 = x - 10 / zoom;
      ring.y0 = y - 10 / zoom;
      ring.x1 = x + 10 / zoom;
      ring.y1 = y + 10 / zoom;
      ring.rgba = 0x101014F0;
      ring.thickness = 4;
      scene.rects.push_back(ring);
      ring.rgba = 0xFFDA65FF;
      ring.thickness = 2;
      scene.rects.push_back(ring);
    }
    if (guide.kind == 1) {
      // The map's text renderer is ASCII. Draw the return glyph as lines.
      const float k = 1 / zoom;
      line(x + 7 * k, y - 19 * k, x + 7 * k, y - 13 * k, 0xFFDA65FF);
      line(x + 7 * k, y - 13 * k, x - 6 * k, y - 13 * k, 0xFFDA65FF);
      line(x - 6 * k, y - 13 * k, x - 2 * k, y - 17 * k, 0xFFDA65FF);
      line(x - 6 * k, y - 13 * k, x - 2 * k, y - 9 * k, 0xFFDA65FF);
    }
    if (guide.kind == 2) {
      float endX = float(guide.targetX), endY = float(guide.targetY);
      if (guide.stage == 1) {
        const auto second = std::find_if(frame.guides.begin(), frame.guides.end(),
            [&](const auto &g) {
              return g.kind == 2 && g.stage == 2 && g.worker == guide.worker;
            });
        if (second != frame.guides.end()) {
          // Command 2 interrupts command 1 here. Show its untravelled
          // continuation as screen-sized dots, without another arrowhead.
          endX = float(second->x);
          endY = float(second->y);
          const float dx = float(guide.targetX) - endX;
          const float dy = float(guide.targetY) - endY;
          const float length = std::hypot(dx, dy);
          if (length * zoom > 1) {
            const float ux = dx / length, uy = dy / length;
            const float stop = std::max(0.f, length - 9 / zoom);
            for (float d = 3 / zoom; d < stop; d += 6 / zoom) {
              const float tail = std::min(d + 1.5f / zoom, stop);
              line(endX + ux * d, endY + uy * d,
                   endX + ux * tail, endY + uy * tail, color);
            }
          }
        }
      }
      const float dx = endX - x, dy = endY - y;
      const float length = std::hypot(dx, dy);
      if (length * zoom > 1) {
        const float ux = dx / length, uy = dy / length;
        const float inset = std::min(9 / zoom, length * .25f);
        const float tx = endX - ux * inset;
        const float ty = endY - uy * inset;
        line(x, y, tx, ty, color);
        line(tx, ty, tx - ux * 7 / zoom - uy * 4 / zoom,
             ty - uy * 7 / zoom + ux * 4 / zoom, color);
        line(tx, ty, tx - ux * 7 / zoom + uy * 4 / zoom,
             ty - uy * 7 / zoom - ux * 4 / zoom, color);
      }
      label(x, y - 14 / zoom, std::to_string(guide.stage), color);
    }
  }
  for (const auto &patch : frame.patches)
    if (patch.lastCycle)
      label(float(patch.x), float(patch.y), std::to_string(patch.lastCycle),
            0xFFF2B8FF);
}
}
