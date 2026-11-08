#include "stardraft/nrbf.h"

#include <cctype>
#include <cstring>

namespace stardraft::nrbf {

namespace {

// [MS-NRBF] 2.1.2.1 RecordTypeEnumeration
enum Record : uint8_t {
    SerializedStreamHeader = 0,
    ClassWithId = 1,
    SystemClassWithMembers = 2,
    ClassWithMembers = 3,
    SystemClassWithMembersAndTypes = 4,
    ClassWithMembersAndTypes = 5,
    BinaryObjectString = 6,
    BinaryArray = 7,
    MemberPrimitiveTyped = 8,
    MemberReference = 9,
    ObjectNull = 10,
    MessageEnd = 11,
    BinaryLibrary = 12,
    ObjectNullMultiple256 = 13,
    ObjectNullMultiple = 14,
    ArraySinglePrimitive = 15,
    ArraySingleObject = 16,
    ArraySingleString = 17,
};
// 2.1.2.2 BinaryTypeEnumeration
enum BinaryType : uint8_t { Primitive = 0, String = 1, ObjectType = 2, SystemClass = 3, Class = 4, ObjectArray = 5, StringArray = 6, PrimitiveArray = 7 };
// 2.1.2.3 PrimitiveTypeEnumeration
enum Prim : uint8_t {
    PBoolean = 1, PByte = 2, PChar = 3, PDecimal = 5, PDouble = 6, PInt16 = 7, PInt32 = 8, PInt64 = 9, PSByte = 10, PSingle = 11,
    PTimeSpan = 12, PDateTime = 13, PUInt16 = 14, PUInt32 = 15, PUInt64 = 16, PNull = 17, PString = 18,
};

struct ClassMeta {
    std::string name, library;
    std::vector<std::string> names;
    std::vector<uint8_t> types;  // BinaryType per member (none: SystemClassWithMembers/ClassWithMembers, all objects)
    std::vector<uint8_t> prims;  // the primitive type of a Primitive or PrimitiveArray member
    bool typed = false;
};

constexpr int kMaxDepth = 512;  // nested inline objects (a tree of trigger blocks); deeper: an error (the stack)
// The values (members and elements) that a stream may make: 2 per byte of the file. A record of a few bytes can say "a
// million nulls", or repeat a class's long member list by its id; the budget keeps the memory in proportion to the file.
constexpr size_t kValuesPerByte = 2;

struct Reader {
    const uint8_t* p;
    size_t n, at = 0;
    std::string err;
    std::map<int32_t, Object>& objects;
    std::map<int32_t, ClassMeta> metas;  // by the object id of the record that had the metadata
    std::map<int32_t, std::string> libraries;
    int depth = 0;
    size_t budget = 0;  // the values that the stream may still make

    Reader(const uint8_t* data, size_t size, std::map<int32_t, Object>& out) : p(data), n(size), objects(out) {}

    bool fail(const std::string& what) {
        if (err.empty()) err = what + " (at byte " + std::to_string(at) + ")";
        return false;
    }
    bool need(size_t k) { return k <= n - at ? true : fail("the file ends inside a record"); }
    uint8_t u8() { return need(1) ? p[at++] : 0; }
    uint32_t u32() {
        if (!need(4)) return 0;
        const uint32_t v = uint32_t(p[at]) | uint32_t(p[at + 1]) << 8 | uint32_t(p[at + 2]) << 16 | uint32_t(p[at + 3]) << 24;
        at += 4;
        return v;
    }
    int32_t i32() { return int32_t(u32()); }
    uint64_t u64() {
        const uint64_t lo = u32();
        return lo | uint64_t(u32()) << 32;
    }
    // 2.1.1.6 LengthPrefixedString: a 7-bit encoded length (at most 5 bytes), then UTF-8.
    std::string str() {
        uint32_t len = 0;
        for (int shift = 0;; shift += 7) {
            if (shift > 28) return fail("a string's length is too long"), std::string();
            const uint8_t b = u8();
            if (!err.empty()) return {};
            len |= uint32_t(b & 0x7F) << shift;
            if (!(b & 0x80)) break;
        }
        if (!need(len)) return {};
        std::string s(reinterpret_cast<const char*>(p + at), len);
        at += len;
        return s;
    }
    // A count from the file: refused when the rest of the file cannot hold `each` bytes for each.
    // Takes `k` values from the budget; false (an error) when the file would make more than its size allows.
    bool spend(size_t k) {
        if (k > budget) return fail("the file makes more objects than its size can hold");
        budget -= k;
        return true;
    }
    bool count(int32_t c, size_t each) {
        if (c < 0) return fail("a negative count");
        if (each && size_t(c) > (n - at) / each) return fail("a count larger than the file");
        return true;
    }

    Value prim(uint8_t t) {
        Value v;
        switch (t) {
        case PBoolean: v.kind = Value::Kind::Bool, v.i = u8() != 0; break;
        case PByte: v.kind = Value::Kind::UInt, v.u = u8(); break;
        case PSByte: v.kind = Value::Kind::Int, v.i = int8_t(u8()); break;
        case PChar: {  // one UTF-8 character
            const uint8_t b = u8();
            const int more = b < 0x80 ? 0 : b >= 0xF0 ? 3 : b >= 0xE0 ? 2 : 1;
            uint32_t c = more == 0 ? b : more == 1 ? b & 0x1F : more == 2 ? b & 0x0F : b & 0x07;
            for (int k = 0; k < more; ++k) c = c << 6 | (u8() & 0x3F);
            v.kind = Value::Kind::Int, v.i = c;
            break;
        }
        case PDecimal: v.kind = Value::Kind::String, v.s = str(); break;
        case PString: v.kind = Value::Kind::String, v.s = str(); break;
        case PDouble: {
            const uint64_t b = u64();
            v.kind = Value::Kind::Real, std::memcpy(&v.d, &b, 8);
            break;
        }
        case PSingle: {
            const uint32_t b = u32();
            float f;
            std::memcpy(&f, &b, 4);
            v.kind = Value::Kind::Real, v.d = f;
            break;
        }
        case PInt16: v.kind = Value::Kind::Int, v.i = int16_t(uint16_t(u8() | u8() << 8)); break;
        case PUInt16: v.kind = Value::Kind::UInt, v.u = uint16_t(u8() | u8() << 8); break;
        case PInt32: v.kind = Value::Kind::Int, v.i = i32(); break;
        case PUInt32: v.kind = Value::Kind::UInt, v.u = u32(); break;
        case PInt64: case PTimeSpan: case PDateTime: v.kind = Value::Kind::Int, v.i = int64_t(u64()); break;
        case PUInt64: v.kind = Value::Kind::UInt, v.u = u64(); break;
        case PNull: break;
        default: fail("an unknown primitive type " + std::to_string(t));
        }
        return v;
    }
    static size_t primSize(uint8_t t) {
        switch (t) {
        case PBoolean: case PByte: case PSByte: case PChar: return 1;
        case PInt16: case PUInt16: return 2;
        case PInt32: case PUInt32: case PSingle: return 4;
        case PDouble: case PInt64: case PUInt64: case PTimeSpan: case PDateTime: return 8;
        default: return 1;  // (strings: at least their length byte)
        }
    }

    // 2.3.1.1 ClassInfo
    bool classInfo(int32_t& id, ClassMeta& m) {
        id = i32();
        m.name = str();
        const int32_t c = i32();
        if (!count(c, 1)) return false;
        m.names.resize(size_t(c));
        for (auto& s : m.names) s = str();
        return err.empty();
    }
    // 2.3.1.2 MemberTypeInfo
    bool memberTypes(ClassMeta& m) {
        m.typed = true;
        m.types.resize(m.names.size());
        m.prims.assign(m.names.size(), 0);
        for (auto& t : m.types) t = u8();
        for (size_t k = 0; k < m.types.size(); ++k) {
            switch (m.types[k]) {
            case Primitive: case PrimitiveArray:
                m.prims[k] = u8();
                if (m.prims[k] == PNull || m.prims[k] == 4 || m.prims[k] == 0 || m.prims[k] > PString) return fail("a member of no primitive type");
                break;
            case SystemClass: str(); break;
            case Class: str(), i32(); break;
            case String: case ObjectType: case ObjectArray: case StringArray: break;
            default: return fail("an unknown member type");
            }
        }
        return err.empty();
    }

    Object& make(int32_t id) {
        Object& o = objects[id];
        o = Object{};
        o.id = id;
        return o;
    }

    // The members of a class record, by its metadata.
    bool members(Object& out, const ClassMeta& m) {
        const int32_t id = out.id;
        if (!spend(m.names.size())) return false;
        std::vector<Value> vals;
        vals.reserve(m.names.size());
        for (size_t k = 0; k < m.names.size(); ++k) {
            if (m.typed && m.types[k] == Primitive) {
                vals.push_back(prim(m.prims[k]));
            } else {
                int nulls = 0;
                vals.push_back(record(nulls));
                if (nulls > 1) return fail("several nulls for one member");
            }
            if (!err.empty()) return false;
        }
        Object& o = objects[id];  // (the map's nodes stay; `out` is the same object)
        o.className = m.name, o.library = m.library, o.names = m.names, o.values = std::move(vals);
        return true;
    }

    // Array elements that are records (ObjectNullMultiple fills several).
    bool elements(int32_t id, size_t total) {
        if (!spend(total)) return false;
        std::vector<Value> vals;
        vals.reserve(std::min<size_t>(total, n - at));
        while (vals.size() < total) {
            int nulls = 0;
            Value v = record(nulls);
            if (!err.empty()) return false;
            if (nulls > 0) {
                if (size_t(nulls) > total - vals.size()) return fail("more nulls than the array holds");
                vals.resize(vals.size() + size_t(nulls));
            } else {
                vals.push_back(std::move(v));
            }
        }
        objects[id].values = std::move(vals);
        return true;
    }
    bool primElements(int32_t id, size_t total, uint8_t t) {
        if (t == PNull || t == 4 || t == 0 || t > PString) return fail("an array of no primitive type");
        if (total > (n - at) / primSize(t)) return fail("an array larger than the file");
        Object& o = objects[id];
        if (t == PByte || t == PSByte) {  // (a picture's bytes: one blob, not a value per byte)
            Value v;
            v.kind = Value::Kind::Bytes, v.s.assign(reinterpret_cast<const char*>(p + at), total), at += total;
            o.values.push_back(std::move(v));
            o.lengths = {int32_t(total)};
            return true;
        }
        if (!spend(total)) return false;
        o.values.reserve(total);
        for (size_t k = 0; k < total && err.empty(); ++k) o.values.push_back(prim(t));
        return err.empty();
    }

    // Reads one record. Returns the value it stands for (an object's id, a string, a primitive, null); `nulls` is the
    // count of an ObjectNullMultiple. An object record registers the object.
    Value record(int& nulls) {
        Value v;
        nulls = 0;
        if (++depth > kMaxDepth) return fail("the objects are nested too deep"), v;
        struct Leave {
            int& d;
            ~Leave() { --d; }
        } leave{depth};
        for (;;) {
            const uint8_t type = u8();
            if (!err.empty()) return v;
            switch (type) {
            case BinaryLibrary: {
                const int32_t lib = i32();
                libraries[lib] = str();
                continue;  // (the record it is for follows)
            }
            case ObjectNull: return v;
            case ObjectNullMultiple256: nulls = u8(); return v;
            case ObjectNullMultiple: {
                const int32_t c = i32();
                if (c < 0) fail("a negative count");
                nulls = c;
                return v;
            }
            case MessageEnd: fail("the message ends inside an object"); return v;
            case MemberReference: v.kind = Value::Kind::Object, v.ref = i32(); return v;
            case MemberPrimitiveTyped: return prim(u8());
            case BinaryObjectString: {
                const int32_t id = i32();
                Object& o = make(id);
                o.className = "System.String";
                Value s;
                s.kind = Value::Kind::String, s.s = str();
                objects[id].values.push_back(s);
                return s;  // (a reference to this id later gets the string through Graph::get)
            }
            case ClassWithId: {
                const int32_t id = i32(), metaId = i32();
                const auto m = metas.find(metaId);
                if (m == metas.end()) return fail("an object refers to metadata that is not there"), v;
                make(id);
                const ClassMeta meta = m->second;  // (copied: members() can add to metas)
                members(objects[id], meta);
                v.kind = Value::Kind::Object, v.ref = id;
                return v;
            }
            case SystemClassWithMembers: case ClassWithMembers: case SystemClassWithMembersAndTypes: case ClassWithMembersAndTypes: {
                ClassMeta m;
                int32_t id = 0;
                if (!classInfo(id, m)) return v;
                if (type == SystemClassWithMembersAndTypes || type == ClassWithMembersAndTypes) memberTypes(m);
                if (type == ClassWithMembers || type == ClassWithMembersAndTypes) {
                    const auto lib = libraries.find(i32());
                    if (lib != libraries.end()) m.library = lib->second;
                }
                if (!err.empty()) return v;
                metas[id] = m;
                make(id);
                members(objects[id], m);
                v.kind = Value::Kind::Object, v.ref = id;
                return v;
            }
            case ArraySinglePrimitive: {
                const int32_t id = i32(), len = i32();
                const uint8_t t = u8();
                if (!count(len, 0)) return v;
                Object& o = make(id);
                o.array = true, o.lengths = {len};
                primElements(id, size_t(len), t);
                v.kind = Value::Kind::Object, v.ref = id;
                return v;
            }
            case ArraySingleObject: case ArraySingleString: {
                const int32_t id = i32(), len = i32();
                if (!count(len, 0)) return v;
                Object& o = make(id);
                o.array = true, o.lengths = {len};
                elements(id, size_t(len));
                v.kind = Value::Kind::Object, v.ref = id;
                return v;
            }
            case BinaryArray: {
                const int32_t id = i32();
                const uint8_t shape = u8();
                const int32_t rank = i32();
                if (rank < 1 || rank > 32) return fail("an array of an unknown rank"), v;
                std::vector<int32_t> lengths(static_cast<size_t>(rank));
                uint64_t total = 1;
                for (auto& l : lengths) {
                    l = i32();
                    if (l < 0) return fail("a negative array length"), v;
                    total *= uint64_t(l);
                    if (total > n) return fail("an array larger than the file"), v;
                }
                if (shape >= 3)
                    for (int k = 0; k < rank; ++k) i32();  // (the lower bounds)
                const uint8_t t = u8();
                uint8_t primType = 0;
                if (t == Primitive || t == PrimitiveArray) primType = u8();
                else if (t == SystemClass) str();
                else if (t == Class) str(), i32();
                if (!err.empty()) return v;
                Object& o = make(id);
                o.array = true, o.lengths = lengths;
                if (t == Primitive) primElements(id, size_t(total), primType);
                else elements(id, size_t(total));
                v.kind = Value::Kind::Object, v.ref = id;
                return v;
            }
            default: fail("an unknown record type " + std::to_string(type)); return v;
            }
        }
    }
};

// VB is not case-sensitive, and EUD Editor 3 changed the case of some names between versions (_Addr, _addr).
bool same(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t k = 0; k < a.size(); ++k)
        if (std::tolower(static_cast<unsigned char>(a[k])) != std::tolower(static_cast<unsigned char>(b[k]))) return false;
    return true;
}

}  // namespace

int64_t Value::asInt() const {
    switch (kind) {
    case Kind::Bool: case Kind::Int: return i;
    case Kind::UInt: return int64_t(u);
    case Kind::Real: return int64_t(d);
    default: return 0;
    }
}

const Value* Object::member(std::string_view name) const {
    for (int pass = 0; pass < 2; ++pass)
        for (size_t k = 0; k < names.size() && k < values.size(); ++k) {
            std::string_view m = names[k];
            if (pass == 1) {  // a base class's member: "Base+name"
                const size_t plus = m.rfind('+');
                if (plus == std::string_view::npos) continue;
                m = m.substr(plus + 1);
            }
            if (same(m, name)) return &values[k];
        }
    // A property's backing field: "_Name", "m_Name", "mName" (VB: mOpenMapName), "<Name>k__BackingField".
    for (size_t k = 0; k < names.size() && k < values.size(); ++k) {
        std::string_view m = names[k];
        if (const size_t plus = m.rfind('+'); plus != std::string_view::npos) m = m.substr(plus + 1);
        if (m.size() == name.size() + 1 && (m[0] == '_' || m[0] == 'm') && same(m.substr(1), name)) return &values[k];
        if (m.size() == name.size() + 2 && m.substr(0, 2) == "m_" && same(m.substr(2), name)) return &values[k];
        if (m.size() == name.size() + 18 && m[0] == '<' && same(m.substr(1, name.size()), name) && m.substr(name.size() + 1) == ">k__BackingField")
            return &values[k];
    }
    return nullptr;
}

std::string Object::shortName() const {
    std::string s = className.substr(0, className.find('['));
    if (const size_t dot = s.rfind('.'); dot != std::string::npos) s = s.substr(dot + 1);
    if (const size_t plus = s.rfind('+'); plus != std::string::npos) s = s.substr(plus + 1);
    return s;
}

bool Graph::read(const uint8_t* data, size_t size, std::string& error) {
    objects_.clear();
    root_ = 0;
    Reader r(data, size, objects_);
    r.budget = size * kValuesPerByte;
    if (size < 17 || data[0] != SerializedStreamHeader) return error = "the file is not a .NET BinaryFormatter file", false;
    r.at = 1;
    root_ = r.i32();
    r.i32();
    const int32_t major = r.i32(), minor = r.i32();
    if (major != 1 || minor != 0) return error = "the file is not a .NET BinaryFormatter file (version " + std::to_string(major) + "." + std::to_string(minor) + ")", false;
    for (;;) {
        if (r.at >= size) {
            r.fail("the file ends before its last record");
            break;
        }
        if (data[r.at] == MessageEnd) break;
        int nulls = 0;
        r.record(nulls);
        if (!r.err.empty()) break;
    }
    if (!r.err.empty()) return error = "the file is damaged: " + r.err, false;
    if (!object(root_)) return error = "the file has no root object", false;
    return true;
}

const Object* Graph::object(int32_t id) const {
    const auto it = objects_.find(id);
    return it == objects_.end() ? nullptr : &it->second;
}

const Object* Graph::get(const Object* o, std::string_view member) const {
    if (!o) return nullptr;
    const Value* v = o->member(member);
    return v ? get(*v) : nullptr;
}

const Value& Graph::value(const Object* o, std::string_view member) const {
    static const Value kNull;
    if (!o) return kNull;
    const Value* v = o->member(member);
    if (!v) return kNull;
    if (v->kind == Value::Kind::Object)  // (a string that another member wrote first: a reference to it)
        if (const Object* s = object(v->ref); s && s->className == "System.String" && !s->values.empty()) return s->values[0];
    return *v;
}

std::vector<Value> Graph::items(const Object* c) const { return items(c, 0); }

std::vector<Value> Graph::items(const Object* c, int depth) const {
    if (!c || depth > 4) return {};  // (a list that holds itself: references can make cycles)
    auto resolve = [this](std::vector<Value> vals) {
        for (Value& v : vals)
            if (v.kind == Value::Kind::Object)
                if (const Object* s = object(v.ref); s && s->className == "System.String" && !s->values.empty()) v = s->values[0];
        return vals;
    };
    if (c->array) return resolve(c->values);
    const std::string n = c->shortName();
    if (n == "List`1" || n == "ArrayList" || n == "Stack`1" || n == "Stack" || n == "Queue`1" || n == "Queue" || n == "ObservableCollection`1" ||
        n == "Collection`1") {
        const Object* arr = get(c, "items");
        if (!arr) arr = get(c, "array");  // (Stack, Queue)
        if (!arr) {
            if (const Object* inner = get(c, "items"); inner) arr = get(inner, "items");  // (Collection`1 wraps a List`1)
        }
        if (!arr && (n == "ObservableCollection`1" || n == "Collection`1")) return items(get(c, "items"), depth + 1);
        if (!arr) return {};
        if (!arr->array) return items(arr, depth + 1);
        std::vector<Value> out = resolve(arr->values);
        const Value& sz = value(c, "size");
        if (!sz.isNull() && sz.asInt() >= 0 && size_t(sz.asInt()) < out.size()) out.resize(size_t(sz.asInt()));
        return out;
    }
    return {};
}

std::vector<std::pair<Value, Value>> Graph::pairs(const Object* d) const {
    std::vector<std::pair<Value, Value>> out;
    if (!d) return out;
    auto str = [this](Value v) {
        if (v.kind == Value::Kind::Object)
            if (const Object* s = object(v.ref); s && s->className == "System.String" && !s->values.empty()) return s->values[0];
        return v;
    };
    if (const Object* kv = get(d, "KeyValuePairs")) {  // Dictionary`2
        for (const Value& p : items(kv))
            if (const Object* pair = get(p)) out.push_back({str(value(pair, "key")), str(value(pair, "value"))});
        return out;
    }
    const Object* keys = get(d, "Keys");  // Hashtable
    const Object* vals = get(d, "Values");
    if (keys && vals)
        for (size_t k = 0; k < keys->values.size() && k < vals->values.size(); ++k) out.push_back({str(keys->values[k]), str(vals->values[k])});
    return out;
}

std::string Graph::text(const Object* o, std::string_view member) const {
    const Value& v = value(o, member);
    return v.kind == Value::Kind::String ? v.s : std::string();
}

int64_t Graph::number(const Value& v) const {
    if (v.kind == Value::Kind::Object)
        if (const Object* e = object(v.ref)) return value(e, "value__").asInt();
    return v.asInt();
}

}  // namespace stardraft::nrbf
