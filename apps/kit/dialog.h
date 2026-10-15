#pragma once
// A dialog: a content view above a row of buttons at the bottom right. The buttons are laid out from the right edge
// in the order given, as ui_kit.inc's buttonsRight does: the last is at the edge and the first one step left of it
// (OK, then Cancel), so the main action goes first. Enter presses the primary button and Escape the cancel one, when
// the focused view does not use the key itself (a focused button takes Enter and Space).
//
// run() shows the dialog in a window and returns which button closed it. The content fills the area above the
// buttons, 16 px from the edges; it should be sized to fit (a Form sets its own height, see form.h).

#include <memory>
#include <string>
#include <vector>

#include "button.h"
#include "view.h"

namespace stardraft::kit {

constexpr int kDialogGap = 24;     // between the content and the buttons, at 100 %
constexpr int kDialogClosed = -1;  // run() and result(): no button closed the dialog

struct DialogButton {
    std::wstring text;
    int result = 0;          // what run() returns when this button closes the dialog
    bool primary = false;    // Enter presses it; drawn as the main action
    bool cancel = false;     // Escape presses it
};

class Dialog : public draw::View {
public:
    Dialog() = default;

    // The content (not owned: it must outlive the dialog or be removed first).
    void setContent(draw::View& c);
    // The buttons, from left to right; the last one is at the right edge.
    void setButtons(std::vector<DialogButton> bs);

    // Shows the dialog in a window `size` big (x: width, y: height) that `from` opens, and returns when it closes:
    // the result of the button that closed it; a close without a button (the window's close box) gives the cancel
    // button's result, or kDialogClosed. A host that does not block (the test host) returns at once: use result().
    int run(draw::View& from, const std::wstring& title, draw::Point size);
    // Closes the dialog with `result` (what a button does). Only the first close counts.
    void close(int result);
    bool closed() const { return done_; }
    // The result of the close, or kDialogClosed while the dialog is open.
    int result() const { return done_ ? result_ : kDialogClosed; }

    // Measures the buttons' text with `c` (paint does it too); the buttons are placed at the last widths.
    void layout(draw::Canvas& c);

    void paint(draw::Canvas& c) override;
    bool key(const draw::KeyEvent& e) override;
    void resized() override { placeAll(); }

private:
    void placeAll();
    const DialogButton* find(bool cancel) const;

    draw::View* content_ = nullptr;
    std::vector<DialogButton> specs_;
    std::vector<std::unique_ptr<Button>> buttons_;
    std::vector<int> widths_;
    draw::View* from_ = nullptr;  // the view that opened the window (its host closes it)
    bool done_ = false;
    int result_ = kDialogClosed;
};

}  // namespace stardraft::kit
