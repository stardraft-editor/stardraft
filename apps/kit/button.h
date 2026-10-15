#pragma once
// A push button (docs/dev/CONTROLS.md, "Buttons"): Normal (a command), Primary (what the form or dialog is for),
// Danger (removes something), Quiet. Click, Space or Enter runs `onClick`. The pattern every kit widget follows:
// a draw::View, colors and sizes from kit::theme(), text through the Canvas, no system calls.

#include <functional>
#include <string>

#include "theme.h"
#include "view.h"

namespace stardraft::kit {

enum class Tone { Normal, Primary, Danger, Quiet };

class Button : public draw::View {
public:
    Button() = default;
    Button(std::wstring text, Tone tone, std::function<void()> onClick) : onClick(std::move(onClick)), text_(std::move(text)), tone_(tone) {}

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
    void setEnabled(bool e) {
        enabled_ = e;
        invalidate();
    }
    bool enabled() const { return enabled_; }
    void setTip(std::wstring t) { tip_ = std::move(t); }
    // The width the text needs (at least kButtonMinW).
    int preferredWidth(draw::Canvas& c) const;

    void paint(draw::Canvas& c) override;
    bool mouse(const draw::MouseEvent& e) override;
    bool key(const draw::KeyEvent& e) override;
    draw::Cursor cursor(draw::Point) override { return enabled_ ? draw::Cursor::Hand : draw::Cursor::Arrow; }
    std::wstring tip(draw::Point) override { return tip_; }
    bool focusable() const override { return enabled_; }

private:
    std::wstring text_, tip_;
    Tone tone_ = Tone::Normal;
    bool enabled_ = true, hot_ = false, down_ = false;
};

// The color of a tone's text.
Color toneText(Tone t);

}  // namespace stardraft::kit
