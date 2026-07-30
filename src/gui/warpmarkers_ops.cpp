#include "warpmarkers_ops.h"

#include "audio.h"
#include "group_position_nudge.h"  // the shared position-nudge flesh (prologue,
                                  // step, commit tail) + the movement doctrine
#include "input_handler.h"      // set_region_to_selection_extent (group step),
                                // clear_region_highlight (the drop's collapse)
#include "warp_frame_map_build.h"
#include "warp_frame_map_view.h"
#include "target_render.h"
#include "warpmarkers.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <utility>
#include <vector>

// Warp-authoring cluster: marker drop / delete / toggle / nudge / tempo
// operations on the warp store, reaching undo, selection, viewport, and
// playback_lifecycle through the struct's reference members; resolver and
// geometry helpers (resolve_inherited_tempo, current_samples_per_pixel,
// waveform_area, ...) are free functions.

// Index of the nearest marker strictly before `time_frame` that survives
// into the render, or -1 if none. Uses the same cascade definition as render
// resolution and hover (effective_disabled: a marker is out if its own
// disabled flag is set, or it is an enabled label ref whose target def is
// disabled), so copy-previous copies the previous render-visible tempo
// rather than attributing to a marker the render ignores. `time_frame`
// need not be present in `mv` — drop_copy_previous_at_playhead calls this
// with the prospective drop time before insertion, landing on the same
// slot insert_marker's lower_bound would place the new marker at, one
// step back.
int find_immediate_prior(const std::vector<GuiWarpMarker>& mv,
                          double time_frame) {
    auto it = std::lower_bound(
        mv.begin(), mv.end(), time_frame,
        [](const GuiWarpMarker& a, double t) { return a.time_frame < t; });
    int i = static_cast<int>(it - mv.begin()) - 1;
    while (i >= 0 && effective_disabled(mv, i)) --i;
    return i;
}

void GuiWarpMarkersOps::drop_marker(double time_frame, bool inherit,
                                     int64_t tempo_cents,
                                     std::optional<double> scale) {
    if (audio.sample_rate() <= 0) return;
    // Marker creation is a commit: the position funnels through
    // snap_authored_frame like every other gesture commit, so the stored
    // value is a whole source frame. Every current caller passes an
    // integer-valued playhead frame, so the snap is the uniformity funnel,
    // not a rounding step. The wall check below IS the load guard's
    // comparison, exactly, applied to the snapped value.
    const int64_t drop_frame = snap_authored_frame(time_frame);
    // Structural warp wall: a warp marker's segment runs to the next
    // breakpoint (or to total_frames for the last marker), and
    // build_warp_frame_map refuses sub-frame segments, so a warp position
    // needs at least one source frame of headroom before EOF. That is the
    // only placement bound — markers may sit arbitrarily close to or
    // exactly on an existing marker; ordering degeneracy is the render
    // boundary's to collapse (exact-frame ties merge to one 1.00 owner),
    // not this drop's.
    if (drop_frame > audio.total_frames() - 1)
        return;
    const auto& mv = app.warpmarkers.markers();
    GuiWarpMarker nm;
    nm.time_frame    = drop_frame;
    nm.tempo_inherits  = inherit;
    nm.tempo_cents     = tempo_cents;
    nm.tempo_scale     = scale;
    // Snapshot pre-mutation state for undo. Captured after the wall check
    // so rejected drops don't leave a no-op entry on the stack.
    std::vector<GuiWarpMarker> pre_state = mv;
    const int new_idx = app.warpmarkers.insert_marker(std::move(nm));
    // Newly-dropped marker becomes the sole selection.
    selection.set_single_selection(new_idx);
    undo.push_undo_warp(std::move(pre_state));
    undo.recompute_dirty();
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();

    // Re-affirm the playhead on the just-dropped marker. The drop is authored
    // AT the playhead (drop_copy_previous_at_playhead — the `s` command and the
    // empty-lane double-click) in warp's
    // SOURCE home view (the home-view binding, architect 2026-07-22), where
    // source_frame_to_active_domain is identity, so this is a no-op
    // reaffirmation — the marker is created under the playhead and the playhead
    // simply stays there. It is not a selection sync. Done last so invalidations
    // in the helper don't double-paint with the ones above.
    const int64_t sample = source_frame_to_active_domain(app, audio, drop_frame);
    viewport.move_playhead_to(sample);
    // A DROP IS A POINT COMMAND (architect 2026-07-29, overruling the drops'
    // earlier keep-the-highlight behavior): it seats the playhead on the marker
    // it creates and single-selects it, so any resting span ends here —
    // unconditionally, any provenance, exactly as the plain marker click's
    // collapse. THE WARP CHOKEPOINT: both entry routes (bare `s` and the
    // empty-lane double-click) reach the warp column only through
    // drop_copy_previous_at_playhead, whose only act is this call, so one clear
    // here covers both. PAST EVERY REFUSAL by construction: the callers' gates
    // (read-only, active_column_authoring_allowed, the double-click's own
    // in-area test) return before calling at all, and this function's own two
    // refusals — no sample rate, and a drop_frame past the EOF wall — return
    // above, before the insert. So a refused drop cannot reach this line and no
    // refusal clears a highlight. clear_region_highlight owns its damage.
    clear_region_highlight(app, viewport);

    // No synchronous re-warp: warp markers author in their source home view
    // only (the home-view binding, architect 2026-07-22), where the source
    // waveform pixels don't depend on the warp map, so there is no displayed
    // target plate to re-warp. The target preview still invalidates — a
    // source-view warp edit changes the rendered target buffer.
    target_render.trigger();
}

// `s` (W view): drop an explicit owner that copies the immediate-prior
// marker's effective tempo (base x scale), via the shared resolver also
// used by the hover popup.
// Exception: when the prior marker is a label ref, the copy is skipped and a
// neutral owner (base 1.0 / empty scale) is dropped instead. Copying the
// ref's resolved effective value would freeze a literal of the pre-drop
// value, but inserting this marker re-deforms the ref's own segment so the
// ref's effective value shifts — the new marker would then hold a value the
// ref no longer carries. A 1.00 owner leaves the ref's segment unchanged.
// Falls back to base 1.0 / no typed scale if there is no prior marker
// (possible: the store may be empty or the playhead may sit before the
// first marker — marker zero is an ordinary, deletable marker).
//
// Copy-previous is PROJECTION TRUTH end to end: the copied value is the tempo
// that RENDERS after the prior marker, not the marker's authored member value.
// marker_effective's OWNER path deliberately returns the marker's own authored
// fields (the ruled authored-display split for coincident group members), so
// when the prior is an owner inside a 2+-survivor exact-frame group the render
// replaces the whole group with one plain enabled 1.00 owner (no labels) while
// marker_effective would still hand back e.g. 2.00 — a copy that would change
// the rendered bytes. So a prior that is a member of a collapsed coincident
// stack contributes the collapse's 1.00 (base 100 / no scale, the no-prior
// fallback and exactly the synthetic owner the resolver seeds), skipping
// marker_effective; the members' own hovers keep their authored readouts (the
// ruled display split). The group predicate mirrors the resolver's stage-2
// collapse: 2+ SURVIVORS sharing one exact int64 frame (effective_disabled is
// the shared cascade survival test; disabled and cascade-disabled members do
// not count). prev_idx itself survives by construction of find_immediate_prior,
// so one OTHER surviving index at its frame makes the group.
void GuiWarpMarkersOps::drop_copy_previous_at_playhead() {
    if (audio.sample_rate() <= 0) return;
    const int64_t src_frame =
        active_domain_to_source_frame(app, audio, app.playhead_cursor_sample);
    const double t = static_cast<double>(src_frame);
    const auto& mv = app.warpmarkers.markers();
    const int prev_idx = find_immediate_prior(mv, t);
    int64_t               base_cents = 100;
    std::optional<double> scale;
    if (prev_idx >= 0 && mv[prev_idx].label_ref.empty()) {
        // Is prev_idx inside a 2+-survivor exact-frame group the render
        // collapses to one plain 1.00 owner? Exact integer frame compares —
        // coincidence IS frame equality in the authored domain.
        const int64_t prev_frame = mv[prev_idx].time_frame;
        bool collapsed_group = false;
        for (int j = 0; j < static_cast<int>(mv.size()); ++j) {
            if (j != prev_idx && mv[j].time_frame == prev_frame &&
                !effective_disabled(mv, j)) {
                collapsed_group = true;
                break;
            }
        }
        // Collapsed group: keep base_cents/scale at the synthetic 1.00 owner's
        // values (the no-prior fallback). Otherwise resolve the prior's
        // projection value (pass/ref/owner, incl. the frame-0 seed and
        // label-ref fallbacks) through marker_effective.
        if (!collapsed_group) {
            const MarkerEffective eff = marker_effective(
                slice_to_warp_markers(mv), prev_idx, audio.total_frames());
            base_cents = eff.base_cents;
            scale      = eff.scale;
        }
    }
    drop_marker(t, /*inherit=*/false, base_cents, scale);
}

// Deleting an owning marker lets downstream pass markers re-resolve to the
// next earlier owner, the same live re-resolution that disabling an owner
// already produces; no values are frozen on delete.
void GuiWarpMarkersOps::delete_selected_marker() {
    if (app.selected_markers.empty()) return;
    const auto& mv = app.warpmarkers.markers();

    // Validate the batch for stale indices only. Any marker is deletable:
    // the parser resolver normalizes whatever arrangement remains at
    // render/preview time (a missing frame-0 owner gets the silent 1.00
    // seed; a dangling ref becomes a plain 1.00 owner with one stderr line
    // per timestamp), so the GUI allows every state.
    for (int idx : app.selected_markers) {
        if (idx < 0 || idx >= static_cast<int>(mv.size())) {
            std::fprintf(stderr,
                "warptempo_gui: delete rejected: stale selection index\n");
            return;
        }
    }

    // All validations passed — capture the snapshot before mutating so the undo
    // can restore the pre-delete state.
    std::vector<GuiWarpMarker> pre_state = app.warpmarkers.markers();
    // Capture the selected markers' active-domain positions BEFORE the store
    // mutation, so a multi-marker delete DEMOTES down to the region spanning
    // them (a DROP former of the selection<->highlight coupling — the delete
    // drops the selection and forms the region; architect 2026-07-23). Warp deletes run in the
    // source home view (home-view binding), where source_frame_to_active_domain
    // is identity, so pre/post mapping agree regardless.
    std::vector<int64_t> del_positions;
    del_positions.reserve(app.selected_markers.size());
    for (int idx : app.selected_markers) {
        // Clamp the forward-map image into the live domain: source-view
        // identity means a legal marker clamps to itself here, so this is a
        // no-op — the spelling matches the target-domain demote captures
        // (input_pointer / phaseresetmarkers_ops, where an EOF item's image can
        // round one past the wall) so all three region-endpoint captures read
        // uniformly.
        del_positions.push_back(clamp_playhead_to_live_domain(
            source_frame_to_active_domain(app, audio, mv[idx].time_frame),
            app, audio));
    }
    // Delete in descending order so earlier indices stay valid.
    for (auto it = app.selected_markers.rbegin();
         it != app.selected_markers.rend(); ++it) {
        app.warpmarkers.remove_marker(*it);
    }
    selection.clear_selection();
    // Demote a multi-marker delete to the spanning region — session scratch,
    // OUTSIDE undo, so undoing the delete restores the markers while the region
    // stays (the standing region-outside-undo rule). A single deleted marker is
    // a point, not a span, so it forms no region (the sliver rule's spirit; the
    // 2-marker + positive-span gate needs no sub-pixel column compare). The
    // waveform damage below covers the region paint. ORDER: the deselect above
    // runs FIRST, so its membership clear cannot reach the span formed here —
    // and could not touch it anyway, the demotion being FREE and the membership
    // clear SelectionExtent-only.
    if (del_positions.size() >= 2) {
        const auto [lo, hi] = std::minmax_element(del_positions.begin(),
                                                  del_positions.end());
        if (*hi > *lo) {
            app.region.active     = true;
            app.region.a_frame    = *lo;
            app.region.b_frame    = *hi;
            // The delete demotion drops the deleted markers, so this region is
            // FREE — tempo gestures skip it.
            app.region.provenance = RegionProvenance::Free;
        }
    }
    undo.push_undo_warp(std::move(pre_state));
    undo.recompute_dirty();
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    // No synchronous re-warp: warp authoring lives in the source home view (see
    // drop_marker), where the source waveform has no map-dependent plate to
    // re-warp. The view-independent target preview trigger stays.
    target_render.trigger();
}

// Ctrl+N: convert each selected marker's tempo source. Cache-free —
// the only stored state on a pass marker is `tempo_inherits = true`
// plus inert defaults. Three input cases per marker:
//   - owning   → pass: inert defaults; label_def preserved; iter bracket
//                cleared (the pass is iter-ineligible).
//   - pass     → owning: freeze the resolved tempo/scale at this moment;
//                label_def preserved.
//   - label_ref → pass: clear the ref; inert defaults; iter bracket cleared
//                (the pass is iter-ineligible).
// Eligibility LOSS (both → pass cases) clears the iter bracket so a hidden
// bracket cannot resurrect through a later pass → owner freeze against a
// different frozen base; undo is the sanctioned restore route (this gesture
// pushes a pre-state snapshot carrying the fields). Tempo changes never clear
// (the adjust_tempo_cents symmetry).
// The first marker is togglable like any other: a pass at frame 0 is
// normalized by the parser resolver at render/preview time (the
// inheritance walk falls back to tempo 1.00 when no owner precedes it) —
// nothing gates this gesture.
void GuiWarpMarkersOps::toggle_inherits() {
    if (app.selected_markers.empty()) return;
    if (app.last_selected_marker < 0) return;
    // FOCUS-COLLAPSE, and this gesture is the group-verb doctrine's own example of
    // COUPLED members (the doctrine is at the head of group_position_nudge.h): the
    // pass -> owner freeze below reads the RESOLVED inheritance walk, so what a
    // member freezes to depends on what the members before it just became — a group
    // toggle's result would depend on iteration order. Collapsing to the focus is
    // the honest act.
    selection.collapse_to_focused();
    // The marker lane owns the playhead (the rule is stated in full at
    // land_playhead_on_marker, input_pointer.cpp): the collapse leaves the FOCUS
    // as the whole selection, so with 3,4,5 selected, the focus at 5 and the
    // playhead resting anywhere else, the lane would rest with the flag at 5
    // claiming to be the playhead while Space played from that other spot. Land on
    // the focus — a PURE playhead write (land_playhead_on_marker), this gesture
    // adding no region clear of its own. THE REGION STORY IS THE COLLAPSE'S OWN
    // membership clear: collapsing a 2+ selection replaces the membership, which
    // takes that selection's SelectionExtent span with it
    // (clear_region_on_membership_replace, app_state.h). A TrimWindow highlight
    // SURVIVES a membership replace by that owner's own rule — moot here since
    // 2026-07-29, one such highlight never resting beside a 2+ selection any more
    // (every trim setter deselects; the rule is at sync_region_to_trim_window's
    // declaration), but the value gesture tears down no cue of the chip row's
    // either way.
    // The land sits at THIS caller and not inside collapse_to_focused, because the
    // site that hands the lane a new focus is the site that owes it a land — and
    // not every caller does: the singleton tempo step has no focus change to land
    // for (its selection is already a singleton — adjust_tempo_cents returns to the
    // group path at size >= 2 — so collapse_to_focused cannot move the focus
    // there). The position nudges pair the collapse with their own land the same
    // way, at their shared prologue.
    // No index guard: the focus was checked >= 0 two lines above, collapse_to_focused
    // cannot move it, and the helper is internally bounds-guarded regardless.
    land_playhead_on_marker(app, audio, viewport, app.last_selected_marker);
    const auto& mv_const = app.warpmarkers.markers();
    std::vector<GuiWarpMarker> proposed = mv_const;
    // Single-marker resolve via marker_effective (slice once) — the
    // projection-aware walk, NOT the raw backward walk. Freezing a pass into
    // an owner must land the value hover shows and the render produces:
    // under a coincident-stack collapse the nearest prior owner is a member
    // of a 2+-survivor exact-frame group, which the render replaces with one
    // plain 1.00 owner, so the raw walk's authored member value (e.g. 150)
    // would silently change the rendered bytes. marker_effective resolves a
    // surviving un-collapsed pass against that same projection, keeping the
    // freeze lossless (resolve_inherited_tempo's contract).
    const std::vector<WarpMarker> resolved_src = slice_to_warp_markers(mv_const);
    bool changed = false;
    for (int idx : app.selected_markers) {
        if (idx < 0 || idx >= static_cast<int>(proposed.size())) continue;
        GuiWarpMarker& m = proposed[idx];
        if (!m.label_ref.empty()) {
            m.label_ref.clear();
            m.tempo_inherits = true;
            m.tempo_cents    = 100;
            m.tempo_scale.reset();
            m.iter_start_cents.reset();
            m.iter_end_cents.reset();
        } else if (m.tempo_inherits) {
            const MarkerEffective eff =
                marker_effective(resolved_src, idx, audio.total_frames());
            m.tempo_inherits = false;
            // base_cents == 0 is marker_effective's "could not resolve"; from
            // a pass it is unreachable (resolve_inherited_tempo returns 100 or
            // a bracketed owner value, never 0), so this mirrors the raw
            // walk's no-owner {100, nullopt} fallback defensively.
            if (eff.base_cents != 0) {
                m.tempo_cents = eff.base_cents;
                m.tempo_scale = eff.scale;
            } else {
                m.tempo_cents = 100;
                m.tempo_scale.reset();
            }
        } else {
            m.tempo_inherits = true;
            m.tempo_cents    = 100;
            m.tempo_scale.reset();
            m.iter_start_cents.reset();
            m.iter_end_cents.reset();
        }
        changed = true;
    }
    if (!changed) return;
    std::vector<GuiWarpMarker> pre_state = mv_const;
    app.warpmarkers.markers_mut() = std::move(proposed);
    undo.push_undo_warp(std::move(pre_state));
    undo.recompute_dirty();
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    // No synchronous re-warp: the Ctrl+N pass toggle authors the warp store in
    // the source home view only (see drop_marker), where the source waveform
    // has no map-dependent plate to re-warp. The target preview trigger stays.
    target_render.trigger();
}

// Toggle the disabled flag on each selected marker. The flag is allowed
// on any marker (cascade still applies only when the toggled marker is a
// label_def).
void GuiWarpMarkersOps::toggle_disabled() {
    if (app.selected_markers.empty()) return;
    const auto& mv_const = app.warpmarkers.markers();
    // Any marker may be disabled, including the one at time 0. A disabled
    // first marker leaves frame 0 ownerless, and the parser resolver
    // normalizes that at render/preview time (the silent 1.00 seed takes
    // the frame-0 slot) — nothing gates this gesture.
    std::vector<GuiWarpMarker> proposed = mv_const;
    bool changed = false;
    for (int idx : app.selected_markers) {
        if (idx < 0 || idx >= static_cast<int>(proposed.size())) continue;
        proposed[idx].disabled = !proposed[idx].disabled;
        changed = true;
    }
    if (!changed) return;
    std::vector<GuiWarpMarker> pre_state = mv_const;
    app.warpmarkers.markers_mut() = std::move(proposed);
    undo.push_undo_warp(std::move(pre_state));
    undo.recompute_dirty();
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    // No synchronous re-warp: this is the WARP column's disable toggle, which
    // authors in the source home view only (see drop_marker), where the source
    // waveform has no map-dependent plate to re-warp. The target preview trigger
    // stays. (The phase-reset sibling in phaseresetmarkers_ops.cpp never touched
    // the warp map and likewise takes no sync.)
    target_render.trigger();
}

// Nudge every selected marker's tempo along the 0.01 grid. Label refs are
// silently skipped (no tempo to nudge — convert via Ctrl+N first). Pass
// markers resolve walk-backward to get their starting tempo/scale, then
// freeze to owning at the nudged value. Owning markers nudge in place.
// `delta_cents` is an integer cent count (one per keypress or wheel
// detent); its sign is the direction of travel. The landed cents are
// clamped into the tempo bracket [kTempoMinCents, kTempoMaxCents]
// (value_format.h). Only dirties / invalidates on real change.
//
// The grid is structural now: authored tempo is integer cents by type, so
// every stored value is on-grid and the step is plain integer addition —
// the old off-grid outward snap has no input left to act on.
void GuiWarpMarkersOps::adjust_tempo_cents(int64_t delta_cents,
                                           bool synthesized_repeat) {
    if (app.active_markers_view != 'W') return;
    if (app.selected_markers.empty()) return;
    if (app.last_selected_marker < 0) return;
    // A 2+ selection is the GROUP step (architect 2026-07-23): all-or-nothing,
    // owner-only, no freeze conversion. The singleton path below is UNCHANGED
    // (per-view behavior bit-for-bit — the source-view pass/ref->owner freeze,
    // the target-only collapsed refusal, the constructive per-marker clamp).
    if (app.selected_markers.size() >= 2) {
        adjust_tempo_cents_group(delta_cents, synthesized_repeat);
        return;
    }
    // architect ruling 2026-07-22: the Up/Down tempo step stays reachable off
    // its source home (target view is exactly where you want to hear/see a tempo
    // change). It is one flavor of the warp column's TEMPO exception there (the
    // others are the tempo drag and its keyboard twin, the bare Left/Right
    // tempo-image step — architect 2026-07-24 second pass; W+target authors
    // tempo only, never position). But the tempo step there is OWNER-ONLY: the
    // focus-collapse target must already own an adjustable tempo, so a pass
    // (tempo_inherits) or a label ref refuses silently — no freeze conversion, no
    // undo entry, no dirty. Source view is UNCHANGED (the pass/ref-to-owner freeze below
    // still applies). The owner test reads the marker's own authored fields, not
    // the resolved projection: the question is whether this marker owns a tempo,
    // which is payload.
    if (app.active_audio_view == 'T') {
        const auto& mv = app.warpmarkers.markers();
        const int f = app.last_selected_marker;
        if (f < 0 || f >= static_cast<int>(mv.size())) return;
        if (mv[f].tempo_inherits || !mv[f].label_ref.empty()) return;
        // A coincident-collapsed owner refuses too (architect 2026-07-22): a
        // coincident group is treated as ONE marker in target view, and its
        // members' authored tempos are render-inert — the resolver replaces
        // every exact-frame group of 2+ effectively-enabled markers with one
        // synthetic plain 1.00 owner. The stack is fixed at the source in warp
        // (source) view, never adjusted from target view. Reuse the
        // normalization-red set: it reddens (a) label-ref fallbacks, (b) passes
        // from a ref, and (c) coincident-collapse members — the two payload
        // checks just above have already rejected ref and pass, so for the
        // payload-OWNER that remains, red-set membership is EXACTLY the
        // coincident-collapse condition (the tempo-drag predecessor test's
        // argument, second consumer). Silent, before any mutation — no freeze,
        // no undo, no dirty, the shape of the ref/pass refusals above.
        const std::set<int>& red = warp_red_flag_set_cached(
            app, audio.sample_rate(),
            static_cast<long>(audio.total_frames())).red;
        if (red.count(f)) return;
    }
    // Undo-coalescing decision. coalesce_gesture keys off the press's own repeat
    // bit (threaded down from the on_key event that reached this handler), so it is
    // order-independent of the focus-collapse below; it just has to run before
    // record_gesture stamps the kind. The Up/Down step is the only route reaching
    // here with kind TempoStep, so a held Up/Down coalesces as intended.
    const bool merge =
        undo.coalesce_gesture(GestureKind::TempoStep, synthesized_repeat);
    selection.collapse_to_focused();
    const auto& mv_const = app.warpmarkers.markers();
    std::vector<GuiWarpMarker> proposed = mv_const;
    // Single-marker resolve via marker_effective (slice once) — the
    // projection-aware walk, NOT the raw backward walk. The Up/Down step freezes a
    // pass to owning at the nudged value, so its starting tempo/scale must be
    // the value hover shows and the render produces: under a coincident-stack
    // collapse the raw walk would seed the freeze from a collapsed group
    // member's authored tempo, silently diverging from the projection's 1.00
    // owner. marker_effective resolves a surviving un-collapsed pass against
    // that same projection, keeping the freeze lossless.
    const std::vector<WarpMarker> resolved_src = slice_to_warp_markers(mv_const);
    bool changed = false;
    for (int idx : app.selected_markers) {
        if (idx < 0 || idx >= static_cast<int>(proposed.size())) continue;
        GuiWarpMarker& m = proposed[idx];
        if (!m.label_ref.empty()) continue;
        int64_t               start_cents;
        std::optional<double> start_scale;
        if (m.tempo_inherits) {
            const MarkerEffective eff =
                marker_effective(resolved_src, idx, audio.total_frames());
            // base_cents == 0 ("could not resolve") is unreachable from a
            // pass; mirror the raw walk's {100, nullopt} no-owner fallback.
            if (eff.base_cents != 0) {
                start_cents = eff.base_cents;
                start_scale = eff.scale;
            } else {
                start_cents = 100;
                start_scale = std::nullopt;
            }
        } else {
            start_cents = m.tempo_cents;
            start_scale = m.tempo_scale;
        }
        // Constructive clamp into the authored-value bracket, the same
        // convention font_size uses: the wheel walks to the bracket edge
        // and stops there, rather than refusing. Exact integer compares at
        // both edges; nothing here can overflow (stored cents are
        // in-bracket, deltas are a handful of detents).
        const int64_t cents = std::clamp(start_cents + delta_cents,
                                         kTempoMinCents, kTempoMaxCents);
        if (!m.tempo_inherits && cents == m.tempo_cents) continue;
        m.tempo_inherits = false;
        m.tempo_cents    = cents;
        m.tempo_scale    = start_scale;
        // The tempo step leaves this marker's iteration bracket untouched,
        // exactly like the flag editor's manual tempo commit: a later tempo
        // change under a live bracket is deliberately not re-gated (symmetry
        // between the two tempo-authoring surfaces wins; staleness is
        // accepted, backstopped by build_warp_frame_map's refusals and the
        // strict promote parse at the ' adopt).
        changed = true;
    }
    if (!changed) return;
    std::vector<GuiWarpMarker> pre_state = mv_const;
    app.warpmarkers.markers_mut() = std::move(proposed);
    // Coalesce a held tempo step: the repeats skip the redundant push so one
    // Ctrl+Z reverts the whole hold.
    if (merge) undo.note_coalesced_commit();
    else       undo.push_undo_warp(std::move(pre_state));
    undo.record_gesture(GestureKind::TempoStep);
    undo.recompute_dirty();
    viewport.invalidate_top_strip();
    viewport.invalidate_timestamp_area();
    // Discrete warp_frame_map change that CAN run in target view: the Up/Down
    // step is a
    // warp authoring gesture reachable off its source home (the ruled exception
    // gated above), so it is one of the target-view re-warp sites (the full
    // inventory lives at Viewport::kick_waveform_sync). When it runs in target
    // view the plate must re-warp, so render synchronously so displayed == live at
    // this command boundary, leaving no divergence window for the displayed-basis
    // gestures (phase drags, trim drags) to ride out. THEN re-land the playhead on
    // the stepped marker's post-step image — the marker lane owns the playhead
    // (the rule is stated in full at land_playhead_on_marker, input_pointer.cpp),
    // and this is the value-gesture form of it: the focus does not change, but its
    // IMAGE can move out from under the playhead. Usually it cannot — a marker's
    // own tempo shapes only the segment AFTER it — but a LABEL DEFINITION reprices
    // every reference to it, including references EARLIER in the timeline, whose
    // spans then change duration and shift everything downstream, the stepped
    // marker's own image included. Re-landing costs nothing when the image did not
    // move (the playhead is written the value it already holds). Source view needs
    // nothing: identity domain, the frame never moved — the same reason the GROUP
    // arm gates its re-land on target view.
    // In SOURCE view nothing moves at all, so the always-on focus stem needs no
    // repaint there; in target view the synchronous re-warp below repaints the
    // waveform area and carries the stem to its new column with the image.
    if (app.active_audio_view == 'T') {
        // NO REGION WORK AT ALL HERE. The #16 TrimWindow highlight re-sync was
        // DELETED 2026-07-29: its reach chain was click a marker -> click the chip
        // row -> Up/Down, and the middle link is foreclosed now that every
        // TrimWindow SETTER deselects (the rule at sync_region_to_trim_window's
        // declaration, input_handler.h) — the chip click empties the selection, and
        // a tempo step needs one, so no TrimWindow highlight can be resting when
        // this runs. The other two provenances were already unreachable here: a
        // SelectionExtent region is cleared by collapse_to_focused above, and a Free
        // region rests only beside an EMPTY selection.
        viewport.kick_waveform_sync();
        const auto& mv_post = app.warpmarkers.markers();
        const int f = app.last_selected_marker;
        if (f >= 0 && f < static_cast<int>(mv_post.size())) {
            viewport.move_playhead_to(source_frame_to_active_domain(
                app, audio, mv_post[f].time_frame));
        }
    }
    target_render.trigger();
}

// Group tempo step (architect 2026-07-23): 2+ selected markers each step their
// OWN tempo by one cent, ALL-OR-NOTHING. An ineligible member is "the wall being
// hit before it starts to move" — if ANY selected marker is in the WALL SET the
// whole press refuses silently (no partial stepping, no per-member pinning), the
// phase-reset nudge's all-or-nothing wall convention carried into the cents
// domain. The wall set (VIEW-INDEPENDENT, max strict): a pass (tempo_inherits),
// a ref (non-empty label_ref) — the singleton step's payload predicates — a
// DISABLED marker (its tempo is render-filtered, so a write would be inaudible),
// a coincident-collapsed marker (warp_red_flag_set_cached — the resolver
// replaces the stack with one 1.00 owner, so the write is render-inert), or a
// marker already AT the bracket edge in the step direction. Disabled and
// collapsed are render-inert regardless of the authoring view, so they wall in
// SOURCE view too — a DELIBERATE asymmetry with the SINGLETON step, whose
// collapsed refusal is target-view-only and whose source-view pass/ref->owner
// FREEZE CONVERSION stays a singleton-only act (a bulk payload conversion from
// one keystroke is refused by design). No freeze conversion here: every stepped
// member is already an owner, so a plain integer add is the whole mutation.
void GuiWarpMarkersOps::adjust_tempo_cents_group(int64_t delta_cents,
                                                 bool synthesized_repeat) {
    const auto& mv = app.warpmarkers.markers();
    const int n = static_cast<int>(mv.size());
    // The coincident-collapse red set, computed VIEW-INDEPENDENTLY here (the
    // group wall is max strict) — the same generation-keyed memoized helper the
    // singleton step and the tempo-drag predecessor use.
    const std::set<int>& red = warp_red_flag_set_cached(
        app, audio.sample_rate(),
        static_cast<long>(audio.total_frames())).red;
    // Bracket edge for the step direction: stepping up walls at the max, down at
    // the min (== the constructive clamp's stopping point for the singleton).
    const int64_t edge = (delta_cents > 0) ? kTempoMaxCents : kTempoMinCents;
    // Wall scan: ANY walled member refuses the whole press.
    for (int idx : app.selected_markers) {
        if (idx < 0 || idx >= n) continue;   // defensive; stale indices skipped
        const GuiWarpMarker& m = mv[idx];
        if (m.tempo_inherits || !m.label_ref.empty() || m.disabled ||
            red.count(idx) || m.tempo_cents == edge) {
            return;   // walled -> silent all-or-nothing refuse
        }
    }
    // Coalesce decision before mutation (a const query, order-independent of the
    // mutation; same as the singleton). The Up/Down step is the only route reaching
    // here with kind TempoStep, and coalesce_gesture keys on the kind + the repeat
    // bit, NOT on any marker index, so a held key over the SAME group collapses to
    // one entry (a selection change requires a command, and a command ends the
    // hold) — the group is coalesce-eligible unchanged.
    const bool merge =
        undo.coalesce_gesture(GestureKind::TempoStep, synthesized_repeat);
    std::vector<GuiWarpMarker> pre_state = mv;
    // Apply +/-1 cent to each selected member (plain integer arithmetic — the
    // structural producer discipline). None is walled (checked above), so every
    // add stays in-bracket and actually changes the value; positions untouched,
    // so no reorder/remap. Each member's iteration bracket is left in place
    // (tempo changes never clear a bracket, per member).
    std::vector<int> touched;
    for (int idx : app.selected_markers) {
        if (idx < 0 || idx >= n) continue;
        GuiWarpMarker* m = app.warpmarkers.marker_mut(idx);
        if (!m) continue;
        m->tempo_cents = m->tempo_cents + delta_cents;
        touched.push_back(idx);
    }
    if (touched.empty()) return;   // defensive (a fully-stale selection)
    // ONE undo entry per press, with identity hints: no reorder happens
    // (positions untouched), so touched_snapshot == touched_live == the stepped
    // indices. A coalesced repeat skips the push (the hold's physical press owns
    // the pre-burst snapshot and its hints). A 2+ selection paints no
    // stem (its cue is the extent region's ground), so the group step has no stem to
    // move.
    if (merge) undo.note_coalesced_commit();
    else       undo.push_undo_warp(std::move(pre_state),
                                   /*affects_persistence=*/true,
                                   touched, touched);
    undo.record_gesture(GestureKind::TempoStep);
    undo.recompute_dirty();
    viewport.invalidate_top_strip();
    viewport.invalidate_timestamp_area();
    // Target-view synchronous re-warp tail (the plate must re-warp when authoring
    // off source home), THEN re-land the playhead on the FOCUSED marker's
    // post-step image: the marker lane owns the playhead (the rule is stated in
    // full at land_playhead_on_marker, input_pointer.cpp) applied to a value
    // gesture that moves images. A GROUP step changes EARLIER selected members'
    // tempos too, and an upstream member's change moves every downstream image
    // INCLUDING the focused member's; without this the coincident playhead (the
    // land put it on the focused marker) would strand off it after the re-warp.
    // The group's REACH is what differs from the singleton's, not the rule — the
    // singleton runs the same target-view re-land, for the rarer label-definition
    // repricing that can move even its own image. Source view needs nothing
    // (identity domain — the frame never moved).
    if (app.active_audio_view == 'T') {
        // NO PROVENANCE BRANCH and NO CAPTURE-BEFORE-KICK PAIR (both deleted
        // 2026-07-29): a tempo gesture needs a selection, every TrimWindow SETTER
        // just cleared it, and a Free region rests only beside an empty selection —
        // so a region resting here is this group's EXTENT by construction (the rule
        // at sync_region_to_trim_window's declaration, input_handler.h). With one
        // provenance left there is nothing to decide before the kick either: the
        // re-derive below runs UNCONDITIONALLY, which is exactly what the captured
        // boolean made it do for a 2+ selection (kick_waveform_sync's live-domain
        // reclamp may wholesale-clear the region, and set_region_to_selection_extent
        // re-activates it), and a <=1 selection cannot reach this group handler.
        viewport.kick_waveform_sync();
        const int f = app.last_selected_marker;
        if (f >= 0 && f < n) {
            viewport.move_playhead_to(source_frame_to_active_domain(
                app, audio, app.warpmarkers.markers()[f].time_frame));
        }
        // No stem to move here: a 2+ selection paints no stem at all (its cue is
        // the extent region's ground, re-derived below). The kick_waveform_sync
        // above already repainted the moved images.
        // Region follows the images (architect 2026-07-23): the group step moved
        // the selected markers' target IMAGES (tempos changed, source frames did
        // not), so re-derive the extent to the selection's NEW extent — the one
        // surviving arm (see the derivation above the kick). Source view needs
        // nothing (identity domain — no image moved), which is why this whole block
        // gates on target view.
        set_region_to_selection_extent(app, audio, viewport);
    }
    target_render.trigger();
}

// Nudge the FOCUSED warp marker by exactly one on-screen pixel column per press.
// direction: -1 for earlier (up/left), +1 for later (down/right).
//
// HORIZONTAL MOVEMENT IS A FOCUS ACT — GROUPS ARE NEVER MOVED (architect
// 2026-07-29): a 2+ selection COLLAPSES TO ITS FOCUS in the shared prologue (which
// also lands the playhead there) and the focus takes this one step, so the body
// below is the singleton op unconditionally. The doctrine, the group-verb rule it
// instances, and the dead per-member machinery are recorded once at the head of
// group_position_nudge.h.
//
// SOURCE HOME VIEW ONLY (the home-view binding, architect 2026-07-22 — restored
// 2026-07-24 second pass: the same-day "third exception", a both-views warp
// position branch, was a misreading and is DELETED; in W+target bare Left/Right
// dispatches the TEMPO-IMAGE STEP instead, MarkerDragOps::step_tempo_image —
// there is no warp position authoring in target view at all, and the dispatch
// site in input_handler.cpp owns the routing).
//
// The wall regime over the identity map, one shape: the marker steps one PAINTED
// column (stepped_anchor_frame — the guarantee and its numeric rationale live in
// the comment there) and its delta is CLAMPED into its own wall headroom, walls
// exactly reachable. Crossing a neighbor is legal and goes through the
// reorder-and-remap below; the render boundary collapses an exact-frame tie to one
// 1.00 owner.
void GuiWarpMarkersOps::nudge_selected_markers(
        int direction, bool synthesized_repeat) {
    // Shared guard prologue: loading / empty-audio refusal, playback stop first,
    // empty/no-focus refusals, the coalesce verdict, the geometry refusals, the
    // focused-index belt, and THE COLLAPSE + LAND that makes this a focus act (the
    // playhead-follows / lead-in rationale lives at the declaration). Refuses
    // silently, navigation-class.
    const GroupNudgePrologue pro = group_position_nudge_prologue(
        app, audio, playback_lifecycle, selection, viewport, undo,
        GestureKind::WarpNudge, synthesized_repeat,
        static_cast<int>(app.warpmarkers.markers().size()));
    if (!pro.ok) return;
    const bool merge = pro.merge;
    const auto& mv = app.warpmarkers.markers();
    const int   f  = pro.focused;   // validated in [0, mv.size()) by the prologue

    // The anchoring map is the DISPLAYED paint basis —
    // displayed_or_live_target_map, the SAME map the flag/trim painters read — so
    // the moved marker travels exactly the commanded pixel column against WHAT
    // IS PAINTED. This gesture runs in warp's SOURCE home view only, so the
    // displayed map is the empty identity map here — the shared painted_column /
    // authored_frame helpers take it naturally, every commit is a plain integer
    // frame, and the working-zoom authoring-grid bit-exactness claims (all
    // source-view) hold.
    const std::vector<WarpFrameMapSegment>& map =
        displayed_or_live_target_map(app, audio);
    const int64_t warp_wall = audio.total_frames() - 1;

    const int64_t orig_f = mv[f].time_frame;

    // (1) THE STEP: one painted column, and D is the plain integer frame
    // difference.
    int64_t D = stepped_anchor_frame(app, audio, map, orig_f, direction) - orig_f;

    // (2) WALLS WIN, in the marker's own wall headroom [0 - orig_f,
    // warp_wall - orig_f] (integer arithmetic, non-empty because every stored
    // marker rests in [0, warp_wall]). Clamping rather than refusing is this
    // column's ruled regime — the warp wall is exactly reachable by a press that
    // would overshoot it (the phase twin refuses instead, one regime per column at
    // its home).
    if (D < -orig_f)             D = -orig_f;
    if (D > warp_wall - orig_f)  D = warp_wall - orig_f;

    // (3) The marker commits orig_f + D. The [0, warp_wall] clamp is a
    // deliberate walls-win belt — provably dead today (this path is all-integer
    // int64 sums, and the headroom clamp above keeps the sum in range), kept as
    // cheap insurance so a future edit to the clamp cannot commit a wall-illegal
    // frame (an out-of-wall authored position would save a load-fatal file).
    int64_t committed_f = orig_f + D;
    if (committed_f < 0)         committed_f = 0;
    if (committed_f > warp_wall) committed_f = warp_wall;
    if (committed_f == orig_f) return;   // saturated / zero-step press

    std::vector<GuiWarpMarker> pre_state = app.warpmarkers.markers();
    // Identity hint (the diff matcher is identity-blind for a column-snapped move
    // — the moved row can land field-identical on another row). touched_snapshot
    // names the nudged marker in PRE-reorder snapshot coordinates = its current
    // index.
    std::vector<int> touched_snapshot{f};
    if (GuiWarpMarker* m = app.warpmarkers.marker_mut(f))
        m->time_frame = committed_f;
    // A nudge may cross a neighbor; restore time order and remap the index-shaped
    // state (the selection follows its marker to the new slot).
    remap_marker_indices_after_reorder(
        app, reorder_markers_by_time(app.warpmarkers.markers_mut()));
    // touched_live: the nudged marker's POST-reorder live index — read off the
    // selection, which the remap rewrote in place and which is exactly this one
    // marker (the prologue collapsed to it).
    std::vector<int> touched_live(app.selected_markers.begin(),
                                  app.selected_markers.end());
    // Coalesce a held key: the PHYSICAL press pushed the pre-burst snapshot with
    // the identity hints; each synthesized repeat skips the redundant push and
    // instead REFRESHES the surviving entry's touched_live to this press's
    // post-reorder index (touched_snapshot stays the first press's pre-burst
    // coordinates — a restore produces that snapshot, and the whole hold moves the
    // same marker, since a selection change needs a command and a command ends the
    // hold). The post-mutation re-record happens in the shared tail.
    if (merge) {
        undo.note_coalesced_commit();
        undo.refresh_coalesced_touched_live(std::move(touched_live));
    } else {
        // The warp POSITION NUDGE (the receiving marker's own image slides). The
        // restore's always-on focus stem follows from the selection — no lateral
        // bit.
        undo.push_undo_warp(std::move(pre_state),
                            /*affects_persistence=*/true,
                            std::move(touched_snapshot),
                            std::move(touched_live));
    }
    // Shared commit tail: record/dirty/invalidate (its full-waveform damage moves
    // the nudged marker's always-on stem), playhead follow (committed_f is
    // reorder-independent), the point command's region collapse, and the
    // view-independent target trigger (no synchronous re-warp — source-view warp
    // pixels don't depend on the map). Ordering rationale at the declaration.
    finish_group_position_nudge(app, audio, viewport, undo,
                                GestureKind::WarpNudge, committed_f,
                                target_render);
}
