#include "app_state.h"

#include "audio.h"
#include "gui_display_context.h"
#include "paint_handler.h"
#include "render.h"
#include "text_editor.h"
#include "warp_frame_map_build.h"  // resolved_marker_payload (value_source_marker, marker_tooltip_value)
#include "warp_frame_map_view.h"
#include "warp_frame_map.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

int64_t monotonic_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

SettingsSnapshot capture_current_settings(const AppState& app) {
    SettingsSnapshot s;
    s.engine_settings = app.engine_settings;
    // Trim is view state, gesture-owned and excluded from undo/redo history;
    // undo entries snapshot engine settings only.
    return s;
}

void remap_marker_indices_after_reorder(AppState& app,
                                        const std::vector<int>& old_to_new) {
    if (old_to_new.empty()) return;
    const int n = static_cast<int>(old_to_new.size());
    auto mapped = [&](int idx) {
        return (idx >= 0 && idx < n) ? old_to_new[idx] : idx;
    };
    auto remap_set = [&](std::set<int>& s) {
        if (s.empty()) return;
        std::set<int> out;
        for (int idx : s) out.insert(mapped(idx));
        s = std::move(out);
    };
    remap_set(app.selected_markers);
    // The focus FOLLOWS its marker, and so does the addressed cell
    // (AppState::addressed_cell) by doing nothing: a reorder is not a focus
    // change — the same marker keeps the same cell — so this is not one of
    // the writes that reset the axis (those go through Selection::seat_focus).
    app.last_selected_marker = mapped(app.last_selected_marker);
    // No parked copies to follow: neither ViewState holds an index (the rule is
    // at ViewState, app_state.h), which is also why this function needs no
    // `column` argument — everything above belongs to the active column, and
    // every caller reorders the active column's store.
    if (app.drag.active) {
        // Pairing between dragging_markers and its parallel time vectors
        // (original_times / moveable_times) is positional (slot 0, the one dragged
        // marker), so an in-place value remap keeps the index bound to its times.
        for (int& idx : app.drag.dragging_markers) idx = mapped(idx);
    }
}

// hit_test_* promoted from lambdas in main(). The captured `app` and `audio`
// references are now explicit arguments. There is no grab tolerance left:
// the trim grabs are the view bar's span's end zones since 2026-10-09 (the
// painted arrow buttons from 2026-10-03) and the marker surfaces (the cues'
// triangles and labels) hit on their painted rects, neither with a halo. Both families read a PAINTER'S STASH: the flag
// lane's (AppState::flag_hit_rects) and, since 2026-09-24, the trim lane's
// (AppState::trim_bar_hit).

// Event-synchronized hit map (ruling at the declaration in app_state.h): in
// target view with a warm displayed map, the item painters and the gesture
// mechanics decide against the map the LAST COMMITTED frame's flag pixels
// were painted with (promoted
// at that frame commit, not the offscreen rebuild or the plate publish);
// otherwise the live display context's map (source view = its identity/empty
// map, target-view cold = the live map until the first committed target frame).
const std::vector<WarpFrameMapSegment>&
displayed_or_live_target_map(const AppState& app, const GuiAudio& audio) {
    const GuiDisplayContext& ctx = active_display_context(app, audio);
    if (ctx.domain == GuiDisplayDomain::TargetLive &&
        !app.displayed_target_warp_frame_map.empty()) {
        return app.displayed_target_warp_frame_map;
    }
    return *ctx.warp_frame_map;
}

// The viewport twin of displayed_or_live_target_map (full rationale at the
// declaration): the vp_start/vp_end/area_w the flag item cache was painted
// with on the last committed frame, so the endcap and editor-box pixels ride
// the same basis the flag pixels do. The warm spp is (vp_end - vp_start) /
// area_w — the flags' OWN samples-per-pixel (span over the effective waveform
// width the item render used), exact on the committing frame. Cold (area_w == 0,
// nothing promoted yet) falls back to the live viewport span at the effective
// waveform width, bit-for-bit the pre-mirror hit-test live basis.
ItemViewportBasis item_viewport_basis(const AppState& app,
                                                const GuiAudio& audio) {
    ItemViewportBasis b;
    if (app.displayed_area_w > 0) {
        b.vp_start_frame = app.displayed_vp_start;
        b.vp_end_frame   = app.displayed_vp_end;
        b.area_w         = app.displayed_area_w;
        b.lattice_origin = app.displayed_lattice_origin;
    } else {
        const GuiRect area = waveform_area(app);
        const double  spp  = current_samples_per_pixel(app, audio);
        b.vp_start_frame = app.viewport_start_sample;
        b.vp_end_frame   = viewport_end_sample(b.vp_start_frame, spp, area.w);
        b.area_w         = area.w;
        b.lattice_origin = app.viewport_lattice_origin;
    }
    b.vp_start = static_cast<double>(b.vp_start_frame);
    b.spp = b.area_w > 0
        ? static_cast<double>(b.vp_end_frame - b.vp_start_frame) /
          static_cast<double>(b.area_w)
        : 0.0;
    return b;
}

GuiRect sweep_highlight_rect(const AppState& app, const GuiAudio& audio) {
    // Contract at the declaration (app_state.h).
    const RegionDragState& d = app.region_drag;
    if (!d.active || !d.moved) return GuiRect{};
    const GuiRect area = waveform_area(app);
    if (area.w <= 0 || area.h <= 0) return GuiRect{};
    int lo = 0;
    int hi = 0;
    if (d.select) {
        if (d.pen_col < 0) return GuiRect{};
        lo = std::min(d.anchor_col, d.pen_col);
        hi = std::max(d.anchor_col, d.pen_col) + 1;
    } else {
        if (!d.wrote_trim) return GuiRect{};
        // paint_trim's chain and its gates (paint_handler.cpp,
        // render_trim_flags): the item basis, the displayed map, each bound
        // through displayed_trim_ms and the int64 round trip of TrimRange
        // (exact: the mapping returns an integral-valued double), then the
        // bound columns at the basis's effective width.
        const ItemViewportBasis basis = item_viewport_basis(app, audio);
        if (basis.area_w <= 0 || basis.spp <= 0.0 ||
            basis.vp_end_frame <= basis.vp_start_frame)
            return GuiRect{};
        const std::vector<WarpFrameMapSegment>& dmap =
            displayed_or_live_target_map(app, audio);
        const std::vector<WarpFrameMapSegment>* map_arg =
            dmap.empty() ? nullptr : &dmap;
        const auto bound_column = [&](int64_t frame) {
            const int64_t ms =
                static_cast<int64_t>(displayed_trim_ms(frame, map_arg));
            return trim_bound_column(static_cast<double>(ms),
                                     basis.vp_start_frame, basis.vp_end_frame,
                                     basis.lattice_origin, basis.area_w);
        };
        const TrimSpanColumns s =
            trim_span_columns(bound_column(app.trim.begin_frame),
                              bound_column(app.trim.end_frame), basis.area_w);
        lo = s.lo;
        hi = s.hi;
    }
    if (hi <= lo) return GuiRect{};
    return GuiRect{area.x + lo, area.y, hi - lo, area.h};
}

TrimHit hit_test_trim_endcap(const AppState& app, int mouse_x, int mouse_y) {
    // THE PAINTER'S STASH IS THE HIT GEOMETRY (architect 2026-09-24, strictly
    // as-painted; the contract is at AppState::trim_bar_hit). The grabs are
    // the view bar's span's two end zones as GuiPaintHandler::paint_trim last
    // DREW the span — on the displayed item basis and the displayed map,
    // through trim_endcap_rect, each published over the lane's whole height
    // (2026-10-09; the arrow buttons from 2026-10-03) — so a press between a
    // trim write and its repaint grabs the span still on screen, a zone
    // sliding off an edge by its visible columns alone, and one wholly off
    // the lane answers nothing. Nothing here reads app.trim or re-runs the
    // owner chain. Cold (nothing painted) nothing is grabbable.
    const TrimBarHit& h = app.trim_bar_hit;
    if (!h.published) return TrimHit::None;

    // The buttons span the trim bar LANE the painter drew them in, so a press
    // outside that band is not on one. (The y-gate spanned the merged
    // trim-bar + ruler band for the trim surface arc's one day, 2026-08-11..12,
    // and came back to the lane with the arc's revert; the ruler is the
    // REGION FORMER's band since 2026-08-12.)
    if (mouse_y < h.lane.y || mouse_y >= h.lane.y + h.lane.h)
        return TrimHit::None;

    // THE ZONE IS THE TARGET (architect 2026-10-03, for the buttons the zones
    // replaced): each published rect is tested as it is, with no tolerance.
    // The two rects never overlap — the end zone stands edge to edge right of
    // the begin's in the narrow case (trim_endcap_rect) — so the order of the
    // two tests decides nothing.
    const auto on = [&](const TrimBarHitCap& cap) {
        return cap.painted && mouse_x >= cap.rect.x &&
               mouse_x < cap.rect.x + cap.rect.w;
    };
    if (on(h.begin)) return TrimHit::Begin;
    if (on(h.end))   return TrimHit::End;
    return TrimHit::None;
}

bool point_in_trim_bridge_span(const AppState& app, int mouse_x, int mouse_y) {
    // THE PAINTER'S STASH, the endcap test's twin (AppState::trim_bar_hit):
    // the interval is the span's body between its two end zones as
    // render_trim_flags last DREW it — trim_bridge_gap over the painted
    // columns, already clipped to the lane's painted width, so the inert
    // non-multiple-of-16 right gutter answers false exactly as it paints no
    // bar. Nothing here reads app.trim. Cold answers false — which is also
    // the no-audio answer, the trim pass painting only over loaded audio.
    const TrimBarHit& h = app.trim_bar_hit;
    if (!h.published) return false;
    // The TRIM BAR LANE ONLY — the band the view bar was painted in, and the
    // exact band hit_test_trim_endcap gates on. A top-strip point BELOW it
    // (the ruler, then the marker lane) is not the bridge handle.
    if (mouse_y < h.lane.y || mouse_y >= h.lane.y + h.lane.h) return false;
    return mouse_x >= h.bridge_lo && mouse_x < h.bridge_hi;
}

// The topmost published flag rect under the point, or nullptr — the ONE walk
// both public answers below take, so "which marker" and "which box of its run"
// cannot disagree. It reads TWO publications, the lane pass's stash and the
// open marker-lane editor's riding boxes, and that too is why it is one body: a
// riding box must resolve to the marker and the cell a resting one resolves
// to, and the only way to be sure of that is to answer both out of the same
// walk with the same boundary idiom.
//
// A CUE IS TWO RECTS (FlagHitRect, render.h, 2026-10-09): its label's box and
// its triangle — the outlined chip's box since 2026-11-04 (cue_chip_rows) —
// and the walk follows the cue painter's own order
// (render.cpp's paint_cues, render.h's marker-lane paragraph): THE CHIPS
// paint last, left to right, so a press on any chip answers the
// rightmost chip under it; THE LABELS paint left to right under them (a
// later label's box masks an earlier one's text), so among labels the
// RIGHTMOST under the point is on top. `on_triangle` says
// which of the two answered (a triangle is the payload's, never a cell).
static const FlagHitRect* topmost_flag_rect(const AppState& app,
                                            int mouse_x, int mouse_y,
                                            bool* on_triangle = nullptr) {
    if (on_triangle) *on_triangle = false;
    // THE OPEN EDITOR'S RIDING BOXES ARE ASKED FIRST — whichever of the
    // marker's boxes stand to the RIGHT of the field, re-painted at its right
    // edge by the editor's painter and published there as a flag rect of their
    // own (FlagEditorBox::riding_cells, render.h; architect 2026-09-05, THE
    // RIDING CELLS ARE THE MARKER'S OWN CELLS FOR THE POINTER TOO). They are
    // asked ahead of the lane's stash because the editor paints LAST, so its
    // run covers whatever the lane pass drew under it — the same
    // last-painted-wins rule the backward walk below applies inside the stash.
    // In practice there is nothing to arbitrate: the two publications cannot
    // overlap. Under the PAYLOAD field the lane pass suppresses that marker
    // whole and it has no resting rect at all; under a BOUND field it publishes
    // one covering what the pass DID draw — the flag box, plus the lower cell
    // beneath an upper field — which ends exactly where the field begins, the
    // riding run starting past the field's far side.
    //
    // IT IS THE LAST PAINTED FRAME'S TRUTH, like the stash below and for the
    // same reason: a reader resolves against the run the user is looking at.
    // (No PRESS reaches this walk while the editor stands since 2026-10-25 —
    // every editor a modal, its veil consuming a press outside the field;
    // until then a riding-cell press closed the editor and resolved its hit
    // here after the close.) The publication is rewritten (and zeroed) by
    // the next paint, which the editor's close damage schedules. It cannot
    // outlive its column into the `h` view either — the editor is
    // keyboard-modal and swallows bare `h`, so the mode cannot be entered with
    // one open, and the mode's own paint zeroes this.
    const FlagHitRect& rc = app.flag_editor_box.riding_cells;
    if (rc.marker_index >= 0 &&
        flag_hit_rect_contains(rc, static_cast<double>(mouse_x),
                               static_cast<double>(mouse_y))) {
        return &rc;
    }
    const double px = static_cast<double>(mouse_x);
    const double py = static_cast<double>(mouse_y);
    for (auto it = app.flag_hit_rects.rbegin();
         it != app.flag_hit_rects.rend(); ++it) {
        const FlagHitRect& r = *it;
        if (px >= r.tri_x && px < r.tri_x + r.tri_w &&
            py >= r.tri_y && py < r.tri_y + r.tri_h) {
            if (on_triangle) *on_triangle = true;
            return &r;
        }
    }
    for (auto it = app.flag_hit_rects.rbegin();
         it != app.flag_hit_rects.rend(); ++it) {
        const FlagHitRect& r = *it;
        if (px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h)
            return &r;
    }
    return nullptr;
}

MarkerCell hit_test_flag_cell(const AppState& app, const GuiAudio& audio,
                              int mouse_x, int mouse_y) {
    (void)audio;
    bool on_triangle = false;
    const FlagHitRect* r =
        topmost_flag_rect(app, mouse_x, mouse_y, &on_triangle);
    // THE PAINTER'S OWN BOUNDARIES, never a re-derivation: each is the box
    // start of the segment it introduces, and each collapses onto the label's
    // right edge where that segment did not paint (FlagHitRect's contract),
    // so the walk from the rightmost segment inward can only answer a segment
    // with pixels — a cell-less label answers Payload everywhere by
    // construction. THE TRIANGLE IS THE MARKER'S OWN, never a cell: a press
    // on it answers Payload whatever the boundaries say.
    if (!r || on_triangle) return MarkerCell::Payload;
    const double x = static_cast<double>(mouse_x);
    if (x >= r->iter_upper_boundary_x) return MarkerCell::Upper;
    if (x >= r->iter_lower_boundary_x) return MarkerCell::Lower;
    return MarkerCell::Payload;
}

int hit_test_flag(const AppState& app, const GuiAudio& audio,
                  int mouse_x, int mouse_y) {
    (void)audio;
    // THE PAINTER'S STASH IS THE HIT GEOMETRY (row 5, 2026-08-01). A marker box
    // is as wide as its SHAPED label, so there is no formula to re-derive it
    // from — recomputing here would mean a second HarfBuzz pass that could
    // disagree with the pixels. The flag-cache rebuild publishes
    // app.flag_hit_rects as it paints — staged, and promoted at the frame that
    // blits the surface it drew (architect 2026-09-24; contract at the field)
    // — which also settles the event-synchronised-hit-geometry question
    // outright: the rects ARE the rects on screen, on the displayed basis
    // those pixels were laid out against, flipping at the blit rather than by
    // two derivations agreeing. The old live rebuild — item_viewport_basis + the displayed map +
    // the drag overlay, threaded into compute_flag_hit_rects — is gone with the
    // functions it called.
    //
    // Cold (nothing painted yet) the stash is empty and nothing is clickable,
    // which is the honest answer: a flag with no pixels has no box to grab.
    //
    // THE SHAPE IS TWO RECTS (2026-10-09, architect: "the union of the
    // triangle and the label's box as painted"): the cue's chip's box — the
    // outlined staircase's widest row over its rows (2026-11-04) — and its
    // label's box. No slope test: the chip's box is its hit.
    //
    // Z-ORDER is the cue painter's (topmost_flag_rect above): chips over
    // labels, the rightmost chip on top, the rightmost label on top.
    // Selection lifts nothing (it is the chip's fill, not a z-rule), so this
    // is the whole arbitration.
    const FlagHitRect* r = topmost_flag_rect(app, mouse_x, mouse_y);
    return r ? r->marker_index : -1;
}

// (hit_test_marker_stem IS DELETED — architect 2026-08-12, the seventh glass
// ruling: marker stems are pointer-inert in all contexts, the flag box being
// the marker's one pointer surface. Its two callers — the live views' plain
// stem click and the `h` view's diff-flag stem click — died with it, and
// kMarkerStemGrabPx with them. The marker_stems stash it read survives as the
// stem PAINTER's input alone.)

// THE WALK'S SEAT — contract at the declaration (app_state.h).
int marker_walk_current_stop(const AppState& a, const GuiAudio& audio) {
    // Both columns through the one store selector pair
    // (active_marker_count / active_marker_time_frame, app_state.h).
    const int n = active_marker_count(a);
    const int last = a.last_selected_marker;
    if (last < 0 || last >= n) return -1;
    const int64_t src_f = active_marker_time_frame(a, last);
    return source_frame_to_active_domain(a, audio, src_f) ==
                   a.playhead_cursor_sample
               ? last
               : -1;
}

// Promoted from a lambda in main(). The captured `app`
// reference is now an explicit argument.
// THE MARKER WALK'S LANDING — contract at the declaration (app_state.h). This
// body is Selection::cycle_selection's own scan, hoisted whole on 2026-08-30
// so the act and the walk button's face
// read one landing; the act calls it and selects what it returns.
int marker_walk_landing(const AppState& a, const GuiAudio& audio,
                        bool forward) {
    const std::vector<GuiWarpMarker>& warp_vec = a.warpmarkers.markers();
    const std::vector<GuiPhaseResetMarker>& phase_reset_vec =
        a.phaseresetmarkers.markers();
    const int n = active_marker_count(a);
    // frame_of / is_disabled are only asked for indices in [0, n), so an
    // empty store simply yields no candidate. Frames are read in the ACTIVE
    // domain — source view is the identity, target view forward-translates
    // through the live map — so they compare with the playhead's frame.
    auto frame_of = [&](int i) -> int64_t {
        return source_frame_to_active_domain(a, audio,
                                             active_marker_time_frame(a, i));
    };
    // The warp side asks the one participation owner (effective_disabled); a
    // phase reset reads its own bit (the column has no labels).
    auto is_disabled = [&](int i) -> bool {
        switch (a.active_markers_view) {
            case 'W': return effective_disabled(warp_vec, i);
            case 'P': return phase_reset_vec[i].disabled;
        }
        return false;
    };
    // The playhead frame is the sole cycle anchor. Strict frame inequalities
    // in the scan prevent re-landing on the stop being stood on; markers
    // sharing one active-domain frame are traversed by the in-group step so
    // every member is reachable (stacks are legal at rest). Disabled markers
    // are skipped as if absent. Trim bounds are not stops.
    const int64_t ph_f = a.playhead_cursor_sample;
    // Current stop: the focused marker when it sits on the playhead frame (a
    // playhead moved elsewhere breaks the equality and disables the in-group
    // step naturally) — the ONE seat owner above.
    const int cur_marker = marker_walk_current_stop(a, audio);
    // In-group step first: one place within the shared frame in the walk
    // direction (ascending index forward, descending backward).
    if (cur_marker >= 0) {
        if (forward) {
            for (int i = cur_marker + 1; i < n; ++i) {
                if (frame_of(i) != ph_f) break;   // frame-sorted: group ends
                if (is_disabled(i)) continue;
                return i;
            }
        } else {
            for (int i = cur_marker - 1; i >= 0; --i) {
                if (frame_of(i) != ph_f) break;
                if (is_disabled(i)) continue;
                return i;
            }
        }
    }
    // Frame scan: the nearest enabled marker strictly past the playhead in
    // the walk direction — frame-sorted, so the first in-direction hit is it.
    if (forward) {
        for (int i = 0; i < n; ++i) {
            if (frame_of(i) > ph_f && !is_disabled(i)) return i;
        }
    } else {
        for (int i = n - 1; i >= 0; --i) {
            if (frame_of(i) < ph_f && !is_disabled(i)) return i;
        }
    }
    return -1;   // nothing ahead
}

PayloadEligibility payload_eligibility(const AppState& app,
                                       const GuiAudio& audio, int idx) {
    using E = PayloadEligibility;
    if (idx < 0) return E::NoResolvedValue;
    // THE COLUMN: the value pair is the WARP column's alone — a phase reset
    // carries no tempo.
    if (app.active_markers_view != 'W') return E::NoResolvedValue;
    const auto& mv = app.warpmarkers.markers();
    if (idx >= static_cast<int>(mv.size())) return E::NoResolvedValue;
    const auto& m = mv[idx];
    // This gates the VALUE PAIR — Ctrl+Shift+J, which copies the focused
    // marker's resolved value, and Ctrl+J, which jumps to the marker that value
    // came from — a marker's OWN value being written on its flag regardless
    // of eligibility. NEITHER ACT MAY REPORT A TEMPO THE RENDER NEVER
    // APPLIES, and the render's three ways of not applying one are the
    // gate's three refusals past the focus and the column:
    //   * the DISABLED marker — resolve_warp_markers_for_render drops it
    //     outright (its own flag alone since 2026-10-29; a ref under a
    //     disabled definition is live, normalized to 1.00 with no source)
    //     — asked of the one owner effective_disabled (warpmarkers.h), the
    //     same call the diff lane, the sweep and the group step read; the
    //     gate hand-rolled the walk beside it until 2026-09-02;
    //   * an OWNER has no resolved value to reach for — its flag shows its
    //     number — so only a pass or a ref goes on;
    //   * the COLLAPSED STACK — a pass or a ref that is a member of a
    //     coincident-collapsed group. The render replaces the stack with one
    //     synthetic 1.00 owner, and the composer, by the ruled
    //     authored/display split, resolves such a member against the RAW
    //     store (marker_effective's second basis) — so the value it would
    //     hand `j` is the authored owner's, which the render never applies.
    //     The membership is the red-flag cache's own `collapsed` subset,
    //     pass 1 of warp_red_flag_set_cached; the cache keys on the audio
    //     identity, which is why the gate takes it. Not the whole red set,
    //     which is a paint cue: its pass-3 coincidence (a frame shared with a
    //     disabled row) is no render normalization at all, and its pass-2
    //     members (a dangling ref, an extreme-ratio ref) resolve against
    //     the projection and so
    //     already read out as the render's own 1.00 or as the empty payload
    //     the acts refuse — refusing them here as a stack would name the
    //     wrong reason. This refusal carries its own sentence
    //     (kValueInCollapsedStack); the two above share the acts' own.
    // Iteration mode is not a term (2026-09-02): the readout-era line
    // that refused under it survived the readout and made `j` card "no
    // resolved value" on a marker that had one. A ref whose definition is
    // missing entirely stays Eligible — resolved_marker_payload already
    // yields an empty string for that case and both acts refuse an empty
    // payload, so it never surfaces a stale tempo.
    if (effective_disabled(mv, idx)) return E::NoResolvedValue;
    if (!(m.tempo_inherits || !m.label_ref.empty()))
        return E::NoResolvedValue;
    const std::set<int>& collapsed = warp_red_flag_set_cached(
        app, audio.sample_rate(),
        static_cast<long>(audio.total_frames())).collapsed;
    if (collapsed.count(idx)) return E::CollapsedStack;
    return E::Eligible;
}

bool payload_eligible_marker(const AppState& app, const GuiAudio& audio,
                             int idx) {
    return payload_eligibility(app, audio, idx) == PayloadEligibility::Eligible;
}

// THE CUE'S HOVER TOOLTIP — the three contracts are at the declarations
// (app_state.h); the model at AppState::RedesignTooltip.
std::string marker_tooltip_value(const AppState& app, const GuiAudio& audio,
                                 int idx) {
    if (app.history_mode.active || app.loading) return "";
    if (app.active_markers_view != 'W') return "";
    const int64_t total = audio.total_frames();
    if (total <= 0) return "";
    const auto& mv = app.warpmarkers.markers();
    if (idx < 0 || idx >= static_cast<int>(mv.size())) return "";
    AppState::MarkerTooltipValueCache& c = app.marker_tooltip_value_cache;
    const long long gen = app.warpmarkers.generation();
    const int sr = audio.sample_rate();
    if (c.valid && c.markers_gen == gen && c.index == idx &&
        c.total_frames == total && c.sample_rate == sr)
        return c.value;
    // THE COPY'S OWN TWO QUESTIONS IN THE COPY'S ORDER
    // (copy_focused_marker_value): the gate, then the composer, its empty
    // answer no value.
    std::string value;
    if (payload_eligibility(app, audio, idx) == PayloadEligibility::Eligible)
        value = resolved_marker_payload(slice_to_warp_markers(mv), idx,
                                        static_cast<long>(total));
    c.valid        = true;
    c.markers_gen  = gen;
    c.index        = idx;
    c.total_frames = total;
    c.sample_rate  = sr;
    c.value        = value;
    return value;
}

// A published cue's face: the bounding box of its label box ∪ its chip,
// either possibly empty, widened to whole pixels.
static GuiRect flag_hit_rect_face(const FlagHitRect& r) {
    double x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0;
    bool any = false;
    const auto take = [&](double x, double y, double w, double h) {
        if (w <= 0.0 || h <= 0.0) return;
        if (!any) {
            x0 = x; y0 = y; x1 = x + w; y1 = y + h;
            any = true;
            return;
        }
        x0 = std::min(x0, x);
        y0 = std::min(y0, y);
        x1 = std::max(x1, x + w);
        y1 = std::max(y1, y + h);
    };
    take(r.x, r.y, r.w, r.h);
    take(r.tri_x, r.tri_y, r.tri_w, r.tri_h);
    if (!any) return GuiRect{0, 0, 0, 0};
    const int ix0 = static_cast<int>(std::floor(x0));
    const int iy0 = static_cast<int>(std::floor(y0));
    const int ix1 = static_cast<int>(std::ceil(x1));
    const int iy1 = static_cast<int>(std::ceil(y1));
    return GuiRect{ix0, iy0, ix1 - ix0, iy1 - iy0};
}

int marker_cue_hit(const AppState& app, int x, int y, GuiRect* face_out) {
    if (app.history_mode.active) return -1;
    const FlagHitRect* r = topmost_flag_rect(app, x, y);
    if (!r || r->marker_index < 0) return -1;
    if (face_out) *face_out = flag_hit_rect_face(*r);
    return r->marker_index;
}

int history_cue_hit(const AppState& app, int x, int y, GuiRect* face_out) {
    if (!app.history_mode.active) return -1;
    const FlagHitRect* r = topmost_flag_rect(app, x, y);
    if (!r || r->marker_index < 0) return -1;
    if (face_out) *face_out = flag_hit_rect_face(*r);
    return r->marker_index;
}

// The topmost rect in the LIVE stash naming index `idx` in ONE INDEX SPACE —
// the warp store's while the `h` view is down (`history` false), the diff
// flags' while it stands (`history` true) — or null where the stash is the
// other space's or no rect names `idx`.
static const FlagHitRect* published_cue_rect(const AppState& app, int idx,
                                             bool history) {
    if (app.history_mode.active != history || idx < 0) return nullptr;
    for (auto it = app.flag_hit_rects.rbegin();
         it != app.flag_hit_rects.rend(); ++it) {
        if (it->marker_index == idx) return &*it;
    }
    return nullptr;
}

GuiRect marker_cue_face(const AppState& app, int idx) {
    const FlagHitRect* r = published_cue_rect(app, idx, /*history=*/false);
    return r ? flag_hit_rect_face(*r) : GuiRect{0, 0, 0, 0};
}

GuiRect history_cue_face(const AppState& app, int idx) {
    const FlagHitRect* r = published_cue_rect(app, idx, /*history=*/true);
    return r ? flag_hit_rect_face(*r) : GuiRect{0, 0, 0, 0};
}

// THE CUE'S BOX SEAT over one published rect, both index spaces' one body.
static GuiRect cue_tooltip_box_rect(const AppState& app, const FlagHitRect* r,
                                    int text_inset, int w, int h) {
    if (!r) return GuiRect{0, 0, 0, 0};
    // THE ANCHOR: the cue's face for the rows (above it, the gap), its x
    // moved so the box's text origin — its left plus `text_inset` — stands
    // on the label's (FlagHitRect::text_x); a cue with no label keeps the
    // face's left, the chip's. Everything else is tooltip_box_rect's.
    GuiRect anchor = flag_hit_rect_face(*r);
    if (anchor.w <= 0 || anchor.h <= 0) return GuiRect{0, 0, 0, 0};
    if (r->has_text_x)
        anchor.x = static_cast<int>(std::nearbyint(r->text_x)) - text_inset;
    return tooltip_box_rect(app, anchor, w, h);
}

GuiRect marker_tooltip_box_rect(const AppState& app, int idx, int text_inset,
                                int w, int h) {
    return cue_tooltip_box_rect(
        app, published_cue_rect(app, idx, /*history=*/false), text_inset, w, h);
}

GuiRect history_tooltip_box_rect(const AppState& app, int idx,
                                 int text_inset, int w, int h) {
    return cue_tooltip_box_rect(
        app, published_cue_rect(app, idx, /*history=*/true), text_inset, w, h);
}

// THE ITEM'S RUNS: the older side's label struck where the item has one, the
// newer side's plain where it has one (the contract at the declaration).
HistoryCueTooltipText history_cue_tooltip_text(const AppState& app,
                                               int idx) {
    if (!app.history_mode.active) return {};
    const std::vector<HistoryDiffFlag>& flags = app.history_mode.flags;
    if (idx < 0 || idx >= static_cast<int>(flags.size())) return {};
    const HistoryDiffFlag& f = flags[static_cast<std::size_t>(idx)];
    HistoryCueTooltipText t;
    if (f.removed) t.struck = f.then_label;
    if (f.added)   t.plain  = f.now_label;
    return t;
}

// THE VALUE'S SOURCE MARKER — the contract and the two readers are at the
// declaration (app_state.h). The composer's out-parameter is written on its
// two success paths and nowhere else (its own contract), so the sentinel
// seeded here comes back unchanged wherever no marker is named; the empty
// payload and the out-of-store belt are the jump's own three-way test,
// answered as one "no source" here.
//
// MEMOIZED AT THE OWNER (2026-09-01): one of the two readers is Jump to
// Defining Marker's FACE (jump_to_value_source_actionable), which the tick
// comparator asks every tick and the roster painter asks at every paint of
// its row — and the backend runs the redraw callback once per pending
// damage rectangle, so during playback the scanner's damage would re-run the
// marker copy and the parser composer at paint cadence on an answer that had
// not changed. (From 2026-09-01 until 2026-09-29 that seat was the Copy
// resolved value button's tooltip, whose shift line named the jump.) The key
// is exactly what this body reads (the fields are at
// AppState::ValueSourceMarkerCache): the focused index, the
// warp store's generation — the store's own change token, which the red-flag
// memos and the flag-cache fingerprint key on already, and which the A/B tabs
// cannot dodge because they share this one store — and the frame count. No
// mutator calls an invalidation; a stale key is the whole invalidation.
int value_source_marker(const AppState& app, int64_t total_frames) {
    const int idx = app.last_selected_marker;
    const auto& mv = app.warpmarkers.markers();
    AppState::ValueSourceMarkerCache& c = app.value_source_marker_cache;
    const long long gen = app.warpmarkers.generation();
    if (c.valid && c.markers_gen == gen && c.focus == idx &&
        c.total_frames == total_frames) {
        return c.source;
    }
    int answer = -1;
    if (idx >= 0 && idx < static_cast<int>(mv.size())) {
        int source = -1;
        const std::string payload = resolved_marker_payload(
            slice_to_warp_markers(mv), idx, total_frames, &source);
        if (!payload.empty() && source >= 0 &&
            source < static_cast<int>(mv.size())) {
            answer = source;
        }
    }
    c.valid        = true;
    c.markers_gen  = gen;
    c.focus        = idx;
    c.total_frames = total_frames;
    c.source       = answer;
    return answer;
}
