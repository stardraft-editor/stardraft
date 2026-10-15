// Text between the library's UTF-8 and the UI's wide strings (apps/common/text_codec.h: the same as Win32 gave).

// UTF-8 when the bytes are valid UTF-8, else Windows-1252 (map strings are often in it).
std::wstring widen(const std::string& s) { return stardraft::text::widen(s); }

std::string narrow(const std::wstring& w) { return stardraft::text::narrow(w); }

// A .dat field or section name as the UI shows it, in sentence case: "Target Acquisition Range" -> "Target acquisition
// range", "Cost & availability" -> "Cost and availability". The schema names stay as they are (code and tests use
// them). Abbreviations (AI, GRP, ID), names with capitals inside (StarEdit) and game names keep their capitals.
std::wstring datLabel(const std::string& name, bool start = true) {
    static const std::set<std::string> keep = {"Brood", "War", "Broodwar", "Staredit", "Iscript", "Terran", "Zerg", "Protoss"};
    std::string out, word;
    bool first = start;
    auto flush = [&] {
        if (word.empty()) return;
        bool plain = std::isupper(static_cast<unsigned char>(word[0])) != 0 && word.size() > 1;  // "Range", "Lift-Off"
        for (size_t i = 1; i < word.size() && plain; ++i)
            plain = std::islower(static_cast<unsigned char>(word[i])) || word[i] == '-' || word[i - 1] == '-';
        if (!first && plain && !keep.count(word))
            for (char& c : word) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (word == "&") word = "and";
        if (word == "Resarch") word = first ? "Research" : "research";
        out += word, word.clear(), first = false;
    };
    for (char c : name) {
        if (c == ' ') flush(), out += ' ';
        else word += c;
    }
    flush();
    return widen(out);
}

// A game text in one line without its codes (a block's sentence, a list): the line ends as a mark.
std::wstring gameTextPlain(const std::string& bytes) {
    std::string t;
    for (const char ch : bytes)
        if (static_cast<unsigned char>(ch) >= 0x20 || ch == '\t') t += ch;
        else if (ch == '\n') t += '\x01';
    std::wstring w = widen(t), out;
    for (const wchar_t c : w) out += c == 1 ? std::wstring(L"  \x21B5  ") : std::wstring(1, c);
    return out;
}
