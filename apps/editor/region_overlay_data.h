// SPDX-License-Identifier: MIT
#pragma once
#include <mapeditor/simulation/regions.h>
#include <stardraft/terrain.h>
#include <stardraft/tileset.h>
#include <algorithm>
#include <stdexcept>

namespace regionoverlay {
// A small, owned snapshot. No editor, archive, graphics or game-simulation state
// is accessed by the worker after the snapshot is taken.
struct Input {
    stardraft::TerrainLayer terrain;
    std::vector<stardraft::TileGroup> groups;
    std::vector<std::array<uint16_t,16>> flags;
};
struct Run { int x, y, length, region; };
struct Edge { int x0, y0, x1, y1, region; };
struct Data {
    int width=0, height=0; // minitiles, eight map pixels each
    std::vector<int> cells; // walkable region IDs; -1 is transparent
    std::vector<mapeditor::simulation::Region> regions;
    std::vector<Run> runs;
    std::vector<Edge> edges;
    double milliseconds=0;
    int at(int x,int y) const {
        return x<0||y<0||x>=width||y>=height?-1:cells[size_t(y)*width+x];
    }
};
inline Data build(const Input& input) {
    namespace sim=mapeditor::simulation;
    // Re-encode the terrain fields consumed by the engine from the editor's
    // loaded tileset. This also uses a mod's active data without reopening files.
    std::vector<uint8_t> cv(input.groups.size()*52),vf(input.flags.size()*32);
    auto word=[](auto& bytes,size_t p,uint16_t v){bytes[p]=uint8_t(v);bytes[p+1]=uint8_t(v>>8);};
    for(size_t g=0;g<input.groups.size();++g) {
        word(cv,g*52+2,input.groups[g].buildability);
        for(int i=0;i<16;++i)word(cv,g*52+20+i*2,input.groups[g].megatiles[i]);
    }
    for(size_t g=0;g<input.flags.size();++g)
        for(int i=0;i<16;++i)word(vf,g*32+i*2,input.flags[g][i]);
    auto tiles=input.terrain.tiles;
    // The engine's scenario loader uses group zero for absent tile groups.
    for(auto& tile:tiles)if(size_t((tile/16)&2047)>=input.groups.size())tile=uint16_t(tile%16);
    sim::Terrain terrain(input.terrain.width,input.terrain.height,tiles,cv,vf);
    sim::Regions graph(terrain);
    Data out;out.width=input.terrain.width*4;out.height=input.terrain.height*4;
    out.regions=graph.entries();out.cells=graph.cells();
    for(auto& id:out.cells)if(!out.regions[size_t(id)].walkable())id=-1;
    for(int y=0;y<out.height;++y)
        for(int x=0;x<out.width;) {
            const int id=out.at(x,y),begin=x++;
            while(x<out.width&&out.at(x,y)==id)++x;
            if(id>=0)out.runs.push_back({begin,y,x-begin,id});
        }
    // Each interface is emitted once, including the outside edges of the map.
    // Adjacent segments of one color become one edge, rather than one draw per cell.
    for(int y=0;y<=out.height;++y)
        for(int x=0;x<out.width;) {
            auto owner=[&](int xx){const int a=out.at(xx,y-1),b=out.at(xx,y);return a==b?-1:b>=0?b:a;};
            const int id=owner(x),begin=x++;
            while(x<out.width&&owner(x)==id)++x;
            if(id>=0)out.edges.push_back({begin,y,x,y,id});
        }
    for(int x=0;x<=out.width;++x)
        for(int y=0;y<out.height;) {
            auto owner=[&](int yy){const int a=out.at(x-1,yy),b=out.at(x,yy);return a==b?-1:b>=0?b:a;};
            const int id=owner(y),begin=y++;
            while(y<out.height&&owner(y)==id)++y;
            if(id>=0)out.edges.push_back({x,begin,x,y,id});
        }
    return out;
}
} // namespace regionoverlay
