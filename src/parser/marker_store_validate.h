#pragma once

#include "warpmarkers_parse.h"        // WarpMarker
#include "phaseresetmarkers_parse.h"  // PhaseResetMarker
#include "settings_file.h"            // SettingsTrim

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The shared adversarial load guard, run identically by the GUI file loader
// and the headless CLI so a sidecar set is loadable in both products or
// neither: the past-EOF marker/trim walls. It refuses the class of file the
// GUI can never author — a marker or trim bound past the audio it was
// authored against (the audio was swapped outside the GUI) — with a
// first-offender detail string; the caller adds its product prefix. It never
// converts a stored position: every comparison is a plain integer compare
// against the stored value, the same compare the gesture walls apply, so a
// legal at-the-wall position always passes. Persisted viewport/playhead
// positions are NOT guarded here: they are display scratch, not authored
// data, and the runtime clamps (clamp_viewport_start, the playhead clamp at
// first use) own any out-of-range value harmlessly.

// The adversarial past-EOF load guard (see the past-EOF paragraph above),
// shared by GUI file_loader and CLI so the six wall checks can never
// drift: warp markers (wall total-1), phase reset markers (wall total-1),
// then tab A trim begin (wall total-1), tab A end (wall total-1), tab B
// begin, tab B end — first offender only, disabled markers included. All
// authored positions — both marker columns and both trim bounds — share
// the one inclusive [0, total-1] domain; an end bound stored at exactly
// total frames is adversarial load-fatal (the GUI can no longer author it,
// and any pre-unification file carrying one fails the load — accepted, no
// migration). Every check is a plain integer compare against the stored
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
