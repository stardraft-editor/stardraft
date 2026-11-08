// A compact symmetry composer: generators, a spatial preview, and one copy's settings.
class SymmetryCopiesView final : public draw::View {
    class Diagram final : public draw::View {
    public:
        explicit Diagram(SymmetryCopiesView& owner) : owner(owner) {}
        draw::Point point(size_t i) const {
            const auto t = owner.transforms[i];
            return {width() / 2 + int((t.a * .27 - t.b * .34) * (width() - scale(40))),
                    height() / 2 + int((t.c * .27 - t.d * .34) * (height() - scale(40)))};
        }
        void paint(draw::Canvas& c) override {
            const auto& th = kit::theme();
            c.roundBox({0, 0, width(), height()}, th.panel, th.border, float(scale(6)));
            c.line(width() / 2, scale(12), width() / 2, height() - scale(12), th.rowLine, 1, draw::LineStyle::Dot);
            c.line(scale(12), height() / 2, width() - scale(12), height() / 2, th.rowLine, 1, draw::LineStyle::Dot);
            for (size_t n = 0; n < owner.transforms.size(); ++n) {
                const size_t i = (size_t(owner.selected()) + 1 + n) % owner.transforms.size();
                const auto p = point(i);
                const bool enabled = owner.copyEnabled(i);
                const auto color = !i ? th.good : enabled ? th.accent : th.faint;
                c.line(width() / 2, height() / 2, p.x, p.y, th.rowLine);
                const int radius = scale(i == size_t(owner.selected()) ? 7 : owner.transforms.size() > 12 ? 3 : 5);
                c.ellipse({p.x - radius, p.y - radius, p.x + radius, p.y + radius}, enabled ? color : th.panel, color);
                if (i == size_t(owner.selected()))
                    c.ring({p.x - radius - scale(3), p.y - radius - scale(3), p.x + radius + scale(3), p.y + radius + scale(3)}, th.text);
                if (owner.transforms.size() <= 12 || !i || i == size_t(owner.selected()))
                    c.text({p.x - scale(14), p.y + scale(10), p.x + scale(18), p.y + scale(28)}, std::to_wstring(i), color, draw::kCenter);
            }
        }
        bool mouse(const draw::MouseEvent& e) override {
            if ((e.kind != draw::MouseKind::Down && e.kind != draw::MouseKind::DoubleClick) || e.button) return false;
            int best = -1, distance = scale(20) * scale(20);
            for (size_t i = 0; i < owner.transforms.size(); ++i) {
                const auto p = point(i);
                const int d = (p.x - e.pt.x) * (p.x - e.pt.x) + (p.y - e.pt.y) * (p.y - e.pt.y);
                if (d <= distance) distance = d, best = int(i);
            }
            if (best < 0) return false;
            owner.list.selectOnly(best), owner.selectCopy();
            if (e.kind == draw::MouseKind::DoubleClick) owner.toggleCopy(best);
            return true;
        }
        std::wstring tip(draw::Point p) override {
            for (size_t i = 0; i < owner.transforms.size(); ++i) {
                const auto at = point(i);
                if (std::hypot(at.x - p.x, at.y - p.y) <= scale(14)) return owner.name(i);
            }
            return tr(L"Click a copy to edit it. Double-click to turn it on or off.");
        }

    private:
        SymmetryCopiesView& owner;
    };
    class CopiesList final : public kit::ListView {
    public:
        explicit CopiesList(SymmetryCopiesView& owner) : owner(owner) {}
        bool mouse(const draw::MouseEvent& e) override {
            if (e.kind == draw::MouseKind::Down && !e.button && e.pt.x < scale(30)) {
                const int row = rowAt(e.pt);
                if (row >= 0) {
                    selectOnly(row);
                    owner.selectCopy();
                    owner.toggleCopy(row);
                    return true;
                }
            }
            return ListView::mouse(e);
        }
        bool key(const draw::KeyEvent& e) override {
            if (e.down && e.key == draw::Key::Space) {
                owner.toggleCopy(cursor());
                return true;
            }
            return ListView::key(e);
        }

    private:
        SymmetryCopiesView& owner;
    };

public:
    explicit SymmetryCopiesView(Editor& ed) : options(ed.copyOptions()), tileset(ed.tileset()), diagram(*this), list(*this) {
        mode.setItems({{tr(L"Off")}, {tr(L"Mirrors")}, {tr(L"Rotations")}, {tr(L"Custom copies")}});
        mode.setValue(options.direct || (ed.turns() > 1 && (ed.mirrors() || options.opposite)) ? 3
                      : ed.turns() > 1                                                         ? 2
                      : (ed.mirrors() || options.opposite)                                     ? 1
                                                                                               : 0);
        combinations.setLabel(tr(L"Include combinations"));
        combinations.setTip(tr(L"Add copies made by combining the selected mirrors and rotations. Turn this off for a direct union."));
        combinations.setOn(!options.direct);
        combinations.onChange = [this](bool) { rebuild(); };
        std::vector<kit::DropdownItem> counts;
        for (int n = 2; n <= kMaxTurns; ++n) counts.push_back({std::to_wstring(n)});
        folds.setItems(std::move(counts)), folds.setValue((ed.turns() > 1 ? ed.turns() : 4) - 2);
        rotated.setLabel(tr(L"Include rotations")), rotated.setOn(!options.direct || ed.turns() > 1);
        reflected.setLabel(tr(L"Mirrored rotations")), reflected.setOn(options.mirroredRotations);
        const wchar_t* mirrorNames[] = {L"Left / right", L"Top / bottom", L"Diagonal \\", L"Diagonal /", L"Opposite corners"};
        for (int m = 0; m < 5; ++m) {
            mirrors[m].setLabel(tr(mirrorNames[m]));
            mirrors[m].setOn(m == 4 ? options.opposite : bool(ed.mirrors() & (1 << m)));
            mirrors[m].onChange = [this](bool) { rebuild(); };
            add(mirrors[m], {});
        }
        for (auto* view : std::vector<draw::View*>{&mode, &folds, &rotated, &reflected, &diagram, &list, &defaultTerrain, &copyTerrain,
                                                   &copyOn, &rawFields, &combinations, &defaultRaw, &copyRaw, &all, &none})
            add(*view, {});
        mode.onChange = folds.onChange = [this](int) { rebuild(); };
        rotated.onChange = reflected.onChange = [this](bool) { rebuild(); };
        std::vector<kit::DropdownItem> items{{tr(L"Same as source brush")}};
        terrains.push_back(-1);
        for (int t = 1; t < 512; ++t) {
            const auto title = ed.terrainName(uint16_t(t));
            if (!title.empty()) items.push_back({widen(title)}), terrains.push_back(t);
        }
        for (const auto& mod : ed.tileset().modTerrains)
            if (!mod.variantOf && std::find(terrains.begin(), terrains.end(), mod.id) == terrains.end()) {
                items.push_back({widen(mod.name)}), terrains.push_back(mod.id);
            }
        defaultTerrain.setItems(items), defaultTerrain.setValue(index(options.terrain));
        defaultTerrain.searchable = true;
        defaultTerrain.onChange = [this](int) {
            list.invalidate();
            diagram.invalidate();
        };
        items[0].label = tr(L"Use default terrain");
        copyTerrain.setItems(items), copyTerrain.searchable = true;
        defaultRaw.setPlaceholder(tr(L"Inherit brush tile"));
        defaultRaw.setText(options.rawTile < 0 ? L"" : std::to_wstring(options.rawTile));
        copyRaw.setPlaceholder(tr(L"Use default"));
        defaultRaw.onChange = [this] { invalidate(); };
        copyRaw.onChange = [this] { saveCopy(); };
        copyTerrain.onChange = [this](int) { saveCopy(); };
        copyOn.setLabel(tr(L"Place this copy"));
        copyOn.onChange = [this](bool) {
            saveCopy();
            selectCopy();
        };
        rawFields.setLabel(tr(L"Raw tile IDs"));
        rawFields.setOn(options.rawTile >= 0 ||
                        std::any_of(options.choices.begin(), options.choices.end(), [](const auto& c) { return c.rawTile >= 0; }));
        rawFields.onChange = [this](bool) { resized(); };
        all.setText(tr(L"Enable all")), none.setText(tr(L"Source only"));
        all.onClick = [this] {
            for (size_t i = 1; i < transforms.size(); ++i) choice(i).enabled = true;
            selectCopy();
        };
        none.onClick = [this] {
            for (size_t i = 1; i < transforms.size(); ++i) choice(i).enabled = false;
            selectCopy();
        };
        list.setRowHeight(40);
        list.label = [this](int row) { return name(size_t(row)); };
        list.onSelect = [this] { selectCopy(); };
        list.onActivate = [this](int row) { toggleCopy(row); };
        list.paintRow = [this](draw::Canvas& c, int row, draw::Rect r, kit::RowState state) {
            const auto& th = kit::theme();
            c.fill(r, state.selected ? th.sel : state.hot ? th.rowHot : th.panel);
            const bool on = copyEnabled(size_t(row));
            const int x = r.left + scale(8), y = (r.top + r.bottom) / 2;
            c.roundBox({x, y - scale(6), x + scale(12), y + scale(6)}, on ? th.accentFill : th.panel, on ? th.accent : th.borderStrong,
                       float(scale(2)));
            if (on) {
                c.line(x + scale(3), y, x + scale(5), y + scale(2), th.onText);
                c.line(x + scale(5), y + scale(2), x + scale(9), y - scale(3), th.onText);
            }
            c.text({x + scale(22), r.top, r.right - scale(6), r.top + scale(21)}, std::to_wstring(row) + L"  " + name(size_t(row)),
                   on ? th.text : th.faint, draw::kMiddle | draw::kEllipsis);
            c.text({x + scale(22), r.top + scale(20), r.right - scale(6), r.bottom}, terrainLabel(size_t(row)), on ? th.dim : th.faint,
                   draw::kMiddle | draw::kEllipsis);
        };
        setSize(preferredSize().x, preferredSize().y);
        rebuild();
    }
    static draw::Point preferredSize() { return {scale(684), scale(424)}; }
    int chosenMirrors() const {
        if (mode.value() == 0 || mode.value() == 2) return 0;
        int mask = 0;
        for (int i = 0; i < 4; ++i)
            if (mirrors[i].on()) mask |= 1 << i;
        return mask;
    }
    int chosenTurns() const { return mode.value() == 2 || (mode.value() == 3 && rotated.on()) ? folds.value() + 2 : 1; }
    CopyOptions chosen() const {
        auto out = options;
        out.direct = mode.value() == 3 && !combinations.on();
        out.mirroredRotations = chosenTurns() > 1 && reflected.on();
        out.opposite = (mode.value() == 1 || mode.value() == 3) && mirrors[4].on();
        out.terrain = terrains[size_t(defaultTerrain.value())], out.rawTile = parse(defaultRaw);
        return out;
    }
    static bool controlsCheck(Editor& ed, std::string& out) {
        const auto original = ed.copyOptions();
        const int originalMirrors = ed.mirrors(), originalTurns = ed.turns();
        int checks = 0, failures = 0;
        auto check = [&](bool ok, const char* label) {
            ++checks;
            if (!ok) {
                ++failures;
                out += std::string("symmetry-controls: ") + label + " (WRONG)\n";
            }
        };
        const std::array<std::array<int, 5>, 7> cases{{{{3, 4, 0, 0, 0}},
                                                       {{3, 4, 1, 0, 0}},
                                                       {{0, 4, 0, 1, 0}},
                                                       {{0, 1, 0, 0, 1}},
                                                       {{1, 15, 1, 1, 1}},
                                                       {{0, 1, 0, 0, 0}},
                                                       {{0, 3, 0, 0, 1}}}};
        for (const auto& setup : cases) {
            CopyOptions options;
            options.direct = setup[2], options.mirroredRotations = setup[3], options.opposite = setup[4];
            ed.setMirrors(setup[0]), ed.setTurns(setup[1]), ed.setCopyOptions(options);
            const auto before = ed.availableCopies();
            SymmetryCopiesView view(ed);
            const auto chosen = view.chosen();
            const auto after =
                copyTransforms(view.chosenMirrors(), view.chosenTurns(), chosen.direct, chosen.mirroredRotations, chosen.opposite);
            check(after.size() == before.size(), "opening the composer changed the copy count");
            bool equal = before.size() == after.size();
            for (size_t i = 0; equal && i < before.size(); ++i) equal = before[i].key() == after[i].key();
            check(equal, "opening the composer changed the transforms");
            check(copyOptionsText(chosen) == copyOptionsText(options), "opening the composer changed copy settings");
            check(ed.mirrors() == setup[0] && ed.turns() == setup[1] && copyOptionsText(ed.copyOptions()) == copyOptionsText(options),
                  "composer edited source settings before OK");
        }
        ed.setMirrors(3), ed.setTurns(4), ed.setCopyOptions({});
        SymmetryCopiesView view(ed);
        check(view.mode.value() == 3 && view.combinations.on() && view.transforms.size() == 8,
              "mixed setup did not use combined custom mode");
        view.combinations.setOn(false), view.rebuild();
        check(view.chosen().direct && view.transforms.size() == 6, "direct custom union is wrong");
        view.combinations.setOn(true), view.rebuild();
        check(!view.chosen().direct && view.transforms.size() == 8, "combined custom union is wrong");
        ed.setMirrors(originalMirrors), ed.setTurns(originalTurns), ed.setCopyOptions(original);
        out += "symmetry-controls: " + std::to_string(checks) + " checks, " + std::to_string(failures) + " failed\n";
        return true;
    }
    bool valid() const {
        if (!validRaw(parse(defaultRaw))) return false;
        for (const auto& c : options.choices)
            if (!validRaw(c.rawTile)) return false;
        return true;
    }

private:
    int selected() const { return std::max(0, list.cursor()); }
    int index(int terrain) const {
        const auto it = std::find(terrains.begin(), terrains.end(), terrain);
        return it == terrains.end() ? 0 : int(it - terrains.begin());
    }
    static int parse(const kit::TextField& field) {
        if (field.text().empty()) return -1;
        try {
            size_t end = 0;
            const int v = std::stoi(field.text(), &end);
            return end == field.text().size() ? v : -2;
        } catch (...) {
            return -2;
        }
    }
    bool validRaw(int v) const { return v == -1 || (v >= 0 && v < 65535 && tileset.groupFor(uint16_t(v))); }
    CopyChoice& choice(size_t row) {
        const auto key = transforms[row].key();
        auto it = std::find_if(options.choices.begin(), options.choices.end(), [&](const auto& c) { return c.key == key; });
        if (it == options.choices.end()) {
            options.choices.push_back({key});
            return options.choices.back();
        }
        return *it;
    }
    CopyChoice currentChoice(size_t row) const {
        const auto key = transforms[row].key();
        const auto it = std::find_if(options.choices.begin(), options.choices.end(), [&](const auto& c) { return c.key == key; });
        return it == options.choices.end() ? CopyChoice{key} : *it;
    }
    bool copyEnabled(size_t row) const {
        if (!row) return true;
        const auto key = transforms[row].key();
        const auto it = std::find_if(options.choices.begin(), options.choices.end(), [&](const auto& c) { return c.key == key; });
        return it == options.choices.end() || it->enabled;
    }
    std::wstring terrainLabel(size_t row) const {
        if (!row) return tr(L"Current source brush");
        const auto key = transforms[row].key();
        const auto it = std::find_if(options.choices.begin(), options.choices.end(), [&](const auto& c) { return c.key == key; });
        const int terrain = it == options.choices.end() ? -1 : it->terrain;
        const int tile = it == options.choices.end() || it->rawTile < 0 ? parse(defaultRaw) : it->rawTile;
        std::wstring label =
            terrain >= 0 ? copyTerrain.items()[size_t(index(terrain))].label : defaultTerrain.items()[size_t(defaultTerrain.value())].label;
        if (tile >= 0) label += std::wstring(L" · ") + tr(L"Tile ") + std::to_wstring(tile);
        return label;
    }
    std::wstring name(size_t row) const {
        if (!row) return tr(L"Source brush");
        const auto t = transforms[row];
        const auto key = t.key();
        if (key == CopyTransform{-1, 0, 0, 1}.key()) return tr(L"Left / right");
        if (key == CopyTransform{1, 0, 0, -1}.key()) return tr(L"Top / bottom");
        if (key == CopyTransform{0, 1, 1, 0}.key()) return tr(L"Diagonal \\");
        if (key == CopyTransform{0, -1, -1, 0}.key()) return tr(L"Diagonal /");
        const bool reflected = t.a * t.d - t.b * t.c < 0;
        double angle = std::atan2(reflected ? -t.c : t.c, reflected ? -t.a : t.a) * 180 / 3.141592653589793;
        if (angle < -.01) angle += 360;
        return tr(L"Rotate ") + std::to_wstring(int(std::lround(angle))) + (reflected ? tr(L" deg + mirror") : tr(L" deg"));
    }
    void rebuild() {
        const auto old = transforms.empty() ? std::string{} : transforms[size_t(std::min(selected(), int(transforms.size()) - 1))].key();
        const auto setting = chosen();
        transforms = copyTransforms(chosenMirrors(), chosenTurns(), setting.direct, setting.mirroredRotations, setting.opposite);
        for (auto& m : mirrors) m.setEnabled(mode.value() == 1 || mode.value() == 3);
        rotated.setVisible(mode.value() == 3);
        combinations.setVisible(mode.value() == 3);
        const bool rotation = mode.value() == 2 || (mode.value() == 3 && rotated.on());
        folds.setEnabled(rotation), reflected.setEnabled(rotation);
        list.setCount(int(transforms.size()));
        const auto it = std::find_if(transforms.begin(), transforms.end(), [&](const auto& t) { return t.key() == old; });
        list.selectOnly(it == transforms.end() ? (transforms.size() > 1 ? 1 : 0) : int(it - transforms.begin()));
        selectCopy();
    }
    void saveCopy() {
        const int row = selected();
        if (!row || row >= int(transforms.size())) return;
        auto& c = choice(size_t(row));
        c.enabled = copyOn.on(), c.terrain = terrains[size_t(std::max(0, copyTerrain.value()))], c.rawTile = parse(copyRaw);
        list.invalidate(), diagram.invalidate(), invalidate();
    }
    void selectCopy() {
        const int row = selected();
        copyOn.setEnabled(row != 0), copyTerrain.setEnabled(row != 0);
        copyRaw.setReadOnly(row == 0), copyRaw.setTabStop(row != 0);
        if (row && row < int(transforms.size())) {
            const auto c = currentChoice(size_t(row));
            copyOn.setOn(c.enabled), copyTerrain.setValue(index(c.terrain)),
                copyRaw.setText(c.rawTile == -1 ? L"" : std::to_wstring(c.rawTile));
            copyTerrain.setEnabled(c.enabled), copyRaw.setReadOnly(!c.enabled);
        } else copyOn.setOn(true), copyTerrain.setValue(0), copyRaw.setText(L"");
        list.invalidate(), diagram.invalidate(), invalidate();
    }
    void toggleCopy(int row) {
        if (row <= 0 || row >= int(transforms.size())) return;
        auto& c = choice(size_t(row));
        c.enabled = !c.enabled;
        selectCopy();
    }
    void resized() override {
        const int pad = scale(12), right = width() - pad;
        mode.setFrame({pad, scale(28), scale(200), scale(52)});
        folds.setFrame({scale(220), scale(28), scale(306), scale(52)});
        reflected.setFrame({scale(326), scale(28), right, scale(52)});
        rotated.setFrame({scale(220), scale(62), scale(450), scale(86)});
        combinations.setFrame({pad, scale(68), scale(206), scale(92)});
        for (int i = 0; i < 5; ++i) mirrors[i].setFrame({pad, scale(100 + i * 28), scale(200), scale(124 + i * 28)});
        diagram.setFrame({scale(220), scale(96), scale(418), scale(264)});
        list.setFrame({scale(434), scale(96), right, scale(264)});
        all.setFrame({pad, scale(250), scale(104), scale(278)});
        none.setFrame({scale(112), scale(250), scale(204), scale(278)});
        rawFields.setFrame({scale(450), scale(279), right, scale(303)});
        defaultTerrain.setFrame({scale(164), scale(312), rawFields.on() ? scale(444) : right, scale(336)});
        defaultRaw.setFrame({scale(456), scale(312), right, scale(336)});
        copyOn.setFrame({pad, scale(350), scale(164), scale(374)});
        copyTerrain.setFrame({scale(164), scale(350), rawFields.on() ? scale(444) : right, scale(374)});
        copyRaw.setFrame({scale(456), scale(350), right, scale(374)});
        defaultRaw.setVisible(rawFields.on()), copyRaw.setVisible(rawFields.on());
        invalidate();
    }
    void paint(draw::Canvas& c) override {
        c.fill({0, 0, width(), height()}, kBg);
        auto text = [&](int x, int y, int right, const std::wstring& s, draw::Color color) {
            c.text({scale(x), scale(y), scale(right), scale(y + 22)}, s, color, draw::kMiddle | draw::kEllipsis);
        };
        text(12, 4, 200, tr(L"Symmetry mode"), kDim);
        text(220, 4, 306, tr(L"Copies"), kDim);
        if (mode.value() != 3) text(12, 72, 200, tr(L"Mirrors"), kDim);
        int count = 1;
        for (size_t i = 1; i < transforms.size(); ++i)
            if (copyEnabled(i)) ++count;
        text(434, 72, 672, std::to_wstring(count) + L" / " + std::to_wstring(transforms.size()) + tr(L" copies enabled"), kDim);
        text(12, 312, 160, tr(L"Default terrain"), kDim);
        text(12, 280, 434, tr(L"Copy ") + std::to_wstring(selected()) + L" · " + name(size_t(selected())), kText);
        const auto note = valid() ? tr(L"Click a dot or row to edit a copy. Double-click to turn it on or off.")
                                  : tr(L"Choose a raw tile ID that exists in this tileset, or leave it blank.");
        text(12, 390, 672, note, valid() ? kDim : kit::theme().bad);
    }
    CopyOptions options;
    const Tileset& tileset;
    std::vector<CopyTransform> transforms;
    std::vector<int> terrains;
    kit::Dropdown mode, folds, defaultTerrain, copyTerrain;
    kit::CheckBox mirrors[5], rotated, reflected, copyOn, rawFields, combinations;
    kit::TextField defaultRaw, copyRaw;
    kit::Button all, none;
    Diagram diagram;
    CopiesList list;
};

bool runSymmetryCopiesDialog(draw::View& from, Editor& ed) {
    syncTerrainCopies();
    SymmetryCopiesView content(ed);
    kit::Dialog dlg;
    dlg.setContent(content);
    dlg.setButtons({{tr(L"OK"), 1, true}, {tr(L"Cancel"), 2, false, true}});
    dlg.setAllow([&](int result) { return result != 1 || content.valid(); });
    auto size = SymmetryCopiesView::preferredSize();
    size.x += scale(32), size.y += scale(80);
    if (dlg.run(from, tr(L"Symmetry copies"), size) != 1) return false;
    ed.setMirrors(content.chosenMirrors()), ed.setTurns(content.chosenTurns()), ed.setCopyOptions(content.chosen());
    rememberTerrainCopies();
    if (gActivePanel) panelStore(*gActivePanel);
    appState().dirty = true;
    return true;
}

bool symmetryCopiesCheck(const std::string& name, std::string& out) {
    if (name != "symmetry-controls") return false;
    if (!appState().ed || !appState().ed->isOpen()) {
        out = "symmetry-controls: open a map (WRONG)\n";
        return true;
    }
    return SymmetryCopiesView::controlsCheck(*appState().ed, out);
}
