// The release notes window (portable, core.inc): a release's notes as its GitHub release page has them (GitHub's
// body_html in the HTML view, html_view.h), with Install (or Download) and Open on GitHub. The top bar's Update opens
// it for the newer release that the update check found; Help > What is new opens it for this version. The host
// (sdl_updates.inc) gets the release from GitHub on a thread, or the copy that the cache folder kept when GitHub cannot
// be reached, and runs Install and the links.
//
// A link to another release page of the repository opens that release's notes in the window (onRelease); a link to
// another place opens in the browser (onOpenUrl).

namespace changelog {

// The HTML view's hooks: the measuring canvas, the UI thread, and the images' download, kept in the cache folder
// (images/, by a hash of their address) so that a page shows its images again without the network.
inline uint64_t urlHash(const std::string& s) {
    uint64_t h = 1469598103934665603ull;  // (FNV-1a)
    for (const unsigned char ch : s) h = (h ^ ch) * 1099511628211ull;
    return h;
}
// `network` false (a test run): only what is on disk.
inline void setHtmlHooks(bool network) {
    stardraft::html::Hooks& h = stardraft::html::hooks();
    if (h.post) return;
    h.measure = [] { return shellMeasureCanvas(); };
    h.post = [](std::function<void()> f) { shellPost(std::move(f)); };
    const fs::path cache = shellCacheFolder();
    h.fetch = [cache, network](const std::string& url, std::string& bytes) {
        char name[32];
        std::snprintf(name, sizeof name, "%016llx", (unsigned long long)urlHash(url));
        const fs::path kept = cache.empty() ? fs::path() : cache / "images" / name;
        if (!kept.empty())
            if (std::ifstream in(kept, std::ios::binary); in) {
                bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
                if (!bytes.empty()) return true;
            }
        if (!network) return false;
        if (!releases::get(url, &bytes, nullptr, "image/*")) return false;
        if (!kept.empty()) {  // (into a .part file, then renamed: a program that stops while it writes leaves no cut image)
            std::error_code ec;
            fs::create_directories(kept.parent_path(), ec);
            fs::path part = kept;
            part += ".part";
            bool written = false;
            if (std::ofstream out(part, std::ios::binary); out) written = bool(out.write(bytes.data(), std::streamsize(bytes.size())).flush());
            if (written) fs::rename(part, kept, ec);
            if (!written || ec) fs::remove(part, ec);
        }
        return true;
    };
}

}  // namespace changelog

class ChangelogView final : public draw::View {
public:
    // What the window shows: GitHub's answer is coming, a release (from GitHub, or the copy kept from before), or
    // nothing (GitHub cannot be reached and no copy is kept).
    enum class State { Loading, Shown, Kept, NotReached };

    std::function<void()> onInstall;                         // Install or Download
    std::function<void(const std::string& url)> onOpenUrl;   // the browser
    std::function<void(const std::string& tag)> onRelease;   // a link to another release page of the repository
    std::function<void()> closed;                             // the view closed its window (Close, Esc)

    ChangelogView() {
        html_.onLink = [this](const std::string& url) {
            const std::string tag = releases::releasePageTag(url);
            if (!tag.empty() && onRelease) onRelease(tag);
            else if (onOpenUrl) onOpenUrl(url);
        };
        html_.onScrollTo = [this](int y) { scroll_.setOffset(0, y); };
        // (the notes have the window's padding inside the scroll view: the content is the HTML view, as wide as it)
        html_.setExtraCss("body { padding: " + std::to_string(stardraft::kit::kDialogPad) + "px " + std::to_string(stardraft::kit::kDialogPad + 4) + "px; }");
        scroll_.setContent(&html_);
        add(scroll_, {});
        install_.setTone(stardraft::kit::Tone::Primary);
        install_.onClick = [this] {
            if (onInstall) onInstall();
        };
        github_.setText(L"Open on GitHub");
        github_.setTip(L"Open the release page in the browser");
        github_.onClick = [this] {
            if (onOpenUrl && !page_.empty()) onOpenUrl(page_);
        };
        close_.setText(L"Close");
        close_.onClick = [this] { close(); };
        for (auto* b : {&install_, &github_, &close_}) add(*b, {});
    }

    // The window's size at 100 %.
    static draw::Point size() { return {scale(760), scale(640)}; }

    // GitHub's answer for the release at `page` (its release page) is coming.
    void setLoading(const std::string& page) {
        state_ = State::Loading, page_ = page, release_ = {};
        install_.setText({});
        html_.setHtml({}, page);
        relayout();
    }
    // The release (`kept`: the copy from the cache folder, GitHub cannot be reached). `install`: the Install button's
    // text ("": none), `installTip` its tooltip; `note`: a line under the title (a Flatpak: its package manager updates it).
    void setRelease(const releases::Release& r, bool kept, const std::wstring& install, const std::wstring& installTip, const std::wstring& note = {}) {
        state_ = kept ? State::Kept : State::Shown, release_ = r, page_ = r.page, note_ = note;
        install_.setText(install), install_.setTip(installTip);
        const std::string html = r.notes.empty() ? "<p>" + narrow(tr(L"This release has no release notes")) + "</p>" : r.notes;
        html_.setHtml(html, r.page);
        scroll_.setOffset(0, 0);
        relayout();
    }
    // GitHub cannot be reached and no copy is kept: a line, and Open on GitHub (`page`: the release page, or the releases).
    void setNotReached(const std::string& page) {
        state_ = State::NotReached, page_ = page, release_ = {};
        install_.setText({});
        html_.setHtml({}, page);
        relayout();
    }

    State state() const { return state_; }
    const releases::Release& release() const { return release_; }
    const std::string& page() const { return page_; }
    stardraft::html::HtmlView& html() { return html_; }
    stardraft::kit::ScrollView& scroller() { return scroll_; }
    // The buttons that show now, in their order ("Install", "Open on GitHub", "Close").
    std::vector<std::wstring> buttons() const {
        std::vector<std::wstring> out;
        for (const auto* b : {&install_, &github_, &close_})
            if (b->visible()) out.push_back(b->text());
        return out;
    }
    // The line under the title (the date, Preview, or why the notes are the kept copy).
    std::wstring statusLine() const {
        switch (state_) {
        case State::Loading: return tr(L"Getting the release notes from GitHub\x2026");
        case State::NotReached: return tr(L"Could not get the release notes. GitHub cannot be reached.");
        default: break;
        }
        std::wstring s;
        if (release_.published.size() >= 10) s = std::wstring(tr(L"Published")) + L" " + widen(release_.published.substr(0, 10));
        if (release_.prerelease) s += std::wstring(s.empty() ? L"" : L" \x00B7 ") + tr(L"Pre-release");
        if (state_ == State::Kept) s += std::wstring(s.empty() ? L"" : L" \x00B7 ") + tr(L"GitHub cannot be reached: this is the copy from before");
        if (!note_.empty()) s += std::wstring(s.empty() ? L"" : L" \x00B7 ") + note_;
        return s;
    }

    void paint(draw::Canvas& c) override {
        const int w = width(), h = height(), pad = scale(stardraft::kit::kDialogPad);
        c.fill({0, 0, w, h}, kPanel);
        draw::FontDesc td;
        td.px = -shellUiFontLfHeight() * 3 / 2, td.weight = 600;
        const draw::Font of = c.setFont(c.font(td));
        const std::wstring title = state_ == State::Shown || state_ == State::Kept ? widen(release_.title) : std::wstring(L"StarDraft 2");
        c.text({pad, pad, w - pad, pad + scale(30)}, title, kText, draw::kMiddle | draw::kEllipsis);
        c.setFont(c.font({}));
        const std::wstring line = statusLine();
        const bool warn = state_ == State::Kept || state_ == State::NotReached;
        c.text({pad, pad + scale(30), w - pad, pad + scale(52)}, line, warn ? kWarn : kDim, draw::kMiddle | draw::kEllipsis);
        c.setFont(of);
        const draw::Rect sr = scroll_.frame();
        c.fill({0, sr.top - 1, w, sr.top}, kBorder);
        c.fill({0, sr.bottom, w, sr.bottom + 1}, kBorder);
        c.fill(sr, kBg);
        if (state_ == State::NotReached || state_ == State::Loading) {
            const draw::Font of2 = c.setFont(c.font({}));
            const std::wstring t = state_ == State::Loading ? std::wstring() : tr(L"Open the release page on GitHub to read the release notes.");
            c.text({sr.left + pad, sr.top + pad, sr.right - pad, sr.top + pad + scale(24)}, t, kDim, draw::kMiddle);
            c.setFont(of2);
        }
    }
    void resized() override { relayout(); }

    bool key(const draw::KeyEvent& e) override {
        if (!e.down || (e.mods & draw::kAlt)) return false;
        if (e.key == draw::Key::Escape) return close(), true;
        return scroll_.key(e);  // (the arrows and the page keys scroll the notes)
    }
    // Closes the window the view is in.
    void close() {
        if (host()) host()->closeWindow(*this);
        if (closed) closed();
    }

private:
    // The notes between the title and the buttons; the buttons at the bottom right (Install first).
    void relayout() {
        const int w = width(), h = height();
        if (w <= 0 || h <= 0) return;
        const int pad = scale(stardraft::kit::kDialogPad), bh = scale(stardraft::kit::kButtonH), gap = scale(stardraft::kit::kButtonGap);
        const int top = pad + scale(60), bottom = h - pad - bh - pad;
        scroll_.setFrame({0, top, w, std::max(top, bottom)});
        draw::Canvas* m = shellMeasureCanvas();
        auto bw = [&](const stardraft::kit::Button& b) {
            return m ? b.preferredWidth(*m) : std::max(scale(stardraft::kit::kButtonMinW), int(b.text().size()) * scale(8) + scale(32));
        };
        const int y = h - pad - bh;
        int x = w - pad;
        install_.setVisible(!install_.text().empty() && (state_ == State::Shown || state_ == State::Kept));
        github_.setVisible(!page_.empty());
        for (stardraft::kit::Button* b : {&close_, &github_, &install_}) {
            if (!b->visible()) continue;
            const int bwid = bw(*b);
            b->setFrame({x - bwid, y, x, y + bh});
            x -= bwid + gap;
        }
        invalidate();
    }

    State state_ = State::Loading;
    releases::Release release_;
    std::string page_;
    std::wstring note_;
    stardraft::kit::ScrollView scroll_;
    stardraft::html::HtmlView html_;
    stardraft::kit::Button install_, github_, close_;
};
