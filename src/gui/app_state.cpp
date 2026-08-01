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
#include <limits>
#include <string>
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
// references are now explicit arguments. The kMarkerHitHalfPx constant
// resolves through app_state.h.

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
// with on the last committed frame, so the marker/chip/lane geometry rides the
// same basis the flag/chip pixels do. The warm spp is (vp_end - vp_start) /
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

TrimHit hit_test_trim_chip(const AppState& app, const GuiAudio& audio,
                           int mouse_x, int mouse_y) {
    // Trim bounds hit-test in the AUTHORING views against the active A/B tab's
    // live bounds. Both bounds are always meaningful (the unset state died
    // 2026-07-30 — a full ordered pair always rests), so this reads them
    // directly; the pair gate that used to stand here is gone with the state it
    // tested.
    const int64_t begin_frame = app.trim.begin_frame;
    const int64_t end_frame   = app.trim.end_frame;

    // The b/e chips are SQUARES in the trim-chip lane (top_trim_row_area,
    // whose height is the chip width flag_lane_w_px()). A press outside that
    // vertical band is not on a chip.
    const GuiRect row = top_trim_row_area(app);
    if (mouse_y < row.y || mouse_y >= row.y + row.h) return TrimHit::None;

    const GuiRect top = top_strip_area(app);
    // Event-synchronized hit geometry, the VIEWPORT half: the b/e chip pixels
    // are painted live by the trim pass (GuiPaintHandler::paint_trim ->
    // render_trim_flags) on the DISPLAYED basis, NOT the live viewport. So the
    // chip columns must resolve on the SAME basis (item_viewport_basis)
    // — the same reason hit_test_flag does — else during an async publish window a
    // chip painted at the OLD column would be grabbed at the NEW/live column.
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

    // Column translation so the chip column lands
    // where the stem (and chip) are painted in the mapped views: the map is
    // the item pixels' own via displayed_or_live_target_map (event-synchronized
    // hit geometry — the ruling at that selector), empty (identity) in source
    // view and the map the flag item cache baked when warm in target view
    // (the live trim pass paints its chips through the same selector).
    const std::vector<WarpFrameMapSegment>& dmap =
        displayed_or_live_target_map(app, audio);
    const std::vector<WarpFrameMapSegment>* target_warp_frame_map =
        dmap.empty() ? nullptr : &dmap;

    // Build the same visible, sorted candidate list render_trim_flags paints.
    // The painter culls a bound whose column is outside the viewport, then
    // reverse-paints so the leftmost chip (b over e at an equal column) lands
    // on top. Hit testing walks the same sorted list FORWARD and returns the
    // first chip whose rect contains mouse_x = the topmost-painted chip, so a
    // click on the visible overlap grabs the chip the user sees on top.
    struct TrimChipHit {
        double  center_x;
        GuiRect rect;
        TrimHit which;
    };
    std::vector<TrimChipHit> chips;
    auto add_chip = [&](int64_t frame, TrimHit which) {
        // Map the authored source frame to the displayed domain and resolve its
        // column through the SAME owners the painter uses (render.h): the mapping
        // via displayed_trim_ms, the column via trim_bound_column against the
        // displayed-basis vp span (the painters' quantized-span denominator), the
        // chip rect via trim_chip_rect. So a hit lands on exactly the drawn chip.
        const double ms = displayed_trim_ms(frame, target_warp_frame_map);
        const TrimBoundColumn c =
            trim_bound_column(ms, vp_start, vp_end, wave_w);
        if (!c.in_viewport) return;
        // THE DRAWN CAP, INFLATED BY THE GRAB TOLERANCE. The rect comes from
        // the one owner so the target is centred on exactly what is painted; the
        // widening is the hit side's own term, because a 2px endcap is below any
        // usable pointing tolerance (the rationale is at trim_chip_rect).
        GuiRect cr_rect =
            trim_chip_rect(which == TrimHit::Begin, top.x, c.col, row);
        const int grab = trim_endcap_grab_px();
        cr_rect.x -= grab;
        cr_rect.w += 2 * grab;
        const double center_x = static_cast<double>(top.x + c.col);
        chips.push_back({center_x, cr_rect, which});
    };

    add_chip(begin_frame, TrimHit::Begin);
    add_chip(end_frame,   TrimHit::End);
    std::sort(chips.begin(), chips.end(),
              [](const TrimChipHit& a, const TrimChipHit& b) {
                  if (a.center_x != b.center_x)
                      return a.center_x < b.center_x;
                  // Deterministic tie-break at an equal column: Begin first, so
                  // the forward walk below returns it. The painted rectangles
                  // are identical there, so occlusion is not visually
                  // distinguishable; this only fixes which bound a click grabs.
                  return a.which == TrimHit::Begin && b.which == TrimHit::End;
              });

    // Forward walk = ascending-x = topmost-painted first. The first chip whose
    // [rect.x, rect.x + w) contains mouse_x is the one the user sees on top.
    for (const TrimChipHit& chip : chips) {
        if (mouse_x >= chip.rect.x &&
            mouse_x < chip.rect.x + chip.rect.w) {
            return chip.which;
        }
    }
    return TrimHit::None;
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
