#pragma once

// The help pages (docs/help/*.md): built into the program at build time by tools/help_build.py (the HTML of each page,
// its sections for the search, and its links), so that they show with no network. The same pages are on GitHub Pages
// (kHelpOnline). The program shows them in its help window (apps/editor/core/views_help.inc).
//
// A page's name is its file's name with no extension ("units-dat"); a place in a page is its name and the id of a
// heading ("units-dat#hit-points"): a topic.

#include <cstddef>
#include <string>
#include <vector>

namespace stardraft {

// Where the pages are on the web: a page is kHelpOnline + name + ".html".
extern const char* const kHelpOnline;

// A part of a page: a heading (its id and text, level 1 to 3) and the text under it up to the next heading (plain
// text, for the search).
struct HelpSection {
    std::string id, title, text;
    int level = 0;
};

struct HelpPage {
    std::string name, title, html;  // html: the page's body (no <html> or <head>)
    std::vector<HelpSection> sections;
};

// The pages: the index ("index") first, then the pages in the order of the index's links. Empty when the build had no
// Python (the help is then only on the web).
const std::vector<HelpPage>& helpPages();
// The pages of the web's pages.json (kHelpOnline + "pages.json", made by tools/help_build.py --site), read into `out`;
// false when the text is not such a file (`out` stays). helpLoadPages: they become helpPages() (the program's thread
// that shows the help: a HelpPage that was read before is gone).
bool helpPagesFromJson(const std::string& text, std::vector<HelpPage>& out);
bool helpLoadPages(const std::string& text);
const HelpPage* helpPage(const std::string& name);
// The page's online address (with "#anchor" when it is not "").
std::string helpOnlineUrl(const std::string& page, const std::string& anchor = "");

// The id of a heading with this text, as tools/help_build.py makes it: lower case, each run of other characters than
// letters and digits one "-", none at the start and the end ("Rank/Sublabel" -> "rank-sublabel").
std::string helpAnchor(const std::string& text);

// A topic split into its page and anchor ("units-dat#hit-points" -> "units-dat", "hit-points").
struct HelpTopic {
    std::string page, anchor;
    bool empty() const { return page.empty(); }
};
HelpTopic helpSplitTopic(const std::string& topic);

// Where a link of a help page goes: another help page (`page` and `anchor`; the link is relative, or the page's online
// address), or a place outside (`url`, for the browser).
struct HelpLink {
    HelpTopic topic;
    std::string url;
};
HelpLink helpLinkTarget(const std::string& href);

// The topic of a .dat field (its name in the schema: "units", "Hit Points") when its page has it; else the .dat's page
// when there is one; else empty.
HelpTopic helpFieldTopic(const std::string& dat, const std::string& field);

// The search: the sections whose heading or text has each word of `query` (no case; at least one letter or digit), the
// ones with the words in their heading or page title first, then in the order of the pages. At most `max`.
struct HelpHit {
    std::string page, anchor;  // anchor "": the page's top
    std::string title;         // the heading (or the page's title)
    std::string pageTitle;
};
std::vector<HelpHit> helpSearch(const std::string& query, size_t max = 50);

// The generated pages (tools/help_build.py --cpp): one per page, its strings in parts (a compiler's limit on the length
// of one string). Read through helpPages().
namespace help_data {
struct Section {
    const char* id;
    const char* title;
    int level;
    const char* const* text;
};
struct Page {
    const char* name;
    const char* title;
    const char* const* html;
    const Section* sections;
    int sectionCount;
};
extern const Page kPages[];
extern const int kPageCount;
extern const long long kPagesStamp;  // the pages' time (tools/help_build.py: pages_stamp)
}  // namespace help_data

}  // namespace stardraft
