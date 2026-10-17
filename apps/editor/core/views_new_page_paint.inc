// The New page's painting and input (views_new_page.inc has the page's choices and the class): the block of the page, its
// columns and the Create bar, the preview, the generator's sliders, and the mouse, the wheel and the scroll bar.

namespace newpage {
// The title's and the small text's fonts (the Start page's sizes).
draw::Font titleFont(draw::Canvas& c) { return c.font({draw::Face::Ui, -(shellUiFontLfHeight() * 3 / 2)}); }
draw::Font smallText(draw::Canvas& c) { return c.font({draw::Face::Ui, -(shellUiFontLfHeight() * 5 / 6)}); }
// A text field's box (its ground and its line; hot: the line is brighter; changed and read-only: their own).
void fieldBox(draw::Canvas& c, draw::Rect r, bool hot, bool changed, bool readOnly) {
    draw::Color fill = kCtl, line = hot ? kBorderHot : kBorder;
    if (changed) fill = kGoodFill, line = kGoodLine;
    if (readOnly) fill = kReadOnly, line = kReadOnlyLine;
    c.roundBox(r, fill, line, float(scale(stardraft::kit::kRadius)));
}
}  // namespace newpage

void NewPageView::buttonFace(draw::Canvas& c, draw::Rect r, const std::wstring& text, bool primary, BtnState s) {
    draw::Color fill = kCtl, line = kBorder;
    if (primary) fill = kSel, line = kAccentLine;
    if (s == BtnState::Hot) fill = primary ? kSelHot : kCtlHot, line = primary ? kAccent : kBorderHot;
    if (s == BtnState::Down) fill = primary ? kSel : kCtlDown;
    if (s == BtnState::Disabled) fill = kBg, line = kBorder;
    c.roundBox(r, fill, line, float(scale(stardraft::kit::kRadius)));
    c.text(r, text, s == BtnState::Disabled ? kFaint : primary ? kAccent : kText, draw::kMiddle | draw::kCenter | draw::kEllipsis);
}

void NewPageView::button(draw::Canvas& c, draw::Rect r, const std::wstring& text, bool primary, std::function<void()> click, const std::wstring& tip,
                         bool enabled) {
    buttonFace(c, r, text, primary, !enabled ? BtnState::Disabled : hot(r) ? BtnState::Hot : BtnState::Normal);
    hitAdd(r, enabled ? std::move(click) : nullptr, tip, enabled ? draw::Cursor::Hand : draw::Cursor::Arrow);
}

// A small button: as wide as its text, in the middle of the row. Returns the right edge.
int NewPageView::smallButton(draw::Canvas& c, int x, int y, int h, const std::wstring& text, std::function<void()> click, const std::wstring& tip) {
    const int bh = std::min(h, scale(24)), w = c.measure(text) + scale(20);
    const draw::Rect r{x, y + (h - bh) / 2, x + w, y + (h - bh) / 2 + bh};
    buttonFace(c, r, text, false, hot(r) ? BtnState::Hot : BtnState::Normal);
    hitAdd(r, std::move(click), tip);
    return r.right;
}

int NewPageView::labelAt(draw::Canvas& c, int x, int yy, int w, const wchar_t* t, bool enabled) {
    c.text({x, yy, x + w, yy + scale(20)}, t, enabled ? kDim : kFaint, draw::kMiddle | draw::kEllipsis);
    return yy + scale(22);
}

// Text in `r`, word-wrapped; returns its bottom.
int NewPageView::paragraph(draw::Canvas& c, draw::Rect r, const std::wstring& text, draw::Color color) {
    const int hgt = c.measureBox(text, r.width(), draw::kWrap).y;
    r.bottom = r.top + hgt;
    c.text(r, text, color, draw::kWrap | draw::kLeft);
    return r.bottom;
}

// A title: big text, and a line under it in the dim color; the icon at the left.
int NewPageView::titleBlock(draw::Canvas& c, int x, int y, int right, const std::wstring& text, const std::wstring& sub, UiIcon icon) {
    c.icon({x, y, x + scale(40), y + scale(40)}, int(icon), kDim, scale(32));
    const int tx = x + scale(52);
    const draw::Font of = c.setFont(newpage::titleFont(c));
    c.text({tx, y, right, y + scale(26)}, text.empty() ? L"(empty)" : text, kText, draw::kMiddle | draw::kEllipsis);
    c.setFont(of);
    if (sub.empty()) return y + scale(40);
    c.text({tx, y + scale(24), right, y + scale(42)}, sub, kDim, draw::kMiddle);
    return y + scale(56);
}

// A dropdown's face: the box, the text and the chevron.
void NewPageView::pickBoxFace(draw::Canvas& c, draw::Rect r, const std::wstring& text, bool changed, bool over, bool enabled) {
    newpage::fieldBox(c, r, enabled && over, enabled && changed, !enabled);
    const int tx = r.left + scale(8);
    c.text({tx, r.top, r.right - scale(22), r.bottom}, text, !enabled ? kFaint : changed ? kGood : kText, draw::kMiddle | draw::kEllipsis);
    if (enabled) c.icon({r.right - scale(20), r.top, r.right - scale(6), r.bottom}, int(UiIcon::Dropdown), over ? kText : kDim, scale(16));
}

void NewPageView::sliderFace(draw::Canvas& c, int x0, int x1, int cy, float frac, bool over, bool enabled) {
    kitpaint::drawSliderFace(c, x0, x1, cy, frac, over, enabled);
}

void NewPageView::radioMark(draw::Canvas& c, draw::Rect b, bool on, bool over) {
    c.ellipse(b, kCtl, on ? kAccent : over ? kBorderHot : kBorderStrong);
    if (!on) return;
    const int in = b.width() * 3 / 10;
    c.ellipse({b.left + in, b.top + in, b.right - in + 1, b.bottom - in + 1}, kAccent, draw::kNone);
}

void NewPageView::listRowFace(draw::Canvas& c, draw::Rect r, bool selected, bool over) {
    if (selected) {
        c.fill(r, kSel);
        c.fill({r.left, r.top, r.left + scale(2), r.bottom}, kAccent);
    } else if (over) {
        c.fill(r, kRowHot);
    }
}

int NewPageView::sectionHeader(draw::Canvas& c, int x0, int colRight, int y, int rowH, const wchar_t* name) {
    c.text({x0, y, colRight, y + rowH}, name, kText, draw::kMiddle);
    c.fill({x0, y + rowH - 1, colRight, y + rowH}, kBorder);
    return y + rowH + scale(6);
}

void NewPageView::paintSizeRow(draw::Canvas& c, int x, int y, int labelW, int right, const wchar_t* name, uint16_t& value, int which) {
    const int n = int(std::size(kMapSizes)), rowH = scale(stardraft::kit::kFormRowH), cy = y + rowH / 2;
    int at = 0;
    for (int i = 0; i < n; ++i)
        if (kMapSizes[i] <= value) at = i;
    c.text({x, y, x + labelW - scale(48), y + rowH}, name, kDim, draw::kMiddle | draw::kEllipsis);
    c.text({x + labelW - scale(56), y, x + labelW - scale(4), y + rowH}, std::to_wstring(value), kText, draw::kMiddle | draw::kRight);
    const int x0 = x + labelW + scale(8), x1 = right - scale(8);
    const int hw = std::max(1, scale(1));
    for (int i = 0; i < n; ++i) {  // the stops
        const int tx = x0 + (x1 - x0) * i / (n - 1);
        c.fill({tx - hw / 2, cy + scale(9), tx - hw / 2 + hw, cy + scale(13)}, kBorderHot);
    }
    const draw::Rect area{x0 - scale(8), y, right, y + rowH};
    sliderFace(c, x0, x1, cy, float(at) / float(n - 1), hot(area));
    sizeX0 = x0, sizeX1 = x1, sizeY[which] = cy;
    Hit label;
    label.r = {x, y, area.left, y + rowH}, label.name = name;  // its name and value: feedback on it
    hits.push_back(label);
    uint16_t* p = &value;
    Hit h;
    h.r = area, h.cursor = draw::Cursor::Hand;
    h.press = [this, p, x0, x1, n](draw::Point pt) {
        const float f = std::clamp(float(pt.x - x0) / float(std::max(1, x1 - x0)), 0.0f, 1.0f);
        const uint16_t nv = kMapSizes[std::clamp(int(std::lround(f * float(n - 1))), 0, n - 1)];
        if (nv != *p) *p = nv, changed();
    };
    h.wheel = [this, p, n](int dir) {
        int i = 0;
        for (int k = 0; k < n; ++k)
            if (kMapSizes[k] <= *p) i = k;
        const uint16_t nv = kMapSizes[std::clamp(i + dir, 0, n - 1)];
        if (nv != *p) *p = nv, changed();
    };
    wheels.push_back({area, h.wheel});
    hits.push_back(h);
}

void NewPageView::paintSliderRow(draw::Canvas& c, int x, int y, int labelW, int right, NewSlider& sl, bool percent) {
    const int rowH = scale(stardraft::kit::kRowH), cy = y + rowH / 2;
    wchar_t v[32];
    if (percent) std::swprintf(v, 32, L"%.1f%%", sl.value / 2.0);
    else std::swprintf(v, 32, L"%d", sl.value);
    c.text({x, y, x + labelW - scale(48), y + rowH}, sl.label, kDim, draw::kMiddle | draw::kEllipsis);
    c.text({x + labelW - scale(56), y, x + labelW - scale(4), y + rowH}, v, kDim, draw::kMiddle | draw::kRight);
    const int x0 = x + labelW + scale(8), x1 = right - scale(8);
    const draw::Rect area{x0 - scale(8), y, right, y + rowH};
    sliderFace(c, x0, x1, cy, float(sl.value - sl.lo) / float(std::max(1, sl.hi - sl.lo)), hot(area));
    Hit label;
    label.r = {x, y, area.left, y + rowH}, label.name = sl.label;
    hits.push_back(label);
    NewSlider* p = &sl;
    Hit h;
    h.r = area, h.cursor = draw::Cursor::Hand;
    h.press = [this, p, x0, x1](draw::Point pt) {
        const float f = std::clamp(float(pt.x - x0) / float(std::max(1, x1 - x0)), 0.0f, 1.0f);
        const int nv = p->lo + int(std::lround(f * float(p->hi - p->lo)));
        if (nv != p->value) p->value = nv, changed();
    };
    h.wheel = [this, p](int dir) {  // a step of the wheel: 1/50 of the range, at least 1
        const int nv = std::clamp(p->value + dir * std::max(1, (p->hi - p->lo) / 50), p->lo, p->hi);
        if (nv != p->value) p->value = nv, changed();
    };
    wheels.push_back({area, h.wheel});
    hits.push_back(h);
}

// The terrain list for the Blank row (the tileset's terrains by name).
void NewPageView::pickTerrain(draw::Rect b) {
    copyMap = false, generate = false, terrainOnly = false;  // the dropdown selects Blank too
    changed();
    const IsomTerrainTable* t = IsomTerrainTable::forEra(era);
    std::vector<NewListItem> items;
    for (uint16_t k = 1; t && k <= t->terrainCount(); ++k)
        if (t->isTerrain(k)) items.push_back({widen(t->terrainName(k)), std::to_string(k)});
    if (!items.empty())
        host_.pickList(L"Terrain", items, std::to_string(terrainId()), b, [this](const std::string& v) { terrain = uint16_t(std::stoi(v)), changed(); });
}

void NewPageView::paint(draw::Canvas& c) {
    if (covered) return;  // (the start page's window is over this one)
    makeSwatches();
    newLoadTileset();
    hits.clear();
    wheels.clear();
    const int W = width(), H = height();
    c.fill({0, 0, W, H}, kBg);
    const int pad = scale(stardraft::kit::kDialogPad), gap = scale(32), rowH = scale(stardraft::kit::kRowH), fieldH = scale(stardraft::kit::kFieldH);
    const int bh = scale(stardraft::kit::kButtonH);
    const int avail = W - 2 * pad;
    // The block (Back, the title, the columns, then Create and Cancel) is in the middle of the page, across and up and
    // down; a block taller than the page starts at the top and scrolls. The height is from the last paint.
    const int startY = std::max(pad, (H - blockH) / 2);
    // The text fields stay above the Create and Cancel bar when it is held at the bottom (a tall block).
    const int footerTop = startY + blockH + pad > H ? H - (bh + 2 * pad) : H;
    // The columns: Project (only for an EUD project), Terrain, Start. Start has the preview (up to 600 x 600 at 100%,
    // as tall as the page lets it be) and to its right the generator's settings; in a narrow window the settings go
    // under the preview. The width does not change with the Start choice.
    const bool proj = project;
    if (!proj) copyMap = false;  // Import an existing map is for an EUD project (a classic map: File > Open)
    const bool imp = importing();
    const int gap1 = proj ? gap : 0, pvGap = scale(24), sideMin = scale(300), gwMin = scale(320), gwMax = scale(420);
    int w1 = proj ? scale(300) : 0, w2 = scale(320);
    const int room = avail - w1 - gap1 - w2 - gap;
    int side = std::max(sideMin, std::min(scale(600), H - 2 * pad - aboveH - scale(24) - bh));
    const bool besides = room >= sideMin + pvGap + gwMin;  // the generator's settings to the right of the preview
    int w3 = 0, gw = 0;
    if (besides) {
        gw = std::clamp(room - side - pvGap, gwMin, gwMax);
        side = std::min(side, room - pvGap - gw);
        w3 = side + pvGap + gw;
    } else {
        w3 = std::clamp(room, scale(300), scale(520));
        if (w1 + w2 + w3 + gap1 + gap > avail) {  // a narrow window: the columns share what there is
            const int k = std::max(1, avail - gap1 - gap);
            if (proj) w1 = k * 30 / 104, w2 = k * 32 / 104, w3 = k - w1 - w2;
            else w2 = k * 32 / 74, w3 = k - w2;
        }
        side = std::min(side, w3), gw = w3;
    }
    const int x1 = std::max(pad, (W - (w1 + gap1 + w2 + gap + w3)) / 2), x2 = x1 + w1 + gap1, x3 = x2 + w2 + gap;
    const int blockTop = startY - scroll;
    int y = blockTop;
    // Back to the cards.
    smallButton(c, x1, y, scale(24), L"Back", [this] { backToStart(); }, L"Select a different kind (Esc)");
    y += scale(28);
    {
        const IsomTerrainTable* table = IsomTerrainTable::forEra(era);
        std::wstring sub = std::wstring(kTilesetNames[era & 7]) + L"  \x00B7  " + std::to_wstring(w) + L" x " + std::to_wstring(h) + L"  \x00B7  " +
                           (generate ? std::wstring(L"generated") : L"filled with " + widen(table ? table->terrainName(terrainId()) : ""));
        if (imp) {  // Import an existing map: the map's file names it (its tileset and size come with it)
            const std::wstring map = fromMapText();
            sub = map.empty() ? L"an imported map" : L"imported from " + fs::path(map).filename().wstring();
        }
        y = titleBlock(c, x1, y, x3 + w3, kKindNames[proj ? 1 : 0], sub, proj ? UiIcon::NewProject : UiIcon::New);
    }
    const int top = y + scale(8);

    // Project (only for an EUD project): the name, the folder and the output file.
    y = proj ? sectionHeader(c, x1, x1 + w1, top, rowH, L"Project") : top;
    projectShown = project;
    warning = problem();
    {
        const int bw = scale(stardraft::kit::kButtonMinW);
        int fy = y;
        auto fieldRow = [&](stardraft::kit::TextField& e, const wchar_t* name, bool browse, std::function<void()> click) {
            if (!project) {
                placeField(e, {}, false, 0, 0);
                return;
            }
            fy = labelAt(c, x1, fy, w1, name);
            const int fr = browse ? x1 + w1 - bw - scale(8) : x1 + w1;
            placeField(e, {x1, fy + scale(2), fr, fy + scale(2) + fieldH}, true, 0, footerTop);
            if (browse) button(c, {x1 + w1 - bw, fy, x1 + w1, fy + scale(stardraft::kit::kButtonH)}, L"Browse\x2026", false, std::move(click));
            fy += scale(stardraft::kit::kFormRowH);
        };
        fieldRow(name_, L"Name", false, nullptr);
        fieldRow(location_, L"Location", true, [this] {
            const std::wstring dir = host_.pickFolder();
            if (!dir.empty()) setLocationText(dir);
        });
        if (project) {  // where the folder goes, or what is wrong
            if (!warning.empty()) {
                c.icon({x1, fy + scale(2), x1 + scale(16), fy + scale(18)}, int(UiIcon::Warning), kWarn, scale(16));
                fy = paragraph(c, {x1 + scale(22), fy, x1 + w1, fy + scale(200)}, warning, kWarn) + scale(8);
            } else {
                const draw::Font of = c.setFont(newpage::smallText(c));
                fy = paragraph(c, {x1, fy, x1 + w1, fy + scale(100)}, L"StarDraft 2 makes the folder " + projectFolder().make_preferred().wstring(), kDim) +
                     scale(8);
                c.setFont(of);
            }
        }
        const std::wstring cue = nameText() + L"_EUD.scx";
        if (output_.placeholder() != cue) output_.setPlaceholder(cue);
        fieldRow(output_, L"Output file", true, [this] {
            const std::wstring file = host_.pickOutputFile(nameText() + L"_EUD.scx");
            if (!file.empty()) setOutputText(file);
        });
        y = std::max(y, fy);
    }
    int bottom = y;

    // Terrain: the tileset and the size (with Import an existing map, the map has them).
    y = sectionHeader(c, x2, x2 + w2, top, rowH, L"Terrain");
    if (imp) {
        y = paragraph(c, {x2, y, x2 + w2, y + scale(200)}, L"The tileset, the size and the terrain come from the map.", kDim);
        bottom = std::max(bottom, y);
    } else {
        y = labelAt(c, x2, y, w2, L"Tileset");
        for (uint16_t e = 0; e < 8; ++e) {
            const draw::Rect r{x2, y, x2 + w2, y + scale(40)};
            const bool on = era == e;
            listRowFace(c, r, on, hot(r));
            const draw::Rect sw{x2 + scale(10), r.top + scale(4), x2 + scale(10) + scale(64), r.bottom - scale(4)};
            if (!swatch[e].empty()) {  // the picture, cropped to the middle
                const draw::Bitmap& bm = swatchBitmap[e];
                const int pw = bm.width(), ph = bm.height();
                const double k = std::max(double(sw.width()) / pw, double(sw.height()) / ph);
                const int cw = int(sw.width() / k), ch = int(sw.height() / k);
                c.bitmap(swatchBitmap[e], {(pw - cw) / 2, (ph - ch) / 2, (pw - cw) / 2 + cw, (ph - ch) / 2 + ch}, sw, true);
            } else {
                c.fill(sw, kPanel);
            }
            c.roundBox(sw, draw::kNone, kBorder, float(scale(2)));
            c.text({sw.right + scale(12), r.top, r.right - scale(8), r.bottom}, kTilesetNames[e], on ? kAccent : kText, draw::kMiddle);
            hitAdd(r, [this, e] {
                if (era != e) era = e, terrain = 0, changed();
            });
            hits.back().name = std::wstring(L"the tileset ") + kTilesetNames[e];
            y = r.bottom;
        }
        y += scale(12);
        paintSizeRow(c, x2, y, std::min(scale(120), w2 * 2 / 5), x2 + w2, L"Width", w, 0);
        y += scale(stardraft::kit::kFormRowH);
        paintSizeRow(c, x2, y, std::min(scale(120), w2 * 2 / 5), x2 + w2, L"Height", h, 1);
        y += scale(stardraft::kit::kFormRowH);
        bottom = std::max(bottom, y);
    }

    // Start: a blank map filled with a terrain (the dropdown in its row), generated, or (an EUD project) imported.
    y = sectionHeader(c, x3, x3 + w3, top, rowH, L"Start");
    for (int k = 0; k < 3; ++k) {
        if (k == 2 && !project) continue;  // Import an existing map: a project (a classic map: File > Open)
        const bool on = k == 2 ? copyMap : !copyMap && !terrainOnly && generate == (k == 1);
        const int hgt = k == 0 ? fieldH + scale(8) : rowH;
        const draw::Rect r{x3, y, x3 + w3, y + hgt};
        const bool over = hot(r);
        radioMark(c, {x3 + scale(2), r.top + (hgt - scale(14)) / 2, x3 + scale(16), r.top + (hgt - scale(14)) / 2 + scale(14)}, on, over);
        const wchar_t* text = k == 2 ? L"Import an existing map" : k == 1 ? L"Generate" : L"Blank map filled with";
        const draw::Rect t{x3 + scale(26), r.top, r.right, r.bottom};
        c.text(t, text, on || over ? kText : kDim, draw::kMiddle);
        const wchar_t* tipText = k == 2 ? L"A copy of a map that you have (.scm or .scx): its terrain, units, triggers and settings"
                                 : k == 1 ? L"Land, water and high ground from a seed and the settings next to the preview"
                                          : L"The terrain on the whole map";
        hitAdd(r, [this, k] {
            copyMap = k == 2, generate = k == 1, terrainOnly = false;
            changed();
        }, tipText);
        if (k == 0) {  // the terrain, in the same row (after the hit of the row: a click on it opens the list)
            const IsomTerrainTable* table = IsomTerrainTable::forEra(era);
            const int bx = t.left + c.measure(text) + scale(10);
            terrainBox = {bx, r.top + scale(4), std::min(int(r.right), bx + scale(260)), r.top + scale(4) + fieldH};
            pickBoxFace(c, terrainBox, widen(table ? table->terrainName(terrainId()) : ""), false, hot(terrainBox));
            hitAdd(terrainBox, [this] { pickTerrain(terrainBox); }, L"The terrain on the whole map");
            hits.back().wheel = [this](int dir) {  // the wheel: the next or the previous terrain of the tileset
                const IsomTerrainTable* tb = IsomTerrainTable::forEra(era);
                if (!tb) return;
                uint16_t id = terrainId();
                for (int step = 0; step < 256; ++step) {
                    id = uint16_t(id + dir);
                    if (id < 1 || id > tb->terrainCount()) return;
                    if (tb->isTerrain(id)) return terrain = id, changed();
                }
            };
        }
        y = r.bottom + scale(4);
    }
    // Import an existing map: the map's file (the copy goes into the project folder; the file stays as it is).
    if (copyMap || terrainOnly) {
        y += scale(8);
        const int bw = scale(stardraft::kit::kButtonMinW), fw = std::min(w3, scale(520));
        placeField(fromMap_, {x3, y + scale(2), x3 + fw - bw - scale(8), y + scale(2) + fieldH}, true, 0, footerTop);
        button(c, {x3 + fw - bw, y, x3 + fw, y + scale(stardraft::kit::kButtonH)}, L"Browse\x2026", false, [this] {
            const std::wstring file = host_.pickMapFile();
            if (!file.empty()) setFromMapText(file), changed(false);
        });
        y += scale(stardraft::kit::kFormRowH);
        const draw::Font of = c.setFont(newpage::smallText(c));
        y = paragraph(c, {x3, y, x3 + fw, y + scale(100)},
                      terrainOnly ? L"The new map gets the map's size, tileset, terrain and doodads. StarDraft 2 does not change the map's file."
                                  : L"StarDraft 2 copies the map into the project folder. StarDraft 2 does not change the original file.",
                      kDim) +
            scale(8);
        c.setFont(of);
    } else {
        placeField(fromMap_, {}, false, 0, 0);
    }
    genShown = generate && !imp;
    previewRect = genRect = {};
    if (!imp) {
        // The preview: for Blank and Generate, under the Start choices.
        y += scale(8);
        if (y - blockTop != aboveH) {  // the preview's size follows the page's height: paint again with this one
            aboveH = y - blockTop;
            invalidate();
        }
        const draw::Rect pv{x3, y, x3 + side, y + side};
        paintPreview(c, pv);
        previewRect = pv;
        y = pv.bottom;
    }
    if (genShown) {
        // The generator's settings: to the right of the preview (under it in a narrow window).
        const int gx = besides ? x3 + side + pvGap : x3, gr = gx + gw;
        int gy = besides ? previewRect.top : y + scale(12);
        const int labelW = std::min(scale(210), gw / 2);
        const int gTop = gy;
        // Seed, symmetry.
        const int bw = buttonWidth(c, L"Randomize seed");
        c.text({gx, gy, gx + labelW, gy + scale(stardraft::kit::kFormRowH)}, L"Seed", kDim, draw::kMiddle | draw::kEllipsis);
        placeField(seed_, {gx + labelW, gy + scale(4), gr - bw - scale(8), gy + scale(4) + fieldH}, true, 0, footerTop);
        button(c, {gr - bw, gy + scale(2), gr, gy + scale(2) + scale(stardraft::kit::kButtonH)}, L"Randomize seed", false, [this] { randomizeSeed(); });
        gy += scale(stardraft::kit::kFormRowH);
        c.text({gx, gy, gx + labelW, gy + scale(stardraft::kit::kFormRowH)}, L"Symmetry", kDim, draw::kMiddle | draw::kEllipsis);
        // (a kit dropdown: its list opens under it)
        if (symmetry_.value() != symmetry) symmetry_.setValue(symmetry);
        placeField(symmetry_, {gx + labelW, gy + scale(4), gr, gy + scale(4) + fieldH}, true, 0, footerTop);
        gy += scale(stardraft::kit::kFormRowH) + scale(4);
        {  // above the sliders: they stay in their place when a tileset has more or fewer sliders
            const int lx = smallButton(c, gx, gy, rowH, L"Randomize values", [this] { randomize(false); }, L"New amounts, shape and seed");
            smallButton(c, lx + scale(8), gy, rowH, L"Randomize all", [this] { randomize(true); },
                        L"Randomize the tileset, the amounts, the shape and the seed (not the size)");
            gy += rowH + scale(8);
        }
        gy = sectionHeader(c, gx, gr, gy, rowH, L"Amounts");
        for (auto& sl : shares) paintSliderRow(c, gx, gy, labelW, gr, sl, true), gy += rowH;
        gy += scale(12);
        gy = sectionHeader(c, gx, gr, gy, rowH, L"Shape");
        for (auto& sl : shape) paintSliderRow(c, gx, gy, labelW, gr, sl, false), gy += rowH;
        gy += scale(8);
        genRect = {gx, gTop, gr, gy};
        y = std::max(y, gy);
    } else {
        placeField(seed_, {}, false, 0, 0);
        placeField(symmetry_, {}, false, 0, 0);
    }
    bottom = std::max(bottom, y);

    // Create and Cancel: at the bottom right of the block, under the columns. When the block is taller than the page,
    // they stay in view at the bottom of the page, on a bar with a line above it (the columns scroll under it).
    createEnabled = warning.empty();
    tallest = std::max(tallest, bottom - blockTop);  // (Generate is taller than Import: Create stays where it was)
    const int natural = blockTop + tallest + scale(24), lowest = H - pad - bh;
    footerStuck = natural > lowest;
    const int by = footerStuck ? lowest : natural;
    if (footerStuck) {
        const draw::Rect bar{0, by - pad, W, H};
        c.fill(bar, kBg);
        c.fill({0, bar.top, W, bar.top + 1}, kBorder);
        hitAdd(bar, nullptr, L"", draw::Cursor::Arrow);  // a click on the bar does not go to the columns under it
    }
    const int right = x3 + w3, cw = buttonWidth(c, L"Cancel"), crw = buttonWidth(c, L"Create"), bgap = scale(stardraft::kit::kButtonGap);
    button(c, {right - cw, by, right, by + bh}, L"Cancel", false, [this] { cancelPage(); });
    button(c, {right - cw - bgap - crw, by, right - cw - bgap, by + bh}, L"Create", true, [this] { create(); },
           createEnabled ? (project ? L"Make the project folder and its map" : L"Make the map in this tab") : warning, createEnabled);
    footer = {right - cw - bgap - crw, by, right, by + bh};
    const int hgt = natural + bh - blockTop;
    height_ = startY + hgt + pad;
    if (hgt != blockH) {  // the height changed (Generate, a warning): paint again, in the middle
        blockH = hgt;
        invalidate();
    }
}

// --- Input ----------------------------------------------------------------------------------------------------------

bool NewPageView::mouse(const draw::MouseEvent& e) {
    switch (e.kind) {
    case draw::MouseKind::Move:
    case draw::MouseKind::Leave: {
        if (dragging_ >= 0 && e.kind == draw::MouseKind::Move) {
            if (hits[size_t(dragging_)].press) hits[size_t(dragging_)].press(e.pt);
            invalidate();
            return true;
        }
        const int hi = e.kind == draw::MouseKind::Leave ? -1 : hitAt(e.pt);
        if (hi != hover_) hover_ = hi, invalidate();
        return false;
    }
    // A quick second press comes as a double click (SDL counts the clicks: the third and the next ones are too): it is
    // a press as well, so that each of quick presses on Randomize, a radio or a slider acts.
    case draw::MouseKind::Down:
    case draw::MouseKind::DoubleClick: {
        if (e.button != 0) return false;
        const int hi = hitAt(e.pt);
        if (hi < 0) return false;
        Hit& h = hits[size_t(hi)];
        if (h.press) {  // a slider: the press sets it, and the drag after it
            dragging_ = hi;
            if (host()) host()->capture(*this, true);
            h.press(e.pt);
            invalidate();
            return true;
        }
        if (h.click) {
            h.click();
            invalidate();
            return true;
        }
        return false;
    }
    case draw::MouseKind::Up:
        if (dragging_ >= 0) {
            dragging_ = -1;
            if (host()) host()->capture(*this, false);
            invalidate();
            return true;
        }
        return false;
    case draw::MouseKind::Wheel: {
        const int notches = e.wheel > 0 ? 1 : -1;
        for (const auto& [r, step] : wheels)
            if (r.contains(e.pt)) {  // over a slider: the wheel changes it
                step(notches);
                invalidate();
                return true;
            }
        scroll = std::clamp(scroll - notches * scale(96), 0, std::max(0, height_ - height()));  // three lines a notch
        invalidate();
        return true;
    }
    default: return false;
    }
}

void NewPageView::captureLost() {
    if (dragging_ >= 0) dragging_ = -1, invalidate();
}

bool NewPageView::vscroll(int code, int pos, int track, int) {
    const int page = height();
    int s = scroll;
    switch (code) {
    case 0: s = scroll - scale(32); break;  // SB_LINEUP
    case 1: s = scroll + scale(32); break;  // SB_LINEDOWN
    case 2: s = scroll - page; break;       // SB_PAGEUP
    case 3: s = scroll + page; break;       // SB_PAGEDOWN
    case 4: s = pos; break;                 // SB_THUMBPOSITION
    case 5: s = track; break;               // SB_THUMBTRACK
    case 6: s = 0; break;                   // SB_TOP
    case 7: s = height_; break;             // SB_BOTTOM
    default: return false;
    }
    const int s2 = std::clamp(s, 0, std::max(0, height_ - page));
    if (s2 != scroll) scroll = s2, invalidate();
    return true;
}
