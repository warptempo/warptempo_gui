#pragma once

#include <string>

// Strict shape validator for MM:SS.mmm settings timestamps: exactly nine
// chars, ':' at index 2, '.' at index 5, digits elsewhere. Returns true iff
// parse_timestamp (time_format.h) can be safely called on `s`. Relocated
// from settings_io.cpp so the GUI parse, the settings editor's commit(), and
// the parser-library trim reader all route through one predicate.
bool is_settings_timestamp(const std::string& s);

// Typed carrier for the three trim slots the .settings format defines. Trim
// is project view-state, deliberately kept OUT of EngineSettings; this is its
// parser-library home. Times are seconds, decoded from the on-disk MM:SS.mmm
// form via parse_timestamp. A has_* of false means the key was absent or
// malformed (silent-skip); the paired _sec field is then left at 0.0 and must
// not be read.
//
// The legacy singleton (trim_begin / trim_end) is accepted on read for
// back-compat; the GUI no longer writes it. The per-slot keys are
// tab_a_trim_begin / tab_a_trim_end and the tab_b_* mirror. The per-tab vs
// legacy precedence is a consumer policy (the GUI resolves it at load time in
// file_loader); it is NOT encoded here — every present, well-formed key is
// reported verbatim in its own slot.
struct SettingsTrim {
    bool   has_legacy_begin = false;
    double legacy_begin_sec = 0.0;
    bool   has_legacy_end   = false;
    double legacy_end_sec   = 0.0;
    bool   has_tab_a_begin  = false;
    double tab_a_begin_sec  = 0.0;
    bool   has_tab_a_end    = false;
    double tab_a_end_sec    = 0.0;
    bool   has_tab_b_begin  = false;
    double tab_b_begin_sec  = 0.0;
    bool   has_tab_b_end    = false;
    double tab_b_end_sec    = 0.0;
};

// Parse the trim keys from the .settings file at `path`. A missing or
// unopenable file yields an all-false carrier, matching parse_settings_file's
// "nothing to load" tolerance. One getline pass; blank lines, comment (#)
// lines, and lines without '=' are skipped; each trim value is validated by
// is_settings_timestamp and decoded by parse_timestamp. A malformed value is
// silent-skipped, leaving that slot unset. On a duplicate key the last
// well-formed occurrence wins.
SettingsTrim read_settings_trim(const std::string& path);
