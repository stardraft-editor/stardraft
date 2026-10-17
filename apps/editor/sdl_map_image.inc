// File > Export map image on this system (the options, the dialog and the scenes are core/map_image.inc): the map's
// renderer draws the picture in parts of 1024 x 1024 pixels into one texture, each part goes back from the GPU and into
// a band of rows, and each band goes into the PNG (stardraft/png_writer.h). The picture can be larger than the GPU's
// largest texture and than the memory (a 256 x 256 map at 64 pixels a tile is 16384 x 16384 pixels).
// Included by main_sdl.cpp after sdl_map_dialogs.inc.

// --mapimage <options> (a snapshot): the options that the next Export map image dialog shows first, one for each
// dialog that opens: comma-separated, the pixels a tile (64, 32, 16, 8, 4), whole, view or selection, units, doodads,
// sprites, locations, grid (each with "no" before it: off), and sel=x0:y0:x1:y1 (a terrain selection in tiles).
std::deque<std::string> gShotMapImages;

bool shellMapImageHd() { return gRenderer && gRenderer->hdArt() && gState.quality != GraphicsQuality::SD; }

void shellMapImageWritten(const fs::path& file, int w, int h, const std::string& what) {
    if (gSilent) std::printf("map image: wrote %s: %d x %d pixels; %s\n", narrow(file.wstring()).c_str(), w, h, what.c_str());
}

bool shellRenderMapImage(const MapImageOptions& o, const fs::path& file, std::string& err) {
    if (!gRenderer || !gDevice || !gState.ed || !gState.ed->isOpen()) return err = "No map is open.", false;
    Editor& ed = *gState.ed;
    const Rect area = mapImageRect(o);
    const draw::Point size = mapImageSize(o);
    if (size.x <= 0 || size.y <= 0) return err = "The area is empty.", false;
    const float zoom = float(o.px) / 32.0f;
    constexpr int kPart = 1024;
    SDL_GPUTextureCreateInfo ti{};
    ti.type = SDL_GPU_TEXTURETYPE_2D;
    ti.format = gRenderer->colorFormat();
    ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ti.width = ti.height = uint32_t(kPart), ti.layer_count_or_depth = 1, ti.num_levels = 1;
    SDL_GPUTexture* tex = SDL_CreateGPUTexture(gDevice, &ti);
    SDL_GPUTransferBufferCreateInfo bi{};
    bi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    bi.size = uint32_t(kPart * kPart * 4);
    SDL_GPUTransferBuffer* tb = tex ? SDL_CreateGPUTransferBuffer(gDevice, &bi) : nullptr;
    auto release = [&] {
        if (tb) SDL_ReleaseGPUTransferBuffer(gDevice, tb);
        if (tex) SDL_ReleaseGPUTexture(gDevice, tex);
        gRenderer->setLoadBudget(30);
        gMapEd = nullptr;  // (the next frame of the map gives the renderer the map's own terrain and layers again)
        gState.dirty = true;
    };
    if (!tex || !tb) return release(), err = std::string("The graphics device could not make the picture: ") + SDL_GetError(), false;
    // The renderer's terrain: the map's, with or without the doodads' tiles; no doodad to place; each image loads at
    // once (a picture has no next frame).
    gRenderer->setSources(&gState.data, &ed.unitSprites(), &ed.tileset(), ed.era());
    gRenderer->updateTerrain(mapImageTerrain(ed, o.doodads));
    gRenderer->setTerrainPreview({});
    uint32_t team[12];
    for (int i = 0; i < 12; ++i) team[i] = playerRgba(i, 0) >> 8;
    gRenderer->setPlayerColors(team);
    gRenderer->setLoadBudget(1e9);
    const bool bgra = gRenderer->colorFormat() == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    PngWriter png;
    if (!png.open(file, size.x, size.y)) return release(), err = "The file cannot be written.", false;
    std::vector<uint8_t> band;
    for (int y0 = 0; y0 < size.y; y0 += kPart) {
        const int bh = std::min(kPart, size.y - y0);
        band.assign(size_t(size.x) * size_t(bh) * 4, 255);
        for (int x0 = 0; x0 < size.x; x0 += kPart) {
            const int pw = std::min(kPart, size.x - x0);
            const render::Scene scene = mapImageScene(o, float(area.left) + float(x0) / zoom, float(area.top) + float(y0) / zoom, zoom, kPart, kPart);
            SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gDevice);
            if (!cmd) return release(), err = std::string("The graphics device could not make the picture: ") + SDL_GetError(), false;
            if (!gRenderer->drawTo(cmd, tex, uint32_t(kPart), uint32_t(kPart), scene)) {
                SDL_CancelGPUCommandBuffer(cmd);
                return release(), err = "The graphics device could not draw the map.", false;
            }
            SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
            SDL_GPUTextureRegion src{};
            src.texture = tex, src.w = uint32_t(pw), src.h = uint32_t(bh), src.d = 1;
            SDL_GPUTextureTransferInfo dst{};
            dst.transfer_buffer = tb;
            SDL_DownloadFromGPUTexture(copy, &src, &dst);
            SDL_EndGPUCopyPass(copy);
            SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
            if (fence) SDL_WaitForGPUFences(gDevice, true, &fence, 1), SDL_ReleaseGPUFence(gDevice, fence);
            const uint8_t* p = static_cast<const uint8_t*>(SDL_MapGPUTransferBuffer(gDevice, tb, false));
            if (!p) return release(), err = "The graphics device could not give the picture back.", false;
            for (int y = 0; y < bh; ++y) {
                const uint8_t* s = p + size_t(y) * size_t(pw) * 4;
                uint8_t* d = band.data() + (size_t(y) * size_t(size.x) + size_t(x0)) * 4;
                for (int x = 0; x < pw; ++x, s += 4, d += 4) {
                    d[0] = bgra ? s[2] : s[0], d[1] = s[1], d[2] = bgra ? s[0] : s[2], d[3] = 255;
                }
            }
            SDL_UnmapGPUTransferBuffer(gDevice, tb);
        }
        if (!png.addRows(band.data(), bh)) return release(), err = "The file cannot be written.", false;
    }
    release();
    if (!png.finish()) return err = "The file cannot be written.", false;
    return true;
}

// File > Export map image (Ctrl+Shift+E). A snapshot's --mapimage gives the dialog its first options.
void sdlExportMapImage() {
    if (!gRoot || !gState.ed || !gState.ed->isOpen()) return;
    if (!gShotMapImages.empty()) {
        const std::string spec = gShotMapImages.front();
        gShotMapImages.pop_front();
        MapImageOptions o;
        size_t at = 0;
        while (at <= spec.size()) {
            const size_t end = std::min(spec.find(',', at), spec.size());
            const std::string t = spec.substr(at, end - at);
            at = end + 1;
            const bool off = t.rfind("no", 0) == 0;
            const std::string n = off ? t.substr(2) : t;
            if (!t.empty() && std::isdigit((unsigned char)t[0])) o.px = std::atoi(t.c_str());
            else if (t == "whole") o.area = MapImageArea::Whole;
            else if (t == "view") o.area = MapImageArea::View;
            else if (t == "selection") o.area = MapImageArea::Selection;
            else if (n == "units") o.units = !off;
            else if (n == "doodads") o.doodads = !off;
            else if (n == "sprites") o.sprites = !off;
            else if (n == "locations") o.locations = !off;
            else if (n == "grid") o.grid = !off;
            else if (t.rfind("sel=", 0) == 0) {
                int v[4] = {0, 0, 0, 0};
                std::sscanf(t.c_str() + 4, "%d:%d:%d:%d", &v[0], &v[1], &v[2], &v[3]);
                gState.terrainSel = {v[0] * 32, v[1] * 32, v[2] * 32, v[3] * 32}, gState.hasTerrainSel = true;
            }
        }
        gMapImageLast = o;
    }
    exportMapImage(*gRoot);
}
