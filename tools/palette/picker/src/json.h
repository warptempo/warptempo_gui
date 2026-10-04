#pragma once
// tools/palette/picker — a tiny JSON reader for the scene's manifest.json and the picker's own state.json. Both
// producers are ours (render.py --export, the picker's commit), so this reads standard JSON and reports the first
// error with its byte offset; it does not try to be a general library (no \u escapes beyond ASCII, numbers as
// doubles).

#include <map>
#include <memory>
#include <string>
#include <vector>

struct Json {
    enum class Kind { Null, Bool, Number, String, Array, Object } kind = Kind::Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<Json> arr;
    std::vector<std::pair<std::string, Json>> obj;   // in file order

    const Json* get(const std::string& key) const {
        for (const auto& kv : obj)
            if (kv.first == key) return &kv.second;
        return nullptr;
    }
    bool is_string() const { return kind == Kind::String; }
    bool is_number() const { return kind == Kind::Number; }
    bool is_object() const { return kind == Kind::Object; }
    bool is_array() const { return kind == Kind::Array; }
};

// text -> true and the value; false and `err` (what and where) on the first error
bool json_parse(const std::string& text, Json& out, std::string& err);
