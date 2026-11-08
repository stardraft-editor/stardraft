// SPDX-License-Identifier: MIT
#include "../../apps/editor/region_overlay_data.h"
#include <iostream>
#include <set>
#include <tuple>
#include <chrono>
using namespace regionoverlay;
void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
void geometry(const Data& data) {
    std::vector<int> reconstructed(data.cells.size(),-1);
    for(const auto& run:data.runs) {
        require(run.x>=0&&run.y>=0&&run.x+run.length<=data.width&&run.y<data.height,"run out of bounds");
        for(int x=run.x;x<run.x+run.length;++x) {
            auto& value=reconstructed[size_t(run.y)*data.width+x];
            require(value==-1,"overlapping fill");value=run.region;
        }
    }
    require(reconstructed==data.cells,"compressed fills differ from region raster");
    using Segment=std::tuple<int,int,int,int>;
    std::set<Segment> actual,expected;
    for(const auto& edge:data.edges) {
        require(edge.x0==edge.x1||edge.y0==edge.y1,"non-axial border");
        for(int y=edge.y0;y<edge.y1;++y)require(actual.emplace(edge.x0,y,edge.x1,y+1).second,"duplicate vertical border");
        for(int x=edge.x0;x<edge.x1;++x)require(actual.emplace(x,edge.y0,x+1,edge.y1).second,"duplicate horizontal border");
    }
    for(int y=0;y<=data.height;++y)for(int x=0;x<data.width;++x)
        if(data.at(x,y-1)!=data.at(x,y))expected.emplace(x,y,x+1,y);
    for(int x=0;x<=data.width;++x)for(int y=0;y<data.height;++y)
        if(data.at(x-1,y)!=data.at(x,y))expected.emplace(x,y,x,y+1);
    require(actual==expected,"borders omit or duplicate region interfaces");
}
int main() {try {
    Input input;input.terrain.width=32;input.terrain.height=24;
    input.terrain.tiles.resize(32*24);input.groups.resize(2);input.flags.resize(4);
    input.groups[0].megatiles[0]=0;input.groups[0].megatiles[1]=1;
    input.groups[0].megatiles[2]=2;input.groups[0].megatiles[3]=3;
    input.flags[0].fill(1);input.flags[1].fill(0);input.flags[2].fill(5);input.flags[3].fill(1);
    for(int y=0;y<4;++y)input.flags[3][y*4]=0;
    for(int y=0;y<24;++y)for(int x=0;x<32;++x)
        input.terrain.tiles[y*32+x]=x==16?3:x>16?2:(y==10&&x>4&&x<12)?1:0;
    const auto original=input.terrain.tiles;auto data=build(input);geometry(data);
    require(input.terrain.tiles==original,"input modified");
    require(data.width==128&&data.height==96,"rectangular map dimensions");
    require(data.at(-1,0)==-1&&data.at(128,0)==-1&&data.at(0,96)==-1,"outside raster");
    for(int x=0;x<128;++x)require(data.at(x,95)==-1,"reserved bottom row colored");
    for(const auto& r:data.runs)require(data.regions[r.region].walkable(),"blocked region colored");
    auto edited=input;edited.terrain.tiles[4*32+4]=1;auto after=build(edited);geometry(after);
    require(data.cells!=after.cells,"terrain edit did not change region result");
    require(build(input).cells==data.cells,"restored terrain not deterministic");
    auto invalid=input;invalid.terrain.tiles[0]=uint16_t(2047*16);
    auto normalized=input;normalized.terrain.tiles[0]=0;
    require(build(invalid).cells==build(normalized).cells,"missing tile-group fallback differs");
    bool rejected=false;try{Input empty;build(empty);}catch(const std::exception&){rejected=true;}
    require(rejected,"malformed terrain accepted");
    input.terrain.width=input.terrain.height=256;input.terrain.tiles.assign(256*256,0);
    const auto start=std::chrono::steady_clock::now();data=build(input);
    require(data.cells.size()==1024*1024,"maximum map truncated");
    geometry(data);
    std::cout<<"PASS: exact raster/run/border coverage, partial/blocked/high terrain, rectangular/maximum maps, edits/restore, missing groups and invalid input\n";
    std::cout<<"256x256 generation: "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<" ms (includes exhaustive geometry test)\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
