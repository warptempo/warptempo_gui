#include "settings_trim.h"

#include "time_format.h"   // parse_timestamp

#include <cctype>
#include <fstream>
#include <string>

namespace {

// Local copy, mirroring the anonymous-namespace trim_ws already present in
// settings_io.cpp and engine_settings_io.cpp. No shared trim_ws helper exists
// in this codebase; introducing one is out of scope for this move.
std::string trim_ws(const std::string& s) {
    size_t a = 0;
    while (a < s.size() &&
           std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    size_t b = s.size();
    while (b > a &&
           std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

}  // namespace

bool is_settings_timestamp(const std::string& s) {
    if (s.size() != 9) return false;
    if (s[2] != ':' || s[5] != '.') return false;
    for (size_t i = 0; i < s.size(); ++i) {
        if (i == 2 || i == 5) continue;
        if (s[i] < '0' || s[i] > '9') return false;
    }
    return true;
}

SettingsTrim read_settings_trim(const std::string& path) {
    SettingsTrim out;
    std::ifstream f(path);
    if (!f) return out;
    std::string line;
    while (std::getline(f, line)) {
        const std::string trimmed = trim_ws(line);
        if (trimmed.empty()) continue;
        if (trimmed[0] == '#') continue;
        const size_t eq = trimmed.find('=');
        if (eq == std::string::npos) continue;
        const std::string key   = trim_ws(trimmed.substr(0, eq));
        const std::string value = trim_ws(trimmed.substr(eq + 1));
        if (key.empty()) continue;

        if (key == "trim_begin") {
            if (is_settings_timestamp(value)) {
                out.has_begin = true;
                out.begin_sec = parse_timestamp(value);
            }
        } else if (key == "trim_end") {
            if (is_settings_timestamp(value)) {
                out.has_end = true;
                out.end_sec = parse_timestamp(value);
            }
        }
    }
    return out;
}
