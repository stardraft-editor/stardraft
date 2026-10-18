// The help pages in the program (stardraft/help.h) and the units of the .dat fields (stardraft/dat_units.h): the
// pages that tools/help_build.py built in, their links, the topic of each .dat field (F1 on the Data form), the search,
// and the converted values that the Data form shows. Needs no game data.
//
// usage: help_test

#include <cstdio>
#include <string>

#include "stardraft/dat_edit.h"
#include "stardraft/dat_units.h"
#include "stardraft/help.h"

using namespace stardraft;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok   " : "FAIL ", what.c_str());
    if (!ok) ++failures;
}

bool sectionHas(const HelpPage& p, const std::string& id) {
    for (const HelpSection& s : p.sections)
        if (s.id == id) return true;
    return false;
}

}  // namespace

int main() {
    // The pages.
    const std::vector<HelpPage>& pages = helpPages();
    check(!pages.empty() && pages.front().name == "index", "the index is the first page (" + std::to_string(pages.size()) + " pages)");
    for (const char* name : {"getting-started", "values-and-units", "units-dat", "weapons-dat", "flingy-dat", "sprites-dat", "images-dat", "upgrades-dat",
                             "techdata-dat", "orders-dat"})
        check(helpPage(name) != nullptr, std::string("the page ") + name + " is there");
    for (const HelpPage& p : pages) {
        check(!p.title.empty() && p.html.find("<h1") != std::string::npos, p.name + ": a title and an <h1>");
        if (p.name != "index") check(sectionHas(p, "sources-and-credits"), p.name + ": a Sources and credits section");
    }
    if (const HelpPage* u = helpPage("units-dat")) {
        check(u->title == "Units (units.dat)", "units-dat's title: \"" + u->title + "\"");
        check(u->html.find("<h3 id=\"hit-points\">Hit points</h3>") != std::string::npos, "units-dat has the heading Hit points with its id");
    }

    // Anchors and links.
    check(helpAnchor("Rank/Sublabel") == "rank-sublabel", "helpAnchor(Rank/Sublabel)");
    check(helpAnchor("Addon Horizontal (X) Position") == "addon-horizontal-x-position", "helpAnchor(Addon Horizontal (X) Position)");
    check(helpAnchor("  Sel.Circle Image ") == "sel-circle-image", "helpAnchor(Sel.Circle Image)");
    HelpLink l = helpLinkTarget("https://stardraft-editor.github.io/stardraft/help/weapons-dat.html#damage-amount");
    check(l.topic.page == "weapons-dat" && l.topic.anchor == "damage-amount" && l.url.empty(), "an online page's link opens the page in the program");
    l = helpLinkTarget("units-dat.html");
    check(l.topic.page == "units-dat" && l.topic.anchor.empty(), "a relative link opens the page");
    l = helpLinkTarget("https://armoha.github.io/eud-book/");
    check(l.topic.empty() && l.url == "https://armoha.github.io/eud-book/", "a link to another site goes to the browser");
    l = helpLinkTarget("https://stardraft-editor.github.io/stardraft/help/no-such-page.html");
    check(l.topic.empty() && !l.url.empty(), "a link to a page that is not there goes to the browser");
    check(helpOnlineUrl("units-dat", "hit-points") == "https://stardraft-editor.github.io/stardraft/help/units-dat.html#hit-points", "the online address");
    const HelpTopic t = helpSplitTopic("units-dat#hit-points");
    check(t.page == "units-dat" && t.anchor == "hit-points", "a topic splits into its page and anchor");

    // Each field of the eight .dat files has its place on its page (F1 on the Data form).
    for (const char* dat : {"units", "weapons", "flingy", "sprites", "images", "upgrades", "techdata", "orders"}) {
        const DatSchema* s = DatSchema::find(dat);
        check(s != nullptr, std::string("the schema of ") + dat + ".dat");
        if (!s) continue;
        int missing = 0;
        for (const DatField& f : s->fields) {
            const HelpTopic ft = helpFieldTopic(dat, f.name);
            if (ft.page != std::string(dat) + "-dat" || ft.anchor.empty()) {
                ++missing;
                std::printf("      %s.dat %s: no place (\"%s#%s\")\n", dat, f.name.c_str(), ft.page.c_str(), ft.anchor.c_str());
            }
        }
        check(missing == 0, std::string(dat) + ".dat: each of its " + std::to_string(s->fields.size()) + " fields has a place on its page");
    }
    const HelpTopic research = helpFieldTopic("techdata", "Resarch Time");
    check(research.page == "techdata-dat" && research.anchor == "research-time", "techdata's Resarch Time opens #research-time");
    check(helpFieldTopic("sfxdata", "Sound File").empty(), "a .dat with no page: no topic");

    // The search.
    std::vector<HelpHit> hits = helpSearch("hit points");
    check(!hits.empty() && hits.front().page == "units-dat" && hits.front().anchor == "hit-points",
          "the search for \"hit points\" finds units.dat's Hit points first" + (hits.empty() ? std::string() : " (" + hits.front().page + "#" + hits.front().anchor + ")"));
    hits = helpSearch("COOLDOWN");
    check(!hits.empty() && hits.front().anchor == "weapon-cooldown", "the search has no case (\"COOLDOWN\")");
    check(helpSearch("  ").empty() && helpSearch("zzqqxx").empty(), "no words, or words that no page has: no hits");
    hits = helpSearch("units dat");
    check(!hits.empty() && hits.front().page == "units-dat" && hits.front().anchor.empty(), "the search for \"units dat\" finds the units.dat page first");

    // The converted values on the Data form.
    struct Case {
        const char* dat;
        const char* field;
        uint32_t raw;
        const char* want;
    };
    const Case cases[] = {
        {"units", "Hit Points", 10240, "= 40 hit points"},
        {"units", "Build Time", 360, "= 15 s (fastest)"},
        {"units", "Sight Range", 7, "= 224 pixels"},
        {"units", "Supply Required", 1, "= 0.5 supply"},
        {"units", "Supply Provided", 16, "= 8 supply"},
        {"weapons", "Maximum Range", 128, "= 4 tiles"},
        {"weapons", "Inner Splash Range", 10, "= 0.313 tiles"},
        {"weapons", "Weapon Cooldown", 15, "= 0.625 s (fastest)"},
        {"weapons", "Attack Angle", 16, "= 22.5 degrees"},
        {"flingy", "Speed", 1024, "= 96 pixels/s (fastest)"},
        {"flingy", "Halt Distance", 512, "= 2 pixels"},
        {"flingy", "Acceleration", 256, "= 576 pixels/s\xC2\xB2 (fastest)"},
        {"flingy", "Turn Radius", 1, "= 33.75 degrees/s (fastest)"},
        {"upgrades", "Research Time Base", 4000, "= 166.667 s (fastest)"},
        {"techdata", "Resarch Time", 1200, "= 50 s (fastest)"},
        {"units", "Armor", 1, ""},
        {"weapons", "Hit Points", 1, ""},
    };
    for (const Case& c : cases) {
        const std::string got = datConvertedText(c.dat, c.field, c.raw);
        check(got == c.want, std::string(c.dat) + " " + c.field + " " + std::to_string(c.raw) + ": \"" + got + "\"");
    }
    // Each field with a conversion is a field of its .dat.
    for (const char* dat : {"units", "weapons", "flingy", "upgrades", "techdata"}) {
        const DatSchema* s = DatSchema::find(dat);
        int converted = 0;
        for (const DatField& f : s ? s->fields : std::vector<DatField>{}) converted += *datFieldUnit(dat, f.name).unit != 0;
        check(converted > 0, std::string(dat) + ".dat has " + std::to_string(converted) + " fields with a converted value");
    }

    std::printf("%s\n", failures ? "help_test: FAILED" : "help_test: all passed");
    return failures ? 1 : 0;
}
