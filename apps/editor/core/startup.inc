// The start-up marks (portable, core.inc): moved from main.cpp. Each start-up step records when it was done, in ms since
// the process was created (shellProcessStart, the host's clock), for the perf: output. After the start-up (gStartUp
// false) later opens add no marks.

double gOpenMs = 0;  // how long opening the file given on the command line took (perf:)
// Start-up steps and when each was done (ms since the process was created), for perf:.
std::vector<std::pair<const char*, double>> gStartMarks;
bool gStartUp = true;  // false after the start-up: later opens add no marks
void startMark(const char* what) {
    if (!gStartUp) return;
    // t0: the process creation (its age from the precise system clock), so the time before wWinMain is in the marks.
    static const auto t0 = shellProcessStart();
    gStartMarks.push_back({what, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count()});
}
