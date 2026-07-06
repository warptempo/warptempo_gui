#pragma once

#include <expected>
#include <string>

// Typed carrier for one tab's trim in the .settings format. Times are seconds,
// decoded from the on-disk MM:SS.mmm form via parse_timestamp. A has_* of
// false means the key was absent; the paired _sec field is then left at 0.0
// and must not be read.
struct SettingsTrim {
    bool   has_begin = false;
    double begin_sec = 0.0;
    bool   has_end   = false;
    double end_sec   = 0.0;
};

// Two-tab carrier for the per-tab trim keys in the .settings format, plus
// the persisted active tab. active_tab comes from the active_tab_view key
// ('A' when the key is absent) and selects which tab's trim a headless
// render applies — the GUI renders the active tab's trim, and the CLI
// binaries follow it.
struct SettingsTrimTabs {
    SettingsTrim tab_a;
    SettingsTrim tab_b;
    char active_tab = 'A';
};

// Parse the per-tab trim keys tab_a_trim_begin / tab_a_trim_end /
// tab_b_trim_begin / tab_b_trim_end, plus the active_tab_view selector,
// from the .settings file at `path`.
// Missing or unopenable file yields an all-false carrier for both tabs.
// One getline pass; blank, comment, and non-key lines are skipped; each
// trim value is validated by is_valid_timestamp_format and decoded by
// parse_timestamp. Missing trim keys are valid, but a present trim key with
// a malformed timestamp is a hard parse failure; a present active_tab_view
// with any value but A or B is a hard parse failure the same way.
// A duplicated key is a hard parse failure, matching the
// engine-settings reader's rejection of a duplicated canonical key.
std::expected<SettingsTrimTabs, std::string> read_settings_trim(
    const std::string& path);
