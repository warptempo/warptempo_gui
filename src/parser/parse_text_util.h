#pragma once

#include <string>

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

}  // namespace warptempo_parse
