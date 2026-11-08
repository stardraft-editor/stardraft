#pragma once
// A card (docs/dev/CONTROLS.md, "Cards"): a box in the panel color with a 1 px border, behind a group of controls that
// belong together (a dialog's step: the file and its state; a summary's count). It only paints: the controls are its
// children, or views at the same place added after it (the owner places them).

#include "theme.h"
#include "view.h"

namespace stardraft::kit {

class Card : public draw::View {
public:
    void paint(draw::Canvas& c) override {
        const Theme& th = theme();
        c.roundBox({0, 0, width(), height()}, th.panel, th.border, float(px(kRadius)));
    }
};

}  // namespace stardraft::kit
