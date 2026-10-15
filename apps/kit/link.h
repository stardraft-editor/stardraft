#pragma once
// A link (docs/dev/CONTROLS.md, "Links"): text in the accent that runs an action. It is only for an entity (a unit, a
// string, a file, a web page), not for a command; a command is a button. Underlined under the mouse and while it has
// the focus. Click or Space runs `onClick`. The quiet tone is dim and lights up under the mouse, with no line.

#include <functional>
#include <string>

#include "button.h"
#include "theme.h"
#include "view.h"

namespace stardraft::kit {

class Link : public draw::View {
public:
    Link() = default;
    Link(std::wstring text, std::function<void()> onClick, Tone tone = Tone::Primary)
        : onClick(std::move(onClick)), text_(std::move(text)), tone_(tone) {}

    std::function<void()> onClick;

    void setText(std::wstring t) {
        text_ = std::move(t);
        invalidate();
    }
    const std::wstring& text() const { return text_; }
    void setTone(Tone t) {
        tone_ = t;
        invalidate();
    }
    Tone tone() const { return tone_; }
    void setTip(std::wstring t) { tip_ = std::move(t); }
    // The width of the text on one line.
    int preferredWidth(draw::Canvas& c) const { return c.measure(text_); }

    void paint(draw::Canvas& c) override;
    bool mouse(const draw::MouseEvent& e) override;
    bool key(const draw::KeyEvent& e) override;
    draw::Cursor cursor(draw::Point) override { return draw::Cursor::Hand; }
    std::wstring tip(draw::Point) override { return tip_; }
    bool focusable() const override { return true; }

private:
    std::wstring text_, tip_;
    Tone tone_ = Tone::Primary;
    bool hot_ = false, down_ = false;
};

}  // namespace stardraft::kit
