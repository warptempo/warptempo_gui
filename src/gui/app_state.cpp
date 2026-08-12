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
// references are now explicit arguments. Each surface's grab tolerance resolves
// through app_state.h (kMarkerStemGrabPx) or render.h (kTrimEndcapGrabPx) —
// there is no shared hit half-width any more.

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
    // column run per bound, edge-anchored on its own column. THE Y-GATE IS THE
    // MERGED TRIM SURFACE (top_trim_surface_area — trim bar + ruler + their
    // gap, the trim surface arc, 2026-08-11): the cap GRAB works from the
    // ruler rows too, a fatter finger target, while the cap still PAINTS in
    // the trim bar lane alone — the stated vertical paint/hit divergence at
    // this function's declaration, the grab tolerance's sibling. A press
    // outside the merged band is not on an endcap.
    const GuiRect row = top_trim_surface_area(app);
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
    // THE MERGED TRIM SURFACE — trim bar + ruler + their gap
    // (top_trim_surface_area, the trim surface arc, 2026-08-11), the exact
    // band hit_test_trim_endcap gates on. A top-strip point BELOW it (the
    // marker lane) is not the bridge handle; the COLUMN interval below is
    // unchanged, the bar's own strictly-inside geometry.
    const GuiRect row = top_trim_surface_area(app);
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
    // rect. Walk backwards and take the first hit. Selection no longer lifts
    // anything (it is a colour swap, not a z-rule), so this is the whole
    // arbitration — one pass, no class split.
    for (auto it = app.flag_hit_rects.rbegin();
         it != app.flag_hit_rects.rend(); ++it) {
        const FlagHitRect& r = *it;
        if (mouse_x >= r.x && mouse_x < r.x + r.w &&
            mouse_y >= r.y && mouse_y < r.y + r.h) {
            return r.marker_index;
        }
    }
    return -1;
}

int hit_test_marker_stem(const AppState& app, int mouse_x, int mouse_y) {
    // The contract — upper half only, the painter's stash, nearest-with-ties-to-
    // later — is at the declaration.
    const GuiRect area = waveform_area(app);
    if (area.w <= 0 || area.h <= 0) return -1;
    if (mouse_y < area.y || mouse_y >= area.y + area.h / 2) return -1;
    if (mouse_x < area.x || mouse_x >= area.x + area.w) return -1;

    const double tol = static_cast<double>(marker_stem_grab_px());
    const double px  = static_cast<double>(mouse_x);
    int    best      = -1;
    double best_gap  = 0.0;
    for (const MarkerStem& stem : app.marker_stems) {
        const double gap = std::fabs(px - stem.x);
        if (gap > tol) continue;
        // <= keeps the LATER entry on a tie; the stash is in paint order, so
        // that is the same topmost rule the flag boxes resolve overlaps with.
        if (best < 0 || gap <= best_gap) {
            best     = stem.marker_index;
            best_gap = gap;
        }
    }
    return best;
}

// Promoted from a lambda in main(). The captured `app`
// reference is now an explicit argument.
bool popup_eligible_marker(const AppState& app, int idx) {
    if (idx < 0) return false;
    if (app.active_markers_view != 'W') return false;
    if (app.iteration_mode_enabled) return false;
    const auto& mv = app.warpmarkers.markers();
    if (idx >= static_cast<int>(mv.size())) return false;
    const auto& m = mv[idx];
    // This gates the BOTTOM-STRIP resolved readout and the Ctrl+C copy — a
    // marker's OWN value is written on its flag regardless of eligibility. Render
    // resolution (resolve_warp_markers_for_render) drops disabled markers
    // outright and drops label refs whose definition is disabled (the
    // cascade). The readout must not report a tempo the render never applies,
    // so eligibility mirrors both drops here: a disabled marker is
    // ineligible, and a ref to a disabled definition is ineligible. A ref
    // whose definition is missing entirely stays eligible —
    // compute_hover_popup_text already yields an empty string for that case
    // and the display sites suppress an empty readout, so it never surfaces a
    // stale tempo.
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
