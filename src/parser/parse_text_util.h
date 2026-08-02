#pragma once

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

// (THE STRICT DOUBLE PARSER IS GONE — parse_double_strict and its private
// has_ascii_alpha helper, deleted here under architect approval 2026-08-02,
// with <charconv> and <cmath>, which nothing else in this header needed.
// Row 7 had already deleted font_size, the one key it was ever written for
// (architect approval 2026-08-01), and it was kept a while as the schemas'
// strict-double primitive beside parse_bool_token — but no caller ever came
// back, and an unused parser primitive in a frozen file is exactly the residue
// the freeze is meant to keep out. The strict-double rules it encoded are not
// lost: value_format.h owns the product's bracketed-double vocabulary and
// frame_format.h the integer one, each with its own from_chars discipline.
// Reinstating it means reinstating a caller with it.)

}  // namespace warptempo_parse
