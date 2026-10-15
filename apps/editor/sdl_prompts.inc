// The questions at start and from Help on this system, and the hover tooltips: the crash question (core/prompts.inc's
// text, after a crash, with Options > Crash reports: Ask), Help > Check for updates (the newest release on GitHub;
// the texts and the reading are core/prompts.inc's) and the views' tips (core/views_tip.inc: HoverTipper, told by the
// host where the mouse is). The questions are the core's message box (msgbox::ask), with the texts as the Windows
// program gives them to its box. Included by main_sdl.cpp after sdl_data.inc.
//
// Not on this system yet: writing a crash report (no crash handler here: the question comes for reports that another
// build left), and installing a release (no release build of this program: Check for updates opens the release page).

#if __has_include("build_info.h")
#include "build_info.h"  // STARDRAFT_COMMIT, when the build writes it
#endif
#ifndef STARDRAFT_COMMIT
#define STARDRAFT_COMMIT "unknown"
#endif

// The hover tooltips of every window (SdlHost::setHoverHook calls it).
HoverTipper gHoverTips;
void sdlHover(draw::View& root, draw::Point pt, const std::wstring& tip, bool reset) {
    if (reset) gHoverTips.reset(root);
    else gHoverTips.moved(root, pt, tip);
}

namespace sdlprompts {

// The token that may open issues: none is built into this program; the environment variable gives one to test with.
std::string token() {
    const char* e = std::getenv("STARDRAFT_ISSUES_TOKEN");
    return e ? std::string(e) : std::string();
}

// Where the crash reports are: the program's data folder (Linux: ~/.local/share/StarDraft/crashes).
fs::path crashFolder() {
    char* p = SDL_GetPrefPath(nullptr, "StarDraft");
    fs::path f = p ? fs::path(p) / "crashes" : fs::path();
    SDL_free(p);
    return f;
}

// A GET to GitHub (the token to api.github.com only; a redirect is followed); the body goes into `body`.
bool get(const std::string& url, std::string* body) {
    stardraft::platform::HttpRequest q;
    q.url = url, q.accept = "application/vnd.github+json", q.token = token();
    const stardraft::platform::HttpReply r = stardraft::platform::httpRequest(q, body);
    return r.status == 200 && r.complete;
}

}  // namespace sdlprompts

// At start, with Options > Crash reports: Ask: the reports of the last crash: send them, always send, or not.
void sdlAskToSendCrashReports() {
    int mode = 0;
    prefs::number(L"CrashReports", mode, 0, 2);
    const std::string token = sdlprompts::token();
    if (mode != 0 || token.empty() || !gRoot) return;
    const auto reports = stardraft::platform::unsentCrashReports(sdlprompts::crashFolder());
    if (reports.empty()) return;
    const int r = msgbox::ask(*gRoot, kCrashAskTitle, crashAskText(reports.front().parent_path().wstring()), crashAskButtons(),
                              msgbox::Icon::Question);
    if (r == 1) prefs::setNumber(L"CrashReports", 1);  // Always send: from now on, without the question
    if (r == 0 || r == 1) {
        std::thread([reports, token] {
            for (const auto& d : reports) stardraft::platform::sendCrashReport(d, narrow(kRepoName), token);
        }).detach();
    } else {
        for (const auto& d : reports) stardraft::platform::markCrashReport(d, "not sent: the user said no");
    }
}

// Help > Check for updates: the newest release; a newer one opens its page (this program is not a release build).
void sdlCheckUpdates() {
    if (!gRoot) return;
    std::string j;
    updcore::Release r;
    if (!sdlprompts::get("https://api.github.com/repos/" + narrow(kRepoName) + "/releases/latest", &j) ||
        !updcore::parseRelease(j, stardraft::platform::releaseAssetSuffix(), r)) {
        msgbox::ask(*gRoot, updcore::kTitle, updcore::kNoReply, {L"OK"}, msgbox::Icon::Warning);
        return;
    }
    if (!updcore::newer(r, STARDRAFT_COMMIT)) {
        msgbox::ask(*gRoot, updcore::kTitle, updcore::upToDate(widen(r.title)), {L"OK"}, msgbox::Icon::Info);
        return;
    }
    if (msgbox::ask(*gRoot, updcore::kTitle, updcore::notReleaseBuild(widen(r.title)), {L"&Open the release page", L"Cancel"},
                    msgbox::Icon::Info) == 0)
        shellOpenUrl(widen(r.page));
}
