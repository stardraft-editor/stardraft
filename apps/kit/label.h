#pragma once
// Text on its own (docs/dev/CONTROLS.md, "Labels"): in the normal, dim or faint tone, or in a status tone (good,
// warning, bad). One line, cut with "..." at its end; or wrapped to the width. An icon can sit at its left. A label
// takes no input: the mouse goes to what is under it.

#include <string>

#include "theme.h"
#include "view.h"

namespace stardraft::kit {

enum class LabelTone { Normal, Dim, Faint, Good, Warn, Bad };

// The color of a tone's text.
Color labelColor(LabelTone t);

class Label : public draw::View {
public:
    Label() = default;
    explicit Label(std::wstring text, LabelTone tone = LabelTone::Normal) : text_(std::move(text)), tone_(tone) {}

    void setText(std::wstring t) {
        text_ = std::move(t);
        invalidate();
    }
    const std::wstring& text() const { return text_; }
    void setTone(LabelTone t) {
        tone_ = t;
        invalidate();
    }
    LabelTone tone() const { return tone_; }
    // Wrapped to the width (lines from the top); else one line, centered up and down.
    void setWrap(bool on) {
        wrap_ = on;
        invalidate();
    }
    bool wrap() const { return wrap_; }
    // A UI icon (an id of apps/editor/res/ui_icons.h) at the left, in the tone's color; -1: none.
    void setIcon(int icon) {
        icon_ = icon;
        invalidate();
    }
    int icon() const { return icon_; }

    // The height the text needs in `width` px (at least the icon's height).
    int preferredHeight(draw::Canvas& c, int width) const;
    // The width of the text on one line, with the icon.
    int preferredWidth(draw::Canvas& c) const;

    void paint(draw::Canvas& c) override;

private:
    int textLeft() const;  // where the text starts: after the icon, when there is one

    std::wstring text_;
    LabelTone tone_ = LabelTone::Normal;
    bool wrap_ = false;
    int icon_ = -1;
};

}  // namespace stardraft::kit
