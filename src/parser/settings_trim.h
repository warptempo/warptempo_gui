#pragma once

#include <string>

// Typed carrier for the project-level trim the .settings format defines. Trim
// is project view-state, deliberately kept OUT of EngineSettings; this is its
// parser-library home. Times are seconds, decoded from the on-disk MM:SS.mmm
// form via parse_timestamp. A has_* of false means the key was absent or
// malformed (silent-skip); the paired _sec field is then left at 0.0 and must
// not be read.
//
// There is a single trim per project, keyed by the canonical trim_begin /
// trim_end. There is no tab notion and no legacy/per-tab precedence: each
// present, well-formed key is reported verbatim in its own slot.
struct SettingsTrim {
    bool   has_begin = false;
    double begin_sec = 0.0;
    bool   has_end   = false;
    double end_sec   = 0.0;
};

// Parse the trim keys from the .settings file at `path`. A missing or
// unopenable file yields an all-false carrier, matching parse_settings_file's
// "nothing to load" tolerance. One getline pass; blank lines, comment (#)
// lines, and lines without '=' are skipped; each trim value is validated by
// is_valid_timestamp_format and decoded by parse_timestamp. A malformed value is
// silent-skipped, leaving that slot unset. On a duplicate key the last
// well-formed occurrence wins.
SettingsTrim read_settings_trim(const std::string& path);
