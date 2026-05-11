#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

struct ViewState;

// Parsed contents of .settings, separated into tab-handled keys (typed with
// presence flags so defaults can be applied per key) and the pass-through
// vector that preserves any other lines verbatim in their original order.
struct ParsedSettings {
    bool    has_tab_a_vp   = false;
    int64_t tab_a_vp       = 0;
    bool    has_tab_a_zoom = false;
    int     tab_a_zoom     = 0;
    bool    has_tab_a_ph   = false;
    int64_t tab_a_ph       = 0;
    bool    has_tab_b_vp   = false;
    int64_t tab_b_vp       = 0;
    bool    has_tab_b_zoom = false;
    int     tab_b_zoom     = 0;
    bool    has_tab_b_ph   = false;
    int64_t tab_b_ph       = 0;
    bool    has_follow         = false;
    bool    follow             = true;
    bool    has_active_mode    = false;
    char    active_mode        = 'W';
    bool    has_playback_speed = false;
    float   playback_speed     = 1.0f;
    bool    has_trim_begin     = false;
    double  trim_begin         = 0.0;   // seconds
    bool    has_trim_end       = false;
    double  trim_end           = 0.0;   // seconds
    std::vector<std::pair<std::string, std::string>> passthrough;
};

// Ensure `p` exists with `contents`. If the file already exists, leave it
// alone. Returns true on success or if file already exists. Failures are
// non-fatal — the audio load still proceeds.
bool create_if_missing(const std::filesystem::path& p,
                       const std::string& contents);

// Parse `.settings`. Missing file → empty result (all has_* false, empty
// passthrough). Returns false only on a file-open failure of an existing
// file; per-line errors are silent-skip. Tab values are stored raw, without
// range validation — the caller clamps against the current audio file.
bool parse_settings_file(const std::string& path, ParsedSettings& out);

// First-open default `.settings` template. Line ordering must match
// write_settings_file's output (passthrough fields first, then follow=,
// then the tab_a_* / tab_b_* triplets) — keep these in sync if either
// side changes.
std::string format_default_settings_template(const std::string& stem,
                                             const std::string& ext_no_dot);

// Atomic write: pass-through lines first in their original order, then the
// six canonical tab lines. Matches the `.warpmarkers` write pattern
// (tmp → fsync → rename). Best-effort: failure is logged by the caller.
bool write_settings_file(
    const std::string& path,
    const ViewState& tab_a,
    const ViewState& tab_b,
    bool follow,
    char active_mode,
    float playback_speed,
    bool has_trim_begin, double trim_begin_seconds,
    bool has_trim_end,   double trim_end_seconds,
    const std::vector<std::pair<std::string, std::string>>& passthrough);
