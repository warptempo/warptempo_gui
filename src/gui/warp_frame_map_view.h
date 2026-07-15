#pragma once

#include "warp_frame_map_build.h"
#include "warpmarkers.h"   // GuiWarpMarker (the view-overload signature)

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

// Builds the target-view warp_frame_map from live warp markers plus scale, mirroring
// the resolve-then-build pipeline paint_handler's on_redraw uses for target-
// view waveform translation. Trim is forced off — target view paints the
// WHOLE song; the warp_frame_map must describe the whole song with warp segments
// where markers exist and identity outside (see paint_handler.cpp commentary
// next to the same construction). Returns an empty vector if
// resolve_warp_markers_for_render or build_warp_frame_map fails; when
// `error_out` is non-null the failure's message (the parser's own string) is
// written there, empty on success — the cache below stores it so the
// target-view validity gate can distinguish "empty because invalid" from
// never-built and kick the user back to source view with the popup. Callers
// in target view route this through compute_flag_hit_rects / render_flags /
// popup-hit helpers so hit-test math and paint stay in sync.
std::vector<WarpFrameMapSegment> build_target_view_warp_frame_map(
    const std::vector<GuiWarpMarker>& markers,
    double scale,
    int sample_rate,
    long total_frames,
    std::string* error_out = nullptr);

struct AppState;

// Memoized target-view warp_frame_map. One entry, keyed on the inputs that
// determine the map: the warp-marker store generation, the scale
// setting, and the audio identity (sample rate, total frames). The
// entry also carries the FNV-1a hash of the segment list, computed at
// rebuild, so the waveform-cache fingerprint reads it instead of
// rehashing per tick. A failed build is cached too (empty warp_frame_map,
// hash 0, build_error carrying the resolve/build message) — display
// callers treat the empty map as identity, and readers consult
// build_error to tell "empty because the build failed" apart from a
// legitimately empty/identity state.
struct TargetWarpFrameMapCache {
    bool      valid        = false;
    long long markers_gen  = -1;
    double    scale        = 0.0;
    int       sample_rate  = 0;
    long      total_frames = 0;
    std::vector<WarpFrameMapSegment> warp_frame_map;
    uint64_t  hash         = 0;

    // Empty when the last rebuild succeeded; otherwise the
    // resolve_warp_markers_for_render / build_warp_frame_map error string
    // verbatim (tripwire-class only — the resolver normalizes ambiguous
    // marker arrangements rather than refusing). Consumed by the caller
    // that must skip target-domain math when no map is in effect (the
    // dispatch snapshot's playhead translation).
    std::string build_error;

    // Deformed-timeline length: the source total forward-translated
    // through this warp_frame_map (the same formula the S-to-T toggle used).
    // Source total when the map is empty. live_total_frames() reads
    // this, so the value every viewport clamp and zoom bound sees is
    // always the total of the map actually in effect.
    int64_t   tgt_total_frames = 0;
};

// Returns the cache entry for the app's live marker store, rebuilding
// it first if the key does not match. The reference is valid until the
// next call with a changed key (single-threaded GUI use only — the
// waveform worker receives its own copy via the job, never this
// reference).
const TargetWarpFrameMapCache& target_view_warp_frame_map_cached(
    const AppState& app, int sample_rate, long total_frames);

// Forward-translate a source-frame coordinate into the active display
// domain's frame coordinates against a CALLER-SUPPLIED map. Source
// domain: identity. TargetLive domain: `map_source_to_target` through
// `warp_frame_map`. The domain decision comes from the active display
// context (active_display_context), while the map stays the explicit one
// the caller passes — its live purpose is explicit-map translation for the
// drag's frozen-map regime (the playhead tracks the grabbed stem through
// the same frozen coordinate system paint walks). An empty map is identity
// in the mapped domain. For live-map translation use
// source_frame_to_active_domain below, which owns the context's own map.
class GuiAudio;
int64_t to_domain_frame(const AppState& app, const GuiAudio& audio,
                        int64_t source_frame,
                        const std::vector<WarpFrameMapSegment>& warp_frame_map);

// Convenience wrappers that own the domain-check and the map selection for the
// common case: translating a single coordinate between the stores' domain and
// the active display domain through the active display context. Source view:
// identity, no map built. Target view: the memoized
// target_view_warp_frame_map_cached, so even repeated calls (e.g. inside a
// loop) cost only a cache-key comparison after the first build. Use these at
// every input / playhead boundary that translates against the live displayed
// domain.
//
// NOT for sites translating against a non-live map — a drag's
// app.drag.frozen_warp_frame_map, or a proposed (pre-commit) marker list. Those keep
// calling to_domain_frame directly with their explicit map.
int64_t source_frame_to_active_domain(const AppState& app, const GuiAudio& audio,
                                      int64_t source_frame);
int64_t active_domain_to_source_frame(const AppState& app, const GuiAudio& audio,
                                      int64_t domain_frame);

// The stem painters' samples-per-pixel and the single source of truth for the
// on-screen column grid: the visible span nearbyint-quantized to whole samples
// (matching the vp_end the waveform cache carries, vp_start +
// nearbyint(spp * area.w)) divided back over the strip width. The
// pixel-anchoring pair below and the viewport snap in clamp_viewport_start
// (main.cpp) all take their `q` from here, so the viewport grid and the marker
// grid are one grid at any window width (not just multiples of 8). Returns 0.0
// on degenerate geometry (no strip width / no zoom).
struct GuiRect;
double painter_samples_per_pixel(const AppState& app, const GuiAudio& audio,
                                 const GuiRect& area);

// The exact source-grid position at pixel column `col` for a grid-snapped
// viewport: recover the viewport's column index m = nearbyint(viewport_start/q)
// and round once. Returns the double (m+col)*q; a marker commit funnels this
// through snap_authored_frame, the click-playhead nearbyints it (a view
// position, not an authored one). Caller supplies q>0 (painter spp). SOURCE
// view only. m recovery is exact for product-reachable audio lengths: at the
// deepest numeric zoom q >= ~27.5 frames/px and a source length fits well within
// the double mantissa, so |viewport_start/q - m| << 0.5; it is NOT claimed exact
// over the whole int64 range.
inline double source_grid_position_at_column(int64_t viewport_start,
                                             int col, double q) {
    const double m = std::nearbyint(static_cast<double>(viewport_start) / q);
    return (m + static_cast<double>(col)) * q;
}

// Pixel-anchoring pair for gesture commits. Every gesture that moves an
// authored position by pixel columns (the Ctrl+Left/Right nudges on both
// marker columns and the trim bounds, the Ctrl+wheel trim end-move) or
// releases one at a pointer position (marker drag commits) anchors to the
// on-screen column grid through these two helpers: read the item's
// currently painted column with painted_column_of_source_frame, pick the
// destination column, and commit authored_frame_at_column of it — which
// funnels through snap_authored_frame (app_state.h), the single
// fractional-to-authored route. Anchoring to the column grid re-derives
// the pixel phase on every gesture, so whole-frame rounding residue can
// never accumulate on top of an off-grid sub-pixel phase; the painted
// move is exactly the commanded number of columns, and the stored value
// is the whole frame the painting already shows.
//
// painted_column_of_source_frame: the pixel column (offset from
// waveform_area(app).x) the stem painters draw `source_frame` at,
// computed with the painters' own math (render_marker_stems_impl /
// render_trim_stems): nearbyint the frame; in the TargetLive domain
// forward-map it through `warp_frame_map` and nearbyint the map output;
// then divide by the painters' samples-per-pixel — the visible span
// nearbyint-quantized to whole samples over the strip width — and round
// with the painters' std::nearbyint. `warp_frame_map` is the map the item is painted through:
// the live cached map at rest, the drag's frozen map at drag commit.
// Ignored in the Source domain; an empty map in a mapped domain falls
// back to identity, exactly like paint. Returns 0 when the strip has no
// width (callers guard the degenerate geometry).
int painted_column_of_source_frame(
    const AppState& app, const GuiAudio& audio, double source_frame,
    const std::vector<WarpFrameMapSegment>& warp_frame_map);

// authored_frame_at_column: the authored source-frame value of pixel
// column `col` under the same coordinate system — active-domain time =
// viewport start + col * the painters' samples-per-pixel; in the
// TargetLive domain that time is quantized to an integer target frame
// (llrint, floored at 0 — the same quantization the target-view nudges
// have always applied) and inverse-mapped through `warp_frame_map` at full
// precision. The
// result returns through snap_authored_frame, so it is a whole source
// frame in the authored int64 domain; callers apply their own walls
// AFTER — the walls win over the
// pixel grid, and every wall is itself an integer frame. Returns 0 when
// the strip has no width (callers guard the degenerate geometry).
int64_t authored_frame_at_column(
    const AppState& app, const GuiAudio& audio, int col,
    const std::vector<WarpFrameMapSegment>& warp_frame_map);
