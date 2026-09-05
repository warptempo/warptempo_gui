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
// open payload editor's riding cells, and that too is why it is one body: a
// riding cell must resolve to the marker and the cell a resting one resolves
// to, and the only way to be sure of that is to answer both out of the same
// walk with the same boundary idiom.
static const FlagHitRect* topmost_flag_rect(const AppState& app,
                                            int mouse_x, int mouse_y) {
    // THE OPEN PAYLOAD EDITOR'S RIDING CELLS ARE ASKED FIRST — the marker's own
    // bound cells and measure box, re-painted at the unrolled field's right
    // edge by the editor's painter and published there as a flag rect of their
    // own (FlagEditorBox::riding_cells, render.h; architect 2026-09-05, THE
    // RIDING CELLS ARE THE MARKER'S OWN CELLS FOR THE POINTER TOO). They are
    // asked ahead of the lane's stash because the editor paints LAST, so its
    // run covers whatever the lane pass drew under it — the same
    // last-painted-wins rule the backward walk below applies inside the stash.
    // There is nothing of this marker to arbitrate against in any case: the
    // flag pass suppresses the edited marker whole, cells and measure with the
    // box, so its resting rect is absent for exactly as long as this one
    // stands.
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
    // rightmost box inward can only answer a box with pixels — a cell-less,
    // measureless flag answers Payload everywhere by construction.
    if (!r) return MarkerCell::Payload;
    const double x = static_cast<double>(mouse_x);
    if (x >= r->measure_boundary_x)    return MarkerCell::Measure;
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
    // so this is the whole arbitration — one pass, no class split. The measure
    // box needs no rule of its own here either: it is part of the same rect, so
    // a later flag covering an earlier measure's tail resolves to the later
    // marker exactly as the pixels say.
    const FlagHitRect* r = topmost_flag_rect(app, mouse_x, mouse_y);
    return r ? r->marker_index : -1;
}

// (hit_test_marker_stem IS DELETED — architect 2026-08-12, the seventh glass
// ruling: marker stems are pointer-inert in all contexts, the flag box being
// the marker's one pointer surface. Its two callers — the live views' plain
// stem click and the `h` view's diff-flag stem click — died with it, and
// kMarkerStemGrabPx with them. The marker_stems stash it read survives as the
// stem PAINTER's input alone.)

// Promoted from a lambda in main(). The captured `app`
// reference is now an explicit argument.
// THE MARKER WALK'S LANDING — contract at the declaration (app_state.h). This
// body is Selection::cycle_selection's own scan, hoisted whole on 2026-08-30
// (planner decision 59) so the act and the Walk previous / Walk next faces
// read one landing; the act calls it and selects what it returns.
int marker_walk_landing(const AppState& a, const GuiAudio& audio,
                        bool forward) {
    const bool phase_reset = (a.active_markers_view == 'P');
    const std::vector<GuiWarpMarker>& warp_vec = a.warpmarkers.markers();
    const std::vector<GuiPhaseResetMarker>& phase_reset_vec =
        a.phaseresetmarkers.markers();
    const int n = phase_reset ? static_cast<int>(phase_reset_vec.size())
                              : static_cast<int>(warp_vec.size());
    // frame_of / is_disabled are only asked for indices in [0, n), so an
    // empty store simply yields no candidate. Frames are read in the ACTIVE
    // domain — source view is the identity, target view forward-translates
    // through the live map — so they compare with the playhead's frame.
    auto frame_of = [&](int i) -> int64_t {
        const int64_t src_f = phase_reset ? phase_reset_vec[i].time_frame
                                          : warp_vec[i].time_frame;
        return source_frame_to_active_domain(a, audio, src_f);
    };
    // The warp side respects the label_ref cascade; a phase reset reads its
    // own bit.
    auto is_disabled = [&](int i) -> bool {
        return phase_reset ? phase_reset_vec[i].disabled
                           : effective_disabled(warp_vec, i);
    };
    // The playhead frame is the sole cycle anchor. Strict frame inequalities
    // in the scan prevent re-landing on the stop being stood on; markers
    // sharing one active-domain frame are traversed by the in-group step so
    // every member is reachable (stacks are legal at rest). Disabled markers
    // are skipped as if absent. Trim bounds are not stops.
    const int64_t ph_f = a.playhead_cursor_sample;
    // Current stop: the focused marker when it sits on the playhead frame (a
    // playhead moved elsewhere breaks the equality and disables the in-group
    // step naturally).
    int cur_marker = -1;
    {
        const int last = a.last_selected_marker;
        if (last >= 0 && last < n && frame_of(last) == ph_f) cur_marker = last;
    }
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
    //     identity, which is why the gate takes it. Not the whole red set:
    //     its pass-2 members (a dangling ref, an extreme-ratio ref, a pass
    //     whose walk ended on a ref) resolve against the projection and so
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

// -- The overview strip's column mapping ------------------------------------
//
// The one owner of the lane's frames-per-column scale and of the two
// conversions built on it (contracts at the declarations, app_state.h). The
// data is the SOURCE domain, always — the ruled choice recorded at
// paint_overview_strip — so the scale is audio.total_frames() over the lane's
// width, and active-domain values pass through the memoized warp-map
// translators (warp_frame_map_view) on their way to or from it.
//
// THE OVERVIEW LANE IS A CELL LANE (architect 2026-09-02, one rounding rule
// per class: GRID points take nearbyint, CELLS take the containing-pixel rule,
// which the 2026-08-25 ruling fixed as FLOOR — and THE LANE DECLARES WHICH
// CLASS IT IS, once, here, for the tick and the box alike). It is a PARTITION,
// not a lattice: the lane's `lane.w` columns cut the whole piece into bins of
// `spp` source frames each, so a source frame s does not name a NEAREST column
// — it names THE COLUMN THAT CONTAINS IT, floor(s / spp)
// (overview_column_containing below). The picture underneath already reads
// that way: the bars draw column c as the half-open source interval
// [c*spp, (c+1)*spp) (render_waveform's carried-endpoint chain, whose edges are
// quantized to whole frames with nearbyint — a sub-frame difference from the
// exact boundaries here, invisible where one column is thousands of frames),
// and so does the inverse road, overview_anchor_sample_at_x returning
// column * spp, the bin's own origin.
//
// WHAT THE CLASS BUYS is the containment two independent nearest-point
// roundings could not give. The defect it closes: the tick rounded the
// playhead and the box rounded its two viewport endpoints, so with the
// playhead INSIDE the viewport (p < vp_end) nearbyint(p / spp) could equal
// nearbyint(vp_end / spp) = x1 — one column right of the box's own right
// outline, which the painter draws at x1 - 1.
//
// THE PROOF, OVER THE ACTUAL ROUNDED TRANSFORMATIONS (restated after codex
// round 2 found the first telling insufficient — it argued the ordering in the
// source domain and so silently assumed the crossing commuted with the minus
// one, which it does not). Write M for active_domain_to_source_frame, the ONE
// crossing both readings take: identity in source view, and in target view
// map_target_to_source under snap_authored_frame's nearbyint. M is MONOTONE
// NON-DECREASING (the warp map is increasing, nearbyint is monotone) but NOT
// injective — a slow segment gives many active frames one source frame — which
// is exactly why the box must cross the same value the tick can cross rather
// than an adjacent one.
//
//   the tick:  p is an active-domain frame, nearbyint of the scanner's
//              fractional position, with vp_start <= p <= vp_end - 1 (the
//              hypothesis: the playhead is inside the viewport, whose end is
//              exclusive). Its column is floor(M(p) / spp).
//   the box:   x0 = floor(M(vp_start) / spp) and
//              x1 = floor(M(vp_end - 1) / spp) + 1 — the column containing the
//              LAST VISIBLE frame, plus one, the span staying half-open. The
//              minus one is taken in the ACTIVE domain, BEFORE the crossing.
//
// M monotone on vp_start <= p <= vp_end - 1 gives
// M(vp_start) <= M(p) <= M(vp_end - 1); floor is monotone and spp > 0, so
// x0 <= floor(M(p) / spp) <= x1 - 1: A PLAYHEAD INSIDE THE VIEWPORT IS INSIDE
// THE PAINTED BOX BY CONSTRUCTION, monotonicity alone carrying it and
// injectivity never needed. The lane clamps that follow only widen the span
// (x1 >= x0 + 1) and clamp both into [0, lane.w], which the tick's own clamp
// mirrors.
//
// WHAT THE HYPOTHESIS IS, EXACTLY, AND THE ONE CASE IT LEAVES OUT (recorded
// 2026-09-02): the proof's "inside the viewport" is FRAME INCLUSION,
// vp_start <= p <= vp_end - 1, and the WAVEFORM's own idea of a visible
// playhead is very slightly wider — render_playhead paints into column 0 for
// a fractional pixel in [-0.5, 0), so a playhead up to half a waveform column
// BEFORE vp_start still shows a line on the plate. In that sliver the
// hypothesis does not hold, and the tick can sit at x0 - 1, one overview
// column left of the box, while the eye sees the line inside the waveform.
// Rare (a fraction of a percent of pan landings at the working zoom, more at
// a half-song box) and derived rather than observed. It is left as it is: the
// structural alternative — the tick reading max(p, vp_start) — would make the
// tick lie about the playhead's own frame to agree with a half-pixel of paint.
//
// THE WAVEFORM'S OWN COLUMNS ARE THE OTHER CLASS AND DO NOT MOVE: the
// authoring lattice is a grid of POINTS (g(c) = nearbyint(c * spp)), where the
// nearest one is the right answer, and every column rule there stays
// nearbyint. Two axes, two classes, each declared where it lives.

// THE LANE'S CELL RULE ON THE LANE'S OWN AXIS: which column contains this
// source frame. It is the SAME RULE as containing_pixel (input_core.h) — a
// cell covers [n, n+1) of its unit and a coordinate names the cell containing
// it, floor, never the nearest boundary — and deliberately not a call to it:
// that owner is the INPUT layer's, for the one conversion from a fractional
// SURFACE coordinate to a window pixel (its inventory is the mouse's, the
// capture ledger's and the touch machine's deliveries), and this layer neither
// includes input_core.h nor hands it a coordinate of that kind. The unit here
// is spp source frames rather than one pixel. A pure scale, not a hit test:
// the two callers own the lane clamps.
static int overview_column_containing(double src_frame, double spp) {
    return static_cast<int>(std::floor(src_frame / spp));
}

double overview_samples_per_pixel(const AppState& a, const GuiAudio& audio) {
    const GuiRect lane = top_overview_row_area(a);
    const int64_t total = audio.total_frames();
    if (lane.w <= 0 || total <= 0) return 0.0;
    return static_cast<double>(total) / static_cast<double>(lane.w);
}

int overview_tick_column(const AppState& a, const GuiAudio& audio,
                         double active_position) {
    const GuiRect lane = top_overview_row_area(a);
    const double spp = overview_samples_per_pixel(a, audio);
    if (spp <= 0.0) return -1;
    // TWO STEPS, TWO CLASSES. POSITION -> FRAME is the GRID class and keeps
    // nearbyint: a frame is a point on the sample grid, and the scanner's
    // fractional position names the nearest one. FRAME -> COLUMN is the lane's
    // CELL class (the block above overview_samples_per_pixel).
    const int64_t active = static_cast<int64_t>(std::nearbyint(active_position));
    const int64_t src = active_domain_to_source_frame(a, audio, active);
    int col = overview_column_containing(static_cast<double>(src), spp);
    if (col < 0) col = 0;
    if (col > lane.w - 1) col = lane.w - 1;
    return col;
}

double overview_anchor_sample_at_x(const AppState& a, const GuiAudio& audio,
                                   int x) {
    const GuiRect lane = top_overview_row_area(a);
    const double spp = overview_samples_per_pixel(a, audio);
    if (spp <= 0.0) return 0.0;
    // THE INVERSE OF THE LANE'S CELL RULE: column c covers the source frames
    // [c*spp, (c+1)*spp), and this returns that bin's ORIGIN — the frame the
    // column's first pixel of ink stands for. Unchanged by the 2026-09-02
    // class declaration, which is what makes it the inverse: the forward rule
    // floors, so the origin is the one point that maps back to the column
    // asked for, and a caller wanting the bin's FAR boundary asks at x + 1
    // (the header's contract, the edge-END drag its one caller).
    //
    // THE ROUND TRIP IS BOUNDED BY THE ORIGIN'S WHOLE-FRAME QUANTIZATION, NOT
    // EXACT (the claim weakened after codex round 2 and given its real bound
    // after round 3, 2026-09-02, which found the one-column figure too small).
    // The origin x*spp is generally FRACTIONAL and becomes a WHOLE SOURCE FRAME
    // under banker's rounding wherever a consumer needs one — inside the
    // target-view arm below, which must hand back a whole ACTIVE-domain frame,
    // and at the teleport's and the box pan's own nearbyint in source view,
    // where this function itself rounds nothing (the edge drag keeps the double
    // through its span-to-level fit). That snap can cross the bin's lower or
    // upper boundary — spp 10.3, column 1, origin 10.3, nearbyint 10 — so the
    // returned coordinate's own rounded inverse lands WITHIN ceil(1/(2*spp))
    // COLUMNS of the column asked for, IN EITHER DIRECTION, plus in target view
    // one whole ACTIVE frame's worth of the map's slope through
    // source_frame_to_active_domain's own rounding.
    //
    // AT spp >= 1 THAT CEILING IS ONE COLUMN, and spp >= 1 is every real
    // source: the lane is ~1000 px wide and a piece is minutes long, so one
    // column is thousands of frames. A SOURCE SHORTER THAN THE LANE IS THE ONLY
    // PRODUCER OF A MULTI-COLUMN MISS and it is ACCEPTED — at spp 0.1 (100
    // frames over a 1000-column lane) column 5's origin 0.5 rounds to frame 0,
    // whose column is 0, five LEFT, while column 6's origin 0.6 rounds to frame
    // 1, whose column is 10, four RIGHT. No road changes for it (no fractional
    // crossing): the box pan measures a GRAB OFFSET through this same reading
    // and so cancels the residue, and the teleport centres on a position whose
    // own column is not re-derived. What the quantization does bound is the
    // ROUND-TRIP CLAIM — see the invariant at apply_overview_drag_at
    // (input_pointer.cpp), which states the same bound.
    const double src = static_cast<double>(x - lane.x) * spp;
    if (a.active_audio_view != 'T') return src;
    // Target view: the pressed column names a SOURCE position (the lane's
    // domain), and the drag body's anchor lives in the ACTIVE domain — map it
    // forward once at the press. The int64 round-trip costs under one source
    // frame here, which is the quantization the paragraph above bounds.
    return static_cast<double>(source_frame_to_active_domain(
        a, audio, static_cast<int64_t>(std::nearbyint(src))));
}

// THE BOX'S TWO EDGES AS ACTIVE-DOMAIN SAMPLES — the wall-clamped viewport
// endpoints the box is drawn from, hoisted out of overview_box_span below
// (2026-08-15) so the EDGE DRAG'S SEAT and the PAINTED box read ONE answer to
// "where is the box's edge", the same shape the region editor's plate basis
// takes. THE END CLAMP IS THE WHOLE REASON IT IS A SEPARATE OWNER: the ruled
// right-wall grid rest deliberately sits up to one waveform pixel PAST the
// song end (max_viewport_start_grid, main.cpp — <1 px of inert past-EOF
// padding, which is what keeps the flush-right rest a true grid point), so at
// that wall alone the RAW viewport end and the PAINTED right edge differ, and a
// gesture whose contract is "the grabbed edge tracks the finger while the
// opposite edge holds" must pivot on the painted one. The begin side needs no
// clamp of its own — clamp_viewport_start floors the start at 0 — and the
// >= 0 line below states that rather than defending against it.
bool overview_box_edge_samples(const AppState& a, const GuiAudio& audio,
                               int64_t* out_begin, int64_t* out_end) {
    const GuiRect area = waveform_area(a);
    const double  spp  = current_samples_per_pixel(a, audio);
    if (area.w <= 0 || spp <= 0.0) return false;
    int64_t b = a.viewport_start_sample;
    int64_t e = viewport_end_sample(b, spp, area.w);
    const int64_t total = live_total_frames(a, audio);
    if (b < 0) b = 0;
    if (total > 0 && e > total) e = total;
    if (e < b) e = b;
    *out_begin = b;
    *out_end   = e;
    return true;
}

// THE VIEWPORT BOX'S LANE COLUMNS — the box arithmetic hoisted whole out of
// paint_overview_strip's box layer when the box grew grab handles (the lane
// rework, 2026-08-12), so painter and hit geometry read ONE derivation and a
// grabbed edge is exactly a painted one. The LIVE viewport's span in the
// active domain (through the edge owner above, which owns the past-EOF end
// clamp), inverse-mapped to source columns in target view (the memoized map —
// the box "does the domain work", the ruled source-domain choice at the
// painter), wall-clamped in the source domain too (the target map's own ends
// are not the active domain's), floored at 1px (the span is never nothing).
// Contract at the declaration (app_state.h).
bool overview_box_span(const AppState& a, const GuiAudio& audio,
                       int* out_x0, int* out_x1) {
    const GuiRect lane = top_overview_row_area(a);
    const double spp_ov = overview_samples_per_pixel(a, audio);
    if (spp_ov <= 0.0) return false;
    int64_t vp_start = 0;
    int64_t vp_end   = 0;
    if (!overview_box_edge_samples(a, audio, &vp_start, &vp_end)) return false;
    // THE LAST VISIBLE FRAME IS NAMED IN THE ACTIVE DOMAIN AND CROSSED THERE
    // (codex round 2, 2026-09-02): vp_end is EXCLUSIVE, so the last visible
    // ACTIVE frame is vp_end - 1 — and it is that frame, not the exclusive
    // endpoint, that goes through the map. The two orders do not commute: the
    // inverse map is monotone but not injective (many active frames share a
    // source frame at a slow segment), so active_domain_to_source_frame(vp_end)
    // can equal active_domain_to_source_frame(vp_end - 1), and subtracting one
    // in the SOURCE domain then dropped a source frame the tick can legally
    // stand on — the tick paints one column right of the box's outline with the
    // playhead inside the viewport. Crossing vp_end - 1 puts the box's end on
    // the tick's OWN conversion at the tick's own endpoint class, which is what
    // makes the containment proof (the class block above) survive the rounding.
    // A degenerate span (vp_end <= vp_start) has no last frame: last_active
    // falls back to vp_start, src_last then clamps onto src_b and x1 lands at
    // x0 + 1, the >= 1px floor's own answer. Source view is the identity arm —
    // vp_end - 1 IS src_e - 1 there — so nothing about it changes.
    const int64_t last_active = vp_end > vp_start ? vp_end - 1 : vp_start;
    int64_t src_b    = vp_start;
    int64_t src_last = last_active;
    if (a.active_audio_view == 'T') {
        src_b    = active_domain_to_source_frame(a, audio, vp_start);
        src_last = active_domain_to_source_frame(a, audio, last_active);
    }
    const int64_t total = audio.total_frames();
    if (src_b < 0) src_b = 0;
    if (src_last > total - 1) src_last = total - 1;
    if (src_last < src_b) src_last = src_b;
    // THE LANE'S CELL CLASS ON BOTH EDGES (the block above
    // overview_samples_per_pixel): the begin column CONTAINS the first visible
    // frame, and the end is the column containing the LAST visible frame plus
    // one, so the span stays half-open [x0, x1) and the painter's right outline
    // at x1 - 1 is that last frame's own column.
    int x0 = overview_column_containing(static_cast<double>(src_b), spp_ov);
    int x1 = overview_column_containing(static_cast<double>(src_last), spp_ov) + 1;
    if (x0 < 0) x0 = 0;
    if (x0 > lane.w - 1) x0 = lane.w - 1;
    if (x1 > lane.w) x1 = lane.w;
    if (x1 < x0 + 1) x1 = x0 + 1;   // >=1px: the span is never nothing
    *out_x0 = x0;
    *out_x1 = x1;
    return true;
}

// The trim's lane columns, the second span the overview paints (architect
// 2026-09-04). The trim bar a lane down is viewport-scaled, so at a zoomed-in
// view the trim's place in the whole song is shown nowhere; this span is that
// picture. There is no domain crossing here — the bounds rest as source frames
// and the lane's data is the source domain in every view — which is the one
// simplification it has over the box above, which inverse-maps the
// active-domain viewport first.
//
// IT HAS TWO FACES AND THEY DIFFER ONLY IN WHAT A FULL WINDOW MEANS, which is
// why one body serves both: overview_trim_span, the trim LINE's, reports
// nothing there — the whole song is not information, so the painter draws no
// line rather than a line the full width of the lane — while
// overview_region_span, the region overlay's, answers the whole content width.
// The recolor must say what the waveform's overlay says, and that overlay
// stands at a full window; a lane painting nothing beside it was an asymmetry
// the architect refused on 2026-09-04 ("it is more distracting to have an
// asymmetry"). The whole-lane answer needs no mapping at all, so the frame to
// column road below stays the one road and the fork costs no second floor.
//
// The span is inclusive rather than half-open, which is the other difference
// from the box: it runs from the column containing the begin bound through the
// column containing the end bound, both under the lane's own cell class (the
// declaration above overview_samples_per_pixel — overview_column_containing is
// the one floor and no caller spells a second). The end bound is itself an
// inclusive authored frame, so its own column is the last one painted, where
// the box's viewport end is exclusive and its span therefore half-open.
//
// The two bounds are ordered on the way out, because a mid-gesture pair may be
// crossed: the store's rest invariant holds only at commit, and this reads the
// live pair on every frame of a drag.
static bool overview_trim_columns(const AppState& a, const GuiAudio& audio,
                                  bool whole_lane_at_full_window,
                                  int* out_x0, int* out_x1) {
    const GuiRect lane = top_overview_row_area(a);
    const double spp_ov = overview_samples_per_pixel(a, audio);
    // A positive spp already carries the lane's width and the piece's length,
    // so the whole-lane arm below can spell lane.w - 1 without a guard of its
    // own (overview_samples_per_pixel returns 0 for either).
    if (spp_ov <= 0.0) return false;
    const int64_t total = audio.total_frames();
    if (trim_is_full_window(a.trim, total)) {
        if (!whole_lane_at_full_window) return false;
        *out_x0 = 0;
        *out_x1 = lane.w - 1;
        return true;
    }
    int64_t b = a.trim.begin_frame;
    int64_t e = a.trim.end_frame;
    if (b > e) { const int64_t t = b; b = e; e = t; }
    if (b < 0) b = 0;
    if (e > total - 1) e = total - 1;
    if (e < b) e = b;
    int x0 = overview_column_containing(static_cast<double>(b), spp_ov);
    int x1 = overview_column_containing(static_cast<double>(e), spp_ov);
    if (x0 < 0) x0 = 0;
    if (x0 > lane.w - 1) x0 = lane.w - 1;
    if (x1 > lane.w - 1) x1 = lane.w - 1;
    if (x1 < x0) x1 = x0;   // >=1px: a sub-window is never nothing
    *out_x0 = x0;
    *out_x1 = x1;
    return true;
}

bool overview_trim_span(const AppState& a, const GuiAudio& audio,
                        int* out_x0, int* out_x1) {
    return overview_trim_columns(a, audio, /*whole_lane_at_full_window=*/false,
                                 out_x0, out_x1);
}

bool overview_region_span(const AppState& a, const GuiAudio& audio,
                          int* out_x0, int* out_x1) {
    return overview_trim_columns(a, audio, /*whole_lane_at_full_window=*/true,
                                 out_x0, out_x1);
}

// The box endcaps' hit test — the trim endcap model on the box outline's two
// edge columns (contract and the TrimHit-return reasoning at the declaration,
// app_state.h). The bands are the 1px edges inflated by the trim bar's own
// grab width; overlap on a narrow box resolves NEAREST EDGE with Begin
// winning the exact tie, the trim sort's own tie-break re-expressed over two
// fixed candidates.
TrimHit hit_test_overview_endcap(const AppState& a, const GuiAudio& audio,
                                 int mouse_x, int mouse_y) {
    const GuiRect lane = top_overview_row_area(a);
    if (mouse_y < lane.y || mouse_y >= lane.y + lane.h) return TrimHit::None;
    if (mouse_x < lane.x || mouse_x >= lane.x + lane.w) return TrimHit::None;
    int x0 = 0;
    int x1 = 0;
    if (!overview_box_span(a, audio, &x0, &x1)) return TrimHit::None;
    const int grab  = trim_endcap_grab_px();
    const int left  = lane.x + x0;       // the outline's left edge column
    const int right = lane.x + x1 - 1;   // its right edge column
    const int dl = std::abs(mouse_x - left);
    const int dr = std::abs(mouse_x - right);
    const bool on_left  = dl <= grab;
    const bool on_right = dr <= grab;
    if (on_left && on_right)
        return dl <= dr ? TrimHit::Begin : TrimHit::End;
    if (on_left)  return TrimHit::Begin;
    if (on_right) return TrimHit::End;
    return TrimHit::None;
}
