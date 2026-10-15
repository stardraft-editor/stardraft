#pragma once
// An icon button (docs/dev/CONTROLS.md, "Icon buttons"): a square icon. Bare: it lights up under the mouse (beside a
// field, in a row). Boxed: in a button's box, flat until it is on or under the mouse (a tool bar, a player). `on`
// marks a toggle that is on; the owner sets it. Face: a button's own box (as Button's: the Primary look when on), the icon
// in the middle. The icon is a UI icon (an id of apps/editor/res/ui_icons.h) or a
// character of the Glyphs face (a Segoe MDL2 Assets code point). Click, Space or Enter runs `onClick`.

#include <functional>
#include <string>

#include "button.h"
#include "theme.h"
#include "view.h"

namespace stardraft::kit {

enum class IconStyle { Bare, Boxed, Face };

// What the button shows: a UI icon, or a glyph (when icon is -1).
struct IconGlyph {
    int icon = -1;
    wchar_t glyph = 0;
    static IconGlyph ofIcon(int id) { return IconGlyph{id, 0}; }
    static IconGlyph ofGlyph(wchar_t g) { return IconGlyph{-1, g}; }
};

class IconButton : public draw::View {
public:
    IconButton() = default;
    IconButton(IconGlyph glyph, IconStyle style, std::wstring tip, std::function<void()> onClick, Tone tone = Tone::Normal)
        : onClick(std::move(onClick)), glyph_(glyph), style_(style), tone_(tone), tip_(std::move(tip)) {}

    std::function<void()> onClick;

    void setGlyph(IconGlyph g) {
        glyph_ = g;
        invalidate();
    }
    void setStyle(IconStyle s) {
        style_ = s;
        invalidate();
    }
    void setTone(Tone t) {
        tone_ = t;
        invalidate();
    }
    // The toggle's state (the box is accent when it is on).
    void setOn(bool on) {
        on_ = on;
        invalidate();
    }
    bool on() const { return on_; }
    void setEnabled(bool e) {
        enabled_ = e;
        invalidate();
    }
    bool enabled() const { return enabled_; }
    void setTip(std::wstring t) { tip_ = std::move(t); }
    // The side of the button at 100 % (kIconBox).
    static int preferredSize();

    void paint(draw::Canvas& c) override;
    bool mouse(const draw::MouseEvent& e) override;
    bool key(const draw::KeyEvent& e) override;
    draw::Cursor cursor(draw::Point) override { return enabled_ ? draw::Cursor::Hand : draw::Cursor::Arrow; }
    std::wstring tip(draw::Point) override { return tip_; }
    bool focusable() const override { return enabled_; }

private:
    void drawGlyph(draw::Canvas& c, draw::Rect r, draw::Color col, int size) const;

    IconGlyph glyph_;
    IconStyle style_ = IconStyle::Bare;
    Tone tone_ = Tone::Normal;
    std::wstring tip_;
    bool on_ = false, enabled_ = true, hot_ = false, down_ = false;
};

}  // namespace stardraft::kit
