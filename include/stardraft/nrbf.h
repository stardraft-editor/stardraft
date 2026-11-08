#pragma once
// A reader of .NET's BinaryFormatter files (MS-NRBF, "[MS-NRBF]: .NET Remoting: Binary Format Data Structure"): the
// object graph as records, by class and member name. EUD Editor 3 saves its projects (.e3s) in this format. The reader
// only reads data: it makes no .NET objects and runs no code. It checks each length against the file, so a damaged or
// hostile file gives an error, not a crash.

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace stardraft::nrbf {

// A value of a member or of an array element.
struct Value {
    enum class Kind { Null, Bool, Int, UInt, Real, String, Object, Bytes } kind = Kind::Null;
    int64_t i = 0;   // Int (also Char, DateTime and TimeSpan ticks); Bool: 0 or 1
    uint64_t u = 0;  // UInt
    double d = 0;    // Real
    std::string s;   // String (UTF-8; also Decimal as text), Bytes (a byte[] or sbyte[])
    int32_t ref = 0; // Object: the object's id

    bool isNull() const { return kind == Kind::Null; }
    // The value as a number (Bool, Int, UInt, Real; else 0).
    int64_t asInt() const;
    bool asBool() const { return asInt() != 0; }
};

// An object: a class with members, or an array.
struct Object {
    int32_t id = 0;
    std::string className;  // "SaveableData", "System.Collections.Generic.List`1[[...]]"; arrays: ""
    std::string library;    // the assembly's name ("EUD Editor 3, Version=...")
    bool array = false;
    std::vector<std::string> names;  // the members' names (empty for an array)
    std::vector<Value> values;       // the members' values, or the array's elements
    std::vector<int32_t> lengths;    // an array's lengths, one per rank

    // A member by its name: the exact name, a VB or C# property's backing field ("_Name", "<Name>k__BackingField",
    // "m_Name"), or a base class's member ("Base+Name"). nullptr: none.
    const Value* member(std::string_view name) const;
    // The class name without its generic arguments and namespace ("List`1", "SaveableData", "CDatFile").
    std::string shortName() const;
};

class Graph {
public:
    // Reads the whole stream. False with `error` when it is not a BinaryFormatter stream or it is damaged.
    bool read(const uint8_t* data, size_t size, std::string& error);
    const Object* root() const { return object(root_); }
    const Object* object(int32_t id) const;
    // The object a value refers to (nullptr: not an object).
    const Object* get(const Value& v) const { return v.kind == Value::Kind::Object ? object(v.ref) : nullptr; }
    // The object of a member (nullptr: not there, or not an object).
    const Object* get(const Object* o, std::string_view member) const;
    // A member's value (a null value when it is not there).
    const Value& value(const Object* o, std::string_view member) const;
    // A collection's elements: an array, a List`1 (its _items, up to _size), an ArrayList, a Stack or a Queue. Empty for
    // anything else.
    std::vector<Value> items(const Object* collection) const;
    // A Dictionary`2's or a Hashtable's pairs, in the file's order.
    std::vector<std::pair<Value, Value>> pairs(const Object* dictionary) const;
    // A string value or a string member ("" when it is not a string).
    std::string text(const Object* o, std::string_view member) const;
    // An enum's or a number's value: an enum is an object with value__.
    int64_t number(const Value& v) const;
    int64_t number(const Object* o, std::string_view member) const { return number(value(o, member)); }
    size_t objectCount() const { return objects_.size(); }
    const std::map<int32_t, Object>& objects() const { return objects_; }

private:
    std::vector<Value> items(const Object* collection, int depth) const;  // (depth: a list in a list, at most a few)
    std::map<int32_t, Object> objects_;
    int32_t root_ = 0;
};

}  // namespace stardraft::nrbf
