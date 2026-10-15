#pragma once
// The control kit's look as data: colors, sizes and the display scale. The editor sets it from its theme
// (apps/editor/theme.inc) and for each change of theme, accent or DPI; every kit widget draws with it, so the widgets
// need nothing from the editor. docs/dev/CONTROLS.md says what each token is for.

#include "canvas.h"

namespace stardraft::kit {

using draw::Color;

struct Theme {
    // Surfaces: the window, side panels, a control's face (under the mouse, pressed), a read-only field.
    Color bg = draw::rgb(32, 32, 32), panel = draw::rgb(28, 28, 30), ctl = draw::rgb(43, 43, 43), ctlHot = draw::rgb(52, 52, 56),
          ctlDown = draw::rgb(36, 36, 38), readOnly = draw::rgb(30, 30, 32), readOnlyLine = draw::rgb(48, 48, 52);
    // Text: normal, dim (labels, notes), faint (what cannot be used now).
    Color text = draw::rgb(224, 224, 224), dim = draw::rgb(148, 148, 148), faint = draw::rgb(90, 90, 96);
    // Lines.
    Color border = draw::rgb(60, 60, 64), borderHot = draw::rgb(88, 88, 94), borderStrong = draw::rgb(110, 110, 118);
    // The accent: links, the selected item's text, focus; selected rows and primary buttons: sel with accentLine.
    Color accent = draw::rgb(98, 176, 240), accentBright = draw::rgb(130, 200, 255), accentLine = draw::rgb(47, 109, 153),
          accentFill = draw::rgb(47, 109, 153), sel = draw::rgb(23, 48, 63), selHot = draw::rgb(30, 62, 82), onText = draw::rgb(230, 240, 250);
    // Value chips, scroll bar thumbs, a table's row line, a grip's dots.
    Color chip = draw::rgb(34, 42, 52), chipLine = draw::rgb(52, 66, 80), badFill = draw::rgb(64, 32, 32), badFillHot = draw::rgb(84, 40, 38),
          chipDim = draw::rgb(38, 38, 41);
    Color thumb = draw::rgb(84, 84, 90), thumbHot = draw::rgb(120, 120, 128), rowLine = draw::rgb(44, 46, 52), gripDot = draw::rgb(110, 110, 116);
    // A list row under the mouse; hot: menus, tabs.
    Color rowHot = draw::rgb(46, 46, 50), hot = draw::rgb(62, 62, 66);
    // Status: changed or right, a warning, an error or a removal.
    Color good = draw::rgb(127, 201, 138), goodFill = draw::rgb(28, 49, 34), goodLine = draw::rgb(61, 107, 71), warn = draw::rgb(224, 164, 88),
          bad = draw::rgb(232, 104, 96);
    bool light = false;
    int dpi = 96;

    // `v` px at 100 % in the display's pixels (as Win32 MulDiv: rounded half away from zero).
    int px(int v) const {
        const long long n = (long long)v * dpi;
        return int(n >= 0 ? (n + 48) / 96 : -((-n + 48) / 96));
    }
};

// The theme every widget uses.
Theme& theme();
inline int px(int v) { return theme().px(v); }

// Sizes at 100 % (docs/dev/CONTROLS.md): spacing 4 / 8 / 12 / 16 / 24 / 32.
constexpr int kRadius = 4;       // every box: buttons, fields, chips, cards, pop-outs
constexpr int kRowH = 26;        // a list row, a row of a dense form
constexpr int kFormRowH = 32;    // a row of a dialog's form
constexpr int kFieldH = 24;      // a field, a dropdown, a search box
constexpr int kButtonH = 28;
constexpr int kButtonMinW = 88;  // a button with a short name
constexpr int kButtonGap = 8;    // between the buttons of a row
constexpr int kDialogPad = 16;   // a dialog's edge to its content
constexpr int kIconBox = 24;     // an icon button in a row
constexpr int kGlyph = 12;       // a glyph in a row

}  // namespace stardraft::kit
