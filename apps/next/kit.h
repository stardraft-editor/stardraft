#pragma once

// stardraft-next's control kit on Dear ImGui: the Win32 kit's colors (apps/editor/theme.inc), sizes and fonts, and
// the few controls that look like the kit's own (the rail, section headers). ImGui draws; the look is StarDraft's.

#include <string>

#include "imgui.h"

namespace next::kit {

// The theme's colors (theme.inc's dark and light tokens, the default blue accent).
struct Theme {
    ImU32 bg, panel, ctl, ctlHot, ctlDown, text, dim, faint, border, borderHot, accent, accentLine, sel, selHot, onText, chip, good, warn, bad;
};
const Theme& theme();
void applyTheme(bool light, float scale);
bool lightTheme();

// The fonts: a Latin UI font with Korean and Chinese merged in (ImGui loads each glyph when it first shows), and a
// monospace font for code. Returns what was loaded, for the log ("Segoe UI + Malgun Gothic + ...").
std::string loadFonts(float scale);
ImFont* monoFont();

// The left rail: one item per workspace. Returns true when it was clicked.
bool railItem(const char* label, bool selected, float width);
// A section's title, dim and small, with space above.
void sectionHeader(const char* text);
// A text in the dim color.
void dimText(const char* fmt, ...);

}  // namespace next::kit
