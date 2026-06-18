#include "settings_trim.h"

#include "parse_text_util.h"
#include "time_format.h"   // parse_timestamp

#include <cctype>
#include <fstream>
#include <string>

namespace {

using warptempo_parse::trim_ws;

}  // namespace

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
            if (is_valid_timestamp_format(value)) {
                out.has_begin = true;
                out.begin_sec = parse_timestamp(value);
            }
        } else if (key == "trim_end") {
            if (is_valid_timestamp_format(value)) {
                out.has_end = true;
                out.end_sec = parse_timestamp(value);
            }
        }
    }
    return out;
}
