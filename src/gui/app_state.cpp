#include "app_state.h"

#include "audio.h"
#include "gui_display_context.h"
#include "paint_handler.h"
#include "render.h"
#include "text_editor.h"
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
// both public answers below take, so "which box" and "which half of it" cannot
// disagree.
static const FlagHitRect* topmost_flag_rect(const AppState& app,
                                            int mouse_x, int mouse_y) {
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

MarkerClickSpan hit_test_flag_span(const AppState& app, const GuiAudio& audio,
                                   int mouse_x, int mouse_y) {
    (void)audio;
    const FlagHitRect* r = topmost_flag_rect(app, mouse_x, mouse_y);
    // THE PAINTER'S OWN BOUNDARY, never a re-derivation: it is the flag box's
    // right edge, and it equals the rect's right edge whenever no measure box
    // painted — so a measureless flag answers Flag everywhere by construction.
    if (!r) return MarkerClickSpan::Flag;
    return (static_cast<double>(mouse_x) >= r->measure_boundary_x)
        ? MarkerClickSpan::Measure : MarkerClickSpan::Flag;
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
bool payload_eligible_marker(const AppState& app, int idx) {
    if (idx < 0) return false;
    if (app.active_markers_view != 'W') return false;
    if (app.iteration_mode_enabled) return false;
    const auto& mv = app.warpmarkers.markers();
    if (idx >= static_cast<int>(mv.size())) return false;
    const auto& m = mv[idx];
    // This gates the VALUE PAIR — bare `j`, which copies the focused marker's
    // resolved value, and Shift+`j`, which jumps to the marker that value
    // came from — a marker's OWN value being written on its flag regardless
    // of eligibility. Render resolution
    // (resolve_warp_markers_for_render) drops disabled markers outright and
    // drops label refs whose definition is disabled (the cascade). Neither
    // act may report a tempo the render never applies, so eligibility mirrors
    // both drops here: a disabled marker is ineligible, and a ref to a
    // disabled definition is ineligible. A ref whose definition is missing
    // entirely stays eligible — resolved_marker_payload already yields an
    // empty string for that case and both acts refuse an empty payload, so it
    // never surfaces a stale tempo.
    if (m.disabled) return false;
    if (!m.label_ref.empty()) {
        for (const auto& def : mv) {
            if (def.label_def == m.label_ref) {
                if (def.disabled) return false;
                break;
            }
        }
    }
    return m.tempo_inherits || !m.label_ref.empty();
}

// -- The overview strip's column mapping ------------------------------------
//
// The one owner of the lane's frames-per-column scale and of the two
// conversions built on it (contracts at the declarations, app_state.h). The
// data is the SOURCE domain, always — the ruled choice recorded at
// paint_overview_strip — so the scale is audio.total_frames() over the lane's
// width, and active-domain values pass through the memoized warp-map
// translators (warp_frame_map_view) on their way to or from it.

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
    const int64_t active = static_cast<int64_t>(std::nearbyint(active_position));
    const int64_t src = active_domain_to_source_frame(a, audio, active);
    int col = static_cast<int>(
        std::nearbyint(static_cast<double>(src) / spp));
    if (col < 0) col = 0;
    if (col > lane.w - 1) col = lane.w - 1;
    return col;
}

double overview_anchor_sample_at_x(const AppState& a, const GuiAudio& audio,
                                   int x) {
    const GuiRect lane = top_overview_row_area(a);
    const double spp = overview_samples_per_pixel(a, audio);
    if (spp <= 0.0) return 0.0;
    const double src = static_cast<double>(x - lane.x) * spp;
    if (a.active_audio_view != 'T') return src;
    // Target view: the pressed column names a SOURCE position (the lane's
    // domain), and the drag body's anchor lives in the ACTIVE domain — map it
    // forward once at the press. The int64 round-trip costs under one frame,
    // invisible at whole-song scale.
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
// paint_overview_strip's layer 3 when the box grew grab handles (the lane
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
    int64_t src_b = vp_start;
    int64_t src_e = vp_end;
    if (a.active_audio_view == 'T') {
        src_b = active_domain_to_source_frame(a, audio, vp_start);
        src_e = active_domain_to_source_frame(a, audio, vp_end);
    }
    const int64_t total = audio.total_frames();
    if (src_b < 0) src_b = 0;
    if (src_e > total) src_e = total;
    int x0 = static_cast<int>(
        std::nearbyint(static_cast<double>(src_b) / spp_ov));
    int x1 = static_cast<int>(
        std::nearbyint(static_cast<double>(src_e) / spp_ov));
    if (x0 < 0) x0 = 0;
    if (x0 > lane.w - 1) x0 = lane.w - 1;
    if (x1 > lane.w) x1 = lane.w;
    if (x1 < x0 + 1) x1 = x0 + 1;   // >=1px: the span is never nothing
    *out_x0 = x0;
    *out_x1 = x1;
    return true;
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
