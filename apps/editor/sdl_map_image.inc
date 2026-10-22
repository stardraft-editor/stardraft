// File > Export map image on this system (the options, the dialogs and the scenes are core/map_image.inc): the map's
// renderer draws the picture in parts of 1024 x 1024 pixels into one texture, each part goes back from the GPU and into
// a band of rows, and each band goes into the PNG (stardraft/png_writer.h) on a thread. A part at a time: the progress
// window stays live between the parts, and its Cancel stops the export. The picture can be larger than the GPU's
// largest texture and than the memory (a 256 x 256 map at 64 pixels a tile is 16384 x 16384 pixels).
// Included by main_sdl.cpp after sdl_map_dialogs.inc.

// --mapimage <options> (a snapshot): the options that the next Export map image dialog shows first, one for each
// dialog that opens: comma-separated, the pixels a tile (64, 32, 16, 8, 4), whole, view or selection, units, doodads,
// sprites, locations, grid (each with "no" before it: off), sel=x0:y0:x1:y1 (a terrain selection in tiles), and
// cancel=N (the export stops after N parts, as the progress window's Cancel does).
std::deque<std::string> gShotMapImages;
int gShotMapImageCancel = 0;  // cancel=N: the export stops after N parts, as a Cancel does

bool shellMapImageHd() { return gRenderer && gRenderer->hdArt() && gState.quality != GraphicsQuality::SD; }

void shellMapImageWritten(const fs::path& file, int w, int h, const std::string& what) {
    if (gSilent) std::printf("map image: wrote %s: %d x %d pixels; %s\n", narrow(file.wstring()).c_str(), w, h, what.c_str());
}

// The export of one picture (core/map_image.inc: MapImageJob). Each step draws a part of 1024 x 1024 pixels into the
// texture and copies it into the band of rows; a full band goes to a thread that compresses it into the PNG while the
// next band is drawn. The PNG is written to "<file>.part" and takes the file's name when it is complete: a Cancel or
// an error leaves the file that was there before.
class SdlMapImageJob final : public MapImageJob {
public:
    static constexpr int kPart = 1024;

    SdlMapImageJob(const MapImageOptions& o, const fs::path& file) : o_(o), file_(file), part_(file) {
        part_ += ".part";
        area_ = mapImageRect(o), size_ = mapImageSize(o), zoom_ = float(o.px) / 32.0f;
        cols_ = (size_.x + kPart - 1) / kPart, rows_ = (size_.y + kPart - 1) / kPart;
    }
    ~SdlMapImageJob() override {
        wait();
        png_.reset();  // (not finished: PngWriter removes the .part file)
        if (tb_) SDL_ReleaseGPUTransferBuffer(gDevice, tb_);
        if (tex_) SDL_ReleaseGPUTexture(gDevice, tex_);
        if (gRenderer) gRenderer->setLoadBudget(30);
        gMapEd = nullptr;  // (the next frame of the map gives the renderer the map's own terrain and layers again)
        gState.dirty = true;
    }

    bool start(std::string& err) {
        if (size_.x <= 0 || size_.y <= 0) return err = "The area is empty.", false;
        SDL_GPUTextureCreateInfo ti{};
        ti.type = SDL_GPU_TEXTURETYPE_2D;
        ti.format = gRenderer->colorFormat();
        ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        ti.width = ti.height = uint32_t(kPart), ti.layer_count_or_depth = 1, ti.num_levels = 1;
        tex_ = SDL_CreateGPUTexture(gDevice, &ti);
        SDL_GPUTransferBufferCreateInfo bi{};
        bi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        bi.size = uint32_t(kPart * kPart * 4);
        tb_ = tex_ ? SDL_CreateGPUTransferBuffer(gDevice, &bi) : nullptr;
        if (!tex_ || !tb_) return err = std::string("The graphics device could not make the picture: ") + SDL_GetError(), false;
        terrain_ = mapImageTerrain(*gState.ed, o_.doodads);
        bgra_ = gRenderer->colorFormat() == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
        png_ = std::make_unique<PngWriter>();
        if (!png_->open(part_, size_.x, size_.y)) return err = "The file cannot be written.", false;
        return true;
    }

    int parts() const override { return cols_ * rows_; }
    int done() const override { return done_; }

    bool step(std::string& err) override {
        if (done_ >= parts() || !gState.ed || !gState.ed->isOpen()) return err = "No map is open.", false;
        const int bx = done_ % cols_, by = done_ / cols_;
        const int x0 = bx * kPart, y0 = by * kPart;
        const int pw = std::min(kPart, size_.x - x0), bh = std::min(kPart, size_.y - y0);
        if (bx == 0) band_.assign(size_t(size_.x) * size_t(bh) * 4, 255);
        // The renderer's terrain: the map's, with or without the doodads' tiles; no doodad to place; each image loads
        // at once (a picture has no next frame). Again for each part: the map's own frames between the parts set theirs.
        Editor& ed = *gState.ed;
        gRenderer->setSources(&gState.data, &ed.unitSprites(), &ed.tileset(), ed.era());
        gRenderer->updateTerrain(terrain_);
        gRenderer->setTerrainPreview({});
        uint32_t team[12];
        for (int i = 0; i < 12; ++i) team[i] = playerRgba(i, 0) >> 8;
        gRenderer->setPlayerColors(team);
        gRenderer->setLoadBudget(1e9);
        gMapEd = nullptr;  // (the map's next frame sets its own terrain again)
        const render::Scene scene = mapImageScene(o_, float(area_.left) + float(x0) / zoom_, float(area_.top) + float(y0) / zoom_, zoom_, kPart, kPart);
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gDevice);
        if (!cmd) return err = std::string("The graphics device could not make the picture: ") + SDL_GetError(), false;
        if (!gRenderer->drawTo(cmd, tex_, uint32_t(kPart), uint32_t(kPart), scene)) {
            SDL_CancelGPUCommandBuffer(cmd);
            return err = "The graphics device could not draw the map.", false;
        }
        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureRegion src{};
        src.texture = tex_, src.w = uint32_t(pw), src.h = uint32_t(bh), src.d = 1;
        SDL_GPUTextureTransferInfo dst{};
        dst.transfer_buffer = tb_;
        SDL_DownloadFromGPUTexture(copy, &src, &dst);
        SDL_EndGPUCopyPass(copy);
        SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
        if (fence) SDL_WaitForGPUFences(gDevice, true, &fence, 1), SDL_ReleaseGPUFence(gDevice, fence);
        const uint8_t* p = static_cast<const uint8_t*>(SDL_MapGPUTransferBuffer(gDevice, tb_, false));
        if (!p) return err = "The graphics device could not give the picture back.", false;
        for (int y = 0; y < bh; ++y) {
            const uint8_t* s = p + size_t(y) * size_t(pw) * 4;
            uint8_t* d = band_.data() + (size_t(y) * size_t(size_.x) + size_t(x0)) * 4;
            for (int x = 0; x < pw; ++x, s += 4, d += 4) d[0] = bgra_ ? s[2] : s[0], d[1] = s[1], d[2] = bgra_ ? s[0] : s[2], d[3] = 255;
        }
        SDL_UnmapGPUTransferBuffer(gDevice, tb_);
        ++done_;
        if (bx == cols_ - 1) {  // the band is complete: the thread compresses it while the next band is drawn
            if (!wait()) return err = "The file cannot be written.", false;
            writing_.swap(band_);
            writer_ = std::thread([this, bh] { writeOk_ = png_->addRows(writing_.data(), bh); });
        }
        return true;
    }

    bool finish(std::string& err) override {
        if (!wait() || !png_->finish()) return png_.reset(), err = "The file cannot be written.", false;
        png_.reset();
        std::error_code ec;
        fs::rename(part_, file_, ec);
        if (ec) {
            fs::remove(part_, ec);
            return err = "The file cannot be written.", false;
        }
        return true;
    }

private:
    // The thread of the last band has ended. False when its write failed.
    bool wait() {
        if (writer_.joinable()) writer_.join();
        return writeOk_;
    }

    MapImageOptions o_;
    fs::path file_, part_;
    Rect area_{};
    draw::Point size_{};
    float zoom_ = 1.0f;
    int cols_ = 0, rows_ = 0, done_ = 0;
    bool bgra_ = false;
    TerrainLayer terrain_;
    SDL_GPUTexture* tex_ = nullptr;
    SDL_GPUTransferBuffer* tb_ = nullptr;
    std::unique_ptr<PngWriter> png_;
    std::vector<uint8_t> band_, writing_;  // the band drawn now, and the band the thread compresses
    std::thread writer_;
    bool writeOk_ = true;  // (written by the thread; read after the join)
};

std::unique_ptr<MapImageJob> shellMapImageJob(const MapImageOptions& o, const fs::path& file, std::string& err) {
    if (!gRenderer || !gDevice || !gState.ed || !gState.ed->isOpen()) return err = "No map is open.", nullptr;
    auto job = std::make_unique<SdlMapImageJob>(o, file);
    if (!job->start(err)) return nullptr;
    return job;
}

bool shellMapImageProgressShown() { return !gShotDialogs; }
int shellMapImageCancelAt() { return gShotMapImageCancel; }
void shellMapImageCanceled(const fs::path& file, int done, int parts) {
    if (!gSilent) return;
    std::error_code ec;
    fs::path part = file;
    part += ".part";
    std::printf("map image: canceled after part %d of %d; %s: %s; %s: %s\n", done, parts, narrow(file.filename().wstring()).c_str(),
                fs::exists(file, ec) ? "there" : "not there", narrow(part.filename().wstring()).c_str(), fs::exists(part, ec) ? "there" : "not there");
}

// File > Export map image (Ctrl+Shift+E). A snapshot's --mapimage gives the dialog its first options.
void sdlExportMapImage() {
    if (!gRoot || !gState.ed || !gState.ed->isOpen()) return;
    if (!gShotMapImages.empty()) {
        const std::string spec = gShotMapImages.front();
        gShotMapImages.pop_front();
        MapImageOptions o;
        gShotMapImageCancel = 0;
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
            else if (t.rfind("cancel=", 0) == 0) gShotMapImageCancel = std::atoi(t.c_str() + 7);
            else if (t.rfind("sel=", 0) == 0) {
                int v[4] = {0, 0, 0, 0};
                std::sscanf(t.c_str() + 4, "%d:%d:%d:%d", &v[0], &v[1], &v[2], &v[3]);
                gState.terrainSel = {v[0] * 32, v[1] * 32, v[2] * 32, v[3] * 32}, gState.hasTerrainSel = true, gState.terrainSelMask.clear();
            }
        }
        gMapImageLast = o;
    }
    exportMapImage(*gRoot);
}
