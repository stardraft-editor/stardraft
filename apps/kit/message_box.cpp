#include "message_box.h"

#include <algorithm>

#include "dialog.h"
#include "theme.h"

namespace stardraft::kit {

using draw::Canvas;
using draw::Rect;

namespace {

// The icon ids of apps/editor/res/ui_icons.h (UiIcon::Error, Warning, Help); the information icon is a glyph.
constexpr int kIconError = 86, kIconWarning = 87, kIconHelp = 88;
constexpr wchar_t kInfoGlyph[] = L"\xE946";
constexpr int kIconPx = 32, kIconGap = 12, kTextWrap = 420, kGlyphPx = 26;
// The estimate of the text before the window opens: the UI font's letters (7 px) and lines (16 px) at 100 %.
constexpr int kEstCharW = 7, kEstLineH = 16;

// The icon and the text beside it, as the box shows them. The dialog gives it the area above the buttons.
class Content : public draw::View {
public:
    Content(const std::wstring& text, MessageIcon icon) : text_(text), icon_(icon) {}

    void paint(Canvas& c) override {
        const Theme& th = theme();
        const int W = width(), H = height();
        const int iw = icon_ == MessageIcon::None ? 0 : px(kIconPx);
        if (iw) icon(c, {0, 0, iw, iw});
        const int tx = iw ? iw + px(kIconGap) : 0;
        const int th_ = c.measureBox(text_, std::max(1, W - tx), draw::kWrap).y;
        const int ty = std::max(0, (H - th_) / 2);  // the text is centered on the icon's height, or the area's
        c.text({tx, ty, W, ty + th_}, text_, th.text, draw::kWrap);
    }

private:
    void icon(Canvas& c, Rect r) {
        const Theme& th = theme();
        draw::Color col = th.accent;
        int id = -1;
        switch (icon_) {
        case MessageIcon::Error: id = kIconError, col = th.bad; break;
        case MessageIcon::Warning: id = kIconWarning, col = th.warn; break;
        case MessageIcon::Question: id = kIconHelp, col = th.accent; break;
        default: break;
        }
        if (id >= 0) {
            c.icon(r, id, col, px(kIconPx));
            return;
        }
        const draw::Font old = c.setFont(c.font({draw::Face::Glyphs, px(kGlyphPx)}));
        c.text(r, kInfoGlyph, col, draw::kMiddle | draw::kCenter);
        c.setFont(old);
    }

    std::wstring text_;
    MessageIcon icon_;
};

}  // namespace

int messageBox(draw::View& from, const std::wstring& title, const std::wstring& text, const std::vector<std::wstring>& buttons, MessageIcon icon) {
    const int pad = px(kDialogPad), iw = icon == MessageIcon::None ? 0 : px(kIconPx), gap = iw ? px(kIconGap) : 0;
    const int wrap = px(kTextWrap);

    // The size, estimated: the text's longest line (and its lines when they wrap), the buttons in a row.
    int longest = 0, lines = 0;
    size_t start = 0;
    while (true) {
        const size_t nl = text.find(L'\n', start);
        const size_t len = (nl == std::wstring::npos ? text.size() : nl) - start;
        const int w = int(len) * px(kEstCharW);
        longest = std::max(longest, w);
        lines += std::max(1, (w + wrap - 1) / wrap);
        if (nl == std::wstring::npos) break;
        start = nl + 1;
    }
    const int textW = std::min(wrap, longest), textH = lines * px(kEstLineH);
    const int contentH = std::max(textH, iw);
    int buttonsW = 0;
    for (size_t k = 0; k < buttons.size(); ++k)
        buttonsW += std::max(px(kButtonMinW), int(buttons[k].size()) * px(kEstCharW) + px(28)) + (k ? px(kButtonGap) : 0);
    const int W = std::max(pad + iw + gap + textW + pad, pad + buttonsW + pad);
    const int H = pad + contentH + px(kDialogGap) + px(kButtonH) + pad;

    Content content(text, icon);
    Dialog dlg;
    dlg.setContent(content);
    std::vector<DialogButton> bs;
    for (size_t k = 0; k < buttons.size(); ++k)
        bs.push_back(DialogButton{buttons[k], int(k), k == 0, k + 1 == buttons.size()});
    dlg.setButtons(std::move(bs));
    return dlg.run(from, title, {W, H});
}

}  // namespace stardraft::kit
