#pragma once

#include <string>

// Typed carrier for one tab's trim in the .settings format. Times are seconds,
// decoded from the on-disk MM:SS.mmm form via parse_timestamp. A has_* of
// false means the key was absent or malformed (silent-skip); the paired _sec
// field is then left at 0.0 and must not be read.
struct SettingsTrim {
    bool   has_begin = false;
    double begin_sec = 0.0;
    bool   has_end   = false;
    double end_sec   = 0.0;
};

// Two-tab carrier for the per-tab trim keys in the .settings format.
struct SettingsTrimTabs {
    SettingsTrim tab_a;
    SettingsTrim tab_b;
};

// Parse the per-tab trim keys tab_a_trim_begin / tab_a_trim_end /
// tab_b_trim_begin / tab_b_trim_end from the .settings file at `path`.
// Missing or unopenable file yields an all-false carrier for both tabs.
// One getline pass; blank, comment, and non-key lines are skipped; each
// value is validated by is_valid_timestamp_format and decoded by
// parse_timestamp; a malformed value is silent-skipped; on a duplicate
// key the last well-formed occurrence wins.
SettingsTrimTabs read_settings_trim(const std::string& path);
