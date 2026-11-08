// SPDX-License-Identifier: MIT
#include "guide_geometry.h"
#include <iostream>
#include <stdexcept>
void check(bool ok) { if (!ok) throw std::runtime_error("Mining guide display contract failed"); }
int main() {
    try {
        maplab::MiningFrame frame;
        frame.guides={{100,100,200,200,0,0,7,42},{110,100,0,0,1,0,7,43},
                      {120,100,200,200,2,1,7,44},{130,100,300,200,2,2,7,44}};
        frame.patches={{200,200,123,400}};
        for (unsigned mask=0;mask<8;++mask) {
            stardraft::render::Scene scene;
            int ghosts=0;
            maplab::appendMiningGuides(scene,frame,mask,1.25f,[&](const auto&){++ghosts;});
            check(ghosts==int(bool(mask&1))+int(bool(mask&2))+2*int(bool(mask&4)));
            const size_t base=1+2*bool(mask&1)+8*bool(mask&2);
            check(mask&4 ? scene.rects.size()>base+14 : scene.rects.size()==base);
            check(scene.texts.size()==size_t(1+2*bool(mask&4)));
            check(scene.texts.back().text=="123");
            check(scene.texts.back().rgba==0xFFF2B8FF);
            check(scene.rects.back().fill && scene.rects.back().rgba==0x101014E8);
            if(mask&4) {
                check(scene.texts[0].text=="1" && scene.texts[1].text=="2");
                check(scene.texts[0].rgba==scene.texts[1].rgba);
            }
            for(const auto& line:scene.rects) check(std::isfinite(line.x0) && std::isfinite(line.y1));
        }
        {
          stardraft::render::Scene smooth;
          maplab::appendMiningGuides(
              smooth, frame, 0, 1.25f, [](const auto &) {}, true);
          check(smooth.rects.empty() && smooth.texts.size() == 1);
          check(smooth.texts[0].smooth && smooth.texts[0].text == "123");
        }
        stardraft::render::Scene scene;
        frame.guides={{100,100,100,100,2,1,7,44},{100,100,0,0,9,0,7,44}};
        maplab::appendMiningGuides(scene,frame,7,0,[](const auto&){});
        check(scene.rects.size()==2); // backplates only; zero-length arrow omitted
        // Command 2 cancels the remainder of path 1. Check gaps, screen-sized
        // dots, matching worker identity and reversed directions at both zooms.
        for(float zoom : {.5f,2.f}) for(int sign : {-1,1}) {
            frame.patches.clear();
            frame.guides={{0,0,sign*200,0,2,1,7,44},
                          {sign*50,0,sign*50,100,2,2,7,44},
                          {900,900,950,950,2,2,7,45}};
            stardraft::render::Scene split;
            maplab::appendMiningGuides(split,frame,4,zoom,[](const auto&){},true);
            int dots=0;
            float previousEnd=50;
            for(const auto &r:split.rects) {
                if(r.rgba!=maplab::miningGuideColor(44) || r.y0!=0 || r.y1!=0)continue;
                const float a=r.x0*sign,b=r.x1*sign;
                if(a<=50) {check(b<50);continue;}
                check(a>previousEnd && b<=200-9/zoom+.001f);
                check(std::abs((b-a)*zoom-1.5f)<.001f || b==200-9/zoom);
                previousEnd=b;++dots;
            }
            check(dots>5);
            // No command 2 yet: path 1 stays a solid arrow to its target.
            frame.guides.resize(1);
            stardraft::render::Scene first;
            maplab::appendMiningGuides(first,frame,4,zoom,[](const auto&){},true);
            check(first.rects.size()==6);
        }
        // A 60-second route with launches every two seconds used to lose its
        // leading trail at 20 seconds. Active IDs older than the retention
        // window remain visible and hoverable, including with Keep set to 1.
        maplab::RushFrame rush;
        for(int id=1;id<=32;++id) {
            maplab::RushPath path;path.id=id;path.finished=id>25;
            rush.paths.push_back(path);
        }
        for(int keep : {1,3,10}) {
            int completed=0;
            for(const auto &path:rush.paths) {
                const bool visible=maplab::showRushTrail(path,rush,keep);
                if(!path.finished)check(visible);
                else completed+=visible;
            }
            check(completed==std::min(keep,7));
        }
        std::cout << "PASS: position/cargo/cycle guides, masks, stage colors and cycle labels\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
