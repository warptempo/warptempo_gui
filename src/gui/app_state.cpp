#include "app_state.h"

#include "audio.h"
#include "gui_display_context.h"
#include "paint_handler.h"
#include "render.h"
#include "text_editor.h"
#include "warp_frame_map_build.h"  // resolved_marker_payload (value_source_marker)
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
    // The SHIFT-RANGE ANCHOR is index-shaped live state over the same active
    // column's store, so it FOLLOWS its marker exactly like the focus above
    // rather than dissolving: a reorder does not end a range interaction — a
    // position nudge reorders whenever it carries its marker across a
    // neighbour, mid-interaction — and the anchor survives shift
    // releases, so a stale pre-reorder index would name the wrong row at the
    // next shift-click. -1 (no anchor) passes through mapped() unchanged.
    app.shift_range_anchor = mapped(app.shift_range_anchor);
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
// references are now explicit arguments. The one surviving grab tolerance is
// the trim endcaps' (kTrimEndcapGrabPx, render.h) — there is no shared hit
// half-width any more, and the marker surfaces (the flag boxes) hit on their
// painted rects with no halo.

// Event-synchronized hit map (ruling at the declaration in app_state.h): in
// target view with a warm displayed map, the item hit tests decide against the
// map the LAST COMMITTED frame's flag pixels were painted with (promoted
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
// with on the last committed frame, so the marker/endcap/lane geometry rides the
// same basis the flag/endcap pixels do. The warm spp is (vp_end - vp_start) /
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
    } else {
        const GuiRect area = waveform_area(app);
        const double  spp  = current_samples_per_pixel(app, audio);
        b.vp_start_frame = app.viewport_start_sample;
        b.vp_end_frame   = viewport_end_sample(b.vp_start_frame, spp, area.w);
        b.area_w         = area.w;
    }
    b.vp_start = static_cast<double>(b.vp_start_frame);
    b.spp = b.area_w > 0
        ? static_cast<double>(b.vp_end_frame - b.vp_start_frame) /
          static_cast<double>(b.area_w)
        : 0.0;
    return b;
}

TrimHit hit_test_trim_endcap(const AppState& app, const GuiAudio& audio,
                           int mouse_x, int mouse_y) {
    // Trim bounds hit-test in the AUTHORING views against the active A/B tab's
    // live bounds. Both bounds are always meaningful (the unset state died
    // 2026-07-30 — a full ordered pair always rests), so this reads them
    // directly; the pair gate that used to stand here is gone with the state it
    // tested.
    const int64_t begin_frame = app.trim.begin_frame;
    const int64_t end_frame   = app.trim.end_frame;

    // The bounds are marked by the trim bar's two ENDCAPS (row 5, 2026-08-01 —
    // the square b/e chips and their strip-crossing stems are gone): a narrow
    // full-lane-height column run per bound, edge-anchored on its own column,
    // inside the trim bar lane (top_trim_row_area). A press outside that
    // vertical band is not on an endcap. (The y-gate spanned the merged
    // trim-bar + ruler band for the trim surface arc's one day, 2026-08-11..12,
    // and came back to the lane with the arc's revert; the ruler is the REGION
    // FORMER's band since 2026-08-12. The lane's height is
    // kTrimBarScalePercent-scaled — resting at 100 since the seventh glass
    // ruling, render.h — and this gate follows whatever it reads through the
    // one accessor.)
    const GuiRect row = top_trim_row_area(app);
    if (mouse_y < row.y || mouse_y >= row.y + row.h) return TrimHit::None;

    const GuiRect top = top_strip_area(app);
    // Event-synchronized hit geometry, the VIEWPORT half: the endcap pixels
    // are painted live by the trim pass (GuiPaintHandler::paint_trim ->
    // render_trim_flags) on the DISPLAYED basis, NOT the live viewport. So the
    // cap columns must resolve on the SAME basis (item_viewport_basis)
    // — the same reason hit_test_flag does — else during an async publish window an
    // endcap painted at the OLD column would be grabbed at the NEW/live column.
    // The visibility
    // cull matches the painter's viewport extent (the painter maps against
    // this same {span, width}), so a gutter column at a non-multiple-of-16 window
    // is culled the same in paint and hit-test. Cold falls back to the live
    // basis, matching the painter's cold fallback.
    const ItemViewportBasis basis = item_viewport_basis(app, audio);
    if (basis.spp <= 0.0) return TrimHit::None;
    const int     wave_w   = basis.area_w;
    const int64_t vp_start = basis.vp_start_frame;
    const int64_t vp_end   = basis.vp_end_frame;
    const int sr = audio.sample_rate();
    if (sr <= 0) return TrimHit::None;

    // Column translation so the cap column lands
    // where the cap is painted in the mapped views: the map is
    // the item pixels' own via displayed_or_live_target_map (event-synchronized
    // hit geometry — the ruling at that selector), empty (identity) in source
    // view and the map the flag item cache baked when warm in target view
    // (the live trim pass paints its endcaps through the same selector).
    const std::vector<WarpFrameMapSegment>& dmap =
        displayed_or_live_target_map(app, audio);
    const std::vector<WarpFrameMapSegment>* target_warp_frame_map =
        dmap.empty() ? nullptr : &dmap;

    // Build the same visible candidate list render_trim_flags paints — the same
    // cull (a bound whose column leaves the viewport gets no cap) through the
    // same column owners — and sort it left to right.
    //
    // OVERLAP ARBITRATION IS THIS HIT TEST'S OWN POLICY, not a mirror of
    // painter z-order: render_trim_flags lays the begin cap down and then the
    // end cap, with no sort and no reverse pass, so there is no
    // "topmost-painted" cap to defer to and the caps carry identical colours
    // anyway — the pixels give no cue either verdict could contradict. The rule
    // here is LEFTMOST WINS, with Begin ahead of End at an equal column (the
    // tie-break below): deterministic and stable, and it names the bound a user
    // aiming at the left of an overlapping pair means. Overlap is mostly the
    // GRAB TOLERANCE's doing — the inflated rects reach far past the caps they
    // came from, while the drawn caps themselves can share at most a cap width
    // (see the tie-break).
    struct TrimEndcapHit {
        double  center_x;
        GuiRect rect;
        TrimHit which;
    };
    std::vector<TrimEndcapHit> endcaps;
    auto add_endcap = [&](int64_t frame, TrimHit which) {
        // Map the authored source frame to the displayed domain and resolve its
        // column through the SAME owners the painter uses (render.h): the mapping
        // via displayed_trim_ms, the column via trim_bound_column against the
        // displayed-basis vp span (the painters' quantized-span denominator), the
        // cap rect via trim_endcap_rect. So a hit lands on exactly the drawn cap.
        const double ms = displayed_trim_ms(frame, target_warp_frame_map);
        const TrimBoundColumn c =
            trim_bound_column(ms, vp_start, vp_end, wave_w);
        if (!c.in_viewport) return;
        // THE DRAWN CAP, INFLATED BY THE GRAB TOLERANCE. The rect comes from
        // the one owner so the target is centred on exactly what is painted; the
        // widening is the hit side's own term, because a 2px endcap is below any
        // usable pointing tolerance (the rationale is at trim_endcap_rect).
        GuiRect cr_rect =
            trim_endcap_rect(which == TrimHit::Begin, top.x, c.col, row);
        const int grab = trim_endcap_grab_px();
        cr_rect.x -= grab;
        cr_rect.w += 2 * grab;
        const double center_x = static_cast<double>(top.x + c.col);
        endcaps.push_back({center_x, cr_rect, which});
    };

    add_endcap(begin_frame, TrimHit::Begin);
    add_endcap(end_frame,   TrimHit::End);
    std::sort(endcaps.begin(), endcaps.end(),
              [](const TrimEndcapHit& a, const TrimEndcapHit& b) {
                  if (a.center_x != b.center_x)
                      return a.center_x < b.center_x;
                  // Deterministic tie-break at an equal column: Begin first, so
                  // the forward walk below returns it. The two DRAWN caps are
                  // NOT the same rect there — trim_endcap_rect anchors them in
                  // opposite directions (begin's left edge on the column, end's
                  // right edge on it), so they mirror about the column and share
                  // only it — but they are the same colour, so nothing painted
                  // distinguishes them. This fixes which bound a click in the
                  // inflated overlap grabs, and nothing else.
                  return a.which == TrimHit::Begin && b.which == TrimHit::End;
              });

    // Forward walk = ascending-x = LEFTMOST FIRST, the policy stated above. The
    // first cap whose inflated [rect.x, rect.x + w) contains mouse_x wins.
    for (const TrimEndcapHit& endcap : endcaps) {
        if (mouse_x >= endcap.rect.x &&
            mouse_x < endcap.rect.x + endcap.rect.w) {
            return endcap.which;
        }
    }
    return TrimHit::None;
}

bool point_in_trim_bridge_span(const AppState& app, const GuiAudio& audio,
                               int mouse_x, int mouse_y) {
    if (audio.total_frames() <= 0) return false;
    // The TRIM BAR LANE ONLY — the band the bar and its endcaps paint in, and
    // the exact band hit_test_trim_endcap gates on. A top-strip point BELOW it
    // (the ruler, then the marker lane) is not the bridge handle.
    const GuiRect row = top_trim_row_area(app);
    if (mouse_y < row.y || mouse_y >= row.y + row.h) return false;

    // Event-synchronized geometry, the VIEWPORT half: the bar's pixels are
    // painted live (paint_trim) on the DISPLAYED basis, so the columns resolve on
    // that same basis and never on the live viewport — else during an async
    // publish window a point on the visible bridge could answer false (or a blank
    // point true). Cold falls back to the live basis, matching the painter's.
    const ItemViewportBasis basis = item_viewport_basis(app, audio);
    if (basis.spp <= 0.0) return false;

    // click_rel_x is waveform-relative from the layout origin area.x (a stable
    // layout constant, not viewport-driven); the gap interval is 0-based columns
    // in the SAME committed-width column space, so the test compares like against
    // like.
    const GuiRect area = waveform_area(app);
    const int click_rel_x = mouse_x - area.x;
    const std::vector<WarpFrameMapSegment>& dmap =
        displayed_or_live_target_map(app, audio);
    const std::vector<WarpFrameMapSegment>* map = dmap.empty() ? nullptr : &dmap;
    auto bound_column = [&](int64_t frame) -> TrimBoundColumn {
        const double ms = displayed_trim_ms(frame, map);
        return trim_bound_column(ms, basis.vp_start_frame, basis.vp_end_frame,
                                 basis.area_w);
    };
    const TrimBoundColumn bc = bound_column(app.trim.begin_frame);
    const TrimBoundColumn ec = bound_column(app.trim.end_frame);
    // The owner already handles the offscreen-flush edges (no endcap-width inset
    // for an unpainted bound), so this needs no min/max of its own.
    const TrimBridgeGap gap =
        trim_bridge_gap(bc, ec, trim_endcap_w_px(), basis.area_w);
    // The [0, area_w) gate — the SAME effective-width clip the PAINTER applies,
    // so paint and hit agree exactly in the inert right gutter.
    return click_rel_x >= 0 && click_rel_x < basis.area_w &&
           click_rel_x >= gap.lo && click_rel_x < gap.hi;
}

// The topmost published flag rect under the point, or nullptr — the ONE walk
// both public answers below take, so "which marker" and "which box of its run"
// cannot disagree. It reads TWO publications, the lane pass's stash and the
// open marker-lane editor's riding boxes, and that too is why it is one body: a
// riding box must resolve to the marker and the cell a resting one resolves
// to, and the only way to be sure of that is to answer both out of the same
// walk with the same boundary idiom.
static const FlagHitRect* topmost_flag_rect(const AppState& app,
                                            int mouse_x, int mouse_y) {
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
    // same reason: the press that closes the editor resolves its marker hit
    // AFTER the close, in the same event, and what it must resolve against is
    // the run the user pressed on. The publication is rewritten (and zeroed)
    // by the next paint, which the close's own damage schedules. It cannot
    // outlive its column into the `h` view either — the editor is
    // keyboard-modal and swallows bare `h`, so the mode cannot be entered with
    // one open, and the mode's own paint zeroes this.
    const FlagHitRect& rc = app.flag_editor_box.riding_cells;
    if (rc.marker_index >= 0 &&
        mouse_x >= rc.x && mouse_x < rc.x + rc.w &&
        mouse_y >= rc.y && mouse_y < rc.y + rc.h) {
        return &rc;
    }
    for (auto it = app.flag_hit_rects.rbegin();
         it != app.flag_hit_rects.rend(); ++it) {
        const FlagHitRect& r = *it;
        if (mouse_x >= r.x && mouse_x < r.x + r.w &&
            mouse_y >= r.y && mouse_y < r.y + r.h) {
            return &r;
        }
    }
    return nullptr;
}

MarkerCell hit_test_flag_cell(const AppState& app, const GuiAudio& audio,
                              int mouse_x, int mouse_y) {
    (void)audio;
    const FlagHitRect* r = topmost_flag_rect(app, mouse_x, mouse_y);
    // THE PAINTER'S OWN BOUNDARIES, never a re-derivation: each is the seam
    // column of the box it introduces, and each collapses onto the next where
    // that box did not paint (FlagHitRect's contract), so the walk from the
    // rightmost box inward can only answer a box with pixels — a cell-less
    // flag answers Payload everywhere by construction.
    if (!r) return MarkerCell::Payload;
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
    // app.flag_hit_rects as it paints (contract at the field), which also
    // settles the event-synchronised-hit-geometry question outright: the rects
    // ARE the painted rects, on the displayed basis those pixels were laid out
    // against, for free and at every moment rather than by two derivations
    // agreeing. The old live rebuild — item_viewport_basis + the displayed map +
    // the drag overlay, threaded into compute_flag_hit_rects — is gone with the
    // functions it called.
    //
    // Cold (nothing painted yet) the stash is empty and nothing is clickable,
    // which is the honest answer: a flag with no pixels has no box to grab.
    //
    // THE SHAPE IS A PLAIN RECT. The fused tip-down triangle below the old flag
    // — and its slope test through flag_triangle_half_width_at — died with the
    // triangle lane; a marker is one box in one lane now.
    //
    // Z-ORDER: the painter walks the store FORWARD and later boxes cover
    // earlier ones, so the topmost box under a point is the LAST containing
    // rect. Walk backwards and take the first hit (topmost_flag_rect above).
    // Selection no longer lifts anything (it is a colour swap, not a z-rule),
    // so this is the whole arbitration — one pass, no class split; a later
    // flag covering an earlier flag's cells resolves to the later marker
    // exactly as the pixels say.
    const FlagHitRect* r = topmost_flag_rect(app, mouse_x, mouse_y);
    return r ? r->marker_index : -1;
}

// (hit_test_marker_stem IS DELETED — architect 2026-08-12, the seventh glass
// ruling: marker stems are pointer-inert in all contexts, the flag box being
// the marker's one pointer surface. Its two callers — the live views' plain
// stem click and the `h` view's diff-flag stem click — died with it, and
// kMarkerStemGrabPx with them. The marker_stems stash it read survives as the
// stem PAINTER's input alone.)

// THE WALK'S SEAT — contract at the declaration (app_state.h). Factored out of
// marker_walk_landing on 2026-09-10, when the cell walk landed and needed the
// very same question ahead of the landing: "am I standing on a marker?" now
// has one spelling for the in-group step and for the cell step alike.
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
// (planner decision 59) so the act and the walk button's face
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
    // The warp side respects the label_ref cascade; a phase reset reads its
    // own bit (the column has no labels).
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
    // step naturally) — the ONE seat owner above, which marker_walk_step asks
    // the same question of before it decides whether the step leaves this
    // marker at all.
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

// ONE TAB / SHIFT+TAB STEP, WHOLE — contract at the declaration (app_state.h).
// The body is a rank walk over the boxes of the SEAT and a fall to the marker
// step, in that order, and it is the only place the two are composed.
//
// THE CELLS ARE ASKED OF THE PAINTER'S OWN PREDICATE (marker_paints_iter_cells,
// app_state.h) and of nothing else, on the ACTIVE column, so a marker whose
// flag shows no cells is walked exactly as it was before grid iterations
// existed: with the mode dark the predicate is false everywhere, every arm
// below falls straight through, and this returns the landing owner's answer
// with a Payload cell — the pre-2026-09-10 walk, unchanged.
MarkerWalkStep marker_walk_step(const AppState& a, const GuiAudio& audio,
                                bool forward) {
    const char column = a.active_markers_view;
    const int  stop   = marker_walk_current_stop(a, audio);
    if (stop >= 0) {
        if (marker_paints_iter_cells(a, column, stop)) {
            // The seat's own boxes, in painted order: payload, lower, upper.
            // Forward off the upper leaves the marker; backward off the
            // payload does.
            if (forward) {
                switch (a.addressed_cell) {
                case MarkerCell::Payload:
                    return {stop, MarkerCell::Lower, true};
                case MarkerCell::Lower:
                    return {stop, MarkerCell::Upper, true};
                case MarkerCell::Upper:
                    break;
                }
            } else {
                switch (a.addressed_cell) {
                case MarkerCell::Upper:
                    return {stop, MarkerCell::Lower, true};
                case MarkerCell::Lower:
                    return {stop, MarkerCell::Payload, true};
                case MarkerCell::Payload:
                    break;
                }
            }
        }
        // A FLAG THAT PAINTS NO CELLS HAS NO ARM HERE AT ALL. With no
        // purple row to walk there is nothing to step THROUGH, so the seat
        // falls whole to the marker step below and the walk is byte-identical
        // to the pre-2026-09-10 walk (architect: "with iterations mode off
        // the tab is unchanged").
    }
    // THE MARKER STEP: the landing owner's answer, untouched.
    const int m = marker_walk_landing(a, audio, forward);
    if (m < 0) return {};
    // SHIFT+TAB ENTERS A MARKER FROM ITS RIGHT, so it comes to rest on the
    // rightmost walkable box the flag actually paints — the upper cell where
    // there are cells, the payload where there are none. Tab enters from the
    // left and always rests on the payload, which is also the axis every
    // focus write seats by itself (Selection::seat_focus).
    if (!forward && marker_paints_iter_cells(a, column, m))
        return {m, MarkerCell::Upper, false};
    return {m, MarkerCell::Payload, false};
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
    // This gates the VALUE PAIR — bare `j`, which copies the focused marker's
    // resolved value, and Shift+`j`, which jumps to the marker that value
    // came from — a marker's OWN value being written on its flag regardless
    // of eligibility. NEITHER ACT MAY REPORT A TEMPO THE RENDER NEVER
    // APPLIES, and the render's three ways of not applying one are the
    // gate's three refusals past the focus and the column:
    //   * the CASCADE — resolve_warp_markers_for_render drops a disabled
    //     marker outright and drops a label ref whose definition is disabled
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
    // Iteration mode is not a term (2026-09-02, R-16): the readout-era line
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

// THE VALUE'S SOURCE MARKER — the contract and the two readers are at the
// declaration (app_state.h). The composer's out-parameter is written on its
// two success paths and nowhere else (its own contract), so the sentinel
// seeded here comes back unchanged wherever no marker is named; the empty
// payload and the out-of-store belt are the jump's own three-way test,
// answered as one "no source" here.
//
// MEMOIZED AT THE OWNER (codex round A, 2026-09-01): one of the two readers is
// the Copy resolved value button's hint, which the tooltip painter asks inside
// the redraw callback — and the Wayland backend runs that callback once per
// pending damage rectangle, so with the hint up during playback the scanner's
// damage re-ran the marker copy and the parser composer several times a frame
// on an answer that had not changed. The key is exactly what this body reads
// (the fields are at AppState::ValueSourceMarkerCache): the focused index, the
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
