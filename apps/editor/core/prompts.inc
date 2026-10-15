// The questions both programs ask at start and from Help (portable, core.inc): the crash question (after a crash, with
// Options > Crash reports: Ask) and Check for updates (the newest release on GitHub). The texts and the reading of the
// release are here; each host asks with its message box and does the system's part: crash.inc and update.inc on
// Windows, sdl_prompts.inc on the other systems.

// --- The crash question ------------------------------------------------------------------------------------------

// The question, about the reports in `folder` (the folder of the first unsent report).
inline std::wstring crashAskText(const std::wstring& folder) {
    return L"StarDraft 2 stopped because of an error the last time. Send the crash report to the developers, so that they can "
           L"repair the error?\n\nThe report has the version, Windows, the video card, where it crashed and the last log lines. "
           L"It has no maps. It is in " + folder + L".";
}
// Its buttons, from left to right: Send (this time), Always send (from now on, without the question), Do not send.
inline const std::vector<std::wstring>& crashAskButtons() {
    static const std::vector<std::wstring> b = {L"&Send", L"&Always send", L"Do &not send"};
    return b;
}
inline constexpr const wchar_t* kCrashAskTitle = L"Crash report";

// --- Check for updates -------------------------------------------------------------------------------------------

namespace updcore {

// A string value after `"key":"` (from `from` on); "" when there is none.
inline std::string jsonString(const std::string& j, const std::string& key, size_t from = 0) {
    const std::string k = "\"" + key + "\":\"";
    const size_t a = j.find(k, from);
    if (a == std::string::npos) return {};
    const size_t b = j.find('"', a + k.size());
    return b == std::string::npos ? std::string() : j.substr(a + k.size(), b - a - k.size());
}

struct Release {
    std::string title, commit, zip, page;  // zip: the asset's API address (it downloads with the token)
};

// The release from GitHub's reply (releases/latest): its title "StarDraft <version> (build <commit>)", its page, and
// the download whose name ends in `ours` (this system's: platform::releaseAssetSuffix). False when it has no title.
inline bool parseRelease(const std::string& j, const std::string& ours, Release& r) {
    r.title = jsonString(j, "name");  // the release's own name comes before its assets' names
    r.page = jsonString(j, "html_url");
    if (const size_t a = r.title.find("(build "); a != std::string::npos) r.commit = r.title.substr(a + 7, r.title.find(')', a) - a - 7);
    for (size_t p = 0; (p = j.find("\"browser_download_url\":\"", p)) != std::string::npos; ++p) {
        const std::string u = jsonString(j, "browser_download_url", p);
        // This system's download (StarDraft-<version>-win64.zip): a release can also have downloads for other systems.
        if (u.size() < ours.size() || u.compare(u.size() - ours.size(), ours.size(), ours) != 0) continue;
        // The asset's "url" (api.github.com/.../releases/assets/<id>) comes before its browser_download_url.
        for (size_t k = j.rfind("\"url\":\"", p); k != std::string::npos; k = k ? j.rfind("\"url\":\"", k - 1) : std::string::npos) {
            const std::string a = jsonString(j, "url", k);
            if (a.find("/releases/assets/") != std::string::npos) {
                r.zip = a;
                break;
            }
        }
    }
    return !r.title.empty();
}

// The release is another build than this one (`commit`: this build's).
inline bool newer(const Release& r, const std::string& commit) { return !r.commit.empty() && r.commit != commit; }

// The texts of Check for updates.
inline constexpr const wchar_t* kTitle = L"Check for updates";
inline constexpr const wchar_t* kNoReply = L"Could not get the newest release from GitHub. Make sure that the computer is connected to the internet.";
inline std::wstring upToDate(const std::wstring& title) { return L"StarDraft 2 is up to date (" + title + L")."; }
inline std::wstring notReleaseBuild(const std::wstring& title) {
    return L"A newer release is available: " + title + L".\n\nThis is not a release build. Open the release page?";
}
inline std::wstring installQuestion(const std::wstring& title) {
    return L"A newer release is available: " + title + L".\n\nDownload and install it? StarDraft 2 starts again after it.";
}
inline std::wstring atStartNote(const std::wstring& title) { return L"A newer release is available: " + title + L". Use Help > Check for updates."; }

}  // namespace updcore
