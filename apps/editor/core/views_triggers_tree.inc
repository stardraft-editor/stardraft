// The Triggers workspace's tree (core): the project's triggers as a kit TreeView: the three categories (Definitions,
// On map start, On every trigger cycle) with their folders, triggers and variables. Selection, the keys (F2 renames,
// Delete deletes), renaming in place (a kit text field over the label), dragging to move and the right button's menu
// go to the program through TrigTreeHost; the program keeps the TriggerSet and its commands (main.cpp). The rows look
// as the native tree view's did (the system's Explorer tree style): 24 px rows, an indent of 19 px, bold categories with
// their number of triggers at the right, the colored icons, and the system's selection box and chevrons.

// A node's issues (its own and its blocks') of `level`.
inline int trigIssueCount(const TriggerNode& n, TriggerIssue::Level level, const std::vector<TriggerIssue>& issues) {
    int c = 0;
    for (const auto& i : issues) c += i.node == &n && i.level == level;
    for (const auto& ch : n.children) c += trigIssueCount(ch, level, issues);
    return c;
}

// The tree's label of a node: the name, with what to look at (errors, heavy) after it.
inline std::wstring trigTreeLabel(const TriggerNode& n, const std::vector<TriggerIssue>& issues) {
    std::wstring s = widen(n.name);
    if (n.kind == TriggerNode::Kind::Variable) s += L"  (" + widen(TriggerVariable::kindLabel(n.variable.kind)) + L")";
    if (n.kind == TriggerNode::Kind::Trigger && !n.enabled) s += L"  (off)";
    if (n.kind == TriggerNode::Kind::Trigger || n.kind == TriggerNode::Kind::Variable) {
        if (const int e = trigIssueCount(n, TriggerIssue::Level::Error, issues)) s += L"  \x26A0 " + std::to_wstring(e);
        if (n.kind == TriggerNode::Kind::Trigger && TriggerSet::cost(n) > TriggerSet::kHeavy) s += L"  \x23F1";
    }
    return s;
}

// What the tree asks of the program. Paths are TriggerSet paths ({category, child, ...}).
class TrigTreeHost {
public:
    virtual ~TrigTreeHost() = default;
    virtual const TriggerNode* treeNode(const std::vector<int>& path) = 0;  // the model's node (its name for renaming)
    virtual void treeSelected(const std::vector<int>& path) = 0;  // the user selected a node
    // The label typed in place (Enter, or the focus left the field); not called for an empty text or Escape.
    virtual void treeRename(const std::vector<int>& path, const std::string& name) = 0;
    virtual void treeDelete(const std::vector<int>& path) = 0;  // Delete
    // A drag dropped `from` as the `index`th child of `parent` (index as TriggerSet::move takes it: before the removal).
    virtual void treeMove(const std::vector<int>& from, const std::vector<int>& parent, int index) = 0;
    virtual void treeMenu(const std::vector<int>& path) = 0;  // the right button on a node (it is selected first)
};

class TrigTreeView final : public stardraft::kit::TreeView {
public:
    explicit TrigTreeView(TrigTreeHost& host) : host_(host) {
        // The native tree's sizes: 24 px rows, levels 19 px apart, the chevron's box at the left, the 16 px icon at 22
        // and the label at 43 (level 0); no focus line around the tree.
        Metrics m;
        m.rowH = 24, m.level = 19, m.arrow = 0, m.icon = 22, m.text = 43, m.focusFrame = false;
        setMetrics(m);
        onSelect = [this](stardraft::kit::TreeNode* n) {
            if (n) host_.treeSelected(pathOf(n));
        };
        onRename = [this](stardraft::kit::TreeNode* n) { startRename(n); };
        onDelete = [this](stardraft::kit::TreeNode* n) { host_.treeDelete(pathOf(n)); };
        onMove = [this](stardraft::kit::TreeNode& node, stardraft::kit::TreeNode& parent, size_t index) {
            // The program moves the node and rebuilds the tree, so the view's nodes are gone: false (the view does
            // nothing more with them).
            const std::vector<int> from = pathOf(&node), to = pathOf(&parent);
            if (from.size() < 2 || to.empty()) return false;  // categories stay; nothing goes beside them
            int at = int(index);
            if (std::vector<int>(from.begin(), from.end() - 1) == to && at >= from.back()) ++at;  // (the index before the removal)
            host_.treeMove(from, to, at);
            return false;
        };
        field_.setVisible(false);
        field_.onEnter = [this] { endRename(true); };
        field_.onEscape = [this] { endRename(false); };
        field_.onFocusLost = [this] { endRename(true); };
        add(field_, {});
    }

    // Rebuilds the rows from `set` (every node open, as the native tree had them) and selects `select`.
    void rebuild(const TriggerSet& set, const std::vector<TriggerIssue>& issues, const std::vector<int>& select) {
        endRename(false);
        root().children.clear();
        paths_.clear(), info_.clear();
        stardraft::kit::TreeNode* selNode = nullptr;
        std::function<void(stardraft::kit::TreeNode&, const TriggerNode&, std::vector<int>)> addNode =
            [&](stardraft::kit::TreeNode& parent, const TriggerNode& n, std::vector<int> path) {
                static const UiIcon glyphs[4] = {UiIcon::Log, UiIcon::Folder, UiIcon::Triggers, UiIcon::Tag};
                stardraft::kit::TreeNode& t = parent.addChild(trigTreeLabel(n, issues), int(glyphs[int(n.kind) & 3]), paths_.size());
                t.expanded = true;
                t.folder = n.kind == TriggerNode::Kind::Category || n.kind == TriggerNode::Kind::Folder;
                Info inf;
                inf.kind = n.kind;
                if (path.size() == 1) {  // a category: how many triggers it has
                    std::function<int(const TriggerNode&)> count = [&](const TriggerNode& k) {
                        int c = k.kind == TriggerNode::Kind::Trigger;
                        for (const auto& ch : k.children) c += count(ch);
                        return c;
                    };
                    inf.count = count(n);
                }
                paths_.push_back(path), info_.push_back(inf);
                if (path == select) selNode = &t;
                for (size_t i = 0; i < n.children.size(); ++i) {
                    std::vector<int> child = path;
                    child.push_back(int(i));
                    addNode(t, n.children[i], child);
                }
            };
        for (size_t i = 0; i < set.roots.size(); ++i) addNode(root(), set.roots[i], {int(i)});
        refresh();
        selectNode(selNode);
    }

    // The selected node's label again (its issues changed).
    void relabelSelected(const std::wstring& label) {
        if (stardraft::kit::TreeNode* n = selected()) n->label = label, invalidate();
    }

    std::vector<int> selectedPath() const { return pathOf(selected()); }
    std::vector<int> pathOf(const stardraft::kit::TreeNode* n) const {
        return n && n->data < paths_.size() ? paths_[n->data] : std::vector<int>{};
    }

    // F2, the menu's Rename and a new node: the label in a text field (categories keep their names).
    void startRename(stardraft::kit::TreeNode* n) {
        if (!n || n->data >= info_.size() || info_[n->data].kind == TriggerNode::Kind::Category) return;
        const TriggerNode* tn = host_.treeNode(pathOf(n));
        if (!tn) return;
        ensureVisible(n);
        const draw::Rect r = labelRect(n);
        const int h = scale(stardraft::kit::kFieldH);
        renaming_ = pathOf(n);
        field_.setText(widen(tn->name));  // without the notes
        field_.model().selectAll();
        field_.setFrame({r.left - scale(3), r.top + (r.height() - h) / 2, r.right - scale(2), r.top + (r.height() - h) / 2 + h});
        field_.setVisible(true);
        field_.focus();
    }
    bool renaming() const { return !renaming_.empty(); }

    bool mouse(const draw::MouseEvent& e) override {
        if (e.kind == draw::MouseKind::Down && e.button == 1) {  // the right button: select the node, then its menu
            stardraft::kit::TreeNode* n = nodeAt(e.pt);
            if (!n) return true;
            const std::vector<int> path = pathOf(n);
            if (n != selected()) {
                selectNode(n);
                host_.treeSelected(path);
            }
            host_.treeMenu(path);  // (it may rebuild the tree: nothing of the view's is used after it)
            return true;
        }
        return stardraft::kit::TreeView::mouse(e);
    }

protected:
    void paintRow(draw::Canvas& c, const RowLook& row) override {
        const auto& n = row.node;
        const Info inf = n.data < info_.size() ? info_[n.data] : Info{};
        if (row.selected || row.hot || row.dragged) paintSelectionBox(c, row.box, row.selected || row.dragged, row.selected && hasFocus());
        if (!n.children.empty()) paintChevron(c, row.arrowLeft, row.box.top, n.expanded);
        static const COLORREF iconColors[4] = {RGB(150, 150, 150), RGB(220, 184, 104), 0, 0};
        const int k = int(inf.kind) & 3;
        const COLORREF ic = k == 2 ? kAccent : k == 3 ? kGood : iconColors[k];
        const int sz = scale(16), top = row.box.top + (row.box.height() - sz) / 2;
        if (n.icon >= 0) {
            if (draw::Bitmap* b = iconBitmap(k, n.icon, ic, sz)) c.bitmap(*b, {0, 0, sz, sz}, {row.iconLeft, top, row.iconLeft + sz, top + sz});
            else c.icon({row.iconLeft, top, row.iconLeft + sz, top + sz}, n.icon, ic, sz);
        }
        const bool bold = inf.kind == TriggerNode::Kind::Category;
        const draw::Font old = c.setFont(c.font({draw::Face::Ui, 0, bold}));
        const draw::Rect tr{row.textLeft, row.box.top, row.box.right, row.box.bottom};
        // (The selected row's text: the system's, white in the dark theme and black in the light one.)
        const COLORREF text = row.selected ? (gLightTheme ? RGB(0, 0, 0) : RGB(255, 255, 255)) : kText;
        c.text(tr, n.label, text, draw::kMiddle | draw::kLeft);
        c.setFont(old);
        if (inf.count > 0)  // the category's number of triggers, dim at the right
            c.text({row.box.left, row.box.top, row.box.right - scale(10), row.box.bottom}, std::to_wstring(inf.count), kDim,
                   draw::kMiddle | draw::kRight);
    }

private:
    struct Info {
        TriggerNode::Kind kind = TriggerNode::Kind::Trigger;
        int count = 0;
    };

    // The Explorer tree's selection box (as the system drew it): a gray box with soft corners; the tree without the
    // focus. With the focus a lighter (light theme: blue) box; under the mouse a quieter one.
    static void paintSelectionBox(draw::Canvas& c, draw::Rect b, bool selected, bool focused) {
        const int w = b.width(), h = b.height();
        if (w < 4 || h < 4) return;
        auto px = [&](int x, int y, COLORREF col) { c.fill({b.left + x, b.top + y, b.left + x + 1, b.top + y + 1}, col); };
        auto row = [&](int y, int x0, int x1, COLORREF col) { c.fill({b.left + x0, b.top + y, b.left + x1, b.top + y + 1}, col); };
        if (!gLightTheme && selected && !focused) {
            c.fill({b.left + 1, b.top + 2, b.right - 1, b.bottom - 2}, RGB(51, 51, 51));
            for (int y : {0, h - 1}) row(y, 2, w - 2, RGB(47, 47, 47)), px(0, y, RGB(37, 37, 37)), px(1, y, RGB(45, 45, 45)), px(w - 2, y, RGB(45, 45, 45)), px(w - 1, y, RGB(37, 37, 37));
            for (int y : {1, h - 2}) row(y, 2, w - 2, RGB(51, 51, 51)), px(0, y, RGB(45, 45, 45)), px(1, y, RGB(50, 50, 50)), px(w - 2, y, RGB(50, 50, 50)), px(w - 1, y, RGB(45, 45, 45));
            c.fill({b.left, b.top + 2, b.left + 1, b.bottom - 2}, RGB(47, 47, 47));
            c.fill({b.right - 1, b.top + 2, b.right, b.bottom - 2}, RGB(47, 47, 47));
            return;
        }
        if (gLightTheme && selected && !focused) {
            c.fill({b.left + 1, b.top + 1, b.right - 1, b.bottom - 1}, RGB(207, 207, 208));
            for (int y : {0, h - 1}) row(y, 1, w - 1, RGB(141, 141, 142)), px(0, y, RGB(150, 150, 151)), px(w - 1, y, RGB(150, 150, 151));
            c.fill({b.left, b.top + 1, b.left + 1, b.bottom - 1}, RGB(141, 141, 142));
            c.fill({b.right - 1, b.top + 1, b.right, b.bottom - 1}, RGB(141, 141, 142));
            return;
        }
        // Focused, or under the mouse: the Explorer style's colors (a box with a 1 px line).
        const COLORREF fill = gLightTheme ? (selected ? RGB(204, 232, 255) : RGB(229, 243, 255)) : (selected ? RGB(77, 77, 77) : RGB(45, 45, 45));
        const COLORREF line = gLightTheme ? (selected ? RGB(153, 209, 255) : RGB(204, 232, 255)) : (selected ? RGB(98, 98, 98) : RGB(45, 45, 45));
        c.fill(b, fill);
        c.frame(b, line);
    }

    // The Explorer tree's chevron (down when open, right when shut) as the system drew it at 100 %; other scales draw
    // it as lines.
    static void paintChevron(draw::Canvas& c, int left, int top, bool open) {
        static const uint8_t dark[7][9] = {{2, 0, 0, 0, 0, 0, 0, 0, 2},           {233, 161, 3, 0, 0, 0, 1, 150, 233},
                                           {150, 255, 165, 3, 0, 1, 151, 255, 150}, {1, 151, 255, 155, 2, 151, 255, 151, 1},
                                           {0, 1, 151, 255, 214, 255, 151, 1, 0},   {0, 0, 1, 151, 255, 151, 1, 0, 0},
                                           {0, 0, 0, 1, 58, 1, 0, 0, 0}};
        static const uint8_t light[7][9] = {{0, 0, 0, 0, 0, 0, 0, 0, 0},           {120, 137, 1, 0, 0, 0, 1, 137, 120},
                                            {137, 255, 151, 1, 0, 1, 151, 255, 137}, {1, 151, 255, 151, 2, 151, 255, 151, 1},
                                            {0, 1, 151, 255, 214, 255, 151, 1, 0},   {0, 0, 1, 151, 255, 151, 1, 0, 0},
                                            {0, 0, 0, 1, 55, 1, 0, 0, 0}};
        const COLORREF col = gLightTheme ? RGB(27, 27, 27) : RGB(255, 255, 255);
        if (scale(100) != 100) {
            const int cx = left + scale(10), cy = top + scale(12), s = scale(4);
            const draw::Point open3[] = {{cx - s, cy - s / 2}, {cx, cy + s / 2}, {cx + s, cy - s / 2}};
            const draw::Point shut3[] = {{cx - s / 2, cy - s}, {cx + s / 2, cy}, {cx - s / 2, cy + s}};
            c.polyline(open ? open3 : shut3, 3, col, std::max(1, scale(1)));
            return;
        }
        const auto& mask = gLightTheme ? light : dark;
        for (int y = 0; y < 7; ++y)
            for (int x = 0; x < 9; ++x) {
                const uint8_t a = mask[y][x];
                if (!a) continue;
                // Open: the mask as it is (x 6, y 8 in the row); shut: turned to point right (its apex at the right).
                const int px = open ? left + 6 + x : left + 7 + y, py = open ? top + 8 + y : top + 8 + x;
                c.fill({px, py, px + 1, py + 1}, col, a);
            }
    }

    // A kind's icon as the native tree's image list drew it: the icon tinted with its color, then blended with its
    // coverage once more (the image list took the tinted pixels as not yet multiplied by their coverage), so the
    // smoothed edges are darker than an icon drawn in the color. nullptr when the atlas cannot be read here.
    draw::Bitmap* iconBitmap(int kind, int icon, COLORREF col, int sz) {
        IconCache& ic = icons_[kind & 3];
        if (ic.failed && ic.sz == sz) return nullptr;
        if (ic.sz == sz && ic.color == col && ic.icon == icon) return &ic.bmp;
        ic.sz = sz, ic.color = col, ic.icon = icon, ic.failed = false;
        const int atlas = sz >= 32 ? 32 : sz >= 28 ? 24 : 16;  // (as uiIconSize)
        std::vector<uint8_t> mask;
        if (!shellUiIconMask(atlas, icon, mask)) return ic.failed = true, nullptr;
        ic.bmp.resize(sz, sz);
        uint32_t* px = ic.bmp.pixels();
        const int off = (sz - atlas) / 2;  // the atlas icon in the middle of the image
        const uint32_t cr = col & 0xFF, cg = (col >> 8) & 0xFF, cb = (col >> 16) & 0xFF;
        for (int y = 0; y < atlas; ++y)
            for (int x = 0; x < atlas; ++x) {
                const int tx = x + off, ty = y + off;
                if (tx < 0 || ty < 0 || tx >= sz || ty >= sz) continue;
                const uint32_t a = mask[size_t(y) * size_t(atlas) + size_t(x)];
                auto twice = [a](uint32_t v) { return (v * a / 255 * a + 127) / 255; };
                const uint32_t r = twice(cr), g = twice(cg), b = twice(cb);
                px[size_t(ty) * size_t(sz) + size_t(tx)] = r | g << 8 | b << 16 | a << 24;
            }
        ic.bmp.changed();
        return &ic.bmp;
    }

    void endRename(bool commit) {
        if (renaming_.empty()) return;
        const std::vector<int> path = std::exchange(renaming_, {});
        const std::wstring text = field_.text();
        field_.setVisible(false);
        if (field_.hasFocus()) focus();
        if (commit && !text.empty()) host_.treeRename(path, narrow(text));
    }

    TrigTreeHost& host_;
    std::vector<std::vector<int>> paths_;  // a node's data -> its path
    std::vector<Info> info_;               // a node's data -> its kind and (categories) number of triggers
    stardraft::kit::TextField field_;      // the label being renamed
    std::vector<int> renaming_;            // the node being renamed (empty: none)
    struct IconCache {
        draw::Bitmap bmp;
        int sz = 0, icon = -1;
        COLORREF color = 0;
        bool failed = false;
    };
    IconCache icons_[4];  // category, folder, trigger, variable
};
