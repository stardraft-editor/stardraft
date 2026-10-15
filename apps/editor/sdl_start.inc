// The start page on this system (core/views_start.inc: StartView). It shows in the Map workspace while no map is open. Its
// recent lists are the core's (gRecentMaps, gRecentProjects, saved in the settings); a recent map opens as on the File
// menu; the cards start the New page, which is not on this system yet. Included by main_sdl.cpp after sdl_menus.inc.

#include <ctime>
#include <cwchar>

StartView gStartView;  // (declared extern in sdl_main_view.inc: the root view shows it)

// The day a file (or a project's folder: its project file) was last saved, as the start page shows it; "" when not found.
// The file clock is turned into the system clock by its offset to now (std::chrono::clock_cast is not on every system yet).
std::wstring startDateSdl(const fs::path& p) {
    std::error_code ec;
    fs::path f = p;
    if (fs::is_directory(p, ec) && fs::exists(p / EudProject::kFileName, ec)) f = p / EudProject::kFileName;
    const fs::file_time_type ft = fs::last_write_time(f, ec);
    if (ec) return L"";
    const auto offset = std::chrono::duration_cast<std::chrono::system_clock::duration>(ft - fs::file_time_type::clock::now());
    const std::time_t tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now() + offset);
    std::tm tm{};
    if (!localtime_r(&tt, &tm)) return L"";
    wchar_t buf[32];
    std::swprintf(buf, 32, L"%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return buf;
}

// Makes the page's callbacks (the host's part of StartView). Called once, when the root view is made.
void initStartPage() {
    gStartView.onCard = [](int kind) { openNewPageSdl(kind); };
    gStartView.onOpenRecent = [](bool project, size_t i) {
        if (project) setMessage(L"Projects are not on this system yet");
        else command(ID_RECENT_MAP + int(i));
    };
    gStartView.onRemoveRecent = [](bool project, size_t i) {
        auto& list = project ? gRecentProjects : gRecentMaps;
        if (i >= list.size()) return;
        const std::wstring name = fs::path(list[i]).filename().wstring();
        list.erase(list.begin() + long(i));
        saveRecent(), refreshRecentMenus();
        setMessage(tr(L"Removed ") + name + tr(L" from the list"));
        gStartView.invalidate();
        gMenusDirty = true;
    };
    gStartView.onOpen = [] { command(ID_FILE_OPEN); };
    gStartView.dateOf = [](const fs::path& p) { return startDateSdl(p); };
    gStartView.scrollInfo = [](int, int, int) {};  // (the page scrolls with the wheel; the window has no bar yet)
}
