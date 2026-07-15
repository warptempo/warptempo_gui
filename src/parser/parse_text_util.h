#pragma once

#include <charconv>
#include <cmath>
#include <cstdint>
#include <expected>
#include <string>
#include <utility>

namespace warptempo_parse {

// Trim leading/trailing space, tab, CR, LF. Deterministic and locale-
// independent; the single trim_ws for the parser TUs.
inline std::string trim_ws(const std::string& s) {
    const char* ws = " \t\r\n";
    const auto a = s.find_first_not_of(ws);
    if (a == std::string::npos) return {};
    const auto b = s.find_last_not_of(ws);
    return s.substr(a, b - a + 1);
}

// Strip a leading UTF-8 BOM in place, if present.
inline void strip_bom(std::string& s) {
    if (s.size() >= 3 &&
        static_cast<unsigned char>(s[0]) == 0xEF &&
        static_cast<unsigned char>(s[1]) == 0xBB &&
        static_cast<unsigned char>(s[2]) == 0xBF) {
        s.erase(0, 3);
    }
}

// Prefix a reader error with "line N: " when ln > 0, otherwise pass the
// message through unchanged. Returns std::unexpected so a std::expected-
// returning reader can `return prefix_line_error(ln, msg);` directly.
inline std::unexpected<std::string> prefix_line_error(int ln, std::string msg) {
    return std::unexpected(ln > 0
        ? "line " + std::to_string(ln) + ": " + std::move(msg)
        : std::move(msg));
}

// Strict whole-token bool parser: the canonical truthy/falsy token set
// shared by the settings schemas (the per-tab read_only keys).
inline bool parse_bool_token(const std::string& s, bool& out) {
    if (s == "true"  || s == "1" || s == "yes" || s == "on")  { out = true;  return true; }
    if (s == "false" || s == "0" || s == "no"  || s == "off") { out = false; return true; }
    return false;
}

// Strict whole-token scalar parsers for the settings schemas: the entire
// field must be consumed (no leading '+', whitespace, or trailing junk —
// std::from_chars grammar), and floating values must be finite. These parse
// SYNTAX only; range and vocabulary rules belong to the per-key schema.

inline bool parse_int_strict(const std::string& s, int& out) {
    if (s.empty()) return false;
    int v = 0;
    const auto res = std::from_chars(s.data(), s.data() + s.size(), v);
    if (res.ec != std::errc{} || res.ptr != s.data() + s.size()) return false;
    out = v;
    return true;
}

inline bool parse_int64_strict(const std::string& s, int64_t& out) {
    if (s.empty()) return false;
    int64_t v = 0;
    const auto res = std::from_chars(s.data(), s.data() + s.size(), v);
    if (res.ec != std::errc{} || res.ptr != s.data() + s.size()) return false;
    out = v;
    return true;
}

inline bool parse_float_strict(const std::string& s, float& out) {
    if (s.empty()) return false;
    float v = 0.0f;
    const auto res = std::from_chars(s.data(), s.data() + s.size(), v);
    if (res.ec != std::errc{} || res.ptr != s.data() + s.size()) return false;
    if (!std::isfinite(v)) return false;
    out = v;
    return true;
}

inline bool parse_double_strict(const std::string& s, double& out) {
    if (s.empty()) return false;
    double v = 0.0;
    const auto res = std::from_chars(s.data(), s.data() + s.size(), v);
    if (res.ec != std::errc{} || res.ptr != s.data() + s.size()) return false;
    if (!std::isfinite(v)) return false;
    out = v;
    return true;
}

}  // namespace warptempo_parse
