#include "settings_trim.h"

#include "parse_text_util.h"
#include "time_format.h"   // parse_timestamp

#include <cctype>
#include <fstream>
#include <string>

namespace {

using warptempo_parse::trim_ws;

}  // namespace

SettingsTrimTabs read_settings_trim(const std::string& path) {
    SettingsTrimTabs out;
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

        if (key == "tab_a_trim_begin") {
            if (is_valid_timestamp_format(value)) {
                out.tab_a.has_begin = true;
                out.tab_a.begin_sec = parse_timestamp(value);
            }
        } else if (key == "tab_a_trim_end") {
            if (is_valid_timestamp_format(value)) {
                out.tab_a.has_end = true;
                out.tab_a.end_sec = parse_timestamp(value);
            }
        } else if (key == "tab_b_trim_begin") {
            if (is_valid_timestamp_format(value)) {
                out.tab_b.has_begin = true;
                out.tab_b.begin_sec = parse_timestamp(value);
            }
        } else if (key == "tab_b_trim_end") {
            if (is_valid_timestamp_format(value)) {
                out.tab_b.has_end = true;
                out.tab_b.end_sec = parse_timestamp(value);
            }
        }
    }
    return out;
}
