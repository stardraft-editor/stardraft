#include "tree_view.h"

#include <algorithm>
#include <cstdlib>
#include <cwctype>

namespace stardraft::kit {

using draw::Canvas;
using draw::Color;
using draw::Point;
using draw::Rect;

namespace {

// Layout, scaled with px(): the arrow's box, the scroll bar's strip at the right edge (its track sits inside it). A
// row's own sizes are the view's (TreeView::Metrics).
int arrowW() { return px(16); }
int scrollStrip() { return px(10); }
int dragSlop() { return px(4); }
int typeMs() { return 1000; }

// A node's place in the tree, as the owner keeps it: found by address.
bool isSelfOrBelow(const TreeNode* a, const TreeNode* x) {
    if (a == x) return true;
    for (const auto& c : a->children)
        if (isSelfOrBelow(c.get(), x)) return true;
    return false;
}
bool locate(TreeNode& parent, const TreeNode* n, TreeNode*& outParent, size_t& index) {
    for (size_t i = 0; i < parent.children.size(); ++i) {
        if (parent.children[i].get() == n) {
            outParent = &parent, index = i;
            return true;
        }
        if (locate(*parent.children[i], n, outParent, index)) return true;
    }
    return false;
}
bool findPath(TreeNode& parent, const TreeNode* target, std::vector<TreeNode*>& path) {
    for (auto& c : parent.children) {
        path.push_back(c.get());
        if (c.get() == target || findPath(*c, target, path)) return true;
        path.pop_back();
    }
    return false;
}

// Decodes UTF-8 (a bad byte is skipped). Code points above 0xFFFF become surrogate pairs where wchar_t is 16 bits.
std::wstring fromUtf8(const std::string& s) {
    std::wstring out;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const int n = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
        if (n < 0 || i + size_t(n) >= s.size()) {
            ++i;
            continue;
        }
        uint32_t cp = n == 0 ? c : uint32_t(c & (0x3F >> n));
        for (int k = 1; k <= n; ++k) cp = cp << 6 | uint32_t(static_cast<unsigned char>(s[i + size_t(k)]) & 0x3F);
        i += size_t(n) + 1;
        if (sizeof(wchar_t) == 2 && cp >= 0x10000) {
            cp -= 0x10000;
            out += wchar_t(0xD800 + (cp >> 10));
            out += wchar_t(0xDC00 + (cp & 0x3FF));
        } else {
            out += wchar_t(cp);
        }
    }
    return out;
}
std::wstring lowered(std::wstring s) {
    for (auto& c : s) c = wchar_t(std::towlower(wint_t(c)));  // ASCII letters in the C locale; other text matches as it is
    return s;
}

// A chevron: right when shut, down when open.
void chevron(Canvas& c, int left, int top, int rowH, bool open, Color col) {
    const int cx = left + arrowW() / 2, cy = top + rowH / 2, s = px(5), w = std::max(1, px(1));
    if (open) {
        const Point p[] = {{cx - s, cy - s / 2}, {cx, cy + s / 2}, {cx + s, cy - s / 2}};
        c.polyline(p, 3, col, w);
    } else {
        const Point p[] = {{cx - s / 2, cy - s}, {cx + s / 2, cy}, {cx - s / 2, cy + s}};
        c.polyline(p, 3, col, w);
    }
}

}  // namespace

TreeNode& TreeNode::addChild(std::wstring text, int iconId, uintptr_t value) {
    auto n = std::make_unique<TreeNode>();
    n->label = std::move(text);
    n->icon = iconId;
    n->data = value;
    children.push_back(std::move(n));
    return *children.back();
}

TreeView::TreeView() = default;

// --- Sizes -----------------------------------------------------------------------------------------------------------

void TreeView::setMetrics(const Metrics& m) {
    metrics_ = m;
    refresh();
}
int TreeView::rowH() const { return px(metrics_.rowH); }
int TreeView::levelW() const { return px(metrics_.level); }
int TreeView::arrowLeft(int depth) const { return px(metrics_.arrow) + depth * levelW(); }
int TreeView::iconLeft(int depth) const { return px(metrics_.icon) + depth * levelW(); }
int TreeView::textLeft(int depth) const { return icons_ ? px(metrics_.text) + depth * levelW() : iconLeft(depth); }
// The level a pointer x shows: the band of each level starts at its arrow.
int TreeView::levelAtX(int x) const {
    const int d = x - px(metrics_.arrow);
    return d < 0 ? 0 : d / levelW();
}

TreeNode* TreeView::nodeAt(Point pt) const {
    const int r = rowAt(pt);
    return r >= 0 && pt.x < rowsRight() ? rows_[size_t(r)].node : nullptr;
}

Rect TreeView::labelRect(const TreeNode* n) const {
    const int r = rowOf(n);
    if (r < 0) return {};
    const int top = rowTop(size_t(r)), tl = textLeft(rows_[size_t(r)].depth);
    return {tl, top, std::max(tl, rowsRight()), top + rowH()};
}

TreeView::~TreeView() {
    if (typeTimerSet_)
        if (draw::Host* h = host()) h->cancelTimer(typeTimer_);
}

// --- The model and the rows ------------------------------------------------------------------------------------------

void TreeView::collect(TreeNode& parent, int parentRow, int depth) {
    for (size_t i = 0; i < parent.children.size(); ++i) {
        TreeNode& n = *parent.children[i];
        if (n.icon >= 0) icons_ = true;
        rows_.push_back({&n, &parent, i, parentRow, depth});
        if (n.expanded && !n.children.empty()) collect(n, int(rows_.size()) - 1, depth + 1);
    }
}

void TreeView::refresh() {
    // A press or a drag ends here: its node may be gone (an onSelect may have rebuilt the model).
    if (pressing_ || dragging_) cancelDrag();
    press_ = dragNode_ = dragParent_ = nullptr;
    rows_.clear();
    icons_ = false;
    collect(root_, -1, 0);
    if (selected_ && rowOf(selected_) < 0) selected_ = nullptr;
    if (hover_ && rowOf(hover_) < 0) hover_ = nullptr;
    scrollY_ = std::clamp(scrollY_, 0, maxScroll());
    invalidate();
}

int TreeView::rowOf(const TreeNode* n) const {
    for (size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i].node == n) return int(i);
    return -1;
}

int TreeView::contentHeight() const { return int(rows_.size()) * rowH(); }
int TreeView::maxScroll() const { return std::max(0, contentHeight() - height()); }
bool TreeView::scrollable() const { return maxScroll() > 0; }
int TreeView::rowsRight() const { return scrollable() ? width() - scrollStrip() : width(); }
int TreeView::rowTop(size_t r) const { return int(r) * rowH() - scrollY_; }

int TreeView::rowAt(Point pt) const {
    if (pt.x < 0 || pt.x >= width() || pt.y < 0 || pt.y >= height()) return -1;
    const int r = (pt.y + scrollY_) / rowH();
    return r < int(rows_.size()) ? r : -1;
}

bool TreeView::selectNode(TreeNode* n) {
    if (!n) {
        selected_ = nullptr;
        invalidate();
        return true;
    }
    std::vector<TreeNode*> path;
    if (!findPath(root_, n, path)) return false;
    for (size_t i = 0; i + 1 < path.size(); ++i) path[i]->expanded = true;  // the ancestors show it
    refresh();
    selected_ = n;
    ensureVisible(n);
    invalidate();
    return true;
}

void TreeView::setExpanded(TreeNode& n, bool open) {
    if (&n == &root_ || n.children.empty() || n.expanded == open) return;
    if (!open && selected_ && selected_ != &n && isSelfOrBelow(&n, selected_)) selected_ = &n;
    n.expanded = open;
    refresh();
}

void TreeView::ensureVisible(TreeNode* n) {
    const int r = rowOf(n);
    if (r < 0) return;
    const int top = r * rowH(), bottom = top + rowH();
    if (top < scrollY_) scrollY_ = top;
    else if (bottom > scrollY_ + height()) scrollY_ = bottom - height();
    scrollY_ = std::clamp(scrollY_, 0, maxScroll());
    invalidate();
}

void TreeView::scrollTo(int y) {
    const int v = std::clamp(y, 0, maxScroll());
    if (v != scrollY_) scrollY_ = v, invalidate();
}

void TreeView::resized() { scrollY_ = std::clamp(scrollY_, 0, maxScroll()); }

// --- Selection and expansion ------------------------------------------------------------------------------------------

void TreeView::select(TreeNode* n) {
    if (n == selected_) return;
    selected_ = n;
    invalidate();
    if (onSelect) onSelect(n);
}

void TreeView::goRow(int r) {
    if (rows_.empty()) return;
    r = std::clamp(r, 0, int(rows_.size()) - 1);
    TreeNode* n = rows_[size_t(r)].node;
    select(n);
    ensureVisible(n);
}

// Opens or shuts a node by the user: a selection that the shut node hides moves to the node (and the owner hears).
void TreeView::toggle(TreeNode* n) {
    TreeNode* before = selected_;
    setExpanded(*n, !n->expanded);
    if (selected_ != before && onSelect) onSelect(selected_);
}

void TreeView::setCapture(bool on) {
    if (draw::Host* h = host()) h->capture(*this, on);
}

// --- Scroll bar -----------------------------------------------------------------------------------------------------------

Rect TreeView::trackRect() const {
    const int in = px(2);
    return {width() - px(8), in, width() - px(2), height() - in};
}
int TreeView::thumbLen() const {
    const int track = trackRect().height();
    return std::min(track, std::max(px(28), track * height() / std::max(1, contentHeight())));
}
Rect TreeView::thumbRect() const {
    const Rect t = trackRect();
    const int len = thumbLen(), room = t.height() - len, mx = maxScroll();
    const int top = t.top + (mx > 0 ? room * scrollY_ / mx : 0);
    return {t.left, top, t.right, top + len};
}

// --- Painting -------------------------------------------------------------------------------------------------------------

void TreeView::paint(Canvas& c) {
    const int H = height(), rh = rowH(), right = rowsRight();
    const size_t first = size_t(std::max(0, scrollY_) / rh);
    for (size_t r = first; r < rows_.size() && rowTop(r) < H; ++r) {
        const Row& row = rows_[r];
        const int top = rowTop(r);
        const RowLook look{*row.node, {0, top, right, top + rh}, row.depth, row.node == selected_, row.node == hover_ && !dragging_,
                           dragging_ && row.node == dragNode_, arrowLeft(row.depth), iconLeft(row.depth), textLeft(row.depth)};
        paintRow(c, look);
    }
    paintScrollBar(c);
}

void TreeView::paintRow(Canvas& c, const RowLook& row) {
    const Theme& th = theme();
    const TreeNode& n = row.node;
    const Rect box = row.box;
    const int top = box.top, rh = box.height(), right = box.right;
    if (row.selected) {  // as ui_kit's listRow: the selected fill (also under the mouse) and a bar on the left
        c.fill(box, th.sel);
        c.fill({0, top, px(2), top + rh}, th.accent);
    } else if (row.hot) {
        c.fill(box, th.rowHot);
    }
    if (row.dragged) c.fill(box, th.sel, 160);  // the row taken
    const Color text = row.selected ? th.accent : n.color != draw::kNone ? n.color : th.text;
    if (!n.children.empty()) chevron(c, row.arrowLeft, top, rh, n.expanded, row.selected || row.hot ? th.text : th.dim);
    if (n.icon >= 0) c.icon({row.iconLeft, top, row.iconLeft + px(16), top + rh}, n.icon, text, px(16));
    const int tl = row.textLeft;
    c.text({tl, top, std::max(tl, right - px(6)), top + rh}, n.label, text, draw::kMiddle | draw::kLeft | draw::kEllipsis);
}

void TreeView::paintScrollBar(Canvas& c) {
    const Theme& th = theme();
    if (scrollable()) {  // as ui_kit's scrollFace: a thin thumb at rest, wider with the track under the mouse
        const bool lit = scrollHot_ || thumbDrag_;
        Rect thumb = thumbRect();
        if (lit) c.fill(trackRect(), th.panel);
        else thumb.left = thumb.right - std::max(2, (thumb.right - thumb.left) / 2);
        c.roundBox(thumb, lit ? th.thumbHot : th.thumb, draw::kNone, px(3));
    }
}

void TreeView::paintOver(Canvas& c) {
    const Theme& th = theme();
    const int w = std::max(1, px(1));
    if (metrics_.focusFrame && hasFocus()) c.frame({0, 0, width(), height()}, th.accent, w);
    if (!dragging_ || !drop_.valid) return;
    if (drop_.box) {  // into the folder: a box around its row
        const int top = rowTop(size_t(drop_.row));
        c.frame({0, top, rowsRight(), top + rowH()}, th.accent, w);
        return;
    }
    c.fill({arrowLeft(drop_.depth), drop_.y - w, rowsRight(), drop_.y + w}, th.accent);  // as ui_kit's dropLine
}

// --- Mouse ----------------------------------------------------------------------------------------------------------------

bool TreeView::mouse(const draw::MouseEvent& e) {
    using draw::MouseKind;
    switch (e.kind) {
    case MouseKind::Move: return pointerMove(e.pt);
    case MouseKind::Leave:
        if (hover_ || scrollHot_) hover_ = nullptr, scrollHot_ = false, invalidate();
        return true;
    case MouseKind::Down: return pointerDown(e);
    case MouseKind::DoubleClick: {
        if (e.button != 0 || (scrollable() && e.pt.x >= width() - scrollStrip())) return false;
        const int r = rowAt(e.pt);
        if (r < 0) return false;
        TreeNode* n = rows_[size_t(r)].node;
        if (e.pt.x < iconLeft(rows_[size_t(r)].depth) && !n->children.empty()) return true;  // the arrow did it
        if (!n->children.empty()) toggle(n);
        else if (onActivate) onActivate(n);
        return true;
    }
    case MouseKind::Up: {
        if (e.button != 0) return false;
        if (thumbDrag_ || trackPress_) {
            thumbDrag_ = trackPress_ = false;
            setCapture(false);
            invalidate();
            return true;
        }
        if (!pressing_) return false;
        const Drop d = drop_;
        const bool wasDragging = dragging_;
        pressing_ = dragging_ = false;
        drop_ = Drop{};
        setCapture(false);
        if (wasDragging) {
            if (d.valid) commitDrop(d);
            invalidate();
        }
        return true;
    }
    case MouseKind::Wheel:
        if (e.horizontal || !scrollable()) return false;
        scrollTo(scrollY_ - int(draw::wheelDistance(e, float(rowH() * 3))));  // notches away from the user scroll up
        return true;
    default: return false;
    }
}

bool TreeView::pointerDown(const draw::MouseEvent& e) {
    if (e.button != 0) return false;
    if (scrollable() && e.pt.x >= width() - scrollStrip()) {
        const Rect th = thumbRect();
        if (e.pt.y >= th.top && e.pt.y < th.bottom) {
            thumbDrag_ = true;
            thumbGrab_ = e.pt.y - th.top;
        } else {  // on the track: a page toward the click (the capture ends on the release)
            trackPress_ = true;
            scrollTo(scrollY_ + (e.pt.y < th.top ? -height() : height()));
        }
        setCapture(true);
        invalidate();
        return true;
    }
    const int r = rowAt(e.pt);
    if (r < 0) return true;
    TreeNode* n = rows_[size_t(r)].node;
    if (!n->children.empty() && e.pt.x < iconLeft(rows_[size_t(r)].depth)) {
        select(n);
        toggle(n);
        return true;
    }
    press_ = n;
    pressAt_ = e.pt;
    pressing_ = true;
    setCapture(true);
    select(n);
    return true;
}

bool TreeView::pointerMove(Point pt) {
    if (thumbDrag_) {
        const Rect t = trackRect();
        const int room = t.height() - thumbLen(), mx = maxScroll();
        if (room > 0) scrollTo((pt.y - thumbGrab_ - t.top) * mx / room);
        return true;
    }
    if (pressing_ && !dragging_ && std::abs(pt.x - pressAt_.x) + std::abs(pt.y - pressAt_.y) >= dragSlop()) {
        dragging_ = true;
        dragNode_ = press_;
        dragParent_ = nullptr;
        dragIndex_ = 0;
        locate(root_, press_, dragParent_, dragIndex_);
    }
    if (dragging_) {  // the rows scroll when the drag reaches an edge
        if (pt.y < rowH()) scrollTo(scrollY_ - px(8));
        else if (pt.y > height() - rowH()) scrollTo(scrollY_ + px(8));
        updateDrop(pt);
        return true;
    }
    const bool strip = scrollable() && pt.x >= width() - scrollStrip();
    const int r = rowAt(pt);
    TreeNode* over = r >= 0 && pt.x < rowsRight() ? rows_[size_t(r)].node : nullptr;
    if (strip != scrollHot_ || over != hover_) {
        scrollHot_ = strip;
        hover_ = over;
        invalidate();
    }
    return true;
}

// Finds the drop under the pointer: the row's upper or lower quarter (a folder) or half (a node without children)
// puts the line above or below it; the middle of a folder means into it.
void TreeView::updateDrop(Point pt) {
    Drop d;
    if (!rows_.empty() && dragNode_) {
        const int rh = rowH();
        const long long at = (long long)pt.y + scrollY_;
        long long row0 = at < 0 ? 0 : at / rh;
        if (row0 > (long long)rows_.size() - 1) row0 = (long long)rows_.size() - 1;
        const size_t r = size_t(row0);
        const Row& row = rows_[r];
        const TreeNode& n = *row.node;
        const int top = rowTop(r), y = pt.y - top, q = rh / 4;
        const bool folder = n.folder || !n.children.empty();
        enum class At { Before, After, Into };
        At at2 = folder ? (y < q ? At::Before : y >= rh - q ? At::After : At::Into) : (y < rh / 2 ? At::Before : At::After);
        TreeNode* parent = nullptr;
        size_t pos = 0;
        int lineY = top, depth = row.depth;
        bool box = false;
        if (at2 == At::Before) {
            parent = row.parent, pos = row.index;
        } else if (at2 == At::After && n.expanded && !n.children.empty()) {  // after an open folder: its first child
            parent = row.node, pos = 0, lineY = top + rh, depth = row.depth + 1;
        } else if (at2 == At::After) {
            // After the row, at the level the pointer shows. An outer level puts the node after an ancestor, so it
            // must be the ancestor's last row: the rows below must not be deeper than that level. The level is never
            // deeper than the row.
            const int below = r + 1 < rows_.size() ? rows_[r + 1].depth : 0;
            const int level = std::clamp(levelAtX(pt.x), below, row.depth);
            size_t a = r;
            while (rows_[a].depth > level) a = size_t(rows_[a].parentRow);
            parent = rows_[a].parent, pos = rows_[a].index + 1, lineY = top + rh, depth = level;
        } else {
            parent = row.node, pos = n.children.size(), box = true;
        }
        // Not into itself or its own subtree; and not where it already is.
        if (!isSelfOrBelow(dragNode_, parent)) {
            size_t finalIndex = pos;
            if (parent == dragParent_ && dragIndex_ < pos) --finalIndex;
            if (!(parent == dragParent_ && finalIndex == dragIndex_)) {
                d.valid = true;
                d.box = box;
                d.parent = parent;
                d.index = finalIndex;
                d.y = lineY;
                d.depth = depth;
                d.row = int(r);
            }
        }
    }
    drop_ = d;
    invalidate();
}

void TreeView::commitDrop(const Drop& d) {
    if (!onMove || !dragNode_ || !d.parent) return;
    TreeNode* node = dragNode_;
    if (!onMove(*node, *d.parent, d.index)) return;
    if (d.box) d.parent->expanded = true;  // so the node shows where it went
    refresh();
    ensureVisible(node);
}

void TreeView::cancelDrag() {
    dragging_ = pressing_ = false;
    drop_ = Drop{};
    setCapture(false);
    invalidate();
}

draw::Cursor TreeView::cursor(Point pt) {
    if (dragging_) return draw::Cursor::SizeAll;
    if (scrollable() && pt.x >= width() - scrollStrip()) return draw::Cursor::Arrow;
    const int r = rowAt(pt);
    if (r >= 0 && !rows_[size_t(r)].node->children.empty() && pt.x < iconLeft(rows_[size_t(r)].depth))
        return draw::Cursor::Hand;
    return draw::Cursor::Arrow;
}

// --- Keyboard -------------------------------------------------------------------------------------------------------------

bool TreeView::key(const draw::KeyEvent& e) {
    if (!e.down) return false;
    if (dragging_ && e.key == draw::Key::Escape) {
        cancelDrag();
        return true;
    }
    if (e.mods & (draw::kCtrl | draw::kAlt | draw::kSuper)) return false;
    if (rows_.empty()) return false;
    const int n = int(rows_.size());
    const int cur = selected_ ? rowOf(selected_) : -1;
    const int page = std::max(1, height() / rowH());
    switch (e.key) {
    case draw::Key::Up: goRow(cur < 0 ? n - 1 : cur - 1); return true;
    case draw::Key::Down: goRow(cur < 0 ? 0 : cur + 1); return true;
    case draw::Key::Home: goRow(0); return true;
    case draw::Key::End: goRow(n - 1); return true;
    case draw::Key::PageUp: goRow(cur < 0 ? 0 : cur - page); return true;
    case draw::Key::PageDown: goRow(cur < 0 ? 0 : cur + page); return true;
    case draw::Key::Left: {  // an open node shuts; else the parent is selected
        if (cur < 0) goRow(0);
        else if (rows_[size_t(cur)].node->expanded && !rows_[size_t(cur)].node->children.empty())
            toggle(rows_[size_t(cur)].node);
        else if (rows_[size_t(cur)].parentRow >= 0) goRow(rows_[size_t(cur)].parentRow);
        return true;
    }
    case draw::Key::Right: {  // a shut node opens; an open one selects its first child
        if (cur < 0) goRow(0);
        else if (!rows_[size_t(cur)].node->children.empty()) {
            if (!rows_[size_t(cur)].node->expanded) toggle(rows_[size_t(cur)].node);
            else goRow(cur + 1);
        }
        return true;
    }
    case draw::Key::Enter:
        if (!selected_) return false;
#ifdef __APPLE__
        if (onRename && !(e.mods & (draw::kCtrl | draw::kAlt | draw::kShift))) return onRename(selected_), true;  // (Return renames, as in the Finder)
#endif
        if (onActivate) onActivate(selected_);
        return true;
    case draw::Key::F2:
        if (!selected_) return false;
        if (onRename) onRename(selected_);
        return true;
    case draw::Key::Delete:
        if (!selected_) return false;
        if (onDelete) onDelete(selected_);
        return true;
    default: return false;
    }
}

// Type-ahead: letters typed in quick succession select the first row that starts with them, from the selected row
// on (a row's own letter repeated goes to the next row that starts with it). The letters reset after a pause.
bool TreeView::text(const draw::TextEvent& e) {
    const std::wstring w = fromUtf8(e.utf8);
    if (w.empty() || std::any_of(w.begin(), w.end(), [](wchar_t c) { return c < 0x20 || c == 0x7F; })) return false;
    typed_ += lowered(w);
    restartTypeTimer();
    if (rows_.empty()) return true;
    const bool same = std::all_of(typed_.begin(), typed_.end(), [&](wchar_t c) { return c == typed_[0]; });
    const std::wstring want = same ? typed_.substr(0, 1) : typed_;
    const bool cycle = same && typed_.size() > 1;
    const int cur = selected_ ? rowOf(selected_) : -1;
    const size_t n = rows_.size();
    const size_t first = cur < 0 ? 0 : size_t(cur) + (cycle ? 1 : 0);
    for (size_t k = 0; k < n; ++k) {
        const size_t idx = (first + k) % n;
        const std::wstring label = lowered(rows_[idx].node->label);
        if (label.size() >= want.size() && label.compare(0, want.size(), want) == 0) {
            goRow(int(idx));
            break;
        }
    }
    return true;
}

void TreeView::restartTypeTimer() {
    draw::Host* h = host();
    if (!h) return;
    if (typeTimerSet_) h->cancelTimer(typeTimer_);
    std::weak_ptr<bool> alive = alive_;
    typeTimer_ = h->timer(typeMs(), [this, alive] {
        if (!alive.lock()) return;  // the view was deleted (its destructor could not cancel the timer)
        typed_.clear();
        typeTimerSet_ = false;
    });
    typeTimerSet_ = true;
}

}  // namespace stardraft::kit
