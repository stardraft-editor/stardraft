// SPDX-License-Identifier: MIT
#pragma once
#include "mining.h"
#include "stardraft/scene.h"
#include <algorithm>
#include <cmath>

namespace maplab {
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
template<class Ghost>
void appendMiningGuides(stardraft::render::Scene& scene, const MiningFrame& frame,
                        unsigned mask, float zoom, Ghost ghost) {
    zoom = std::max(.01f, zoom);
    auto line = [&](float ax, float ay, float bx, float by, std::uint32_t color) {
        stardraft::render::RectDraw d;
        d.x0=ax; d.y0=ay; d.x1=bx; d.y1=by; d.line=true; d.rgba=color;
        scene.rects.push_back(d);
    };
    for (const auto& guide : frame.guides) {
        if (guide.kind < 0 || guide.kind > 2 || !(mask & (1u << guide.kind))) continue;
        ghost(guide);
        const auto color = workerGuideColor(guide.worker);
        const float x = float(guide.x), y = float(guide.y);
        if (guide.kind == 1) {
            // The map's text renderer is ASCII. Draw the return glyph as lines.
            const float k=1/zoom;
            line(x+7*k,y-19*k,x+7*k,y-13*k,0x8ED7F6FF);
            line(x+7*k,y-13*k,x-6*k,y-13*k,0x8ED7F6FF);
            line(x-6*k,y-13*k,x-2*k,y-17*k,0x8ED7F6FF);
            line(x-6*k,y-13*k,x-2*k,y-9*k,0x8ED7F6FF);
        }
        if (guide.kind == 2) {
            const float dx=float(guide.targetX)-x, dy=float(guide.targetY)-y;
            const float length=std::hypot(dx,dy);
            if (length * zoom > 1) {
                const float ux=dx/length, uy=dy/length;
                const float tx=float(guide.targetX)-ux*9/zoom;
                const float ty=float(guide.targetY)-uy*9/zoom;
                line(x,y,tx,ty,color);
                line(tx,ty,tx-ux*7/zoom-uy*4/zoom,ty-uy*7/zoom+ux*4/zoom,color);
                line(tx,ty,tx-ux*7/zoom+uy*4/zoom,ty-uy*7/zoom-ux*4/zoom,color);
            }
            scene.texts.push_back({x,y-14/zoom,std::to_string(guide.stage),color});
        }
    }
    for (const auto& patch : frame.patches)
        if (patch.lastCycle)
            scene.texts.push_back({float(patch.x),float(patch.y),std::to_string(patch.lastCycle),0x8ED7F6FF});
}
}
