#pragma once
// A badge (docs/dev/CONTROLS.md, "Badges"): a short state in a small box ("Found", "Not supported", "Needs the build
// folder"). The text says the state; the tone's colors only repeat it. The box is as wide as its text and sits at the
// left of the frame, or at its right (setAlignRight). A badge takes no input: the mouse goes to what is under it,
// except for its tooltip.

#include <string>

#include "theme.h"
#include "view.h"

namespace stardraft::kit {

enum class BadgeTone { Neutral, Good, Warn, Bad };

class Badge : public draw::View {
public:
    Badge() = default;
    Badge(std::wstring text, BadgeTone tone) : text_(std::move(text)), tone_(tone) {}

    void setText(std::wstring t) {
        if (t == text_) return;
        text_ = std::move(t);
        invalidate();
    }
    const std::wstring& text() const { return text_; }
    void setTone(BadgeTone t) {
        if (t == tone_) return;
        tone_ = t;
        invalidate();
    }
    BadgeTone tone() const { return tone_; }
    void setAlignRight(bool on) {
        right_ = on;
        invalidate();
    }
    void setTip(std::wstring t) { tip_ = std::move(t); }
    // The box's width for the text (its padding on both sides included).
    int preferredWidth(draw::Canvas& c) const;

    void paint(draw::Canvas& c) override;
    std::wstring tip(draw::Point) override { return tip_; }

private:
    std::wstring text_, tip_;
    BadgeTone tone_ = BadgeTone::Neutral;
    bool right_ = false;
};

}  // namespace stardraft::kit
