#include "stardraft/help.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>

namespace stardraft {

const char* const kHelpOnline = "https://stardraft-editor.github.io/stardraft/help/";

namespace {

std::string joined(const char* const* parts) {
    std::string s;
    for (; parts && *parts; ++parts) s += *parts;
    return s;
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The words of a query: runs of letters and digits (and of the bytes of letters that are not ASCII), lower case.
std::vector<std::string> words(const std::string& q) {
    std::vector<std::string> out;
    std::string w;
    for (const char ch : q) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (std::isalnum(c) || c >= 0x80) w += char(std::tolower(c));
        else if (!w.empty()) out.push_back(w), w.clear();
    }
    if (!w.empty()) out.push_back(w);
    return out;
}

bool hasAll(const std::string& text, const std::vector<std::string>& ws) {
    for (const std::string& w : ws)
        if (text.find(w) == std::string::npos) return false;
    return true;
}

}  // namespace

const std::vector<HelpPage>& helpPages() {
    static const std::vector<HelpPage> pages = [] {
        std::vector<HelpPage> out;
        for (int i = 0; i < help_data::kPageCount; ++i) {
            const help_data::Page& d = help_data::kPages[i];
            HelpPage p;
            p.name = d.name, p.title = d.title, p.html = joined(d.html);
            for (int k = 0; k < d.sectionCount; ++k) {
                const help_data::Section& s = d.sections[k];
                p.sections.push_back({s.id, s.title, joined(s.text), s.level});
            }
            out.push_back(std::move(p));
        }
        return out;
    }();
    return pages;
}

const HelpPage* helpPage(const std::string& name) {
    for (const HelpPage& p : helpPages())
        if (p.name == name) return &p;
    return nullptr;
}

std::string helpOnlineUrl(const std::string& page, const std::string& anchor) {
    return std::string(kHelpOnline) + page + ".html" + (anchor.empty() ? "" : "#" + anchor);
}

std::string helpAnchor(const std::string& text) {
    std::string out;
    bool dash = false;
    for (const char ch : text) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (std::isalnum(c)) {
            if (dash && !out.empty()) out += '-';
            out += char(std::tolower(c)), dash = false;
        } else {
            dash = true;
        }
    }
    return out;
}

HelpTopic helpSplitTopic(const std::string& topic) {
    const size_t hash = topic.find('#');
    if (hash == std::string::npos) return {topic, ""};
    return {topic.substr(0, hash), topic.substr(hash + 1)};
}

HelpLink helpLinkTarget(const std::string& href) {
    HelpLink l;
    std::string rest = href;
    const std::string online = kHelpOnline;
    if (rest.rfind(online, 0) == 0) rest = rest.substr(online.size());
    else if (rest.find("://") != std::string::npos || rest.rfind("mailto:", 0) == 0) return l.url = href, l;
    const size_t hash = rest.find('#');
    std::string file = rest.substr(0, hash), anchor = hash == std::string::npos ? "" : rest.substr(hash + 1);
    if (const size_t slash = file.find_last_of('/'); slash != std::string::npos) file = file.substr(slash + 1);
    for (const char* ext : {".html", ".md"})
        if (file.size() > std::string(ext).size() && file.compare(file.size() - std::string(ext).size(), std::string::npos, ext) == 0)
            file.resize(file.size() - std::string(ext).size());
    if (file.empty()) file = "index";
    if (!helpPage(file)) return l.url = href, l;  // (not one of the pages: the browser)
    l.topic = {file, anchor};
    return l;
}

HelpTopic helpFieldTopic(const std::string& dat, const std::string& field) {
    const HelpPage* p = helpPage(dat + "-dat");
    if (!p) return {};
    std::string name = field;
    if (const size_t typo = name.find("Resarch"); typo != std::string::npos) name.replace(typo, 7, "Research");  // (techdata's)
    const std::string id = helpAnchor(name);
    for (const HelpSection& s : p->sections)
        if (s.id == id) return {p->name, id};
    return {p->name, ""};
}

std::vector<HelpHit> helpSearch(const std::string& query, size_t max) {
    const std::vector<std::string> ws = words(query);
    std::vector<HelpHit> first, then;
    if (ws.empty()) return first;
    for (const HelpPage& p : helpPages()) {
        const std::string pageTitle = lower(p.title);
        for (const HelpSection& s : p.sections) {
            const std::string title = lower(s.title);
            const bool inTitle = hasAll(title, ws) || (s.level == 1 && hasAll(pageTitle, ws));
            if (!inTitle && !hasAll(title + " " + lower(s.text) + " " + pageTitle, ws)) continue;
            HelpHit h{p.name, s.level == 1 ? "" : s.id, s.title, p.title};
            (inTitle ? first : then).push_back(std::move(h));
        }
    }
    first.insert(first.end(), then.begin(), then.end());
    if (first.size() > max) first.resize(max);
    return first;
}

}  // namespace stardraft
