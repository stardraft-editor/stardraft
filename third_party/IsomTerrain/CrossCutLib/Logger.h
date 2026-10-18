#pragma once
// StarDraft's stand-in for Chkdraft's CrossCutLib/Logger.h (IsomTerrain's IsomApi.h includes it): a logger that
// writes nothing. IsomApi.h only logs from its archive loading, which StarDraft does not use.
#include <string>

enum class LogLevel { None, Error, Warn, Info, Debug };

class Logger {
public:
    struct Sink {
        template <typename T>
        const Sink& operator<<(const T&) const { return *this; }
        const Sink& operator<<(std::ostream& (*)(std::ostream&)) const { return *this; }
    };
    Logger() = default;
    explicit Logger(LogLevel) {}
    Sink error() const { return {}; }
    Sink warn() const { return {}; }
    Sink info() const { return {}; }
    Sink debug() const { return {}; }
    void error(const std::string&) const {}
    void warn(const std::string&) const {}
    void info(const std::string&) const {}
    void debug(const std::string&) const {}
};
