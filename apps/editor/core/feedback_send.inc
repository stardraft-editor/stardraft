// Feedback's files and sending (portable, core.inc; moved from feedback.inc): the dialog's Send writes the feedback to
// a file in the feedback folder (shellFeedbackFolder), then files it on GitHub as an issue (label "feedback") with the
// token that may open issues (the same as the crash reports'). Feedback that could not go waits there and goes at the
// next start. The request is apps/common's (WinHTTP on Windows, curl elsewhere). Both programs' dialogs call
// feedbackSend; the Windows program's menu item and window are feedback.inc and feedback_window.inc.

// (The includer includes <mutex> and apps/common's http.h before the core: main.cpp, main_sdl.cpp, core_check.cpp.)

#if __has_include("build_info.h")
#include "build_info.h"  // STARDRAFT_COMMIT, written on each build of the Windows program (cmake/build_info.cmake)
#endif
#ifndef STARDRAFT_VERSION_NAME
#define STARDRAFT_VERSION_NAME "dev"
#endif
#ifndef STARDRAFT_COMMIT
#define STARDRAFT_COMMIT "unknown"
#endif
#ifndef STARDRAFT_ISSUES_TOKEN
#define STARDRAFT_ISSUES_TOKEN ""
#endif

// The repository that gets the feedback and the crash reports.
constexpr const wchar_t* kIssueRepo = L"stardraft-editor/stardraft";

// A token that may only open issues on the repository. A build made here has none; the environment variable
// STARDRAFT_ISSUES_TOKEN gives one to test with.
std::string issueToken() {
    static const std::string t = [] {
        std::string v = STARDRAFT_ISSUES_TOKEN;
        if (v.empty())
            if (const char* e = std::getenv("STARDRAFT_ISSUES_TOKEN")) v = e;
        return v;
    }();
    return t;
}

// A JSON string of UTF-8 text.
std::string jsonText(const std::string& s) {
    std::string o = "\"";
    for (unsigned char ch : s) {
        if (ch == '"' || ch == '\\') o += '\\', o += char(ch);
        else if (ch == '\n') o += "\\n";
        else if (ch == '\r') continue;
        else if (ch == '\t') o += "\\t";
        else if (ch < 0x20) continue;
        else o += char(ch);
    }
    return o + "\"";
}

// The first line of a text, without access keys and the end's punctuation, at most 48 characters (a title).
std::wstring feedbackShort(std::wstring t) {
    if (const size_t n = t.find_first_of(L"\r\n\t"); n != std::wstring::npos) t.resize(n);
    std::erase(t, L'&');
    while (!t.empty() && (t.back() == L' ' || t.back() == L':' || t.back() == L'.')) t.pop_back();
    if (t.size() > 48) t = t.substr(0, 47) + L"\x2026";
    return t;
}

// The workspace's name in a report (the crash reports' and the feedback's "Workspace:" line), in English.
const wchar_t* reportWorkspaceName(Workspace w) {
    static const wchar_t* const names[] = {L"Map",      L"Data",       L"Triggers", L"Assets",  L"Project",          L"Briefing",
                                           L"Assets",   L"Properties", L"Players",  L"Classic triggers", L"Map data"};
    const int i = int(w);
    return i >= 0 && i < int(sizeof names / sizeof names[0]) ? names[i] : L"Map";
}

std::mutex gSentMutex;  // sent.tsv: written by the thread that sends, read by the window
std::filesystem::path feedbackFolder() { return shellFeedbackFolder(); }

// The feedback sent from this computer (sent.tsv), the newest first: the issue number, its page, the title.
struct SentFeedback {
    long number;
    std::wstring page, title;
};
std::vector<SentFeedback> sentFeedback() {
    std::lock_guard<std::mutex> lock(gSentMutex);
    std::vector<SentFeedback> out;
    std::ifstream in(feedbackFolder() / "sent.tsv", std::ios::binary);
    for (std::string line; std::getline(in, line);) {
        const size_t a = line.find('\t'), b = a == std::string::npos ? a : line.find('\t', a + 1);
        if (b == std::string::npos) continue;
        out.push_back({std::strtol(line.c_str(), nullptr, 10), widen(line.substr(a + 1, b - a - 1)), widen(line.substr(b + 1))});
    }
    std::reverse(out.begin(), out.end());
    return out;
}

// Sends one waiting feedback file (line 1: the title; then the body); deleted when it arrived.
bool sendFeedbackFile(const std::filesystem::path& file) {
    if (issueToken().empty()) return false;
    std::ifstream in(file, std::ios::binary);
    std::string title, body;
    std::getline(in, title);
    body.assign(std::istreambuf_iterator<char>(in), {});
    in.close();
    std::string reply;
    stardraft::platform::HttpRequest q;
    q.url = "https://api.github.com/repos/" + narrow(kIssueRepo) + "/issues", q.method = "POST", q.accept = "application/vnd.github+json";
    q.token = issueToken();
    q.body = "{\"title\":" + jsonText(title) + ",\"body\":" + jsonText(body) + ",\"labels\":[\"feedback\"]}";
    const stardraft::platform::HttpReply r = stardraft::platform::httpRequest(q, &reply);
    // A reply that was not read completely, or has no link, keeps the file: the next start sends it again (a possible
    // second issue is better than a lost note).
    if (r.status != 201 || !r.complete) return false;
    // The issue's number and page (the first "html_url" of the reply is the issue's), kept in sent.tsv: one line each,
    // "number<TAB>page<TAB>title", so the feedback window can list them.
    long number = 0;
    std::string page;
    if (const size_t a = reply.find("\"number\":"); a != std::string::npos) number = std::strtol(reply.c_str() + a + 9, nullptr, 10);
    if (const size_t a = reply.find("\"html_url\":\""); a != std::string::npos) page = reply.substr(a + 12, reply.find('"', a + 12) - a - 12);
    if (number <= 0 || page.empty()) return false;
    {
        std::lock_guard<std::mutex> lock(gSentMutex);
        std::ofstream(feedbackFolder() / "sent.tsv", std::ios::binary | std::ios::app) << number << '\t' << page << '\t' << title << '\n';
    }
    std::error_code ec;
    std::filesystem::remove(file, ec);
    return true;
}

// A note that waits to go: a .txt file. The list of the sent ones is not one (sent.tsv; sent.txt in the builds of
// 2026-10-11, which sent that list as an issue).
bool waitingFeedbackFile(const std::filesystem::path& p) { return p.extension() == ".txt" && p.filename() != "sent.txt"; }

// At start, on a thread: feedback that could not go before.
void sendPendingFeedback() {
    if (issueToken().empty()) return;
    std::thread([] {
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(feedbackFolder(), ec))
            if (waitingFeedbackFile(e.path())) sendFeedbackFile(e.path());
    }).detach();
}

// Writes the feedback to a file (it waits there if it cannot go now); returns the file. `where`: the workspace;
// `build`: "built <date>"; `system`: the system and its version.
std::filesystem::path feedbackWriteFile(const std::wstring& on, const std::wstring& typed, const std::wstring& where, const std::string& build,
                                        const std::string& system) {
    const auto now = std::chrono::system_clock::now();
    const std::time_t tt = std::chrono::system_clock::to_time_t(now);
    const int ms = int(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
    std::tm t{};
    if (const std::tm* lt = std::localtime(&tt)) t = *lt;
    char stamp[40];
    std::snprintf(stamp, sizeof stamp, "%04d%02d%02d-%02d%02d%02d-%03d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, ms);
    std::error_code ec;
    std::filesystem::create_directories(feedbackFolder(), ec);
    const std::filesystem::path file = feedbackFolder() / (std::string(stamp) + ".txt");
    std::string body = narrow(typed);
    std::erase(body, '\r');
    const std::string summary = narrow(feedbackShort(typed));
    std::ofstream(file, std::ios::binary) << "Feedback on " << narrow(on) << ": " << summary << "\n"
                                          << body << "\n\n---\nOn: " << narrow(on) << "\nWorkspace: " << narrow(where) << "\nStarDraft " STARDRAFT_VERSION_NAME ", "
                                          << build << ", commit " STARDRAFT_COMMIT "\n" << system << "\n";
    return file;
}

// After a feedback send (on the UI thread): sent, or kept for the next start.
void feedbackSentResult(bool sent) {
    // A build made here has no token: it cannot send, so say that and where the feedback is.
    if (sent) {  // its issue: the number in the status bar, the page in the log (Help > Report feedback > Your feedback lists them)
        const auto list = sentFeedback();
        if (!list.empty()) {
            setMessage(L"Sent the feedback as #" + std::to_wstring(list[0].number) + L". Thank you.");
            shellLogLine(L"Feedback #" + std::to_wstring(list[0].number) + L": " + list[0].page);
        } else {
            setMessage(L"Sent the feedback. Thank you.");
        }
    } else {
        setMessage(issueToken().empty() ? L"This build cannot send feedback. The feedback is in " + feedbackFolder().wstring()
                                        : L"StarDraft 2 cannot send the feedback now. It sends it at the next start.");
    }
    if (sent) sendPendingFeedback();  // GitHub can be reached now: feedback that waits goes too
}

// The dialog's Send: `file` (feedbackWriteFile's) goes on a thread; the dialog shows the spinner, then the result.
// `stillOpen` (on the UI thread, after the send) gives the dialog that sent it while it is open, else null: the result
// then goes only to the status bar.
void feedbackSend(FeedbackView& v, const std::filesystem::path& file, std::function<FeedbackView*()> stillOpen) {
    // A build made here has no token: it cannot send, so the note says where the feedback is.
    if (issueToken().empty()) {
        v.sending(L"");
        v.done(false, 0, L"");
        v.showStatus(L"This build cannot send feedback. StarDraft 2 keeps it in " + file.wstring() + L".");
        return;
    }
    v.sending(L"Sending your feedback\x2026");
    setMessage(L"Sending the feedback\x2026");
    std::thread([file, stillOpen] {
        const bool sent = sendFeedbackFile(file);
        long number = 0;
        std::wstring page;
        if (sent)
            if (const auto list = sentFeedback(); !list.empty()) {
                number = list[0].number;
                page = L"https://github.com/" + std::wstring(kIssueRepo) + L"/issues/" + std::to_wstring(number);
            }
        shellPost([sent, number, page, stillOpen] {  // on the UI thread: the dialog (when it is still open), then the result
            if (FeedbackView* open = stillOpen()) open->done(sent, number, page);
            feedbackSentResult(sent);
        });
    }).detach();
}
