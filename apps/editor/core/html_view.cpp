// The HTML view (html_view.h): litehtml's document container on the program's Canvas.

#include "html_view.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <thread>

#include <litehtml.h>
#include <litehtml/url.h>
#include <utility>

// stb_image of its own (static: the kit's canvas has one for PNG only), for the images of a page: PNG, JPEG, GIF, BMP.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_PSD
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include <stb_image.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "theme.h"  // apps/kit: the colors and the display scale

namespace stardraft::html {

namespace fs = std::filesystem;
using litehtml::pixel_t;

Hooks& hooks() {
    static Hooks h;
    return h;
}

namespace {

// UTF-8 to the canvas's wide text (UTF-16 on Windows, UTF-32 elsewhere); a bad byte is U+FFFD.
std::wstring wide(const char* s, size_t n) {
    std::wstring out;
    out.reserve(n);
    for (size_t i = 0; i < n;) {
        const unsigned char b = (unsigned char)s[i];
        unsigned cp = 0xFFFD;
        size_t len = 1;
        if (b < 0x80) cp = b;
        else if ((b >> 5) == 6 && i + 1 < n) cp = (b & 0x1Fu) << 6 | (s[i + 1] & 0x3Fu), len = 2;
        else if ((b >> 4) == 14 && i + 2 < n) cp = (b & 0x0Fu) << 12 | (s[i + 1] & 0x3Fu) << 6 | (s[i + 2] & 0x3Fu), len = 3;
        else if ((b >> 3) == 30 && i + 3 < n) cp = (b & 0x07u) << 18 | (s[i + 1] & 0x3Fu) << 12 | (s[i + 2] & 0x3Fu) << 6 | (s[i + 3] & 0x3Fu), len = 4;
        i += len;
        if constexpr (sizeof(wchar_t) == 2) {
            if (cp >= 0x10000) {
                cp -= 0x10000;
                out += wchar_t(0xD800 + (cp >> 10)), out += wchar_t(0xDC00 + (cp & 0x3FF));
                continue;
            }
        }
        out += wchar_t(cp);
    }
    return out;
}
std::wstring wide(const std::string& s) { return wide(s.data(), s.size()); }

draw::Color color(const litehtml::web_color& c) { return draw::rgb(c.red, c.green, c.blue); }

std::string hex(draw::Color c) {
    char b[8];
    std::snprintf(b, sizeof b, "#%02x%02x%02x", unsigned(c & 255), unsigned(c >> 8 & 255), unsigned(c >> 16 & 255));
    return b;
}

bool hasScheme(const std::string& u) {
    const size_t colon = u.find(':');
    if (colon == std::string::npos || colon < 2) return false;  // (a drive letter, C:\..., is not a scheme)
    for (size_t i = 0; i < colon; ++i)
        if (!std::isalpha((unsigned char)u[i]) && u[i] != '+' && u[i] != '-' && u[i] != '.') return false;
    return true;
}
fs::path upath(const std::string& utf8) { return fs::path(std::u8string(utf8.begin(), utf8.end())); }
bool isWeb(const std::string& u) { return u.rfind("http://", 0) == 0 || u.rfind("https://", 0) == 0; }

// `ref` as a full URL (or a file path) from `base`: a web address resolves as a browser does; a file path joins the
// base's folder.
std::string resolveUrl(const std::string& base, const std::string& ref) {
    if (ref.empty() || hasScheme(ref)) return ref;
    if (isWeb(base)) return litehtml::resolve(litehtml::url(base), litehtml::url(ref)).str();
    if (ref[0] == '#') return ref;
    fs::path p = upath(ref);
    if (p.is_absolute() || base.empty()) return ref;
    const std::u8string joined = (upath(base).parent_path() / p).lexically_normal().u8string();
    return std::string(joined.begin(), joined.end());
}

// An image file's pixels in a bitmap (premultiplied alpha); false when it does not decode.
bool decode(const std::string& bytes, draw::Bitmap& bmp) {
    int w = 0, h = 0, n = 0;
    unsigned char* px = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(bytes.data()), int(bytes.size()), &w, &h, &n, 4);
    if (!px) return false;
    bmp.resize(w, h);
    uint32_t* out = bmp.pixels();
    bool opaque = true;
    for (size_t i = 0; i < size_t(w) * size_t(h); ++i) {
        const unsigned r = px[i * 4], g = px[i * 4 + 1], b = px[i * 4 + 2], a = px[i * 4 + 3];
        if (a != 255) opaque = false;
        out[i] = (r * a / 255) | (g * a / 255) << 8 | (b * a / 255) << 16 | a << 24;
    }
    stbi_image_free(px);
    bmp.opaque = opaque;
    bmp.changed();
    return true;
}

bool readFile(const fs::path& p, std::string& out) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

}  // namespace

struct HtmlView::Impl final : litehtml::document_container {
    HtmlView* owner = nullptr;
    draw::Canvas* c = nullptr;  // the canvas of this layout or paint
    float s = 1;                // the display's pixels for each CSS pixel
    std::string html_, base, caption;
    litehtml::document::ptr doc;
    int clipDepth = 0;

    struct FontRec {
        draw::Font font;
        bool underline = false, strike = false;
        int height = 0, descent = 0;  // the display's pixels
    };
    std::vector<std::unique_ptr<FontRec>> fonts;

    struct Img {
        enum State { Loading, Ready, Failed } state = Loading;
        draw::Bitmap bmp;
    };
    std::map<std::string, Img> images;

    std::string resolve(const char* src, const char* baseurl) const {
        return resolveUrl(baseurl && *baseurl ? std::string(baseurl) : base, src ? src : "");
    }

    // --- fonts and text ---
    litehtml::uint_ptr create_font(const litehtml::font_description& d, const litehtml::document*, litehtml::font_metrics* fm) override {
        auto rec = std::make_unique<FontRec>();
        std::string fam = d.family;
        std::transform(fam.begin(), fam.end(), fam.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
        const bool mono = fam.find("mono") != std::string::npos || fam.find("consol") != std::string::npos ||
                          fam.find("courier") != std::string::npos || fam.find("menlo") != std::string::npos;
        draw::FontDesc fd;
        fd.face = mono ? draw::Face::Mono : draw::Face::Ui;
        fd.px = std::max(1, int(std::lround(d.size * s)));
        fd.bold = d.weight >= 600, fd.italic = d.style != litehtml::font_style_normal;
        rec->underline = (d.decoration_line & litehtml::text_decoration_line_underline) != 0;
        rec->strike = (d.decoration_line & litehtml::text_decoration_line_line_through) != 0;
        if (c) {
            rec->font = c->font(fd);
            const draw::Font old = c->setFont(rec->font);
            const draw::FontMetrics m = c->metrics();
            rec->height = m.height, rec->descent = m.descent;
            if (fm) {
                fm->font_size = d.size;
                fm->height = pixel_t(m.height) / s;
                fm->descent = pixel_t(m.descent) / s;
                fm->ascent = fm->height - fm->descent;
                fm->x_height = fm->ascent * 0.55f;
                fm->ch_width = pixel_t(c->measureAt(L"0")) / s;
                fm->draw_spaces = rec->underline || rec->strike;
                fm->sub_shift = fm->height / 5, fm->super_shift = fm->height / 3;
            }
            c->setFont(old);
        }
        fonts.push_back(std::move(rec));
        return litehtml::uint_ptr(fonts.back().get());
    }
    void delete_font(litehtml::uint_ptr) override {}  // (they go with the view's layout: fonts)
    pixel_t text_width(const char* text, litehtml::uint_ptr hFont) override {
        const FontRec* f = reinterpret_cast<const FontRec*>(hFont);
        if (!c || !f || !f->font) return 0;
        const draw::Font old = c->setFont(f->font);
        const int w = c->measureAt(wide(text, std::strlen(text)));
        c->setFont(old);
        return pixel_t(w) / s;
    }
    void draw_text(litehtml::uint_ptr hdc, const char* text, litehtml::uint_ptr hFont, litehtml::web_color col, const litehtml::position& pos) override {
        draw::Canvas* cv = reinterpret_cast<draw::Canvas*>(hdc);
        const FontRec* f = reinterpret_cast<const FontRec*>(hFont);
        if (!cv || !f || !f->font) return;
        const draw::Font old = cv->setFont(f->font);
        const int x = int(std::lround(pos.x * s)), y = int(std::lround(pos.y * s));
        const std::wstring t = wide(text, std::strlen(text));
        cv->textAt({x, y}, t, color(col));
        if (f->underline || f->strike) {
            const int w = cv->measureAt(t);
            const int ly = f->underline ? y + f->height - f->descent + std::max(1, int(s)) : y + (f->height - f->descent) * 3 / 5;
            cv->fill({x, ly, x + w, ly + std::max(1, int(std::lround(s)))}, color(col));
        }
        cv->setFont(old);
    }
    pixel_t pt_to_px(float pt) const override { return pt * 96 / 72; }
    pixel_t get_default_font_size() const override { return 15; }
    const char* get_default_font_name() const override { return "sans-serif"; }
    void transform_text(litehtml::string& text, litehtml::text_transform tt) override {
        if (tt == litehtml::text_transform_uppercase)
            for (char& ch : text) ch = char(std::toupper((unsigned char)ch));
        else if (tt == litehtml::text_transform_lowercase)
            for (char& ch : text) ch = char(std::tolower((unsigned char)ch));
        else if (tt == litehtml::text_transform_capitalize && !text.empty())
            text[0] = char(std::toupper((unsigned char)text[0]));
    }

    // --- boxes ---
    draw::Rect rect(const litehtml::position& p) const {
        return {int(std::lround(p.x * s)), int(std::lround(p.y * s)), int(std::lround((p.x + p.width) * s)), int(std::lround((p.y + p.height) * s))};
    }
    void draw_list_marker(litehtml::uint_ptr hdc, const litehtml::list_marker& m) override {
        draw::Canvas* cv = reinterpret_cast<draw::Canvas*>(hdc);
        if (!cv) return;
        const draw::Rect r = rect(m.pos);
        if (!m.image.empty()) {
            auto it = images.find(resolve(m.image.c_str(), m.baseurl));
            if (it != images.end() && it->second.state == Img::Ready) {
                cv->bitmap(it->second.bmp, {0, 0, it->second.bmp.width(), it->second.bmp.height()}, r, true);
                return;
            }
        }
        const draw::Color col = color(m.color);
        switch (m.marker_type) {
        case litehtml::list_style_type_circle: cv->smoothArc({float(r.left), float(r.top)}, float(r.width()), float(r.height()), 0, 360, col, std::max(1.0f, s)); break;
        case litehtml::list_style_type_square: cv->fill(r, col); break;
        case litehtml::list_style_type_none: break;
        default: cv->smoothEllipse({float(r.left), float(r.top)}, float(r.width()), float(r.height()), col); break;
        }
    }
    void draw_solid_fill(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const litehtml::web_color& col) override {
        draw::Canvas* cv = reinterpret_cast<draw::Canvas*>(hdc);
        if (!cv || col.alpha == 0) return;
        const draw::Rect r = rect(layer.border_box);
        const float rad = layer.border_radius.top_left_x * s;
        if (rad >= 1 && col.alpha == 255) cv->roundBox(r, color(col), draw::kNone, rad);
        else cv->fill(r, color(col), col.alpha);
    }
    void draw_linear_gradient(litehtml::uint_ptr, const litehtml::background_layer&, const litehtml::background_layer::linear_gradient&) override {}
    void draw_radial_gradient(litehtml::uint_ptr, const litehtml::background_layer&, const litehtml::background_layer::radial_gradient&) override {}
    void draw_conic_gradient(litehtml::uint_ptr, const litehtml::background_layer&, const litehtml::background_layer::conic_gradient&) override {}
    void draw_borders(litehtml::uint_ptr hdc, const litehtml::borders& b, const litehtml::position& pos, bool) override {
        draw::Canvas* cv = reinterpret_cast<draw::Canvas*>(hdc);
        if (!cv) return;
        const draw::Rect r = rect(pos);
        auto shown = [](const litehtml::border& e) { return e.width > 0 && e.style != litehtml::border_style_none && e.style != litehtml::border_style_hidden; };
        auto px = [this](pixel_t w) { return std::max(1, int(std::lround(w * s))); };
        const bool same = shown(b.left) && shown(b.top) && shown(b.right) && shown(b.bottom) && b.left.width == b.top.width &&
                          b.left.width == b.right.width && b.left.width == b.bottom.width && b.left.color == b.top.color &&
                          b.left.color == b.right.color && b.left.color == b.bottom.color;
        if (same && b.radius.top_left_x >= 1) {
            cv->roundBox(r, draw::kNone, color(b.left.color), b.radius.top_left_x * s, px(b.left.width));
            return;
        }
        if (shown(b.top)) cv->fill({r.left, r.top, r.right, r.top + px(b.top.width)}, color(b.top.color), b.top.color.alpha);
        if (shown(b.bottom)) cv->fill({r.left, r.bottom - px(b.bottom.width), r.right, r.bottom}, color(b.bottom.color), b.bottom.color.alpha);
        if (shown(b.left)) cv->fill({r.left, r.top, r.left + px(b.left.width), r.bottom}, color(b.left.color), b.left.color.alpha);
        if (shown(b.right)) cv->fill({r.right - px(b.right.width), r.top, r.right, r.bottom}, color(b.right.color), b.right.color.alpha);
    }
    void set_clip(const litehtml::position& pos, const litehtml::border_radiuses&) override {
        if (!c) return;
        c->pushClip(rect(pos));
        ++clipDepth;
    }
    void del_clip() override {
        if (!c || clipDepth <= 0) return;
        c->popClip();
        --clipDepth;
    }

    // --- images ---
    void load_image(const char* src, const char* baseurl, bool) override {
        const std::string url = resolve(src, baseurl);
        if (url.empty() || images.count(url)) return;
        Img& img = images[url];
        if (!isWeb(url)) {  // a file: read now (a page on disk has its images laid out at once)
            std::string path = url;
            if (path.rfind("file://", 0) == 0) {
                path.erase(0, 7);
                if (path.size() > 2 && path[0] == '/' && path[2] == ':') path.erase(0, 1);  // (file:///C:/...)
            }
            std::string bytes;
            img.state = readFile(upath(path), bytes) && decode(bytes, img.bmp) ? Img::Ready : Img::Failed;
            return;
        }
        const Hooks h = hooks();
        if (!h.fetch || !h.post) {
            img.state = Img::Failed;
            return;
        }
        std::weak_ptr<Impl> self = owner->impl_;
        std::thread([self, url, h] {
            std::string bytes;
            const bool got = h.fetch(url, bytes);
            h.post([self, url, got, bytes = std::move(bytes)] {
                const std::shared_ptr<Impl> p = self.lock();
                if (!p) return;  // (the view is gone)
                const auto it = p->images.find(url);
                if (it == p->images.end()) return;  // (another page shows since: its images are not this one's)
                Img& im = it->second;
                im.state = got && decode(bytes, im.bmp) ? Img::Ready : Img::Failed;
                if (im.state == Img::Ready) p->owner->laidOutWidth_ = -1, p->owner->heights_.clear(), p->owner->layout(p->owner->width());  // (its size changes the layout)
                p->owner->invalidate();
            });
        }).detach();
    }
    void get_image_size(const char* src, const char* baseurl, litehtml::size& sz) override {
        const auto it = images.find(resolve(src, baseurl));
        if (it != images.end() && it->second.state == Img::Ready) sz.width = pixel_t(it->second.bmp.width()), sz.height = pixel_t(it->second.bmp.height());
        else sz.width = sz.height = 0;
    }
    void draw_image(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const std::string& url, const std::string& base_url) override {
        draw::Canvas* cv = reinterpret_cast<draw::Canvas*>(hdc);
        if (!cv) return;
        const auto it = images.find(resolve(url.c_str(), base_url.c_str()));
        if (it == images.end() || it->second.state != Img::Ready) return;
        draw::Bitmap& b = it->second.bmp;
        cv->pushClip(rect(layer.clip_box));
        cv->bitmap(b, {0, 0, b.width(), b.height()}, rect(layer.origin_box), true);
        cv->popClip();
    }

    // --- the page ---
    void set_caption(const char* cap) override { caption = cap ? cap : ""; }
    void set_base_url(const char* url) override {
        if (url && *url) base = resolveUrl(base, url);
    }
    void link(const std::shared_ptr<litehtml::document>&, const litehtml::element::ptr&) override {}
    void on_anchor_click(const char*, const litehtml::element::ptr&) override {}  // (the view follows its links itself: HtmlView::mouse)
    void on_mouse_event(const litehtml::element::ptr&, litehtml::mouse_event) override {}
    void set_cursor(const char*) override {}
    void import_css(litehtml::string&, const litehtml::string&, litehtml::string&) override {}  // (a page's own stylesheets: not fetched)
    void get_viewport(litehtml::position& vp) const override {
        vp = litehtml::position(0, 0, pixel_t(owner->width()) / s, pixel_t(owner->parent() ? owner->parent()->height() : owner->height()) / s);
    }
    litehtml::element::ptr create_element(const char*, const litehtml::string_map&, const std::shared_ptr<litehtml::document>&) override { return nullptr; }
    void get_media_features(litehtml::media_features& m) const override {
        litehtml::position vp;
        get_viewport(vp);
        m.type = litehtml::media_type_screen;
        m.width = m.device_width = vp.width;
        m.height = m.device_height = vp.height;
        m.color = 8, m.monochrome = 0, m.color_index = 256, m.resolution = 96;
    }
    void get_language(litehtml::string& language, litehtml::string& culture) const override { language = "en", culture = ""; }
};

HtmlView::HtmlView() : impl_(std::make_shared<Impl>()) { impl_->owner = this; }
HtmlView::~HtmlView() = default;

std::string HtmlView::themeCss() {
    const kit::Theme& t = kit::theme();
    const std::string text = hex(t.text), dim = hex(t.dim), border = hex(t.border), accent = hex(t.accent), code = hex(t.chip),
                      block = hex(t.readOnly), head = hex(t.ctl);
    return "html, body { background: transparent; color: " + text + "; font-family: sans-serif; font-size: 15px; line-height: 1.5; margin: 0; padding: 0; }\n"
           "h1, h2, h3, h4, h5, h6 { font-weight: bold; line-height: 1.25; margin: 20px 0 10px 0; }\n"
           "h1 { font-size: 26px; padding-bottom: 6px; border-bottom: 1px solid " + border + "; }\n"
           "h2 { font-size: 21px; padding-bottom: 4px; border-bottom: 1px solid " + border + "; }\n"
           "h3 { font-size: 18px; } h4 { font-size: 15px; } h5 { font-size: 14px; } h6 { font-size: 13px; color: " + dim + "; }\n"
           "body > :first-child, .markdown-heading:first-child h1, .markdown-heading:first-child h2 { margin-top: 0; }\n"
           "p, ul, ol, dl, table, pre, blockquote, details { margin: 0 0 12px 0; }\n"
           "ul, ol { padding-left: 26px; } li { margin: 2px 0; } li > p { margin: 0; } ul ul, ol ol, ul ol, ol ul { margin: 0; }\n"
           "a { color: " + accent + "; text-decoration: none; }\n"
           "code, tt, kbd, samp, pre { font-family: monospace; font-size: 13px; }\n"
           "code, tt { background-color: " + code + "; padding: 1px 4px; border-radius: 4px; }\n"
           "pre { background-color: " + block + "; border: 1px solid " + border + "; border-radius: 4px; padding: 10px 12px; white-space: pre; overflow: hidden; line-height: 1.4; }\n"
           "pre code, pre tt { background-color: transparent; padding: 0; border-radius: 0; }\n"
           "blockquote { color: " + dim + "; border-left: 3px solid " + border + "; padding: 0 12px; margin-left: 0; margin-right: 0; }\n"
           "table { border-collapse: collapse; } th, td { border: 1px solid " + border + "; padding: 5px 10px; }\n"
           "th { font-weight: bold; background-color: " + head + "; }\n"
           "hr { border: 0; border-top: 1px solid " + border + "; height: 0; margin: 16px 0; }\n"
           "kbd { border: 1px solid " + border + "; border-radius: 4px; padding: 0 4px; background-color: " + head + "; }\n"
           "img { max-width: 100%; border: 0; }\n"
           "strong, b, th { font-weight: bold; } em, i { font-style: italic; } del, s { text-decoration: line-through; }\n"
           ".anchor, .octicon, svg, .sr-only { display: none; }\n";
}

void HtmlView::setHtml(const std::string& html, const std::string& baseUrl) {
    impl_->doc.reset();
    impl_->fonts.clear();
    impl_->images.clear();  // (the page before's images: their pixels and the canvas's copies are freed)
    impl_->caption.clear();
    impl_->base = base_ = baseUrl;
    impl_->html_ = html;
    laidOutWidth_ = -1;
    layout(width());
    invalidate();
}

void HtmlView::setExtraCss(const std::string& css) {
    extraCss_ = css;
    themeChanged();
}

void HtmlView::themeChanged() {
    impl_->doc.reset();
    impl_->fonts.clear();
    laidOutWidth_ = -1;
    layout(width());
    invalidate();
}

int HtmlView::layout(int w) {
    if (w <= 0) w = width();
    if (laying_ || w <= 0) return height_;
    Impl& m = *impl_;
    draw::Canvas* canvas = m.c ? m.c : hooks().measure ? hooks().measure() : nullptr;
    if (!canvas) return height_;  // (laid out at the first paint)
    if (w == laidOutWidth_ && m.doc) return height_;
    laying_ = true;
    draw::Canvas* const was = m.c;
    m.c = canvas;
    const float scaleNow = float(kit::theme().dpi) / 96.0f;
    if (!m.doc || scaleNow != m.s) {
        m.s = scaleNow;
        m.doc.reset();
        m.fonts.clear();
        m.base = base_;
        m.doc = litehtml::document::createFromString(m.html_, &m, litehtml::master_css, themeCss() + extraCss_);
        heights_.clear();
    }
    if (m.doc) {
        m.doc->render(pixel_t(w) / m.s);
        height_ = int(std::ceil(m.doc->height() * m.s));
        heights_[w] = height_;
    } else {
        height_ = 0;
    }
    laidOutWidth_ = w;
    links_ = findLinks();
    hot_ = -1;
    m.c = was;
    laying_ = false;
    if (height() != height_) setSize(w, height_);
    return height_;
}

// A scroll view tries the width without its bar, then with it, at each event and paint: a width that was laid out
// before takes its height at once (the page is laid out at the paint, at the width it keeps).
void HtmlView::resized() {
    if (width() == laidOutWidth_) return;
    if (const auto it = heights_.find(width()); impl_->doc && it != heights_.end()) {
        height_ = it->second;
        if (height() != height_) setSize(width(), height_);
        return;
    }
    layout(width());
}

void HtmlView::paint(draw::Canvas& c) {
    Impl& m = *impl_;
    const kit::Theme& t = kit::theme();
    const unsigned long long key = (unsigned long long)t.text << 40 ^ (unsigned long long)t.accent << 16 ^ (unsigned long long)t.bg ^ (unsigned long long)t.dpi << 56;
    if (key != themeKey_) {
        if (themeKey_ != 0) m.doc.reset(), m.fonts.clear();
        themeKey_ = key;
    }
    m.c = &c;
    if (!m.doc || laidOutWidth_ != width()) layout(width());
    if (m.doc) {
        // Only the part that shows (in a scroll view: the content's frame is above the viewport's top by its offset).
        int top = 0, bottom = height();
        if (const draw::View* p = parent()) top = std::max(0, -frame().top), bottom = std::min(height(), top + p->height());
        const litehtml::position clip(0, pixel_t(top) / m.s, pixel_t(width()) / m.s, pixel_t(bottom - top) / m.s);
        m.clipDepth = 0;
        m.doc->draw(litehtml::uint_ptr(&c), 0, 0, &clip);
        while (m.clipDepth > 0) c.popClip(), --m.clipDepth;
    }
    // The link under the mouse is underlined (one line: a link that wraps is not). (litehtml's :hover cannot do it:
    // a style that changes under the mouse loses the link's boxes.)
    if (hot_ >= 0 && hot_ < int(links_.size())) {
        const draw::Rect& r = links_[size_t(hot_)].box;
        if (r.height() <= int(28 * m.s)) c.fill({r.left, r.bottom - std::max(1, int(m.s)), r.right, r.bottom}, kit::theme().accent);
    }
    m.c = nullptr;
}

// The links are the view's own (their boxes from the layout: links_): the mouse on one shows the hand and underlines
// it, and a press and a release on the same link follow it. (litehtml's own mouse handling is not used: a style that
// changes under the mouse loses the link's boxes, and its click depends on its hover state.)
bool HtmlView::mouse(const draw::MouseEvent& e) {
    if (!impl_->doc) return false;
    switch (e.kind) {
    case draw::MouseKind::Move:
        if (const int h = linkAt(e.pt); h != hot_) hot_ = h, invalidate();
        return true;
    case draw::MouseKind::Leave:
        if (hot_ >= 0) hot_ = -1, invalidate();
        return true;
    case draw::MouseKind::Down:
    case draw::MouseKind::DoubleClick:
        if (e.button != 0) return false;
        pressed_ = linkAt(e.pt);
        return true;
    case draw::MouseKind::Up: {
        if (e.button != 0) return false;
        const int at = linkAt(e.pt), was = std::exchange(pressed_, -1);
        if (at < 0 || at != was) return true;
        const std::string url = links_[size_t(at)].url;  // (a copy: onLink can show another page)
        if (url[0] == '#') {
            const int y = anchorY(url.substr(1));
            if (y >= 0 && onScrollTo) onScrollTo(y);
            return true;
        }
        if (onLink) onLink(url);
        return true;
    }
    default: return false;  // (the wheel: the scroll view)
    }
}

draw::Cursor HtmlView::cursor(draw::Point p) { return linkAt(p) >= 0 ? draw::Cursor::Hand : draw::Cursor::Arrow; }

std::wstring HtmlView::tip(draw::Point p) {
    const int i = linkAt(p);
    return i >= 0 ? wide(links_[size_t(i)].url) : std::wstring();
}

int HtmlView::linkAt(draw::Point p) const {
    for (size_t i = 0; i < links_.size(); ++i)
        if (links_[i].box.contains(p)) return int(i);
    return -1;
}

std::vector<Link> HtmlView::findLinks() const {
    std::vector<Link> out;
    const Impl& m = *impl_;
    if (!m.doc || !m.doc->root()) return out;
    // An inline element's box: its own, or the box around its children's (the words of a link are boxes of their own).
    std::function<litehtml::position(const litehtml::element::ptr&)> placement = [&placement](const litehtml::element::ptr& el) {
        litehtml::position p = el->get_placement();
        if (p.width > 0 && p.height > 0) return p;
        bool any = false;
        pixel_t l = 0, t = 0, r = 0, b = 0;
        for (const litehtml::element::ptr& ch : el->children()) {
            const litehtml::position q = placement(ch);
            if (q.width <= 0 || q.height <= 0) continue;
            if (!any) l = q.x, t = q.y, r = q.right(), b = q.bottom(), any = true;
            else l = std::min(l, q.x), t = std::min(t, q.y), r = std::max(r, q.right()), b = std::max(b, q.bottom());
        }
        return any ? litehtml::position(l, t, r - l, b - t) : litehtml::position();
    };
    for (const litehtml::element::ptr& a : m.doc->root()->select_all("a[href]")) {
        if (a->css().get_display() == litehtml::display_none) continue;
        const litehtml::position p = placement(a);
        if (p.width <= 0 || p.height <= 0) continue;  // (not shown: GitHub's heading anchors)
        const char* href = a->get_attr("href", "");
        out.push_back({m.rect(p), href[0] == '#' ? std::string(href) : resolveUrl(m.base, href)});
    }
    return out;
}

std::string HtmlView::title() const { return impl_->caption; }

int HtmlView::anchorY(const std::string& name) const {
    const Impl& m = *impl_;
    if (!m.doc || !m.doc->root() || name.empty()) return -1;
    for (const std::string& sel : {"[id=\"" + name + "\"]", "[name=\"" + name + "\"]", "[id=\"user-content-" + name + "\"]"}) {
        if (const litehtml::element::ptr el = m.doc->root()->select_one(sel)) {
            const litehtml::position p = el->get_placement();
            return int(std::lround(p.y * m.s));
        }
    }
    return -1;
}

int HtmlView::imagesAsked() const { return int(impl_->images.size()); }
int HtmlView::imagesShown() const {
    int n = 0;
    for (const auto& [url, img] : impl_->images) n += img.state == Impl::Img::Ready;
    return n;
}

}  // namespace stardraft::html
