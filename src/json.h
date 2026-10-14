#pragma once
// A small JSON reader and writer (objects, arrays, strings, numbers, booleans), shared by the library's
// project files (triggers.json, the Data workspace's strings, requirements and button sets).
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace stardraft {

// --- A small JSON reader (objects, arrays, strings, numbers, booleans) -------------------------------------

struct Json {
    enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> items;
    std::vector<std::pair<std::string, Json>> fields;
    const Json* get(const std::string& k) const {
        for (const auto& [key, v] : fields)
            if (key == k) return &v;
        return nullptr;
    }
    std::string str(const std::string& k, const std::string& def = {}) const {
        const Json* v = get(k);
        return v && v->type == Type::String ? v->s : def;
    }
    bool boolean(const std::string& k, bool def) const {
        const Json* v = get(k);
        return v && v->type == Type::Bool ? v->b : def;
    }
    int integer(const std::string& k, int def) const {
        const Json* v = get(k);
        return v && v->type == Type::Number ? int(v->n) : def;
    }
};

inline std::string quote(const std::string& s);
inline std::string jsonText(const Json& j) {
    switch (j.type) {
    case Json::Type::Null: return "null";
    case Json::Type::Bool: return j.b ? "true" : "false";
    case Json::Type::Number: {
        char b[40];
        if (j.n == double(static_cast<long long>(j.n))) std::snprintf(b, sizeof b, "%lld", static_cast<long long>(j.n));
        else std::snprintf(b, sizeof b, "%.17g", j.n);
        return b;
    }
    case Json::Type::String: return quote(j.s);
    case Json::Type::Array: {
        std::string o = "[";
        for (size_t i = 0; i < j.items.size(); ++i) o += (i ? ", " : "") + jsonText(j.items[i]);
        return o + "]";
    }
    case Json::Type::Object: {
        std::string o = "{";
        for (size_t i = 0; i < j.fields.size(); ++i) o += (i ? ", " : "") + quote(j.fields[i].first) + ": " + jsonText(j.fields[i].second);
        return o + "}";
    }
    }
    return "null";
}
// The fields of an object not in `known`, as ", \"k\": v" text.
inline std::string extraFields(const Json& j, std::initializer_list<const char*> known) {
    std::string o;
    for (const auto& [k, v] : j.fields)
        if (std::none_of(known.begin(), known.end(), [&](const char* n) { return k == n; })) o += ", " + quote(k) + ": " + jsonText(v);
    return o;
}

struct JsonParser {
    const std::string& t;
    size_t i = 0;
    bool ok = true;
    void ws() {
        while (i < t.size() && (t[i] == ' ' || t[i] == '\n' || t[i] == '\r' || t[i] == '\t')) ++i;
    }
    bool eat(char ch) {
        ws();
        if (i < t.size() && t[i] == ch) return ++i, true;
        return false;
    }
    std::string string() {
        std::string out;
        if (!eat('"')) return ok = false, out;
        while (i < t.size() && t[i] != '"') {
            char ch = t[i++];
            if (ch != '\\' || i >= t.size()) {
                out += ch;
                continue;
            }
            ch = t[i++];
            switch (ch) {
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned cp = 0;
                if (i + 4 > t.size() || std::from_chars(t.data() + i, t.data() + i + 4, cp, 16).ptr != t.data() + i + 4) return ok = false, out;  // a bad \u escape
                i += 4;
                if (cp < 0x80) out += char(cp);
                else if (cp < 0x800) out += char(0xC0 | cp >> 6), out += char(0x80 | (cp & 0x3F));
                else out += char(0xE0 | cp >> 12), out += char(0x80 | (cp >> 6 & 0x3F)), out += char(0x80 | (cp & 0x3F));
                break;
            }
            default: out += ch;
            }
        }
        ++i;
        return out;
    }
    Json value() {
        Json v;
        ws();
        if (i >= t.size()) return ok = false, v;
        if (t[i] == '{') {
            ++i;
            v.type = Json::Type::Object;
            if (eat('}')) return v;
            do {
                std::string k = string();
                if (!eat(':')) return ok = false, v;
                v.fields.push_back({k, value()});
            } while (ok && eat(','));
            if (!eat('}')) ok = false;
        } else if (t[i] == '[') {
            ++i;
            v.type = Json::Type::Array;
            if (eat(']')) return v;
            do v.items.push_back(value());
            while (ok && eat(','));
            if (!eat(']')) ok = false;
        } else if (t[i] == '"') {
            v.type = Json::Type::String, v.s = string();
        } else if (t.compare(i, 4, "true") == 0) {
            v.type = Json::Type::Bool, v.b = true, i += 4;
        } else if (t.compare(i, 5, "false") == 0) {
            v.type = Json::Type::Bool, i += 5;
        } else if (t.compare(i, 4, "null") == 0) {
            i += 4;
        } else {
            size_t end = i;
            while (end < t.size() && (isdigit(static_cast<unsigned char>(t[end])) || t[end] == '-' || t[end] == '.' || t[end] == 'e' || t[end] == 'E' || t[end] == '+')) ++end;
            if (end == i) return ok = false, v;
            // strtod and not from_chars: Apple's libc++ has no from_chars for double before macOS 26. The library
            // never calls setlocale, so the decimal point is '.'.
            if (t[i] == '+') return ok = false, v;  // (from_chars and JSON do not take it; strtod does)
            const std::string number = t.substr(i, end - i);
            char* stop = nullptr;
            errno = 0;
            v.n = std::strtod(number.c_str(), &stop);
            if (errno == ERANGE || stop != number.c_str() + number.size()) return ok = false, v;  // not a number, or too large
            v.type = Json::Type::Number, i = end;
        }
        return v;
    }
};

inline std::string quote(const std::string& s) {
    std::string out = "\"";
    for (unsigned char ch : s) {
        switch (ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (ch < 0x20) {
                char b[8];
                std::snprintf(b, sizeof b, "\\u%04x", ch);
                out += b;
            } else {
                out += char(ch);
            }
        }
    }
    return out + "\"";
}

}  // namespace stardraft
