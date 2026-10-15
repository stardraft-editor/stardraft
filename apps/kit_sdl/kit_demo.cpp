// kit_demo: the control kit's widgets and the Canvas's drawing, in SDL windows (apps/kit_sdl). A window with the
// Buttons, and a sheet of the canvas's features: text flags, wrapped text, fonts, clipped views, bitmaps, icons, lines
// and shapes. Not shown yet: ListView, TreeView, ScrollView, Dialog, MessageBox and Form.
//
// The last section is a texture the program draws each frame: a moving color pattern, uploaded in the host's frame hook
// (SdlHost::setFrameHook) and shown with Canvas::externalImage.
//
// usage: kit_demo [--snapshot <out.png>] [--size <w>x<h>] [--scale <s>]
//
// --snapshot: renders one frame without a window (SDL_VIDEODRIVER=offscreen needs no display), writes it, exits.
// Without it: a window that stays open until it is closed; it animates (SdlHost::setAnimating).

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <clocale>
#include <cstdlib>
#include <string>

#include "canvas_imgui.h"
#include "imgui.h"
#include "sdl_host.h"
#include "stardraft/renderer.h"
#include "stardraft/terrain.h"
#include "button.h"
#include "theme.h"
#include "view.h"

namespace {

using namespace stardraft;
namespace draw = stardraft::draw;
namespace kit = stardraft::kit;
namespace kit_sdl = stardraft::kit_sdl;
using draw::Canvas;
using draw::Color;
using draw::Rect;
using draw::rgb;

// A view that draws past its frame: the canvas clips what is outside it.
class Clipped final : public draw::View {
public:
    void paint(Canvas& c) override {
        const kit::Theme& th = kit::theme();
        c.fill(Rect(0, 0, width(), height()), th.chip);
        c.fill(Rect(-40, -20, width() + 60, height() / 2), rgb(200, 60, 60));
        draw::FontDesc big;
        big.px = kit::px(34);
        const draw::Font old = c.setFont(c.font(big));
        c.text(Rect(kit::px(8), 0, width() * 4, height()), L"Clipped at the frame", th.onText, draw::kLeft | draw::kMiddle);
        c.setFont(old);
    }
};

// A view that shows a texture the program draws itself, once per frame, in the host's frame hook.
class Pattern final : public draw::View {
public:
    explicit Pattern(SDL_GPUDevice* device) : device_(device) {}
    ~Pattern() override {
        if (tex_) SDL_ReleaseGPUTexture(device_, tex_);
        if (tb_) SDL_ReleaseGPUTransferBuffer(device_, tb_);
    }
    Pattern(const Pattern&) = delete;
    Pattern& operator=(const Pattern&) = delete;

    // The frame hook: the pattern of the next frame, copied into the texture with `cmd` (the view's own size).
    void drawFrame(SDL_GPUCommandBuffer* cmd) {
        const int w = std::max(1, width()), h = std::max(1, height());
        if (!tex_ || w != w_ || h != h_) resize(w, h);
        uint8_t* px = static_cast<uint8_t*>(SDL_MapGPUTransferBuffer(device_, tb_, true));
        if (!px) return;
        const float t = float(frame_++);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                uint8_t* p = px + (size_t(y) * size_t(w) + size_t(x)) * 4;
                p[0] = uint8_t(x * 255 / w);  // red: across
                p[1] = uint8_t(y * 255 / h);  // green: down
                p[2] = uint8_t(128 + 127 * std::sin((float(x + y) + t * 4) * 0.05f));  // blue: a wave that moves
                p[3] = 255;
            }
        SDL_UnmapGPUTransferBuffer(device_, tb_);
        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureTransferInfo src{};
        src.transfer_buffer = tb_;
        SDL_GPUTextureRegion dst{};
        dst.texture = tex_, dst.w = uint32_t(w), dst.h = uint32_t(h), dst.d = 1;
        SDL_UploadToGPUTexture(copy, &src, &dst, true);
        SDL_EndGPUCopyPass(copy);
    }

    void paint(Canvas& c) override {
        if (tex_) c.externalImage(tex_, Rect(0, 0, width(), height()));
    }

private:
    void resize(int w, int h) {
        SDL_WaitForGPUIdle(device_);  // (the old texture may still be in use)
        if (tex_) SDL_ReleaseGPUTexture(device_, tex_);
        if (tb_) SDL_ReleaseGPUTransferBuffer(device_, tb_);
        SDL_GPUTextureCreateInfo ti{};
        ti.type = SDL_GPU_TEXTURETYPE_2D;
        ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        ti.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        ti.width = uint32_t(w), ti.height = uint32_t(h), ti.layer_count_or_depth = 1, ti.num_levels = 1;
        tex_ = SDL_CreateGPUTexture(device_, &ti);
        SDL_GPUTransferBufferCreateInfo bi{};
        bi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        bi.size = uint32_t(w * h * 4);
        tb_ = SDL_CreateGPUTransferBuffer(device_, &bi);
        w_ = w, h_ = h;
    }

    SDL_GPUDevice* device_;
    SDL_GPUTexture* tex_ = nullptr;
    SDL_GPUTransferBuffer* tb_ = nullptr;
    int w_ = 0, h_ = 0;
    uint32_t frame_ = 0;
};

// Where the sections of the sheet are (pixels, from the top), so the layout and the paint agree.
struct Geometry {
    int m, buttonsY, boxesLabelY, boxesY, boxesH, wrapLabelY, wrapY, wrapH, fontsLabelY, fontsY, rowLabelY, rowY, linesLabelY, linesY;
    int gpuLabelY, gpuY;
};

Geometry geometry() {
    using kit::px;
    Geometry g{};
    g.m = px(16);
    g.buttonsY = g.m;
    g.boxesLabelY = g.buttonsY + px(kit::kButtonH) + px(20);
    g.boxesY = g.boxesLabelY + px(22);
    g.boxesH = px(40) * 2 + px(8);
    g.wrapLabelY = g.boxesY + g.boxesH + px(20);
    g.wrapY = g.wrapLabelY + px(22);
    g.wrapH = px(72);
    g.fontsLabelY = g.wrapY + g.wrapH + px(20);
    g.fontsY = g.fontsLabelY + px(22);
    g.rowLabelY = g.fontsY + px(36) + px(20);
    g.rowY = g.rowLabelY + px(22);
    g.linesLabelY = g.rowY + px(110) + px(20);
    g.linesY = g.linesLabelY + px(22);
    g.gpuLabelY = g.linesY + px(60);
    g.gpuY = g.gpuLabelY + px(22);
    return g;
}

class Demo final : public draw::View {
public:
    explicit Demo(SDL_GPUDevice* device)
        : save(L"Save", kit::Tone::Primary, [] { std::printf("kit_demo: Save\n"); }),
          cancel(L"Cancel", kit::Tone::Normal, [] { std::printf("kit_demo: Cancel\n"); }),
          del(L"Delete", kit::Tone::Danger, [] { std::printf("kit_demo: Delete\n"); }),
          quiet(L"More", kit::Tone::Quiet, [] { std::printf("kit_demo: More\n"); }),
          pattern(device) {
        // A checker with a gradient, premultiplied: the Bitmap's own pixels.
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x) {
                const uint32_t a = (x < 16) ? 255u : 128u;
                const uint32_t r = uint32_t(x * 8) * a / 255, g = uint32_t(y * 8) * a / 255;
                const uint32_t b = ((x / 4 + y / 4) & 1 ? 220u : 60u) * a / 255;
                bmp.pixels()[y * 32 + x] = r | (g << 8) | (b << 16) | (a << 24);
            }
        bmp.changed();
        add(save, Rect(0, 0, 1, 1)), add(cancel, Rect(0, 0, 1, 1)), add(del, Rect(0, 0, 1, 1)), add(quiet, Rect(0, 0, 1, 1));
        add(clipped, Rect(0, 0, 1, 1));
        add(pattern, Rect(0, 0, 1, 1));
    }

    // The frame hook: the program's own texture for this frame.
    void drawFrame(SDL_GPUCommandBuffer* cmd) { pattern.drawFrame(cmd); }

    void resized() override { layout(); }

    void layout() {
        using kit::px;
        const Geometry g = geometry();
        const int bw = px(112), bh = px(kit::kButtonH), gap = px(kit::kButtonGap);
        int x = g.m;
        for (kit::Button* b : {&save, &cancel, &del, &quiet}) {
            b->setFrame(Rect(x, g.buttonsY, x + bw, g.buttonsY + bh));
            x += bw + gap;
        }
        clipped.setFrame(Rect(g.m, g.rowY, g.m + px(200), g.rowY + px(56)));
        pattern.setFrame(Rect(g.m, g.gpuY, g.m + px(320), g.gpuY + px(180)));
    }

    void paint(Canvas& c) override {
        using kit::px;
        const kit::Theme& th = kit::theme();
        const Geometry g = geometry();
        const int m = g.m, w = width();
        auto label = [&](int y, const wchar_t* text) { c.text(Rect(m, y, w - m, y + px(18)), text, th.dim, draw::kLeft | draw::kMiddle); };
        auto well = [&](Rect r) {  // a box to look at a text in
            c.roundBox(r, th.ctl, th.border, float(px(kit::kRadius)));
        };

        // Text flags: each box shows one combination.
        label(g.boxesLabelY, L"Text flags: left, center, right, middle, bottom, ellipsis, path ellipsis");
        const int cellW = (w - 2 * m - px(16)) / 3, cellH = px(40), gap = px(8);
        struct Cell {
            const wchar_t* text;
            unsigned flags;
        };
        const Cell cells[] = {
            {L"Left, middle", draw::kLeft | draw::kMiddle},
            {L"Centered", draw::kCenter | draw::kMiddle},
            {L"Right", draw::kRight | draw::kMiddle},
            {L"Bottom", draw::kCenter | draw::kBottom},
            {L"A very long text that does not fit the box", draw::kLeft | draw::kMiddle | draw::kEllipsis},
            {L"C:/Users/example/Documents/maps/long/path/match.scx", draw::kLeft | draw::kMiddle | draw::kPathEllipsis},
        };
        for (int i = 0; i < 6; ++i) {
            const int col = i % 3, row = i / 3;
            const Rect r(m + col * (cellW + px(8)), g.boxesY + row * (cellH + gap), m + col * (cellW + px(8)) + cellW,
                         g.boxesY + row * (cellH + gap) + cellH);
            well(r);
            c.text(Rect(r.left + px(6), r.top, r.right - px(6), r.bottom), cells[i].text, th.text, cells[i].flags);
        }

        // Wrapped text, Korean and Chinese included.
        label(g.wrapLabelY, L"Wrapped text in a box (kWrap, with a line break and CJK text)");
        const Rect wr(m, g.wrapY, w - m, g.wrapY + g.wrapH);
        well(wr);
        c.text(Rect(wr.left + px(8), wr.top + px(6), wr.right - px(8), wr.bottom - px(6)),
               L"Words break at spaces to fit the box. 한국어 문장도 글자 단위로 줄이 바뀝니다.\n"
               L"A second paragraph, after a line break. 中文文本也会换行显示。",
               th.text, draw::kLeft | draw::kWrap);

        // Fonts: an underlined line and the monospace face.
        label(g.fontsLabelY, L"Fonts: underline, monospace, sizes");
        draw::FontDesc under;
        under.px = px(15), under.underline = true;
        draw::Font old = c.setFont(c.font(under));
        c.text(Rect(m, g.fontsY, m + px(260), g.fontsY + px(24)), L"Underlined UI text", th.accent, draw::kLeft | draw::kMiddle);
        draw::FontDesc mono;
        mono.face = draw::Face::Mono, mono.px = px(14);
        c.setFont(c.font(mono));
        c.text(Rect(m + px(280), g.fontsY, m + px(560), g.fontsY + px(24)), L"Mono 0123456789 {}", th.text, draw::kLeft | draw::kMiddle);
        draw::FontDesc big;
        big.px = px(22);
        c.setFont(c.font(big));
        c.text(Rect(m + px(580), g.fontsY, w - m, g.fontsY + px(36)), L"Size 22", th.text, draw::kLeft | draw::kMiddle);
        c.setFont(old);

        // The clipped view (a child), bitmaps (nearest, smooth), icons.
        label(g.rowLabelY, L"Clipped view (child), bitmaps (nearest, smooth), icons (16, 24 px)");
        if (std::getenv("KIT_NO_BITMAP")) return;
        const int bx = m + px(220);
        c.bitmap(bmp, Rect(0, 0, 32, 32), Rect(bx, g.rowY, bx + 96, g.rowY + 96), false);
        c.bitmap(bmp, Rect(0, 0, 32, 32), Rect(bx + px(110), g.rowY, bx + px(110) + 96, g.rowY + 96), true, 200);
        const int ix = bx + px(230);
        for (int i = 0; i < 12; ++i) c.icon(Rect(ix + (i % 6) * px(24), g.rowY + (i / 6) * px(24), ix + (i % 6) * px(24) + px(24), g.rowY + (i / 6) * px(24) + px(24)), i, th.accent, px(16));
        for (int i = 12; i < 18; ++i) c.icon(Rect(ix + (i - 12) * px(32), g.rowY + px(56), ix + (i - 12) * px(32) + px(32), g.rowY + px(88)), i, th.text, px(24));

        // Lines and shapes: solid, dashed, dotted, wide, outlines, rings, a concave polygon.
        label(g.linesLabelY, L"Lines and shapes");
        const int ly = g.linesY;
        c.line(m, ly + px(4), m + px(120), ly + px(4), th.text, 1);
        c.line(m, ly + px(14), m + px(120), ly + px(14), th.text, 1, draw::LineStyle::Dash);
        c.line(m, ly + px(24), m + px(120), ly + px(24), th.text, 1, draw::LineStyle::Dot);
        c.line(m, ly + px(36), m + px(120), ly + px(36), th.accent, px(3), draw::LineStyle::Dash);
        const draw::Point zig[] = {{m + px(140), ly + px(40)}, {m + px(170), ly}, {m + px(200), ly + px(40)}, {m + px(230), ly}};
        c.polyline(zig, 4, th.warn, 2);
        c.frame(Rect(m + px(250), ly, m + px(330), ly + px(40)), th.good, 1, draw::LineStyle::Dash);
        c.ring(Rect(m + px(350), ly, m + px(390), ly + px(40)), th.accent, 2);
        c.ring(Rect(m + px(400), ly, m + px(440), ly + px(40)), th.bad, 1, draw::LineStyle::Dash);
        c.ellipse(Rect(m + px(450), ly, m + px(490), ly + px(40)), th.sel, th.accent);
        c.roundBox(Rect(m + px(500), ly, m + px(580), ly + px(40)), th.sel, th.accent, float(px(10)), 2);
        const draw::Point arrow[] = {{m + px(600), ly + px(20)}, {m + px(620), ly}, {m + px(620), ly + px(10)}, {m + px(650), ly + px(10)},
                                     {m + px(650), ly + px(30)}, {m + px(620), ly + px(30)}, {m + px(620), ly + px(40)}};
        c.polygon(arrow, 7, th.good, th.text);

        // The program's texture (Pattern, a child view that paints it with externalImage).
        label(g.gpuLabelY, L"A texture the program draws each frame (SdlHost::setFrameHook, Canvas::externalImage)");
    }

private:
    kit::Button save, cancel, del, quiet;
    Clipped clipped;
    draw::Bitmap bmp{32, 32};
    Pattern pattern;
};

// The snapshot's pixels, written as PNG.
bool writeSnapshot(SDL_GPUDevice* device, SDL_GPUTexture* tex, int w, int h, const std::string& out) {
    SDL_GPUTransferBufferCreateInfo ti{};
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    ti.size = uint32_t(w * h * 4);
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(device, &ti);
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion src{};
    src.texture = tex, src.w = uint32_t(w), src.h = uint32_t(h), src.d = 1;
    SDL_GPUTextureTransferInfo dst{};
    dst.transfer_buffer = tb;
    SDL_DownloadFromGPUTexture(copy, &src, &dst);
    SDL_EndGPUCopyPass(copy);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_WaitForGPUFences(device, true, &fence, 1);
    SDL_ReleaseGPUFence(device, fence);
    Image img;
    img.width = w, img.height = h;
    const uint8_t* p = static_cast<const uint8_t*>(SDL_MapGPUTransferBuffer(device, tb, false));
    img.rgba.assign(p, p + size_t(w) * size_t(h) * 4);
    SDL_UnmapGPUTransferBuffer(device, tb);
    SDL_ReleaseGPUTransferBuffer(device, tb);
    for (size_t i = 3; i < img.rgba.size(); i += 4) img.rgba[i] = 255;
    return writePng(out.c_str(), img);
}

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::string snapshot;
    int width = 1000, height = 720;
    float scaleArg = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&] { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--snapshot") snapshot = next();
        else if (a == "--size") std::sscanf(next().c_str(), "%dx%d", &width, &height);
        else if (a == "--scale") scaleArg = float(std::atof(next().c_str()));
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) return std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()), 1;

    // The device first (the renderer makes it with its shader formats), then the host on it.
    stardraft::render::Renderer renderer;
    std::string err;
    if (!renderer.createDevice(err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    SDL_GPUDevice* device = renderer.device();

    float scale = scaleArg > 0 ? scaleArg : SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    if (scale <= 0) scale = 1;
    // The fonts and icon atlases beside the program (the build copies them there), as stardraft-next's.
    const char* basePath = SDL_GetBasePath();
    const std::string base = basePath ? basePath : "";
    {
        // Order matters: the host (which makes the ImGui context the fonts go into) outlives the demo's views.
        kit_sdl::SdlHost host(device);
        host.setScale(scaleArg);  // (0: the display's scale; the views lay out at it)
        std::printf("kit_demo: fonts %s, scale %.2f\n", kit_sdl::loadFonts(base + "fonts/").c_str(), double(scale));
        if (!kit_sdl::loadIconAtlases(device, base + "res/")) std::printf("kit_demo: no icon atlases in res/\n");
        Demo demo(device);
        host.setFrameHook([&demo](SDL_GPUCommandBuffer* cmd) { demo.drawFrame(cmd); });
        if (!snapshot.empty()) {
            SDL_GPUTextureCreateInfo ti{};
            ti.type = SDL_GPU_TEXTURETYPE_2D;
            ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
            ti.width = uint32_t(width), ti.height = uint32_t(height), ti.layer_count_or_depth = 1, ti.num_levels = 1;
            SDL_GPUTexture* tex = SDL_CreateGPUTexture(device, &ti);
            if (!tex) return std::fprintf(stderr, "snapshot texture: %s\n", SDL_GetError()), 1;
            // Two frames: the first fills the font atlas with the glyphs the text needs; the second is the picture.
            host.snapshot(demo, tex, width, height, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
            host.snapshot(demo, tex, width, height, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
            const bool ok = writeSnapshot(device, tex, width, height, snapshot);
            std::printf("%s %s\n", ok ? "wrote" : "could not write", snapshot.c_str());
            SDL_WaitForGPUIdle(device);
            SDL_ReleaseGPUTexture(device, tex);
        } else {
            if (!host.show(demo, L"StarDraft kit demo", width, height)) return std::fprintf(stderr, "could not open a window\n"), 1;
            host.setAnimating(true);  // the pattern moves: frames keep coming
            host.run();
        }
    }
    kit_sdl::unloadIconAtlases();
    renderer.shutdown();
    SDL_Quit();
    return 0;
}
