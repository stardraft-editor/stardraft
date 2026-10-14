#include "kit.h"

#include <SDL3/SDL_filesystem.h>

#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <vector>

namespace next::kit {

namespace {

constexpr ImU32 rgb(int r, int g, int b) { return IM_COL32(r, g, b, 255); }

// theme.inc's tokens with the default (blue) accent.
const Theme kDark = {rgb(32, 32, 32),    rgb(28, 28, 30),    rgb(43, 43, 43),   rgb(52, 52, 56),   rgb(36, 36, 38),
                     rgb(224, 224, 224), rgb(148, 148, 148), rgb(90, 90, 96),   rgb(60, 60, 64),   rgb(88, 88, 94),
                     rgb(98, 176, 240),  rgb(47, 109, 153),  rgb(23, 48, 63),   rgb(30, 62, 82),   rgb(230, 240, 250),
                     rgb(34, 42, 52),    rgb(127, 201, 138), rgb(224, 164, 88), rgb(232, 104, 96)};
const Theme kLight = {rgb(243, 243, 245), rgb(236, 236, 239), rgb(255, 255, 255), rgb(244, 245, 248), rgb(230, 231, 235),
                      rgb(28, 28, 32),    rgb(98, 98, 106),   rgb(158, 158, 166), rgb(208, 208, 214), rgb(162, 162, 172),
                      rgb(0, 101, 189),   rgb(120, 170, 225), rgb(229, 239, 250), rgb(214, 229, 247), rgb(255, 255, 255),
                      rgb(232, 240, 250), rgb(30, 128, 56),   rgb(158, 90, 8),    rgb(196, 46, 40)};
bool gLight = false;
ImFont* gMono = nullptr;

ImVec4 col(ImU32 c, float alpha = 1) {
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
    v.w = alpha;
    return v;
}

// The first file of the list that is there.
std::string firstFile(std::initializer_list<const char*> paths) {
    std::error_code ec;
    for (const char* p : paths)
        if (std::filesystem::is_regular_file(std::filesystem::path(reinterpret_cast<const char8_t*>(p)), ec)) return p;  // (UTF-8)
    return {};
}

}  // namespace

const Theme& theme() { return gLight ? kLight : kDark; }
bool lightTheme() { return gLight; }

void applyTheme(bool light, float scale) {
    gLight = light;
    const Theme& t = theme();
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    // The kit's shapes: small corners, thin borders, rows of 28 px, fields of 26 px (at 100 %).
    s.WindowRounding = 0, s.ChildRounding = 4, s.FrameRounding = 4, s.PopupRounding = 6, s.GrabRounding = 4, s.TabRounding = 4;
    s.ScrollbarRounding = 6, s.ScrollbarSize = 12;
    s.WindowBorderSize = 0, s.ChildBorderSize = 1, s.FrameBorderSize = 1, s.PopupBorderSize = 1;
    s.WindowPadding = {12, 10}, s.FramePadding = {8, 5}, s.ItemSpacing = {8, 6}, s.ItemInnerSpacing = {6, 4};
    s.CellPadding = {6, 4};
    s.ScaleAllSizes(scale);
    s.FontScaleDpi = scale;
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = col(t.text);
    c[ImGuiCol_TextDisabled] = col(t.dim);
    c[ImGuiCol_WindowBg] = col(t.bg);
    c[ImGuiCol_ChildBg] = col(t.panel);
    c[ImGuiCol_PopupBg] = col(t.panel);
    c[ImGuiCol_MenuBarBg] = col(t.panel);
    c[ImGuiCol_Border] = col(t.border);
    c[ImGuiCol_BorderShadow] = {0, 0, 0, 0};
    c[ImGuiCol_FrameBg] = col(t.ctl);
    c[ImGuiCol_FrameBgHovered] = col(t.ctlHot);
    c[ImGuiCol_FrameBgActive] = col(t.ctlDown);
    c[ImGuiCol_TitleBg] = c[ImGuiCol_TitleBgActive] = c[ImGuiCol_TitleBgCollapsed] = col(t.panel);
    c[ImGuiCol_ScrollbarBg] = {0, 0, 0, 0};
    c[ImGuiCol_ScrollbarGrab] = col(t.faint);
    c[ImGuiCol_ScrollbarGrabHovered] = c[ImGuiCol_ScrollbarGrabActive] = col(t.dim);
    c[ImGuiCol_CheckMark] = col(t.accent);
    c[ImGuiCol_SliderGrab] = c[ImGuiCol_SliderGrabActive] = col(t.accent);
    c[ImGuiCol_Button] = col(t.ctl);
    c[ImGuiCol_ButtonHovered] = col(t.ctlHot);
    c[ImGuiCol_ButtonActive] = col(t.ctlDown);
    c[ImGuiCol_Header] = col(t.sel);
    c[ImGuiCol_HeaderHovered] = col(t.selHot);
    c[ImGuiCol_HeaderActive] = col(t.selHot);
    c[ImGuiCol_Separator] = c[ImGuiCol_SeparatorHovered] = c[ImGuiCol_SeparatorActive] = col(t.border);
    c[ImGuiCol_ResizeGrip] = {0, 0, 0, 0};
    c[ImGuiCol_Tab] = col(t.panel);
    c[ImGuiCol_TabHovered] = col(t.ctlHot);
    c[ImGuiCol_TabSelected] = col(t.bg);
    c[ImGuiCol_TabSelectedOverline] = col(t.accent);
    c[ImGuiCol_TableHeaderBg] = col(t.panel);
    c[ImGuiCol_TableBorderStrong] = c[ImGuiCol_TableBorderLight] = col(t.border);
    c[ImGuiCol_TableRowBg] = {0, 0, 0, 0};
    c[ImGuiCol_TableRowBgAlt] = col(t.ctl, 0.35f);
    c[ImGuiCol_TextSelectedBg] = col(t.accentLine, 0.6f);
    c[ImGuiCol_NavCursor] = col(t.accent);
    c[ImGuiCol_InputTextCursor] = col(t.text);
}

std::string loadFonts(float scale) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    const float size = 15.0f;  // the base size; style.FontScaleDpi scales it (ImGui loads each size as it shows)
    (void)scale;
    // Latin first, then Korean and Chinese merged in. The program's own fonts (fonts/ beside it: Noto Sans, KR, SC;
    // the build puts them there) come first, so every system shows the same; else the system's.
    std::string bundled;
    if (const char* base = SDL_GetBasePath()) bundled = std::string(base) + "fonts/";
    const std::string ownLatin = firstFile({(bundled + "NotoSans-Regular.ttf").c_str()});
    const std::string ownKorean = firstFile({(bundled + "NotoSansKR-Regular.otf").c_str()});
    const std::string ownChinese = firstFile({(bundled + "NotoSansSC-Regular.otf").c_str()});
#if defined(_WIN32)
    std::string latin = firstFile({"C:\\Windows\\Fonts\\segoeui.ttf"});
    std::string korean = firstFile({"C:\\Windows\\Fonts\\malgun.ttf"});
    std::string chinese = firstFile({"C:\\Windows\\Fonts\\msyh.ttc", "C:\\Windows\\Fonts\\simsun.ttc"});
    const std::string mono = firstFile({"C:\\Windows\\Fonts\\CascadiaMono.ttf", "C:\\Windows\\Fonts\\consola.ttf"});
#elif defined(__APPLE__)
    std::string latin = firstFile({"/System/Library/Fonts/Supplemental/Arial.ttf", "/System/Library/Fonts/Helvetica.ttc"});
    std::string korean = firstFile({"/System/Library/Fonts/AppleSDGothicNeo.ttc", "/System/Library/Fonts/Supplemental/AppleGothic.ttf"});
    std::string chinese = firstFile({"/System/Library/Fonts/PingFang.ttc", "/System/Library/Fonts/STHeiti Medium.ttc", "/System/Library/Fonts/Hiragino Sans GB.ttc"});
    const std::string mono = firstFile({"/System/Library/Fonts/SFNSMono.ttf", "/System/Library/Fonts/Menlo.ttc"});
#else
    std::string latin = firstFile({"/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                                         "/usr/share/fonts/TTF/DejaVuSans.ttf"});
    std::string korean = firstFile({"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
                                          "/usr/share/fonts/truetype/nanum/NanumGothic.ttf"});
    std::string chinese = korean;  // (Noto Sans CJK has both)
    const std::string mono = firstFile({"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", "/usr/share/fonts/TTF/DejaVuSansMono.ttf"});
#endif
    if (!ownLatin.empty()) latin = ownLatin;
    if (!ownKorean.empty()) korean = ownKorean;
    if (!ownChinese.empty()) chinese = ownChinese;
    std::string loaded;
    ImFontConfig cfg;
    if (!latin.empty() && io.Fonts->AddFontFromFileTTF(latin.c_str(), size, &cfg)) loaded = std::filesystem::path(latin).filename().string();
    else io.Fonts->AddFontDefault(), loaded = "ImGui's own (no system font found)";
    for (const std::string* f : {&korean, &chinese}) {
        if (f->empty() || (f == &chinese && *f == korean)) continue;
        ImFontConfig merge;
        merge.MergeMode = true;
        if (io.Fonts->AddFontFromFileTTF(f->c_str(), size, &merge)) loaded += " + " + std::filesystem::path(*f).filename().string();
    }
    gMono = mono.empty() ? nullptr : io.Fonts->AddFontFromFileTTF(mono.c_str(), size);
    if (gMono) loaded += "; code: " + std::filesystem::path(mono).filename().string();
    return loaded;
}

ImFont* monoFont() { return gMono; }

bool railItem(const char* label, bool selected, float width) {
    const Theme& t = theme();
    const float h = ImGui::GetFontSize() * 2.6f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushID(label);
    const bool clicked = ImGui::InvisibleButton("##rail", {width, h});
    const bool hot = ImGui::IsItemHovered();
    ImDrawList* d = ImGui::GetWindowDrawList();
    const float r = ImGui::GetStyle().FrameRounding;
    if (selected || hot) d->AddRectFilled({p.x + 4, p.y + 2}, {p.x + width - 4, p.y + h - 2}, selected ? t.sel : t.ctlHot, r);
    if (selected) d->AddRectFilled({p.x + 4, p.y + h * 0.25f}, {p.x + 7, p.y + h * 0.75f}, t.accent, 2);
    const ImVec2 ts = ImGui::CalcTextSize(label);
    d->AddText({p.x + (width - ts.x) / 2, p.y + (h - ts.y) / 2}, selected ? t.text : t.dim, label);
    ImGui::PopID();
    return clicked;
}

void sectionHeader(const char* text) {
    ImGui::Dummy({0, ImGui::GetFontSize() * 0.4f});
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(theme().dim));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

void dimText(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(theme().dim));
    ImGui::TextV(fmt, args);
    ImGui::PopStyleColor();
    va_end(args);
}

}  // namespace next::kit
