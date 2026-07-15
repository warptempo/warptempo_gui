#pragma once

#include "warpmarkers_parse.h"        // WarpMarker
#include "phaseresetmarkers_parse.h"  // PhaseResetMarker
#include "settings_file.h"            // SettingsTrim, SettingsFileTab

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The two shared adversarial load guards, run identically by the GUI file
// loader and the headless CLI so a sidecar set is loadable in both products
// or neither: the past-EOF marker/trim walls and the persisted view-scratch
// range check. Both refuse the class of file the GUI can never author — a
// marker or trim bound past the audio it was authored against (the audio was
// swapped outside the GUI), or a persisted viewport/playhead past its own
// domain — with a first-offender detail string; the caller adds its product
// prefix. Neither converts a stored position: every comparison is a plain
// integer compare against the stored value, the same compare the gesture
// walls apply, so a legal at-the-wall position always passes.

// The adversarial past-EOF load guard (see the past-EOF paragraph above),
// shared by GUI file_loader and CLI so the six wall checks can never
// drift: warp markers (wall total-1), phase reset markers (wall total-1,
// the same wall, not a total-exact wall — both columns stay inside the
// playhead's [0, total-1] domain), then tab A
// trim begin (wall total-1), tab A end (wall total exactly — the
// surviving trim-vs-marker asymmetry, an exclusive bound expressing the
// half-open [begin, end) render window), tab B begin, tab B end — first
// offender only, disabled markers
// included. Every check is a plain integer compare against the stored
// value — literally the same comparison the gesture walls apply, no
// rounding anywhere — so a legal at-the-wall position always passes.
// Returns the first violation's detail string (embedded times are display
// renderings via format_timestamp(frame / sample_rate)); callers add
// their own product prefix. std::nullopt when every position is inside
// its wall.
std::optional<std::string> first_past_eof_wall_defect(
    const std::vector<WarpMarker>&       warp_markers,
    const std::vector<PhaseResetMarker>& phase_resets,
    const SettingsTrim& tab_a_trim,
    const SettingsTrim& tab_b_trim,
    int64_t total_frames,
    long    sample_rate);

// Persisted view-scratch range guard, shared by GUI file_loader and CLI so
// the four wall checks can never drift: tab A viewport start (refuses at or
// past the domain total — a viewport starting on the total's frame shows
// nothing), tab A playhead (refuses strictly past the domain total — the
// playhead may rest on the end exactly), then tab B likewise — first
// offender only, absent keys skipped.
//
// Persisted viewport/playhead positions live in the persisted
// active_audio_view's domain: while 'T' the live view fields carry
// target-frame values and the S/T toggle translates BOTH tabs together, so
// the domain is global to the file and a Ctrl+S from target view under a
// slowing map legitimately writes positions past the SOURCE total. The
// caller therefore selects and passes the domain total: 'S' or absent is
// the source total; 'T' is the deformed target total of the warp frame map
// built from the loaded markers and scale (each product computes it with
// the same math its runtime clamps use). When 'T' is persisted but the map
// cannot build (the tripwire class — the resolver normalizes marker
// arrangements, so it never refuses) the caller SKIPS this check entirely:
// there is no target total to wall against, and the runtime viewport
// clamps own the values then. Returns the first violation's detail
// string; callers add their own product prefix. std::nullopt when every
// present position is inside the domain.
std::optional<std::string> first_view_range_defect(
    const SettingsFileTab& tab_a,
    const SettingsFileTab& tab_b,
    int64_t domain_total_frames);
