#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "kit_check.h"
#include "tree_view.h"

using namespace stardraft;
using namespace stardraft::kit::test;
using kit::TreeNode;
using kit::TreeView;

namespace {

// A (a1, a2), B, C; A is open. Rows at 26 px: A 0, a1 1, a2 2, B 3, C 4.
void sample(TreeView& tv) {
    TreeNode& r = tv.root();
    TreeNode& a = r.addChild(L"A");
    a.addChild(L"a1");
    a.addChild(L"a2");
    a.expanded = true;
    r.addChild(L"B");
    r.addChild(L"C");
    tv.refresh();
}

TreeNode* findLabel(TreeNode& p, std::wstring_view label) {
    for (auto& c : p.children) {
        if (c->label == label) return c.get();
        if (TreeNode* f = findLabel(*c, label)) return f;
    }
    return nullptr;
}

// Moves `n` (wherever it is) to be the `index`th child of `to`, after its removal from the tree.
void moveNode(TreeNode& root, TreeNode* n, TreeNode& to, size_t index) {
    std::unique_ptr<TreeNode> owned;
    std::function<bool(TreeNode&)> take = [&](TreeNode& p) {
        for (size_t i = 0; i < p.children.size(); ++i) {
            if (p.children[i].get() == n) {
                owned = std::move(p.children[i]);
                p.children.erase(p.children.begin() + std::ptrdiff_t(i));
                return true;
            }
            if (take(*p.children[i])) return true;
        }
        return false;
    };
    take(root);
    to.children.insert(to.children.begin() + std::ptrdiff_t(index), std::move(owned));
}

struct Move {
    std::wstring node, parent;
    size_t index;
};

// Records each move asked for and makes it when `accept`.
void ownerMoves(TreeView& tv, std::vector<Move>& log, bool accept = true) {
    tv.onMove = [&tv, &log, accept](TreeNode& node, TreeNode& parent, size_t index) {
        log.push_back({node.label, parent.label, index});
        if (!accept) return false;
        moveNode(tv.root(), &node, parent, index);
        return true;
    };
}

}  // namespace

KIT_TEST(tree_navigation) {
    draw::View root;
    FakeHost host(root);
    TreeView tv;
    root.add(tv, {0, 0, 200, 130});
    sample(tv);
    std::vector<std::wstring> selected;
    tv.onSelect = [&](TreeNode* n) { selected.push_back(n->label); };

    host.click(60, 13);  // A
    CHECK(tv.selected() && tv.selected()->label == L"A");
    CHECK(host.press(draw::Key::Down));
    CHECK(tv.selected()->label == L"a1");
    host.press(draw::Key::Down);
    CHECK(tv.selected()->label == L"a2");
    host.press(draw::Key::Down);
    CHECK(tv.selected()->label == L"B");
    host.press(draw::Key::Up);
    CHECK(tv.selected()->label == L"a2");
    host.press(draw::Key::End);
    CHECK(tv.selected()->label == L"C");
    host.press(draw::Key::Home);
    CHECK(tv.selected()->label == L"A");
    host.press(draw::Key::Up);  // the top stays
    CHECK(tv.selected()->label == L"A");
    host.press(draw::Key::PageDown);  // 5 rows a page: past the end stops at C
    CHECK(tv.selected()->label == L"C");
    host.press(draw::Key::PageUp);
    CHECK(tv.selected()->label == L"A");
    host.press(draw::Key::Right);  // an open node selects its first child
    CHECK(tv.selected()->label == L"a1");
    host.press(draw::Key::Left);  // a leaf selects its parent
    CHECK(tv.selected()->label == L"A");
    host.press(draw::Key::Left);  // an open node shuts
    CHECK(tv.rowOf(findLabel(tv.root(), L"a1")) == -1);
    CHECK(tv.selected()->label == L"A");
    CHECK(tv.contentHeight() == 3 * 26);
    host.press(draw::Key::Right);  // a shut node opens and keeps the selection
    CHECK(tv.contentHeight() == 5 * 26);
    CHECK(tv.selected()->label == L"A");
    host.press(draw::Key::End);
    host.press(draw::Key::Left);  // a leaf of the top level has no parent: nothing
    CHECK(tv.selected()->label == L"C");
    host.press(draw::Key::Right);  // a leaf: nothing
    CHECK(tv.selected()->label == L"C");
    CHECK(host.press(draw::Key::Down));  // at the bottom: stays
    CHECK(tv.selected()->label == L"C");
    CHECK(selected.size() == 12);  // each change of selection, once
    CHECK(selected.back() == L"C");
}

KIT_TEST(tree_empty_keys_and_modifiers) {
    draw::View root;
    FakeHost host(root);
    TreeView tv;
    root.add(tv, {0, 0, 200, 130});
    CHECK(!host.press(draw::Key::Down));  // no rows
    CHECK(!host.press(draw::Key::Enter));
    sample(tv);
    host.click(60, 39);  // a1
    CHECK(!host.press(draw::Key::Down, draw::kCtrl));  // with Ctrl: not a tree key
    CHECK(tv.selected()->label == L"a1");
    host.press(draw::Key::Down, draw::kAlt);
    CHECK(tv.selected()->label == L"a1");
    tv.selectNode(nullptr);
    CHECK(tv.selected() == nullptr);
    CHECK(!host.press(draw::Key::Enter));  // nothing selected
    CHECK(!host.press(draw::Key::F2));
    CHECK(!host.press(draw::Key::Delete));
    CHECK(host.press(draw::Key::Down));  // with nothing selected, Down takes the first row
    CHECK(tv.selected()->label == L"A");
}

KIT_TEST(tree_expand_collapse) {
    draw::View root;
    FakeHost host(root);
    TreeView tv;
    root.add(tv, {0, 0, 200, 130});
    sample(tv);
    TreeNode* a = findLabel(tv.root(), L"A");
    TreeNode* b = findLabel(tv.root(), L"B");
    std::vector<std::wstring> activated, selected;
    tv.onActivate = [&](TreeNode* n) { activated.push_back(n->label); };
    tv.onSelect = [&](TreeNode* n) { selected.push_back(n->label); };

    host.click(10, 13);  // the arrow of A shuts it (and selects it)
    CHECK(!a->expanded);
    CHECK(tv.rowOf(findLabel(tv.root(), L"a1")) == -1);
    CHECK(tv.selected() == a);
    host.click(10, 13);  // and opens it again
    CHECK(a->expanded);
    CHECK(tv.rowOf(findLabel(tv.root(), L"a1")) == 1);

    host.doubleClick(60, 13);  // a double click on the label toggles
    CHECK(!a->expanded);
    host.doubleClick(60, 13);
    CHECK(a->expanded);

    host.click(60, 39);  // a1: selected
    CHECK(tv.selected()->label == L"a1");
    host.click(10, 13);  // shutting A moves the selection to A, and the owner hears it
    CHECK(tv.selected() == a);
    CHECK(selected.back() == L"A");

    host.click(10, 39);  // B (a row with no arrow): selected, not activated
    CHECK(activated.empty());
    CHECK(tv.selected() == b);
    host.doubleClick(60, 39);  // a double click on a node without children activates it
    CHECK(activated.size() == 1 && activated[0] == L"B");
    CHECK(selected.back() == L"B");

    tv.setExpanded(*a, true);
    CHECK(a->expanded && tv.rowOf(findLabel(tv.root(), L"a2")) == 2);
    tv.setExpanded(*a, false);
    CHECK(!a->expanded);
    tv.setExpanded(*b, true);  // a node without children stays as it is
    CHECK(tv.rowOf(b) == 1);
}

KIT_TEST(tree_select_node) {
    draw::View root;
    FakeHost host(root);
    TreeView tv;
    root.add(tv, {0, 0, 200, 52});  // two rows high
    sample(tv);
    int calls = 0;
    tv.onSelect = [&](TreeNode*) { ++calls; };
    TreeNode* a = findLabel(tv.root(), L"A");
    TreeNode* a2 = findLabel(tv.root(), L"a2");
    TreeNode* c = findLabel(tv.root(), L"C");
    tv.setExpanded(*a, false);
    CHECK(tv.selectNode(a2));  // hidden: its ancestor opens to show it
    CHECK(a->expanded);
    CHECK(tv.selected() == a2);
    CHECK(tv.rowOf(a2) == 2);
    CHECK(tv.scrollY() == 2 * 26 + 26 - 52);  // scrolled so the row shows
    CHECK(tv.selectNode(c));
    CHECK(tv.selected() == c);
    CHECK(tv.scrollY() == 4 * 26 + 26 - 52);
    TreeNode stranger;
    CHECK(!tv.selectNode(&stranger));  // not in the tree
    CHECK(tv.selected() == c);
    tv.ensureVisible(findLabel(tv.root(), L"A"));
    CHECK(tv.scrollY() == 0);
    CHECK(calls == 0);
}

KIT_TEST(tree_drop_before_row) {
    draw::View root;
    FakeHost host(root);
    TreeView tv;
    root.add(tv, {0, 0, 200, 130});
    sample(tv);
    std::vector<Move> moves;
    ownerMoves(tv, moves);
    host.down(60, 117);  // C
    host.move(60, 29);   // the top quarter of a1 (row 1): a line above it, in A
    auto c = host.paint();
    CHECK(c.drew("fill 22,25,200,27 "));  // from a1's level
    host.up(60, 29);
    CHECK(moves.size() == 1 && moves[0].node == L"C" && moves[0].parent == L"A" && moves[0].index == 0);
    CHECK(findLabel(tv.root(), L"A")->children[0]->label == L"C");
    CHECK(tv.root().children.size() == 2);
    CHECK(tv.selected() && tv.selected()->label == L"C");
}

KIT_TEST(tree_drop_after_open_folder) {
    draw::View root;
    FakeHost host(root);
    TreeView tv;
    root.add(tv, {0, 0, 200, 130});
    sample(tv);
    std::vector<Move> moves;
    ownerMoves(tv, moves);
    host.down(60, 91);  // B
    host.move(60, 23);  // the lower quarter of the open folder A: its first child
    CHECK(host.paint().drew("fill 22,25,200,27 "));
    host.up(60, 23);
    CHECK(moves.size() == 1 && moves[0].parent == L"A" && moves[0].index == 0);
    CHECK(findLabel(tv.root(), L"A")->children[0]->label == L"B");
}

KIT_TEST(tree_drop_after_leaf) {
    draw::View root;
    FakeHost host(root);
    TreeView tv;
    root.add(tv, {0, 0, 200, 130});
    sample(tv);
    std::vector<Move> moves;
    ownerMoves(tv, moves);
    host.down(60, 91);  // B
    host.move(60, 75);  // the lower half of a2 (row 2): after it, in A
    CHECK(host.paint().drew("fill 22,77,200,79 "));
    host.up(60, 75);
    CHECK(moves.size() == 1 && moves[0].parent == L"A" && moves[0].index == 2);
    TreeNode* a = findLabel(tv.root(), L"A");
    CHECK(a->children.size() == 3 && a->children[2]->label == L"B");
}

KIT_TEST(tree_drop_before_leaf_at_top) {
    draw::View root;
    FakeHost host(root);
    TreeView tv;
    root.add(tv, {0, 0, 200, 130});
    sample(tv);
    std::vector<Move> moves;
    ownerMoves(tv, moves);
    host.down(60, 65);  // a2
    host.move(60, 81);  // the upper half of B (row 3): before B, at the top level
    CHECK(host.paint().drew("fill 4,77,200,79 "));
    host.up(60, 81);
    CHECK(moves.size() == 1 && moves[0].parent.empty() && moves[0].index == 1);
    CHECK(tv.root().children[1]->label == L"a2");
    CHECK(tv.root().children.size() == 4);
}

KIT_TEST(tree_drop_into_folder) {
    draw::View root;
    FakeHost host(root);
    TreeView tv;
    root.add(tv, {0, 0, 200, 130});
    sample(tv);
    std::vector<Move> moves;
    ownerMoves(tv, moves);
    host.down(60, 117);  // C
    host.move(60, 13);   // the middle of A: into it, at the end
    auto c = host.paint();
    CHECK(c.drew("frame 0,0,200,26 "));  // a box around A's row
    CHECK(!c.drew("fill 22,"));          // and no line
    host.up(60, 13);
    CHECK(moves.size() == 1 && moves[0].parent == L"A" && moves[0].index == 2);
    TreeNode* a = findLabel(tv.root(), L"A");
    CHECK(a->children.size() == 3 && a->children[2]->label == L"C");
}

KIT_TEST(tree_drop_into_empty_folder) {
    draw::View root;
    FakeHost host(root);
    TreeView tv;
    root.add(tv, {0, 0, 200, 130});
    sample(tv);
    findLabel(tv.root(), L"B")->folder = true;  // a folder with no children takes drops
    tv.refresh();
    std::vector<Move> moves;
    ownerMoves(tv, moves);
    host.down(60, 117);  // C
    host.move(60, 91);  // B's middle
    CHECK(host.paint().drew("frame 0,78,200,104 "));
    host.up(60, 91);
    CHECK(moves.size() == 1 && moves[0].parent == L"B" && moves[0].index == 0);
    CHECK(findLabel(tv.root(), L"B")->expanded);  // the folder opens to show the node
}

KIT_TEST(tree_drop_no_op_and_own_subtree) {
    draw::View root;
    FakeHost host(root);
    TreeView tv;
    root.add(tv, {0, 0, 200, 130});
    sample(tv);
    std::vector<Move> moves;
    ownerMoves(tv, moves);

    host.down(60, 39);  // a1: after itself is where it is
    host.move(60, 46);
    CHECK(!host.paint().drew("fill 22,"));
    host.up(60, 46);
    CHECK(moves.empty());

    host.down(60, 117);  // C: after itself, at the end of the top level
    host.move(60, 126);
    CHECK(!host.paint().drew("fill 4,"));
    host.up(60, 126);
    CHECK(moves.empty());

    host.down(60, 13);  // A onto a1: that is inside A
    host.move(60, 29);
    CHECK(!host.paint().drew("fill 22,"));
    host.move(60, 13);  // and into itself
    CHECK(!host.paint().drew("frame 0,0,200,26 "));
    host.up(60, 13);
    CHECK(moves.empty());
    CHECK(findLabel(tv.root(), L"A")->children.size() == 2);
}

KIT_TEST(tree_drop_not_into_own_subtree_deep) {
    draw::View root;
    FakeHost host(root);
    TreeView tv;
    root.add(tv, {0, 0, 200, 130});
    sample(tv);
    TreeNode* a2 = findLabel(tv.root(), L"a2");
    a2->addChild(L"x");
    a2->expanded = true;
    tv.refresh();  // rows: A 0, a1 1, a2 2, x 3, B 4, C 5
    std::vector<Move> moves;
    ownerMoves(tv, moves);
    host.down(60, 13);  // drag A
    host.move(60, 91);  // before x: inside A's own subtree
    CHECK(!host.paint().drew("fill 22,"));
    host.up(60, 91);
    CHECK(moves.empty());
    CHECK(tv.root().children[0]->label == L"A");
}

KIT_TEST(tree_drop_refused_by_owner) {
    draw::View root;
    FakeHost host(root);
    TreeView tv;
    root.add(tv, {0, 0, 200, 130});
    sample(tv);
    std::vector<Move> moves;
    ownerMoves(tv, moves, false);
    host.down(60, 117);  // C
    host.move(60, 29);
    CHECK(host.paint().drew("fill 22,25,200,27 "));
    host.up(60, 29);
    CHECK(moves.size() == 1);  // asked
    CHECK(tv.root().children.size() == 3 && tv.root().children[1]->label == L"B");  // not moved
    CHECK(findLabel(tv.root(), L"A")->children.size() == 2);
    CHECK(!host.paint().drew("fill 22,25,200,27 "));  // the line is gone after the drop
    CHECK(tv.selected() && tv.selected()->label == L"C");
}

KIT_TEST(tree_drag_threshold_cancel_and_no_owner) {
    draw::View root;
    FakeHost host(root);
    TreeView tv;
    root.add(tv, {0, 0, 200, 130});
    sample(tv);
    std::vector<Move> moves;
    ownerMoves(tv, moves);
    host.down(60, 117);  // a small move is a click
    host.move(61, 118);
    CHECK(!host.paint().drew("fill 22,"));
    host.up(61, 118);
    CHECK(moves.empty());
    CHECK(tv.selected()->label == L"C");

    host.down(60, 117);  // a drag, cancelled with Escape
    host.move(60, 29);
    CHECK(host.paint().drew("fill 22,25,200,27 "));
    CHECK(host.press(draw::Key::Escape));
    host.up(60, 29);
    CHECK(moves.empty());
    CHECK(!host.paint().drew("fill 22,25,200,27 "));

    tv.onMove = nullptr;  // without an owner: nothing moves
    host.down(60, 117);
    host.move(60, 29);
    host.up(60, 29);
    CHECK(tv.root().children.size() == 3);
}

KIT_TEST(tree_scrolling) {
    draw::View root;
    FakeHost host(root);
    TreeView tv;
    root.add(tv, {0, 0, 200, 130});
    for (int i = 0; i < 30; ++i) tv.root().addChild(L"item " + std::to_wstring(i));
    tv.refresh();
    CHECK(tv.contentHeight() == 780);
    host.wheel(60, 60, -1.0f);  // away from the user: down three rows
    CHECK(tv.scrollY() == 78);
    host.wheel(60, 60, -100.0f);  // stops at the end (780 - 130)
    CHECK(tv.scrollY() == 650);
    host.wheel(60, 60, 1.0f);
    CHECK(tv.scrollY() == 572);
    host.click(60, 13);  // the row under the top: the 23rd (index 22)
    CHECK(tv.selected() && tv.selected()->label == L"item 22");
    host.press(draw::Key::End);  // the last row shows
    CHECK(tv.scrollY() == 650);
    host.press(draw::Key::Home);
    CHECK(tv.scrollY() == 0);
    tv.ensureVisible(findLabel(tv.root(), L"item 29"));
    CHECK(tv.scrollY() == 650);
    tv.ensureVisible(findLabel(tv.root(), L"item 0"));
    CHECK(tv.scrollY() == 0);

    // The scroll bar: its thumb is 28 px at the top; dragging it moves the rows in proportion.
    host.down(195, 10);
    host.move(195, 60);
    CHECK(tv.scrollY() == 331);  // (60 - 8 grab - 2 inset) * 650 / 98
    host.up(195, 60);
    host.down(195, 100);  // on the track below the thumb (now at 51 to 79): a page down
    CHECK(tv.scrollY() == 461);
    host.up(195, 100);

    host.wheel(60, 60, 100.0f);
    CHECK(tv.scrollY() == 0);
    host.down(195, 100);  // a page down, from the top
    CHECK(tv.scrollY() == 130);
    host.up(195, 100);
    host.down(195, 5);  // above the thumb (at 21 to 49): a page up
    CHECK(tv.scrollY() == 0);
    host.up(195, 5);

    // The track shows under the mouse, and the rows stop before the bar; at rest the thumb is thin.
    host.click(60, 13);  // item 0 selected
    host.move(195, 10);
    auto lit = host.paint();
    CHECK(lit.drew("fill 192,2,198,128 "));
    CHECK(lit.drew("roundBox 192,2,198,30 "));
    CHECK(lit.drew("fill 0,0,190,26 "));
    host.move(100, 100);
    CHECK(host.paint().drew("roundBox 195,2,198,30 "));
}

KIT_TEST(tree_scrolling_drag_edge_and_not_scrollable) {
    draw::View root;
    FakeHost host(root);
    TreeView tv;
    root.add(tv, {0, 0, 200, 130});
    for (int i = 0; i < 30; ++i) tv.root().addChild(L"item " + std::to_wstring(i));
    tv.refresh();
    host.click(60, 65);  // row 2 is selected
    CHECK(tv.selected() && tv.selected()->label == L"item 2");
    host.down(60, 65);
    host.move(60, 125);  // the drag starts near the bottom edge: the rows scroll
    CHECK(tv.scrollY() > 0);
    host.up(60, 125);
    tv.setSize(200, 900);  // all rows fit: no scroll, the rows take the whole width
    CHECK(tv.scrollY() == 0);
    host.click(60, 13);  // item 0
    CHECK(host.paint().drew("fill 0,0,200,26 "));
    host.wheel(60, 60, -1.0f);  // a list that fits does not take the wheel
    CHECK(tv.scrollY() == 0);
}

KIT_TEST(tree_type_ahead) {
    draw::View root;
    FakeHost host(root);
    TreeView tv;
    root.add(tv, {0, 0, 200, 130});
    for (const wchar_t* f : {L"Apple", L"Apricot", L"Banana", L"Blueberry", L"Cherry"}) tv.root().addChild(f);
    tv.refresh();
    host.click(60, 13);  // Apple, and the tree has the focus
    CHECK(tv.selected()->label == L"Apple");
    host.type("b");
    CHECK(tv.selected()->label == L"Banana");
    host.type("l");  // "bl"
    CHECK(tv.selected()->label == L"Blueberry");
    host.type("x");  // no match: stays
    CHECK(tv.selected()->label == L"Blueberry");
    host.runTimers();  // a pause: the letters are forgotten
    host.type("C");  // case does not matter
    CHECK(tv.selected()->label == L"Cherry");
    host.runTimers();
    host.type("a");  // from Cherry on, wrapping to Apple
    CHECK(tv.selected()->label == L"Apple");
    host.type("a");  // "aa": the same letter again goes to the next row that starts with it
    CHECK(tv.selected()->label == L"Apricot");
    host.type("a");  // "aaa": wraps back
    CHECK(tv.selected()->label == L"Apple");
    host.runTimers();
    host.type("z");  // none
    CHECK(tv.selected()->label == L"Apple");
    host.type(" ");  // a space is a letter too (the spaces of a name)
    CHECK(tv.selected()->label == L"Apple");
    host.type(std::string("\x01", 1));  // control characters are not typed text
    CHECK(tv.selected()->label == L"Apple");
}

KIT_TEST(tree_type_ahead_in_expanded_nodes) {
    draw::View root;
    FakeHost host(root);
    TreeView tv;
    root.add(tv, {0, 0, 200, 130});
    sample(tv);
    host.click(60, 39);  // a1
    host.type("b");  // B is below the children of A
    CHECK(tv.selected()->label == L"B");
}

KIT_TEST(tree_keys_activate_rename_delete) {
    draw::View root;
    FakeHost host(root);
    TreeView tv;
    root.add(tv, {0, 0, 200, 130});
    sample(tv);
    std::vector<std::wstring> log;
    tv.onActivate = [&](TreeNode* n) { log.push_back(L"activate " + n->label); };
    tv.onRename = [&](TreeNode* n) { log.push_back(L"rename " + n->label); };
    tv.onDelete = [&](TreeNode* n) { log.push_back(L"delete " + n->label); };
    host.click(60, 39);  // a1
    CHECK(host.press(draw::Key::Enter));
    CHECK(host.press(draw::Key::F2));
    CHECK(host.press(draw::Key::Delete));
    CHECK(log.size() == 3);
    CHECK(log.size() == 3 && log[0] == L"activate a1" && log[1] == L"rename a1" && log[2] == L"delete a1");
    tv.onActivate = nullptr;  // without a callback the key is still used
    CHECK(host.press(draw::Key::Enter));
}

KIT_TEST(tree_refresh_keeps_state) {
    draw::View root;
    FakeHost host(root);
    TreeView tv;
    root.add(tv, {0, 0, 200, 130});
    sample(tv);
    TreeNode* a1 = findLabel(tv.root(), L"a1");
    host.click(60, 39);
    CHECK(tv.selected() == a1);
    findLabel(tv.root(), L"A")->addChild(L"a3");
    tv.refresh();  // a new row shows; the selection stays
    CHECK(tv.selected() == a1);
    CHECK(tv.rowOf(findLabel(tv.root(), L"a3")) == 3);
    TreeNode* a = findLabel(tv.root(), L"A");
    a->children.erase(a->children.begin());  // a1 leaves the model
    tv.refresh();
    CHECK(tv.selected() == nullptr);
    CHECK(a->expanded);  // the open node stays open
    CHECK(tv.contentHeight() == 5 * 26);
}

KIT_TEST(tree_paint_and_cursor) {
    draw::View root;
    FakeHost host(root);
    TreeView tv;
    root.add(tv, {0, 0, 200, 130});
    sample(tv);
    findLabel(tv.root(), L"B")->icon = 7;
    findLabel(tv.root(), L"C")->color = draw::rgb(1, 2, 3);
    tv.refresh();
    host.click(60, 13);  // A selected: a fill, and the accent bar
    auto c = host.paint();
    CHECK(c.drew("fill 0,0,200,26 0x3f3017 255"));  // the theme's sel
    CHECK(c.drew("fill 0,0,2,26 0xf0b062 255"));
    CHECK(c.drew("polyline 3 0x"));                   // arrows: a chevron down for A (open), right for shut nodes
    CHECK(c.drew("icon 22,78,38,104 7 0xe0e0e0"));          // B's icon, in the text color
    CHECK(c.drew("text 44,78,194,104 'B' 0xe0e0e0 12"));    // the label after the icon (the icon column is kept)
    CHECK(c.drew("text 44,104,194,130 'C' 0x030201 12"));   // C's own color, and its label in the same column
    CHECK(c.drew("frame 0,0,200,130 0xf0b062"));           // the click gave the tree the focus: its frame shows
    CHECK(tv.cursor({10, 13}) == draw::Cursor::Hand);
    CHECK(tv.cursor({60, 13}) == draw::Cursor::Arrow);
    CHECK(tv.cursor({10, 117}) == draw::Cursor::Arrow);  // C has no children
}

// A click on the scroll bar's track ends its capture on the release: the next click reaches the view beside the tree.
KIT_TEST(tree_track_click_releases_capture) {
    draw::View root;
    FakeHost host(root);
    TreeView tv;
    root.add(tv, {0, 0, 200, 130});
    struct Probe : draw::View {
        int downs = 0;
        bool mouse(const draw::MouseEvent& e) override {
            if (e.kind == draw::MouseKind::Down) ++downs;
            return true;
        }
    } probe;
    root.add(probe, {210, 0, 260, 130});
    for (int i = 0; i < 30; ++i) tv.root().addChild(L"item " + std::to_wstring(i));
    tv.refresh();
    host.down(195, 100);  // the track below the thumb: a page down
    CHECK(tv.scrollY() == 130);
    host.up(195, 100);
    host.click(230, 10);  // the probe gets the click, not the tree
    CHECK(probe.downs == 1);
    CHECK(tv.scrollY() == 130);
    CHECK(tv.selected() == nullptr);
}

// Left on an open node without children selects its parent: the node has nothing to shut.
KIT_TEST(tree_left_on_open_empty_folder) {
    draw::View root;
    FakeHost host(root);
    TreeView tv;
    root.add(tv, {0, 0, 200, 130});
    sample(tv);
    TreeNode* a = findLabel(tv.root(), L"A");
    TreeNode& f = a->addChild(L"F");
    f.addChild(L"x");
    f.expanded = true;
    tv.refresh();  // rows: A 0, a1 1, a2 2, F 3, x 4, B 5, C 6
    host.click(60, 91);  // F
    CHECK(tv.selected() == &f);
    f.children.clear();  // the owner drags its only child away: F stays open, with no children
    tv.refresh();
    CHECK(f.expanded);
    CHECK(host.press(draw::Key::Left));
    CHECK(tv.selected() == a);
    CHECK(f.expanded);  // nothing was shut
}

// The owner deletes the view while a type-ahead pause is pending: the pause runs and must not touch the view.
KIT_TEST(tree_type_ahead_timer_after_deletion) {
    draw::View root;
    FakeHost host(root);
    auto tv = std::make_unique<TreeView>();
    root.add(*tv, {0, 0, 200, 130});
    sample(*tv);
    host.click(60, 13);
    host.type("a");  // a pause is pending
    CHECK(!host.timers.empty());
    root.remove(*tv);  // the owner takes the view out and deletes it
    tv.reset();
    host.runTimers();  // runs without a view: nothing is written
    CHECK(host.timers.empty());
}

// The owner rebuilds the model in onSelect, which runs while the mouse is down on a row. The press ends with the
// refresh, so the moves after it start no drag from the freed node.
KIT_TEST(tree_refresh_in_select_ends_press) {
    draw::View root;
    FakeHost host(root);
    TreeView tv;
    root.add(tv, {0, 0, 200, 130});
    sample(tv);
    bool rebuilt = false;
    tv.onSelect = [&](TreeNode*) {
        if (rebuilt) return;
        rebuilt = true;
        tv.root().children.clear();  // the node pressed on is freed here
        tv.root().addChild(L"new");
        tv.refresh();
    };
    std::vector<Move> moves;
    ownerMoves(tv, moves);
    host.down(60, 117);  // C
    CHECK(rebuilt);
    host.move(60, 29);  // no drag: no drop line and no move
    auto c = host.paint();
    CHECK(!c.drew("fill 4,"));
    CHECK(!c.drew("fill 22,"));
    host.up(60, 29);
    CHECK(moves.empty());
    CHECK(tv.root().children.size() == 1);
}

// A drag refreshed by the owner in mid-drag ends too: no line, and the release does nothing.
KIT_TEST(tree_refresh_in_drag_cancels_it) {
    draw::View root;
    FakeHost host(root);
    TreeView tv;
    root.add(tv, {0, 0, 200, 130});
    sample(tv);
    std::vector<Move> moves;
    ownerMoves(tv, moves);
    host.down(60, 117);  // C
    host.move(60, 29);
    CHECK(host.paint().drew("fill 22,25,200,27 "));
    tv.refresh();
    CHECK(!host.paint().drew("fill 22,25,200,27 "));
    host.up(60, 29);
    CHECK(moves.empty());
}

// Drops after the last top-level row when that row is a leaf below an open folder: the pointer's level decides.
// Rows: A 0, F 1 (open), x 2 (in F). Drag A below x at the left edge: after F at the top level.
KIT_TEST(tree_drop_after_nested_row_at_outer_level) {
    draw::View root;
    FakeHost host(root);
    TreeView tv;
    root.add(tv, {0, 0, 200, 130});
    TreeNode& a = tv.root().addChild(L"A");
    TreeNode& f = tv.root().addChild(L"F");
    f.addChild(L"x");
    f.expanded = true;
    tv.refresh();
    std::vector<Move> moves;
    ownerMoves(tv, moves);
    host.down(60, 13);  // A
    host.move(10, 72);  // the lower half of x, at the left edge: the top level
    CHECK(host.paint().drew("fill 4,77,200,79 "));  // the line starts at the top level
    host.up(10, 72);
    CHECK(moves.size() == 1 && moves[0].node == L"A" && moves[0].parent.empty() && moves[0].index == 1);
    CHECK(tv.root().children.size() == 2 && tv.root().children[0]->label == L"F" && tv.root().children[1].get() == &a);
}

// The same rows, the pointer inside F's level: after x, in F (the usual place).
KIT_TEST(tree_drop_after_nested_row_at_own_level) {
    draw::View root;
    FakeHost host(root);
    TreeView tv;
    root.add(tv, {0, 0, 200, 130});
    tv.root().addChild(L"A");
    TreeNode& f = tv.root().addChild(L"F");
    f.addChild(L"x");
    f.expanded = true;
    tv.refresh();
    std::vector<Move> moves;
    ownerMoves(tv, moves);
    host.down(60, 13);  // A
    host.move(60, 72);
    CHECK(host.paint().drew("fill 22,77,200,79 "));  // at x's level
    host.up(60, 72);
    CHECK(moves.size() == 1 && moves[0].parent == L"F" && moves[0].index == 1);
}

// A row with a sibling after it: the level stops at the next row's level, so the drop cannot leave the folder
// above the rows that follow inside it. Rows: A 0, a1 1, a2 2, B 0. Pointer at the left edge after a1: stays in A.
KIT_TEST(tree_drop_after_row_not_past_next_sibling) {
    draw::View root;
    FakeHost host(root);
    TreeView tv;
    root.add(tv, {0, 0, 200, 130});
    sample(tv);
    std::vector<Move> moves;
    ownerMoves(tv, moves);
    host.down(60, 117);  // C
    host.move(4, 46);  // the lower half of a1 at the left edge: a2 follows at its level, so after a1 in A
    CHECK(host.paint().drew("fill 22,51,200,53 "));
    host.up(4, 46);
    CHECK(moves.size() == 1 && moves[0].parent == L"A" && moves[0].index == 1);
}

// The selected row keeps its fill under the mouse, as ui_kit's listRow does (no hover fill).
KIT_TEST(tree_selected_row_under_mouse_keeps_fill) {
    draw::View root;
    FakeHost host(root);
    TreeView tv;
    root.add(tv, {0, 0, 200, 130});
    sample(tv);
    host.click(60, 13);  // A selected
    host.move(60, 13);   // the mouse over it
    auto c = host.paint();
    CHECK(c.drew("fill 0,0,200,26 0x3f3017 255"));  // the selected fill
    CHECK(!c.drew("0x523e1e"));                       // not the hot fill (selHot)
}
