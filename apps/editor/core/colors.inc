// The color tokens (docs/dev/CONTROLS.md) and the themes (theme.inc): portable (core.inc), moved from main.cpp as they
// were.

// The colors (docs/dev/CONTROLS.md). Every control takes its colors from these. The values here are the dark theme's;
// setThemeColors (theme.inc) sets them for the theme and the accent the user picked.
// Surfaces: the window, side panels, a control's face (and under the mouse, pressed), a read-only field.
COLORREF kBg = RGB(32, 32, 32), kPanel = RGB(28, 28, 30), kCtl = RGB(43, 43, 43), kCtlHot = RGB(52, 52, 56), kCtlDown = RGB(36, 36, 38),
         kReadOnly = RGB(30, 30, 32), kReadOnlyLine = RGB(48, 48, 52);
// Text: normal, dim (labels, notes), faint (what cannot be used now).
COLORREF kText = RGB(224, 224, 224), kDim = RGB(148, 148, 148), kFaint = RGB(90, 90, 96);
// Lines: a quiet 1px border a few shades above the field, brighter under the mouse; the box of a check box.
COLORREF kBorder = RGB(60, 60, 64), kBorderHot = RGB(88, 88, 94), kBorderStrong = RGB(110, 110, 118);
// The accent: links, the selected item's text, focus. Selected rows and primary buttons: kSel with kAccentLine.
COLORREF kAccent = RGB(98, 176, 240), kAccentBright = RGB(130, 200, 255), kAccentLine = RGB(47, 109, 153), kAccentFill = RGB(47, 109, 153),
         kSel = RGB(23, 48, 63), kSelHot = RGB(30, 62, 82), kOnText = RGB(230, 240, 250);
// A value chip (trigger sentences): its ground and line (a quiet tint of the accent), and a chip with an error.
// Scroll bar thumbs, a table's row line, a grip's dots.
COLORREF kChip = RGB(34, 42, 52), kChipLine = RGB(52, 66, 80), kBadFill = RGB(64, 32, 32), kBadFillHot = RGB(84, 40, 38), kChipDim = RGB(38, 38, 41);
COLORREF kThumb = RGB(84, 84, 90), kThumbHot = RGB(120, 120, 128), kRowLine = RGB(44, 46, 52), kGripDot = RGB(110, 110, 116);
// A list row under the mouse; kHot is the same (menus, tabs).
COLORREF kRowHot = RGB(46, 46, 50), kHot = RGB(62, 62, 66);
// Status: changed or right (green), a warning (amber), an error or a removal (red).
COLORREF kGood = RGB(127, 201, 138), kGoodFill = RGB(28, 49, 34), kGoodLine = RGB(61, 107, 71), kWarn = RGB(224, 164, 88), kBad = RGB(232, 104, 96);
// Report feedback: the same glyph and color wherever it is (the menu item, the dialog).
COLORREF kFeedback = RGB(242, 204, 84);
constexpr UiIcon kFeedbackIcon = UiIcon::Feedback;

#include "../theme.inc"
