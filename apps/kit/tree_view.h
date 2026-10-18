#pragma once
// A tree of labelled nodes (the Triggers workspace's triggers and folders): each level indented, an arrow on the nodes
// with children, a painted scroll bar, the keyboard and the mouse. Dragging a row moves it: a line shows where it
// goes between rows, a box around a folder means "into it". The owner makes the move (onMove). The view keeps no copy
// of the model: it shows the nodes the owner gives it, and after the owner changes the model it calls refresh().

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "theme.h"
#include "view.h"

namespace stardraft::kit {

struct TreeNode {
    std::wstring label;
    int icon = -1;                    // a UI icon (apps/editor/res/ui_icons.h); -1: none
    draw::Color color = draw::kNone;  // the text's color; kNone: the theme's
    bool expanded = false;            // shows its children (a node without children has no arrow)
    bool folder = false;              // takes drops into it even when it has no children
    std::vector<std::unique_ptr<TreeNode>> children;
    uintptr_t data = 0;               // for the owner (an index, a key of its own)

    // Adds a child at the end and returns it.
    TreeNode& addChild(std::wstring text, int iconId = -1, uintptr_t value = 0);
};

class TreeView : public draw::View {
public:
    TreeView();
    ~TreeView() override;

    // The rows' sizes at 100 % (scaled with px()): a row's height, a level's indent, and where the arrow, the icon and
    // the text start at level 0 (the text: when a shown node has an icon). focusFrame: a line around the view while it
    // has the focus. The defaults are the kit's look.
    struct Metrics {
        int rowH = kRowH, level = 18, arrow = 4, icon = 22, text = 44;
        bool focusFrame = true;
    };
    void setMetrics(const Metrics& m);
    const Metrics& metrics() const { return metrics_; }

    // The root: its children are the top rows. Change the model through it, then call refresh().
    TreeNode& root() { return root_; }
    // Rebuilds the rows after the model changed. Expanded nodes and the selection stay when still shown. A node the
    // owner removed must not be used again (the view compares its address only until the next refresh). A press or
    // a drag in progress ends: its node may be gone (onSelect may rebuild the model).
    void refresh();

    TreeNode* selected() const { return selected_; }
    // Selects `n`, expanding its ancestors so it shows, and scrolls it into view. onSelect is not called. False when
    // `n` is not in the tree. nullptr clears the selection.
    bool selectNode(TreeNode* n);
    // Expands or collapses `n` (a node without children stays as it is). A selection in a collapsed node moves to
    // `n`. onSelect is not called.
    void setExpanded(TreeNode& n, bool open);
    // Scrolls so that `n`'s row shows (nothing when it is hidden).
    void ensureVisible(TreeNode* n);

    // The rows shown: the index of `n`'s row, -1 when it is hidden. The scroll offset in pixels, and the height of
    // all rows.
    int rowOf(const TreeNode* n) const;
    int scrollY() const { return scrollY_; }
    int contentHeight() const;
    // The node of the row at `pt` (not on the scroll bar), and where a shown node's label is (empty when hidden).
    TreeNode* nodeAt(draw::Point pt) const;
    draw::Rect labelRect(const TreeNode* n) const;

    // The user selected a node (click, arrow keys, Home, End, Page keys, type-ahead).
    std::function<void(TreeNode*)> onSelect;
    // Enter, or a double click on a node without children.
    std::function<void(TreeNode*)> onActivate;
    // F2 (on macOS also Return, as in the Finder), and Delete: the owner asks to rename or to delete the selected node.
    std::function<void(TreeNode*)> onRename;
    std::function<void(TreeNode*)> onDelete;
    // A drag ends on a place: `node` should become the `index`th child of `parent`, where `index` is its position
    // after the move. Return true when the owner has moved it (the view then refreshes and keeps the node
    // selected); false refuses. Without onMove nothing is moved. The view never offers a folder's own subtree.
    std::function<bool(TreeNode& node, TreeNode& parent, size_t index)> onMove;

    void paint(draw::Canvas& c) override;
    void paintOver(draw::Canvas& c) override;
    bool mouse(const draw::MouseEvent& e) override;
    bool sameItem(draw::Point a, draw::Point b) const override { return rowAt(a) == rowAt(b); }  // (a double click: one row)
    bool key(const draw::KeyEvent& e) override;
    bool text(const draw::TextEvent& e) override;
    draw::Cursor cursor(draw::Point pt) override;
    bool focusable() const override { return true; }
    void focusChanged(bool) override { invalidate(); }
    void resized() override;

protected:
    // A row as paint shows it: its box (the full row, left of the scroll bar), its level, its state, and where its
    // arrow, icon and text start. A view with a look of its own paints the row itself.
    struct RowLook {
        const TreeNode& node;
        draw::Rect box;
        int depth;
        bool selected, hot, dragged;
        int arrowLeft, iconLeft, textLeft;
    };
    virtual void paintRow(draw::Canvas& c, const RowLook& row);

private:
    struct Row {
        TreeNode* node;
        TreeNode* parent;  // its parent (the root for the top rows)
        size_t index;      // its place among the parent's children
        int parentRow;     // the parent's row, -1 for the top rows
        int depth;
    };
    // Where a drag would drop: a line above or below a row (box: a box around a folder's row, into it).
    struct Drop {
        bool valid = false;
        bool box = false;
        TreeNode* parent = nullptr;
        size_t index = 0;  // the final index in the parent
        int y = 0;         // the line's y in the view (line only)
        int depth = 0;     // the line starts at this level (line only)
        int row = -1;      // the folder's row (box only)
    };

    int rowH() const;
    int levelW() const;
    int arrowLeft(int depth) const;
    int iconLeft(int depth) const;
    int textLeft(int depth) const;
    int levelAtX(int x) const;
    void paintScrollBar(draw::Canvas& c);
    void collect(TreeNode& parent, int parentRow, int depth);
    void select(TreeNode* n);
    void goRow(int r);
    void toggle(TreeNode* n);
    void scrollTo(int y);
    void setCapture(bool on);
    int rowAt(draw::Point pt) const;
    int rowTop(size_t r) const;
    int maxScroll() const;
    bool scrollable() const;
    int rowsRight() const;
    draw::Rect trackRect() const;
    draw::Rect thumbRect() const;
    int thumbLen() const;
    void updateDrop(draw::Point pt);
    void commitDrop(const Drop& d);
    void cancelDrag();
    void restartTypeTimer();
    bool pointerDown(const draw::MouseEvent& e);
    bool pointerMove(draw::Point pt);

    Metrics metrics_;
    TreeNode root_;
    std::vector<Row> rows_;
    bool icons_ = false;  // a shown node has an icon: the labels leave room for it
    TreeNode* selected_ = nullptr;
    TreeNode* hover_ = nullptr;
    TreeNode* press_ = nullptr;  // the node a button was pressed on
    draw::Point pressAt_;
    bool pressing_ = false, dragging_ = false, thumbDrag_ = false, trackPress_ = false, scrollHot_ = false;
    TreeNode* dragNode_ = nullptr;
    TreeNode* dragParent_ = nullptr;  // where the dragged node was when the drag began
    size_t dragIndex_ = 0;
    Drop drop_;
    int scrollY_ = 0, thumbGrab_ = 0;
    std::wstring typed_;  // type-ahead: the letters typed in a row (lower case)
    int typeTimer_ = 0;
    bool typeTimerSet_ = false;
    // The type-ahead timer's callback checks this before it touches the view: a view the owner deleted is not used.
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}  // namespace stardraft::kit
