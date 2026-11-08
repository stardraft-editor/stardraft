#pragma once
// For the import tests: streams in .NET's BinaryFormatter format ([MS-NRBF]) from a small tree of values, with EUD
// Editor 3's class and member names (nrbf.h reads them).

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

// --- A small BinaryFormatter writer: classes with typed members, arrays, strings, references ([MS-NRBF]) --------------

struct N {
    enum T { I32, I64, Bool, U8, Str, Null, Obj, Arr, Bytes, Bools, Ref } t = Null;
    long long i = 0;
    std::string s;  // Str; Obj: the class; Ref: the label
    std::vector<std::pair<std::string, N>> members;  // Obj
    std::vector<N> items;                            // Arr
    std::vector<uint8_t> bytes;                      // Bytes, Bools
    std::string label;                               // Obj: the name a Ref uses
};
inline N i32(long long v) { N n; n.t = N::I32, n.i = v; return n; }
inline N i64(long long v) { N n; n.t = N::I64, n.i = v; return n; }
inline N boolean(bool v) { N n; n.t = N::Bool, n.i = v; return n; }
inline N str(std::string v) { N n; n.t = N::Str, n.s = std::move(v); return n; }
inline N null() { return N{}; }
inline N obj(std::string cls, std::vector<std::pair<std::string, N>> m, std::string label = {}) {
    N n;
    n.t = N::Obj, n.s = "EUD_Editor_3." + cls, n.members = std::move(m), n.label = std::move(label);
    return n;
}
inline N arr(std::vector<N> items) { N n; n.t = N::Arr, n.items = std::move(items); return n; }
inline N bytes(std::vector<uint8_t> b) { N n; n.t = N::Bytes, n.bytes = std::move(b); return n; }
inline N bools(std::vector<uint8_t> b) { N n; n.t = N::Bools, n.bytes = std::move(b); return n; }
inline N ref(std::string label) { N n; n.t = N::Ref, n.s = std::move(label); return n; }
inline N list(std::vector<N> items) {
    const int n = int(items.size());
    N l = obj("", {{"_items", arr(std::move(items))}, {"_size", i32(n)}, {"_version", i32(1)}});
    l.s = "System.Collections.Generic.List`1[[X]]";
    return l;
}
inline N enumOf(long long v) { return obj("SomeEnum", {{"value__", i32(v)}}); }

struct Writer {
    std::string out;
    int next = 2;  // (1 is the root)
    std::map<std::string, int> labels;
    void u8(uint8_t b) { out += char(b); }
    void i32w(int32_t v) {
        for (int k = 0; k < 4; ++k) out += char(uint32_t(v) >> (8 * k));
    }
    void strw(const std::string& s) {
        size_t n = s.size();
        do {
            uint8_t b = n & 0x7F;
            n >>= 7;
            if (n) b |= 0x80;
            u8(b);
        } while (n);
        out += s;
    }
    void prim(const N& n) {
        if (n.t == N::I32) i32w(int32_t(n.i));
        else if (n.t == N::I64) i32w(int32_t(n.i)), i32w(int32_t(n.i >> 32));
        else u8(uint8_t(n.i));
    }
    static bool isPrim(const N& n) { return n.t == N::I32 || n.t == N::I64 || n.t == N::Bool || n.t == N::U8; }
    void record(const N& n, int id = 0) {
        switch (n.t) {
        case N::Null: u8(10); return;
        case N::Ref: u8(9), i32w(labels.at(n.s)); return;
        case N::Str: u8(6), i32w(next++), strw(n.s); return;
        case N::Bytes: case N::Bools:
            u8(15), i32w(next++), i32w(int32_t(n.bytes.size())), u8(n.t == N::Bytes ? 2 : 1);
            for (uint8_t b : n.bytes) u8(b);
            return;
        case N::Arr:
            u8(16), i32w(next++), i32w(int32_t(n.items.size()));
            for (const N& e : n.items) record(e);
            return;
        case N::Obj: {
            const int me = id ? id : next++;
            if (!n.label.empty()) labels[n.label] = me;
            u8(5), i32w(me), strw(n.s), i32w(int32_t(n.members.size()));
            for (const auto& m : n.members) strw(m.first);
            for (const auto& m : n.members) u8(isPrim(m.second) ? 0 : m.second.t == N::Str ? 1 : 2);
            for (const auto& m : n.members)
                if (isPrim(m.second)) u8(m.second.t == N::I32 ? 8 : m.second.t == N::I64 ? 9 : m.second.t == N::Bool ? 1 : 2);
            i32w(2);  // the library
            for (const auto& m : n.members) isPrim(m.second) ? prim(m.second) : record(m.second);
            return;
        }
        default: prim(n);
        }
    }
    std::string stream(const N& root) {
        out.clear();
        u8(0), i32w(1), i32w(-1), i32w(1), i32w(0);
        u8(12), i32w(2), strw("EUD Editor 3, Version=0.19.6.0, Culture=neutral, PublicKeyToken=null");
        record(root, 1);
        u8(11);
        return out;
    }
};

