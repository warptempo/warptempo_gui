#pragma once

#include <charconv>
#include <cmath>
#include <expected>
#include <string>
#include <utility>

namespace warptempo_parse {

// Prefix a reader error with "line N: " when ln > 0, otherwise pass the
// message through unchanged. Returns std::unexpected so a std::expected-
// returning reader can `return prefix_line_error(ln, msg);` directly.
inline std::unexpected<std::string> prefix_line_error(int ln, std::string msg) {
    return std::unexpected(ln > 0
        ? "line " + std::to_string(ln) + ": " + std::move(msg)
        : std::move(msg));
}

// Strict whole-token bool parser: the two-token vocabulary shared by the
// settings schemas (`follow`, the per-tab read_only keys). The writer emits
// only `true`/`false`, so those are the sole accepted spellings.
inline bool parse_bool_token(const std::string& s, bool& out) {
    if (s == "true")  { out = true;  return true; }
    if (s == "false") { out = false; return true; }
    return false;
}

// Strict whole-token double parser for the settings schemas. IT HAS NO CALLER
// since row 7 deleted the font_size key it was written for (architect approval
// 2026-08-01); it stays as the schemas' strict-double primitive, beside
// parse_bool_token. The entire field must be consumed (no leading '+', whitespace, or
// trailing junk — std::from_chars grammar), and the value must be finite. It
// parses SYNTAX only; range and vocabulary rules belong to the per-key schema.
//
// Values are plain fixed-decimal spellings: digits and at most one dot — no
// exponent notation and no inf/nan words. Scientific notation adds no
// precision (a decimal string already parses to the nearest double; '0.1' and
// '1e-1' are the identical bits), and no writer, current or historical, ever
// emitted one, so no product-written file is affected. from_chars' float
// grammar admits both an exponent marker (upper or lower: '7E-1' / '7e-1')
// and the 'inf'/'nan' spellings, so the parser rejects any ASCII alphabetic
// byte up front, which refuses all of these in one rule.
inline bool has_ascii_alpha(const std::string& s) {
    for (char c : s)
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) return true;
    return false;
}

inline bool parse_double_strict(const std::string& s, double& out) {
    if (s.empty()) return false;
    if (has_ascii_alpha(s)) return false;  // plain decimal only: rejects '7e-1' / '7E-1'
    double v = 0.0;
    const auto res = std::from_chars(s.data(), s.data() + s.size(), v);
    if (res.ec != std::errc{} || res.ptr != s.data() + s.size()) return false;
    if (!std::isfinite(v)) return false;
    out = v;
    return true;
}

}  // namespace warptempo_parse
