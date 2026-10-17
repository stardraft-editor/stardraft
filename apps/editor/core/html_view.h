#pragma once
// A view that shows a static HTML page (HTML and CSS, no JavaScript): text, headings, lists, tables, code blocks,
// links and images, laid out by litehtml (https://github.com/litehtml/litehtml) and drawn through the program's Canvas
// (apps/common/canvas.h) with the program's fonts. Any window can use it: the release notes (views_changelog.inc), and
// later the help pages.
//
// The view is as high as its laid-out page: put it in a kit::ScrollView (apps/kit/scroll_view.h) for the wheel, the
// trackpad, the keys and the bar. Its width sets the layout; each width change lays the page out again.
//
// The look: the program's theme (kit::theme(): its colors and the UI and code fonts), as a user stylesheet that maps
// GitHub's markdown HTML (headings, code, tables, quotes) to them (themeCss). A link click goes to onLink with the
// link's full URL; a link to a part of the page ("#name") scrolls to it there (onScrollTo).
//
// Images: a file path or file:// URL is read at once; an http(s) URL goes to the hooks' fetch on a thread (the program
// gives one that downloads with apps/common/http.h and keeps a copy in its cache folder), and the page is laid out
// again when it comes. PNG, JPEG, GIF (its first frame) and BMP; other images (SVG) are not shown.
//
// Text selection: not done (litehtml has no selection model; a page's text is not copied).
//
// Coordinates: litehtml lays the page out in CSS pixels; the view draws them at the kit's display scale.

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "canvas.h"
#include "view.h"

namespace stardraft::html {

// What the view gets from the program (set once, before the first page).
struct Hooks {
    // A canvas that measures text outside a paint (the fonts are the paint's). Null: the page is laid out at its first paint.
    std::function<draw::Canvas*()> measure;
    // The bytes at an http(s) URL; runs on a thread of its own. False: none (the image is not shown).
    std::function<bool(const std::string& url, std::string& bytes)> fetch;
    // Runs `f` on the UI thread soon (an image that came).
    std::function<void(std::function<void()> f)> post;
};
Hooks& hooks();

// A link of the laid-out page: its box (the view's pixels) and its full URL.
struct Link {
    draw::Rect box;
    std::string url;
};

class HtmlView : public draw::View {
public:
    HtmlView();
    ~HtmlView() override;

    // The page (UTF-8 HTML) and the URL its relative links and images are relative to (a web address, or a file path).
    void setHtml(const std::string& html, const std::string& baseUrl);
    const std::string& baseUrl() const { return base_; }
    // The stylesheet after litehtml's own and the theme's (themeCss): a window's own rules.
    void setExtraCss(const std::string& css);
    // The theme changed: the page takes its colors and fonts again.
    void themeChanged();

    // The theme's stylesheet for GitHub's markdown HTML (kit::theme()'s colors; the UI font, code in the code font).
    static std::string themeCss();

    // A link was clicked: its full URL ("#name" links scroll in the page and do not come here).
    std::function<void(const std::string& url)> onLink;
    // The page asks to show the point `y` (the view's pixels) at the top: a "#name" link.
    std::function<void(int y)> onScrollTo;

    // Lays the page out for `width` pixels (the view's width when it is 0) and gives its height in pixels. The view
    // takes that height.
    int layout(int width = 0);
    int contentHeight() const { return height_; }
    // The links of the last layout, in the order of the page.
    const std::vector<Link>& links() const { return links_; }
    // The page's <title> ("" when it has none), and where the element with id or name `name` is (-1: none).
    std::string title() const;
    int anchorY(const std::string& name) const;
    // The images the page asked for and the ones that are shown (loaded and decoded).
    int imagesAsked() const;
    int imagesShown() const;

    void paint(draw::Canvas& c) override;
    void resized() override;
    bool mouse(const draw::MouseEvent& e) override;
    draw::Cursor cursor(draw::Point) override;
    std::wstring tip(draw::Point p) override;
    bool hoverTip() const override { return true; }

    struct Impl;

private:
    std::vector<Link> findLinks() const;
    int linkAt(draw::Point p) const;  // the link at `p` (-1: none)
    std::shared_ptr<Impl> impl_;
    std::vector<Link> links_;  // the last layout's
    int hot_ = -1;             // the link under the mouse
    int pressed_ = -1;         // the link a press is on
    std::map<int, int> heights_;  // the page's height at each width it was laid out at (since the page or its images changed)  // (shared: an image that comes on a thread finds it only while the view is there)
    std::string base_, extraCss_;
    int height_ = 0;
    int laidOutWidth_ = -1;
    unsigned long long themeKey_ = 0;  // the theme the page has (its text and accent colors and the scale): a change makes the page again
    bool laying_ = false;
};

}  // namespace stardraft::html
