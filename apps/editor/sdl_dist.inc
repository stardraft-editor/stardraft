// What a map that opens gives the system (shell.h: shellMapOpened; on macOS mac_dist.mm: the Quick Look extensions'
// tile colors and Spotlight; on Linux the thumbnailer's tile colors in the cache), and the checks of a test run that print what the program set up for it (--check):
//   --check bundle          the app bundle's document types, exported types, plug-ins and signature (macOS)
//   --check spotlight       the open map's Spotlight entry: made, indexed and removed again (macOS)
//   --check preview:<dir>   the open map's Quick Look picture (apps/quicklook/map_preview.h, every system): its tile
//                           color table goes into <dir>, the picture is drawn from the file and the table and written to
//                           <dir>/preview.png, and its terrain is compared with the minimap's

void shellMapOpened(const std::wstring& path) {
#ifdef __APPLE__
    if (gSilent || !gState.ed || gState.ed->tileset().megatiles.empty()) return;  // (a test run leaves the system as it is)
    macMapOpened(fs::path(path), stardraft::preview::tileColors(gState.ed->tileset()), gState.ed->era());
#elif defined(STARDRAFT_XDG)
    // The tile color table of the map's tileset in the cache, for the thumbnailer (stardraft-thumbnailer): written
    // when it is not there or not the same.
    (void)path;
    if (gSilent || !gState.ed || gState.ed->tileset().megatiles.empty()) return;
    const fs::path dir = stardraft::preview::tileColorsFolder();
    if (dir.empty()) return;
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path file = stardraft::preview::tileColorsFile(dir, gState.ed->era());
    const std::vector<uint32_t> colors = stardraft::preview::tileColors(gState.ed->tileset());
    std::vector<uint32_t> old;
    std::string err;
    if ((!stardraft::preview::readTileColors(file, old) || old != colors) && !stardraft::preview::writeTileColors(file, colors, err))
        std::fprintf(stderr, "tile colors: %s\n", err.c_str());
#else
    (void)path;
#endif
}

namespace {

std::string sdlPreviewCheck(const std::string& dirArg) {
    if (!gState.ed || !gState.ed->isOpen() || gState.ed->path().empty()) return "preview: no map open (WRONG)\n";
    Editor& ed = *gState.ed;
    if (ed.tileset().megatiles.empty()) return "preview: no game data (WRONG)\n";
    std::error_code ec;
    const fs::path dir = dirArg.empty() ? fs::temp_directory_path(ec) : fs::path(widen(dirArg));
    const std::vector<uint32_t> colors = stardraft::preview::tileColors(ed.tileset());
    std::string err, out;
    if (!stardraft::preview::writeTileColors(stardraft::preview::tileColorsFile(dir, ed.era()), colors, err)) return "preview: " + err + " (WRONG)\n";
    Image img;
    stardraft::preview::MapInfo info;
    bool table = false;
    if (!stardraft::preview::renderFile(ed.path(), dir, 512, img, info, &table, err)) return "preview: " + err + " (WRONG)\n";
    char line[512];
    std::snprintf(line, sizeof line, "preview: %d x %d tiles, %s, picture %d x %d, table %s, players %d, start locations %d\n", info.width,
                  info.height, tilesetName(info.era), img.width, img.height, table ? "yes" : "no (WRONG)", info.players, info.startLocations);
    out += line;
    out += "preview: name \"" + info.name + "\", description " + std::to_string(info.description.size()) + " bytes\n";
    // The terrain alone (no units) against the minimap's colors of the same tiles (core/views_minimap.inc).
    Bytes bytes;
    if (!readMapChk(ed.path(), bytes, err)) return out + "preview: " + err + " (WRONG)\n";
    Chk chk = Chk::parse(bytes);
    chk.remove("UNIT");
    const Image terrain = stardraft::preview::render(chk, &colors, std::max(info.width, info.height));
    rebuildMinimapTerrain();
    size_t differ = 0, tiles = std::min(gState.miniTerrain.size(), size_t(terrain.width) * size_t(terrain.height));
    for (size_t i = 0; i < tiles; ++i) {
        const uint8_t* p = &terrain.rgba[i * 4];
        if ((uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | p[2]) != gState.miniTerrain[i]) ++differ;
    }
    out += "preview: the terrain is the minimap's: " + std::to_string(tiles - differ) + " of " + std::to_string(tiles) + " tiles" +
           (tiles && differ * 100 <= tiles ? " (yes)" : " (WRONG)") + "\n";
    const fs::path png = dir / "preview.png";
    out += std::string("preview: ") + (writePng(png.string().c_str(), img) ? "wrote " : "could not write (WRONG) ") + narrow(png.wstring()) + "\n";
    return out;
}

// One --check value: bundle, spotlight or preview:<folder>. false: not one of these (another part of the program can
// have it: main_sdl.cpp, the --check list).
bool sdlDistCheck(const std::string& what, std::string& out) {
    if (what == "bundle") out = macBundleReport();
    else if (what == "spotlight")
        out = !gState.ed || gState.ed->path().empty() ? "spotlight: no map open (WRONG)\n" : macSpotlightCheck(gState.ed->path());
    else if (what == "preview" || what.rfind("preview:", 0) == 0) out = sdlPreviewCheck(what.size() > 8 ? what.substr(8) : std::string());
    else return false;
    return true;
}

}  // namespace
