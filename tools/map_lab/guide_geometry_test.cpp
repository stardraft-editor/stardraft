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
            check(scene.rects.size()==size_t(1+2*bool(mask&1)+8*bool(mask&2)+14*bool(mask&4)));
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
        stardraft::render::Scene scene;
        frame.guides={{100,100,100,100,2,1,7,44},{100,100,0,0,9,0,7,44}};
        maplab::appendMiningGuides(scene,frame,7,0,[](const auto&){});
        check(scene.rects.size()==2); // backplates only; zero-length arrow omitted
        std::cout << "PASS: position/cargo/cycle guides, masks, stage colors and cycle labels\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
