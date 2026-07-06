#include "settings_trim.h"

#include "parse_text_util.h"
#include "time_format.h"   // parse_timestamp

#include <cctype>
#include <expected>
#include <fstream>
#include <set>
#include <string>
#include <utility>

namespace {

using warptempo_parse::trim_ws;

}  // namespace

std::expected<SettingsTrimTabs, std::string> read_settings_trim(
        const std::string& path) {
    SettingsTrimTabs out;
    std::ifstream f(path);
    if (!f) return out;
    std::string line;
    int ln = 0;
    // Tracks which of the keys this reader owns (the four canonical trim
    // keys plus active_tab_view) have already been assigned, so a
    // hand-edited duplicate is rejected the same way the engine-settings
    // reader rejects a duplicated canonical key.
    std::set<std::string> seen;
    while (std::getline(f, line)) {
        ++ln;
        if (ln == 1) warptempo_parse::strip_bom(line);
        const std::string trimmed = trim_ws(line);
        if (trimmed.empty()) continue;
        if (trimmed[0] == '#') continue;
        const size_t eq = trimmed.find('=');
        if (eq == std::string::npos) continue;
        const std::string key   = trim_ws(trimmed.substr(0, eq));
        const std::string value = trim_ws(trimmed.substr(eq + 1));
        if (key.empty()) continue;

        if (key == "tab_a_trim_begin") {
            if (!seen.insert(key).second) {
                return warptempo_parse::prefix_line_error(
                    ln, "duplicate trim key '" + key + "'");
            }
            if (!is_valid_timestamp_format(value)) {
                return warptempo_parse::prefix_line_error(
                    ln, "invalid trim timestamp for " + key + ": '" + value + "'");
            }
            out.tab_a.has_begin = true;
            out.tab_a.begin_sec = parse_timestamp(value);
        } else if (key == "tab_a_trim_end") {
            if (!seen.insert(key).second) {
                return warptempo_parse::prefix_line_error(
                    ln, "duplicate trim key '" + key + "'");
            }
            if (!is_valid_timestamp_format(value)) {
                return warptempo_parse::prefix_line_error(
                    ln, "invalid trim timestamp for " + key + ": '" + value + "'");
            }
            out.tab_a.has_end = true;
            out.tab_a.end_sec = parse_timestamp(value);
        } else if (key == "tab_b_trim_begin") {
            if (!seen.insert(key).second) {
                return warptempo_parse::prefix_line_error(
                    ln, "duplicate trim key '" + key + "'");
            }
            if (!is_valid_timestamp_format(value)) {
                return warptempo_parse::prefix_line_error(
                    ln, "invalid trim timestamp for " + key + ": '" + value + "'");
            }
            out.tab_b.has_begin = true;
            out.tab_b.begin_sec = parse_timestamp(value);
        } else if (key == "tab_b_trim_end") {
            if (!seen.insert(key).second) {
                return warptempo_parse::prefix_line_error(
                    ln, "duplicate trim key '" + key + "'");
            }
            if (!is_valid_timestamp_format(value)) {
                return warptempo_parse::prefix_line_error(
                    ln, "invalid trim timestamp for " + key + ": '" + value + "'");
            }
            out.tab_b.has_end = true;
            out.tab_b.end_sec = parse_timestamp(value);
        } else if (key == "active_tab_view") {
            if (!seen.insert(key).second) {
                return warptempo_parse::prefix_line_error(
                    ln, "duplicate key '" + key + "'");
            }
            if (value != "A" && value != "B") {
                return warptempo_parse::prefix_line_error(
                    ln, "invalid active_tab_view value '" + value +
                        "' (must be A or B)");
            }
            out.active_tab = value[0];
        }
    }

    // Bounds are validated per tab at read time so no consumer ever sees a
    // crossed or empty window: an end at or before its begin yields a zero- or
    // negative-length window that has no meaning downstream (the GUI clamps it,
    // the wav render's emit cap goes non-positive, and the .warpframemap /
    // .miditempomap artifacts emit a
    // non-monotonic end anchor). GUI gestures maintain ordering, so only a hand
    // edit can author a crossed pair. No line-number prefix: the defect spans
    // the two bound lines, not one.
    for (const auto& [name, tab] :
         {std::pair<const char*, const SettingsTrim&>{"tab_a", out.tab_a},
          std::pair<const char*, const SettingsTrim&>{"tab_b", out.tab_b}}) {
        if (tab.has_begin && tab.has_end && tab.end_sec <= tab.begin_sec) {
            return std::unexpected(
                std::string("crossed trim bounds for ") + name + ": end " +
                format_timestamp(tab.end_sec) + " <= begin " +
                format_timestamp(tab.begin_sec));
        }
    }

    return out;
}
