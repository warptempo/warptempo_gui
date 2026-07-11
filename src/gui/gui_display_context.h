#pragma once

#include "warp_frame_map.h"

#include <cstdint>
#include <vector>

// The active display context: a description of the domain the GUI is
// currently displaying, produced by active_display_context below. The
// GUI has three display DOMAINS — source view (identity), live target
// view (the memoized live map), and render view (the displayed entry's
// snapshot map) — and the composite rule for which
// domain is on screen (active_audio_view AND render_view.enabled
// together) lives in exactly one place: the accessor is the ONLY reader
// of app.active_audio_view / app.render_view.enabled for DOMAIN QUERIES
// (position translation, domain totals, hit-test positioning).
// Mode-logic reads — gesture gating, view toggles, persistence, playback
// lifecycle — are outside this rule and keep their own flag reads.

enum class GuiDisplayDomain { Source, TargetLive, Render };

struct GuiDisplayContext {
    GuiDisplayDomain domain = GuiDisplayDomain::Source;
    // Translation map for source-position -> displayed-domain-position.
    // NEVER null; points at an empty vector when the displayed domain is
    // the source timeline (identity), or when the target map cannot
    // build (the existing empty-map identity fallthrough). In the Render
    // domain it aliases the entry's snapshot map
    // (app.render_view.snapshot_warp_frame_map), built once at entry load
    // and immutable while displayed.
    const std::vector<WarpFrameMapSegment>* warp_frame_map = nullptr;
    // The displayed domain's total frames (live_total_frames semantics):
    // the source total, or the map's target total (the live target map's
    // in TargetLive, the snapshot map's in Render).
    int64_t domain_total_frames = 0;
    int sample_rate = 0;
};

struct AppState;
class GuiAudio;

// Returns the context for the currently displayed domain. The returned
// reference is to function-local static storage refreshed on every call
// (single-threaded GUI loop); warp_frame_map aliases the app-owned
// target-view cache in the TargetLive arm — stable until a changed-key
// rebuild, the same lifetime direct cache callers rely on — so do not
// hold either the reference or the map pointer across a call that could
// mutate markers, scale, or audio identity. Implementation lives in
// warp_frame_map_view.cpp.
const GuiDisplayContext& active_display_context(const AppState& app,
                                                const GuiAudio& audio);
