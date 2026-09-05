#include "warpmarkers_ops.h"

#include "audio.h"
#include "position_nudge.h"  // the shared position-nudge flesh (prologue,
                                  // step, commit tail) + the movement doctrine
#include "input_handler.h"      // land_playhead_on_marker (the Ctrl+N collapse,
                                // which owns the overlay hide with it)
#include "warp_frame_map_build.h"
#include "warp_frame_map_view.h"
#include "target_render.h"
#include "warpmarkers.h"

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

// Warp-authoring cluster: marker drop / delete / toggle / nudge / tempo
// operations on the warp store, reaching undo, selection, viewport, and
// playback_lifecycle through the struct's reference members; resolver and
// geometry helpers (resolve_inherited_tempo, current_samples_per_pixel,
// waveform_area, ...) are free functions.

// THE WARP STATUS/VALUE FAMILY'S TARGET-VIEW TAIL (architect 2026-08-24), one
// statement of a contract FOUR sites share: Ctrl+D (toggle_disabled), Ctrl+N
// (toggle_inherits), Delete (delete_selected_marker) and the flag editor's
// payload commit (flag_editor.cpp). They are the home-view binding's FIFTH
// ruled exception, and the split that admits them is POSITIONAL vs NOT: the
// binding exists because a PLACEMENT edit in target view mutates the map the
// view is displayed in, and a status, an existence or a value edit does not
// move a marker under the pointer that way (the ruling and the whole exception
// inventory are at active_column_authoring_allowed, app_state.h). So each of
// the four dispatches in W+TARGET as well as W+source, and each owes the same
// tail when app.active_audio_view == 'T' — the flag-editor commit adding one
// term its three siblings do not need, canonical_changed, because it alone can
// land a change that is NO MAP INPUT (an iter-bracket-only commit; the
// argument is at that site):
//   1. viewport.kick_waveform_sync() — the synchronous re-warp, so displayed
//      == live at the command boundary, leaving no divergence window for the
//      displayed-basis gestures (phase drags, trim drags) to ride out. These
//      four JOIN the target-view re-warp inventory, whose one owner is
//      Viewport::kick_waveform_sync's declaration (viewport.h).
//   2. The playhead RE-LAND, through Viewport::reseat_playhead_to and NEVER
//      through a movement owner: the marker's IMAGE moved out from under a
//      resting cursor and the cursor follows it, which is a TRANSLATION and
//      not a movement, so the trim region overlay must stand (the rule at
//      clear_region_highlight, input_handler.h). The three sites that keep a
//      focus re-land on ITS post-change image; the delete keeps none and
//      re-lands the playhead's own musical instant instead (its site says how).
//      THE DELETE'S FORM IS THE WHOLE-MAP REWRITE'S, and it is not this
//      cluster's alone: the SETTINGS ENGINE COMMIT (settings_editor.cpp) and
//      the `h` view's WARP REVERT (input_key_dispatch.cpp) took the same two
//      lines on 2026-09-02 (the four-tier review's R-17d) — capture the
//      instant before the rewrite, re-land after it — each site arguing its
//      own subject. The reseat's caller inventory is one owner,
//      Viewport::reseat_playhead_to's definition (viewport.cpp).
//   3. target_render.trigger(), unchanged and view-independent.
// SOURCE VIEW NEEDS NOTHING: it is the identity domain, where no image moves
// at all — exactly the cent step's own split, and the cent step's target-view
// tail (adjust_tempo_cents, below) argues the re-land in full as the precedent
// these four take.
// THE POSITIONAL FAMILY IS NOT HERE and stays source-only IN THE WARP
// COLUMN — the one block left since the P column opened to both audio views
// (architect 2026-08-30) — refusing in T+W at its own DISPATCH site, through
// the one predicate (the keyboard members on a card, the two POINTER members
// silent; the split is recorded at the predicate): the drop (drop_marker / drop_copy_previous_at_playhead),
// the flag drag (marker_drag.cpp), the bare Left/Right nudge
// (nudge_selected_markers, whose dispatch joined the predicate's callers
// 2026-08-29, having spelled its two arms by hand until then) and the `m` bpm
// open, which rewrites tempo through a derivation over a SPAN. The predicate's
// own site owns the call-site inventory (active_column_authoring_allowed,
// app_state.h).

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
    // SILENT, AND IT HAS NO PRODUCER (re-derived 2026-08-30 under the
    // strictness ruling, which is why no card composes here): both drop roads
    // author AT the playhead — the `s` key through
    // drop_copy_previous_at_playhead and the empty-lane double-click through
    // the same body — and the playhead rests in [0, total-1] by every writer's
    // own clamp, warp's SOURCE home making that conversion the identity. So
    // the wall is a structural belt, not a refusal a press can meet; an error
    // arm exists iff a producer exists (validation_topology.md).
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
    // it creates and single-selects it, so the trim region overlay goes with it.
    // SINCE 2026-08-19 THE SEAT ABOVE OWNS THAT — move_playhead_to is one of the
    // rule's two movement owners (the rule at clear_region_highlight,
    // input_handler.h) — so this site's own call is deleted with the inventory
    // it belonged to, and the drop's answer is unchanged. PAST EVERY REFUSAL by
    // construction, which still matters because the seat is what hides: the
    // callers' gates (read-only, active_column_authoring_allowed, the
    // double-click's own in-area test) return before calling at all, and this
    // function's own two refusals — no sample rate, and a drop_frame past the
    // EOF wall — return above, before the insert. So a refused drop reaches
    // neither the insert nor the seat, and hides nothing.

    // No synchronous re-warp, and THIS OP ALONE IS WHAT THAT CLAIM COVERS: a
    // DROP is a PLACEMENT, which is exactly what the home-view binding gates
    // (architect 2026-07-22), so it authors in warp's source home view only,
    // where the source waveform pixels don't depend on the warp map and there
    // is no displayed target plate to re-warp. Its siblings no longer share
    // the answer — Ctrl+D, Ctrl+N, Delete and the flag-editor commit are the
    // status/value family admitted in W+target on 2026-08-24 and each carries
    // the re-warp tail contracted at the head of this file. The target preview
    // still invalidates here — a source-view warp edit changes the rendered
    // target buffer.
    target_render.trigger();
}

// `s` (W view): drop an explicit owner that copies the immediate-prior
// marker's effective tempo (base x scale), via the shared resolver the VALUE
// PAIR also reads — bare `j`'s clipboard copy and Shift+`j`'s jump to the
// marker the value came from (resolved_marker_payload; the readout that used
// to display it retired 2026-08-29).
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
    // THE SUBJECT REFUSAL READS ITS ONE OWNER (marker_selection_standing,
    // app_state.h — 2026-08-30, when the Delete button's face began reading
    // the same fact through marker_selection_verb_actionable). SILENT, and
    // deliberately so: the Delete dispatch arm asks the composed predicate
    // ahead of this call and CARDS the refusal there (input_handler.cpp), so
    // this is the belt and a card here would be the second for one press.
    if (!marker_selection_standing(app)) return;
    const auto& mv = app.warpmarkers.markers();

    // THE STALE-INDEX BELT, ONE POLICY FOR EVERY VERB THAT ITERATES THE SELECTION
    // (architect 2026-07-30): a stale member is SILENTLY SKIPPED and the rest of
    // the batch proceeds — the shape the sibling verbs (both toggle_disabled arms,
    // toggle_inherits, both tempo-step arms) already had. This is a belt against a
    // sanitization invariant — the selection
    // layer keeps its indices live, so nothing here is reachable today — and a
    // belt reports nothing and refuses nothing; the delete arms used to print one
    // stderr line and refuse the WHOLE batch, alone among the six. Any LIVE marker
    // is deletable: the parser resolver normalizes whatever arrangement remains at
    // render/preview time (a missing frame-0 owner gets the silent 1.00 seed; a
    // dangling ref becomes a plain 1.00 owner with one stderr line per timestamp),
    // so the GUI allows every state.
    std::vector<int> live_idx;
    live_idx.reserve(app.selected_markers.size());
    for (int idx : app.selected_markers) {
        if (idx < 0 || idx >= static_cast<int>(mv.size())) continue;
        live_idx.push_back(idx);
    }
    // Nothing live to delete: no snapshot, no undo entry, no damage — the
    // "only on real change" shape the sibling group verbs already have.
    if (live_idx.empty()) return;

    // THE PLAYHEAD'S OWN MUSICAL INSTANT, in SOURCE frames and read while the
    // OLD map still stands — the subject of this op's target-view re-land (the
    // family contract at the head of this file). A delete leaves NO FOCUS to
    // re-land on: the selection clears below, so the cursor itself is what has
    // to survive the re-warp. active_domain_to_source_frame
    // (warp_frame_map_view.h) is the product's one inverse for a bare frame —
    // the identity in source view, the memoized target map's inverse in target
    // view — so this costs two compares off home and is read unconditionally.
    const int64_t playhead_source_frame =
        active_domain_to_source_frame(app, audio, app.playhead_cursor_sample);

    // Capture the snapshot before mutating so the undo can restore the pre-delete
    // state.
    std::vector<GuiWarpMarker> pre_state = app.warpmarkers.markers();
    // Delete in descending order so earlier indices stay valid (live_idx is
    // ascending — app.selected_markers is an ordered set and the skip above
    // preserves its order).
    for (auto it = live_idx.rbegin(); it != live_idx.rend(); ++it) {
        app.warpmarkers.remove_marker(*it);
    }
    // A DELETE RESTS AN EMPTY SELECTION (architect 2026-07-30): the
    // demotion that used to drop a 2+ delete down to a span over the deleted
    // positions is gone with the SPAN FORM, and there is no span state left for
    // one to write into — the region IS the trim. The delete leaves the trim
    // and the overlay's visibility exactly as it found them.
    selection.clear_selection();
    undo.push_undo_warp(std::move(pre_state));
    undo.recompute_dirty();
    viewport.invalidate_waveform_area();
    // THE TARGET-VIEW TAIL, the family contract at the head of this file. A
    // delete reshapes the map from the deleted marker onward, so in W+target
    // the plate re-warps and the PLAYHEAD re-lands — on its own musical
    // instant, captured above, because the delete clears the selection and
    // leaves no focus whose image could be the subject.
    if (app.active_audio_view == 'T') {
        viewport.kick_waveform_sync();
        viewport.reseat_playhead_to(
            source_frame_to_active_domain(app, audio, playhead_source_frame));
    }
    target_render.trigger();
}

// Ctrl+N: convert the FOCUSED marker's tempo source — a 2+ selection
// COLLAPSES to its focus first (the body below), the group-verb doctrine's own
// example of COUPLED members, so this never acts on more than one marker.
// Cache-free — the only stored state on a pass marker is `tempo_inherits =
// true` plus inert defaults. THREE INPUT CASES for that one marker:
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
    // THE LEADING REFUSAL IS ONE PREDICATE (inherit_toggle_actionable,
    // app_state.h — 2026-08-30): the P view, an empty selection, no focus.
    // The P-view return lived at the Ctrl+N dispatch arm until then and the
    // two subject returns here; the Toggle inherit button's face reads the
    // same predicate, so the act and the glyph are one decision. SILENT: the
    // dispatch arm asks the same predicate ahead of this call so it can NAME
    // the refusal on a card (input_handler.cpp), and one press owes one card,
    // so this stays the belt.
    if (!inherit_toggle_actionable(app)) return;
    // FOCUS-COLLAPSE, and this gesture is the group-verb doctrine's own example of
    // COUPLED members (the doctrine is at the head of position_nudge.h): the
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
    // the focus — through land_playhead_on_marker, WHICH HIDES THE TRIM REGION
    // OVERLAY since 2026-08-19 (the rule at clear_region_highlight,
    // input_handler.h). That is a membership CHANGE for this gesture and a
    // deliberate one: the rule reads "the playhead's position in the music
    // changes, or a marker is touched", and a Ctrl+N collapse does both — it
    // moves the cursor onto the focus from wherever it stood and it freezes
    // inheritance on markers. Its old non-member argument ("a value edit is not
    // a turn to other work") was a call-site judgement, and call-site judgements
    // are what the rule replaced. It discards nothing, the trim standing behind
    // the hidden overlay.
    // (The belt that stood here — "a region rests only beside an EMPTY
    // selection, and Ctrl+N needs a focus" — is retired, 2026-08-18: bare `[`
    // shows the overlay and writes no selection, so a shown overlay may rest
    // beside any selection.)
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
    // THE TARGET-VIEW TAIL, the family contract at the head of this file. A
    // pass/owner conversion changes the tempo that sounds from this marker on,
    // so in W+target the plate re-warps and the FOCUS re-lands on its
    // post-toggle image. The focus survives by construction — the collapse
    // above made it the whole selection — and the pre-mutation land it already
    // took (land_playhead_on_marker, which HIDES the overlay) is a different
    // act from this one: that one moved the cursor onto the focus, this one
    // follows the focus's image as the domain re-derives under it.
    if (app.active_audio_view == 'T') {
        viewport.kick_waveform_sync();
        const auto& mv_post = app.warpmarkers.markers();
        const int f = app.last_selected_marker;
        if (f >= 0 && f < static_cast<int>(mv_post.size())) {
            viewport.reseat_playhead_to(source_frame_to_active_domain(
                app, audio, mv_post[f].time_frame));
        }
    }
    target_render.trigger();
}

// Toggle the disabled flag on each selected marker. The flag is allowed
// on any marker (cascade still applies only when the toggled marker is a
// label_def).
void GuiWarpMarkersOps::toggle_disabled() {
    // The subject refusal reads its one owner (marker_selection_standing,
    // app_state.h), the delete's shape — and, like the delete's, it is SILENT
    // because the Ctrl+D dispatch arm cards the composed refusal ahead of it.
    if (!marker_selection_standing(app)) return;
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
    // THE TARGET-VIEW TAIL, the family contract at the head of this file. A
    // disabled marker stops shaping its segment (and a disabled label_def
    // reprices every reference to it, including references EARLIER in the
    // timeline), so in W+target the plate re-warps and the FOCUS re-lands on
    // its post-toggle image. The focus is a member of the toggled selection by
    // the never-parked rule; a selection resting without one simply skips the
    // re-land, the toggle having nothing to follow.
    // (The phase-reset sibling in phaseresetmarkers_ops.cpp never touched the
    // warp map and takes no sync; its column keeps its home-view gate at the
    // dispatch, nothing having been ruled about it.)
    if (app.active_audio_view == 'T') {
        viewport.kick_waveform_sync();
        const auto& mv_post = app.warpmarkers.markers();
        const int f = app.last_selected_marker;
        if (f >= 0 && f < static_cast<int>(mv_post.size())) {
            viewport.reseat_playhead_to(source_frame_to_active_domain(
                app, audio, mv_post[f].time_frame));
        }
    }
    target_render.trigger();
}

// Nudge the selected marker(s)' tempo along the 0.01 grid. SINGLETON ARM
// ONLY: a label ref is silently skipped (no tempo to nudge — convert via
// Ctrl+N first); pass markers resolve walk-backward to get their starting
// tempo/scale, then freeze to owning at the nudged value; owning markers
// nudge in place. THE GROUP ARM (adjust_tempo_cents_group, below) is
// all-or-nothing instead: a label ref anywhere in the selection WALLS the
// whole press, refusing before any marker changes. `delta_cents` is an
// integer cent count (one per keypress — bare Up/Down are the only two
// callers); its sign is the direction of travel. The landed cents are
// clamped into the tempo bracket [kTempoMinCents, kTempoMaxCents]
// (value_format.h). Only dirties / invalidates on real change.
//
// The grid is structural now: authored tempo is integer cents by type, so
// every stored value is on-grid and the step is plain integer addition —
// the old off-grid outward snap has no input left to act on.
GuiOpRefusal GuiWarpMarkersOps::adjust_tempo_cents(int64_t delta_cents,
                                                   bool synthesized_repeat) {
    // THE LEADING REFUSAL BLOCK IS NAMED WHOLE (tempo_cent_step_actionable,
    // app_state.h — the column gate plus the empty-selection and invalid-focus
    // refusals), and IT HAS TWO READERS AGAIN since 2026-08-30: this body and
    // the bottom row's Up and Down buttons' disabled face. That face read it
    // for one evening on 2026-08-15 under the whole-row honesty ruling, lost it
    // the same day (every term past the column gate is a SELECTION fact and the
    // pair blinked on every marker click), and took it back under the
    // truthful-buttons ruling, whose one user withdrew the flicker argument;
    // the predicate's own header carries all three states. The refusals below
    // it are value-shaped per-marker facts and they split TWO WAYS since
    // 2026-08-31: a label ref and, in target view, a pass, a ref or a
    // coincident-collapse member keep a LIVE face and answer on a CARD (each
    // needs this act's own resolution run, which a per-tick face cannot do),
    // while THE BRACKET WALL is faced and silent — the Up / Down pair greys at
    // the bracket's end (tempo_cent_step_direction_actionable) and the key says
    // nothing, so the wall is now refused at the singleton arm's head, ahead of
    // the coalesce stamp, and never reaches the loop below.
    //
    // THE BLOCK NOW ANSWERS ON A CARD (architect 2026-08-30, the strictness
    // ruling): this act composes the sentence and RETURNS it, the dispatch
    // raising it — the reason channel's contract is at GuiOpRefusal
    // (warpmarkers_ops.h). One sentence for all three terms: the P column, an
    // empty selection and a missing focus are the same answer from the user's
    // side — the step wants a focused warp marker and has none. The Up / Down
    // buttons grey on the same predicate, so no lift reaches it.
    if (!tempo_cent_step_actionable(app))
        return "Select a warp marker to change its tempo";
    // A 2+ selection is the GROUP step (architect 2026-07-23): all-or-nothing,
    // owner-only, no freeze conversion. The singleton path below is UNCHANGED
    // (per-view behavior bit-for-bit — the source-view pass/ref->owner freeze,
    // the target-only collapsed refusal, the constructive per-marker clamp).
    // Its refusal is ITS OWN sentence, forwarded verbatim.
    if (app.selected_markers.size() >= 2)
        return adjust_tempo_cents_group(delta_cents, synthesized_repeat);
    // THE BRACKET WALL IS ASKED AHEAD OF THE COALESCE STAMP (2026-08-31,
    // converting codex round A's MED finding), through the DIRECTIONAL half of
    // this arm's own face (tempo_cent_step_direction_actionable, defined below
    // — here it is a singleton by the fork just above, so it is the singleton
    // arm that answers: the focused OWNER's clamped landing compared against
    // the value it already holds). A WALL NO-OP TOUCHES NOTHING: the call below
    // invalidates the coalescing stamp on a physical press, so discovering this
    // wall AFTER it — as the loop's `changed` flag used to, and only there —
    // split an undo run at the KEY while the same press on the now-GREYED Up /
    // Down button never dispatched and left the run whole. The discriminator is
    // the FACE, not the card: a refusal the button greys on runs BEFORE the
    // stamp, a refusal that keeps a LIVE face stays behind it, both surfaces
    // poisoning alike. That is why ONLY the wall moved: the VALUE-SHAPED tails
    // below — a source-view label ref, and in target view a pass, a ref or a
    // coincident-collapse member — keep a live face and a card, so they keep
    // their place past the stamp. The supersession of the old "an early call
    // must poison for a press that goes on to refuse" clause is recorded at
    // Undo::coalesce_gesture.
    if (!tempo_cent_step_direction_actionable(app, audio, delta_cents))
        return std::nullopt;
    // THE COALESCE VERDICT, past the wall and ahead of every remaining refusal
    // (the tails named above): the call has a side effect — a PHYSICAL press
    // INVALIDATES the coalescing stamp inside it (the derivation is at
    // Undo::coalesce_gesture) — and for a refusal the face cannot see, an
    // invalidate that sits behind it is not an invalidate on arrival. `merge`
    // is consumed far below, and nothing between here and there reads
    // coalescing state, so the placement moves no other behavior.
    // coalesce_gesture computes its verdict BEFORE that invalidate (the
    // hybrid's order rule, stated at its definition), so the call answers this
    // press correctly either way. It reads the press's own repeat bit (threaded
    // down from the on_key event that reached this handler) to pick its arm, so
    // it is order-independent of the focus-collapse below; it just has to run
    // before record_gesture stamps the burst. The Up/Down step is the only
    // route reaching here with kind TempoStep, so a held Up/Down coalesces by
    // identity and a rapid re-tap by the tap window (architect 2026-08-01) —
    // the SUBJECT the tap arm compares is this singleton selection, unchanged
    // by the collapse below.
    const bool merge =
        undo.coalesce_gesture(GestureKind::TempoStep, synthesized_repeat);
    // architect ruling 2026-07-22: the Up/Down tempo step stays reachable off
    // its source home (target view is exactly where you want to hear/see a tempo
    // change). Since 2026-07-29 it is the WHOLE of the warp column's TEMPO
    // exception there, and the whole tempo surface anywhere: the other two flavors
    // — the pointer tempo drag and its keyboard twin, the bare Left/Right
    // tempo-image step — were deleted (the list is at the head of marker_drag.h).
    // W+target authors tempo only, never position. The tempo step there is
    // OWNER-ONLY: the
    // focus-collapse target must already own an adjustable tempo, so a pass
    // (tempo_inherits) or a label ref refuses — on a card since 2026-08-30
    // (the arm below names the view with the marker), with no freeze
    // conversion, no undo entry, no dirty. Source view is UNCHANGED (the pass/ref-to-owner freeze below
    // still applies). The owner test reads the marker's own authored fields, not
    // the resolved projection: the question is whether this marker owns a tempo,
    // which is payload.
    if (app.active_audio_view == 'T') {
        const auto& mv = app.warpmarkers.markers();
        const int f = app.last_selected_marker;
        // A stale focus is a belt against the selection layer's own
        // invariant, so it says nothing (GuiOpRefusal's contract).
        if (f < 0 || f >= static_cast<int>(mv.size())) return std::nullopt;
        // THE KIND REFUSALS, THROUGH THEIR ONE OWNER since 2026-09-02 (the
        // four-tier review's R-17e): both sentences and both tests live at
        // tempo_cent_step_target_view_refusal below, so the CARD this press
        // raises and the Up / Down tooltip's dropped modifier line read one
        // decision. Before any mutation — no freeze, no undo, no dirty.
        if (const char* refusal =
                tempo_cent_step_target_view_refusal(app, audio))
            return refusal;
    }
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
        // convention the settings brackets use: the value walks to the edge
        // and stops there, rather than refusing. This SINGLETON arm already
        // conformed to the unified wall policy (singleton steps clamp; the
        // policy is stated once at the head of position_nudge.h) and is
        // untouched by it — a cent step moves exactly one grid unit, so
        // clamping and refusing coincide here and only the resting-at-the-edge
        // press differs from a position nudge's near-wall overshoot. Exact
        // integer compares at both edges; nothing here can overflow (stored
        // cents are in-bracket, deltas are a handful of detents).
        // THROUGH THE LANDING OWNER since 2026-08-31 (tempo_cent_step_landing,
        // app_state.h): the clamp is the same one line it always was, and
        // naming it is what lets the Up / Down face compare THIS arithmetic
        // against the resting value instead of re-spelling "at the bracket
        // edge" (tempo_cent_step_direction_actionable, below).
        const int64_t cents = tempo_cent_step_landing(start_cents, delta_cents);
        if (!m.tempo_inherits && cents == m.tempo_cents) continue;
        m.tempo_inherits = false;
        m.tempo_cents    = cents;
        m.tempo_scale    = start_scale;
        // THE BRACKET RIDES THE BASE: the stepped base drags this marker's
        // live iteration bracket with it, so no sweep cell can leave the tempo
        // bracket. Silent, deltas only — the rule and the loud/silent division
        // with the flag editor's typed-bracket gate live at the one owner
        // (clamp_iter_bracket_to_tempo_bracket, warpmarkers.h), which the flag
        // editor's manual tempo commit calls too. A no-op on the freeze arm
        // above: a pass carries no bracket (eligibility loss clears it).
        clamp_iter_bracket_to_tempo_bracket(m);
        changed = true;
    }
    // NOTHING CHANGED, AND THE ONE LIVE REASON SAYS SO (architect 2026-08-30,
    // the strictness ruling; this act's reason channel is GuiOpRefusal,
    // warpmarkers_ops.h). The loop has exactly ONE subject here —
    // collapse_to_focused ran above, so the selection is the focus alone — and
    // what can leave it untouched is:
    //   a LABEL REF, skipped whole by the loop's first `continue` because it
    //     has no tempo of its own to step. What reaches here is the SOURCE-view
    //     ref: in target view the payload arm above already refused it in that
    //     view's own words, so the two sentences never collide.
    // AN OWNER AT A BRACKET END NO LONGER REACHES HERE (2026-08-31): its
    // clamped step lands the value it already holds, and that is the wall the
    // directional predicate now refuses on at this arm's head, ahead of the
    // coalesce stamp. The silent return below is what a stale focused index
    // falls to — a belt against an invariant the selection layer keeps.
    // A pass reaches no arm at all: it always freezes, so it always changes.
    if (!changed) {
        const int f = app.last_selected_marker;
        // In range by tempo_cent_step_actionable, which proved it above and
        // which nothing since has invalidated (no store resize on this path).
        if (f >= 0 && f < static_cast<int>(mv_const.size()) &&
            !mv_const[f].label_ref.empty())
            return "A label reference has no tempo of its own";
        // THE BRACKET END IS SILENT (architect 2026-08-31, superseding the
        // 2026-08-30 card "The tempo is already at its limit"): a benign
        // one-dimensional refusal already at its state says nothing — the
        // focused marker's own value is the one place to glance, and the
        // Up/Down face greys at the bracket's end. It is REFUSED AT THIS ARM'S
        // HEAD since that same day, so this line answers the stale-index belt
        // alone. The LABEL-REF arm above keeps its sentence: that is a fact
        // about the marker's kind, not a wall the value is resting on.
        return std::nullopt;
    }
    std::vector<GuiWarpMarker> pre_state = mv_const;
    app.warpmarkers.markers_mut() = std::move(proposed);
    // Coalesce a held tempo step: the repeats skip the redundant push so one
    // Ctrl+Z reverts the whole hold. The skip is the WHOLE per-press action now
    // — note_coalesced_commit existed to mirror the push helpers' hover-popup
    // clear, and died with the hover popup (row 5).
    if (!merge) undo.push_undo_warp(std::move(pre_state));
    // Settle the burst, POST-mutation: the stamp, or — on a merged press that
    // stepped the value back to the burst entry's own snapshot — the BYTE-EQUAL
    // POP of that entry (the rule is at Undo::record_gesture).
    undo.record_gesture(GestureKind::TempoStep, merge);
    undo.recompute_dirty();
    viewport.invalidate_top_strip();
    // AND THE WAVEFORM, for the STEMS (row 5): a tempo step can move a marker in
    // or out of the RED set (a value that normalizes to the 1.00 fallback), and
    // the stem carries its class's colour now — #da4453 for red, the calm purple
    // otherwise. In SOURCE view nothing else here damages the waveform at all, so
    // without this the stem would keep its old colour until some unrelated
    // repaint. In target view the synchronous re-warp below repaints anyway; this
    // is the cheaper honest owner for both.
    viewport.invalidate_waveform_area();
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
    // In SOURCE view nothing moves at all, so the marker's always-on stem needs
    // no repaint there; in target view the synchronous re-warp below repaints
    // the waveform area and carries the stem to its new column with the image.
    if (app.active_audio_view == 'T') {
        // NO REGION WORK AT ALL HERE, and none is needed: the overlay is
        // DERIVED from the trim every frame (trim_overlay_span, app_state.h),
        // so a step that re-warps the target re-derives it in the new domain on
        // the next frame with nothing to maintain. (The belt that stood here —
        // "a region rests only beside an EMPTY selection, and a tempo step
        // needs a selection" — is retired, 2026-08-18: bare `[` shows the
        // overlay and writes no selection.) The #16 trim-highlight re-sync that
        // stood here was deleted 2026-07-29 and the highlight itself
        // 2026-07-30.
        viewport.kick_waveform_sync();
        const auto& mv_post = app.warpmarkers.markers();
        const int f = app.last_selected_marker;
        if (f >= 0 && f < static_cast<int>(mv_post.size())) {
            // THROUGH THE RESEAT, NOT THE MOVER (2026-08-19): move_playhead_to
            // HIDES the trim region overlay and this write must not. A
            // TRANSLATION IS NOT A MOVEMENT — the focus did not change and the
            // playhead did not leave it; the marker's IMAGE moved under a
            // resting cursor and the cursor follows it, which is the `t` flip's
            // act in another spelling. reseat_playhead_to is the identical write
            // without the hide, and it keeps the keep-visible scroll this tail
            // wants (the rule at clear_region_highlight, input_handler.h).
            viewport.reseat_playhead_to(source_frame_to_active_domain(
                app, audio, mv_post[f].time_frame));
        }
    }
    target_render.trigger();
    return std::nullopt;
}

// Group tempo step (architect 2026-07-23): 2+ selected markers each step their
// OWN tempo by one cent, ALL-OR-NOTHING over the members the act SEES. An
// ineligible member is "the wall being hit before it starts to move" — if ANY
// surviving selected marker is in the WALL SET the whole press refuses, on
// its own sentence since 2026-08-30 (no partial stepping, no per-member
// pinning). THIS IS GROUP RIGIDITY, NOT A WALL POLICY (the unified wall
// policy, architect 2026-07-30, is stated once at the head of position_nudge.h
// and rules that SINGLETON steps clamp): per-member clamping would pool the
// walled members at the bracket edge while the rest kept stepping, deforming
// the group's relative values, which is precisely what a group edit must not
// do — the same reason the deleted per-member group nudge refused the whole
// press. (The comment here used to cite the phase-reset nudge's whole-press
// refusal as its precedent; that gesture CLAMPS as of the unified policy, so
// the justification stands on group rigidity alone.)
// A DISABLED MEMBER IS INVISIBLE, NOT WALLED (architect 2026-09-02, the
// four-tier review's R-12 — the `m` BPM sweep's own rule, "a disabled marker is
// invisible to the act", asked of this step): an effectively disabled member
// (effective_disabled, warpmarkers.h — its own bit, or a reference whose
// definition is disabled) is SKIPPED by both loops below, the scan's and the
// mutation's — not counted, not stepped, every field untouched, so a marker
// re-enabled later carries the tempo it had when it was disabled — and the
// rigidity applies to the SURVIVORS: they step together or not at all. Until
// that day a disabled member walled the press on the argument that its
// render-filtered tempo made the write inaudible; the same fact is why it is
// now passed over — an inaudible write neither deforms the group nor serves
// it, and one disabled flag inside a selection should not veto the rest. A
// selection whose EVERY member is invisible is the EMPTY step: it refuses on
// the singleton's own empty-selection sentence — the step wants an enabled
// warp marker and has none — and greys the pair on the same verdict.
// The wall set over the survivors (VIEW-INDEPENDENT, max strict): a pass
// (tempo_inherits), a ref (non-empty label_ref) — the singleton step's payload
// predicates — a coincident-collapsed marker (warp_red_flag_set_cached — the
// resolver replaces the stack with one 1.00 owner, so the write is
// render-inert; a disabled member is never a stack survivor, so it cannot be
// collapsed-red either), or a marker that cannot take the WHOLE step without
// leaving the tempo bracket (the edge compare's generalization, R12 — at the
// bare ±1 the two are the same test). Collapse is render-inert regardless of
// the authoring view, so it walls in SOURCE view too — a DELIBERATE asymmetry
// with the SINGLETON step, whose collapsed refusal is target-view-only, which
// steps a disabled singleton (the one marker asked for is the one stepped),
// and whose source-view pass/ref->owner FREEZE CONVERSION stays a
// singleton-only act (a bulk payload conversion from one keystroke is refused
// by design). No freeze conversion here: every stepped member is already an
// owner, so a plain integer add is the whole mutation.
// THE WALL SCAN AS A CONST OWNER (architect 2026-08-31, R3): the act below is
// the first reader and the Up / Down buttons' face is the second, through the
// composed predicate under it. It is EXTRACTED rather than mirrored — the
// terms are the group's own and a face may not restate them — and it mutates
// nothing, which is what made the extraction necessary at all: the act's scan
// used to sit inside a body that had already asked the coalesce verdict (a
// call with a side effect on the undo stamp), so there was no callable form.
// It answers a three-way VERDICT since R-12 (the act forks its sentence on
// it; the face reads the boolean wrapper, app_state.h). Declared in
// app_state.h beside the face that reads it; the wall set and its GROUP
// RIGIDITY justification are at the act.
TempoCentStepGroupVerdict tempo_cent_step_group_verdict(const AppState& a,
                                                        const GuiAudio& audio,
                                                        int64_t delta_cents) {
    const auto& mv = a.warpmarkers.markers();
    const int   n  = static_cast<int>(mv.size());
    const std::set<int>& red = warp_red_flag_set_cached(
        a, audio.sample_rate(), static_cast<long>(audio.total_frames())).red;
    int survivors = 0;
    for (int idx : a.selected_markers) {
        if (idx < 0 || idx >= n) continue;   // defensive; stale indices skipped
        if (effective_disabled(mv, idx)) continue;   // invisible to the act
        ++survivors;
        const GuiWarpMarker& m = mv[idx];
        if (m.tempo_inherits || !m.label_ref.empty() || red.count(idx))
            return TempoCentStepGroupVerdict::Walled;
        // THE WALL IS "CAN THIS MEMBER TAKE THE WHOLE STEP", not "is it AT the
        // bracket edge" (2026-08-31, with the step ladder — R12): the group
        // arm ADDS delta_cents raw, so with the ten-cent chord a member three
        // cents from the max would land OUT of bracket, and clamping it would
        // be exactly the pooling GROUP RIGIDITY refuses. Asked through the
        // landing owner, so the bracket's ends are named nowhere here: the
        // clamp bites iff the member cannot take the full step. At the bare
        // ±1 this is the old edge compare exactly — a member is out of bracket
        // after one cent iff it was resting on that edge — so the day's other
        // behaviour is untouched.
        if (tempo_cent_step_landing(m.tempo_cents, delta_cents) !=
            m.tempo_cents + delta_cents)
            return TempoCentStepGroupVerdict::Walled;
    }
    return survivors > 0 ? TempoCentStepGroupVerdict::Steps
                         : TempoCentStepGroupVerdict::Empty;
}

// THE TARGET VIEW'S KIND REFUSAL — the contract and the readers are at the
// declaration (app_state.h). The terms are the act's own, in the act's own
// order: source view and a GROUP press are not this refusal's business, the
// stale-focus belt says nothing, then the two PAYLOAD refusals and then the
// coincident-collapse one.
//
// THE TWO PAYLOAD SENTENCES NAME THE VIEW as well as the marker (2026-08-30):
// in SOURCE view this very marker would be stepped (the freeze converts it),
// so a card that said only "it owns no tempo" would be false half the time.
//
// THE COLLAPSE REFUSAL (architect 2026-07-22): a coincident group is treated
// as ONE marker in target view, and its members' authored tempos are
// render-inert — the resolver replaces every exact-frame group of 2+
// effectively-enabled markers with one synthetic plain 1.00 owner. The stack
// is fixed at the source in warp (source) view, never adjusted from target
// view. It reuses the normalization-red set, which reddens (a) label-ref
// fallbacks, (b) passes from a ref, and (c) coincident-collapse members — the
// two payload checks just above have already rejected ref and pass, so for the
// payload-OWNER that remains, red-set membership is EXACTLY the
// coincident-collapse condition (the same argument the GROUP step's wall scan
// makes — the two cent-step arms are the red set's two consumers here since
// the tempo-drag predecessor walk was deleted).
const char* tempo_cent_step_target_view_refusal(const AppState& a,
                                                const GuiAudio& audio) {
    if (a.active_audio_view != 'T') return nullptr;
    if (a.selected_markers.size() >= 2) return nullptr;
    const auto& mv = a.warpmarkers.markers();
    const int   f  = a.last_selected_marker;
    if (f < 0 || f >= static_cast<int>(mv.size())) return nullptr;
    const GuiWarpMarker& m = mv[static_cast<size_t>(f)];
    if (m.tempo_inherits || !m.label_ref.empty())
        return "In target view only a marker that owns its tempo can be "
               "stepped";
    const std::set<int>& red = warp_red_flag_set_cached(
        a, audio.sample_rate(),
        static_cast<long>(audio.total_frames())).red;
    return red.count(f) ? "That marker shares its frame with another"
                        : nullptr;
}

// THE BPM SWEEP'S OPEN VERDICT — the contract, the reader list and the arm
// inventory are at the declaration (app_state.h). It sits here, beside
// tempo_cent_step_group_verdict, because it is the same kind of thing: a const
// walk of the warp store that the ACT reads for its refusal and a BUTTON'S FACE
// reads for its grey, extracted so the two cannot be two spellings of one
// ladder.
//
// THE ARMS ARE THE DISPATCH'S OWN, IN ITS OWN ORDER, and each is argued at the
// dispatch where its sentence is raised (input_key_dispatch.cpp's bare `m`
// arm). What is worth stating HERE is the one thing the extraction changed:
// the coincident-collapse test reads warp_red_flag_set_cached's `collapsed`
// set rather than calling warp_coincident_collapse_members fresh. It is the
// SAME verdict — that cache's pass 1 is that classifier over the same slice of
// the same committed store (warp_frame_map_view.cpp) — memoized on the store's
// generation, which is what makes a per-tick face read cost nothing.
BpmSweepPlan bpm_sweep_plan(const AppState& a, const GuiAudio& audio) {
    const auto refused = [](BpmSweepRefusal r) {
        BpmSweepPlan p;
        p.refusal = r;
        return p;
    };
    if (a.active_markers_view != 'W')
        return refused(BpmSweepRefusal::WrongColumn);
    if (!active_column_authoring_allowed(a))
        return refused(BpmSweepRefusal::OffHomeView);
    if (a.selected_markers.empty())
        return refused(BpmSweepRefusal::NothingSelected);

    const std::vector<GuiWarpMarker>& mv = a.warpmarkers.markers();
    const int n        = static_cast<int>(mv.size());
    const int owner    = *a.selected_markers.begin();
    const int last_sel = *a.selected_markers.rbegin();
    if (owner < 0 || last_sel >= n)
        return refused(BpmSweepRefusal::StaleSelection);
    // std::set is ascending, so a run [owner .. last_sel] is contiguous iff
    // its extent equals its count.
    if (last_sel - owner + 1 != static_cast<int>(a.selected_markers.size()))
        return refused(BpmSweepRefusal::NotOneRun);

    const int boundary = section_end_index(mv, last_sel);
    const int scan_end = std::min(boundary, n - 1);
    for (int i = owner; i <= scan_end; ++i) {
        if (!mv[static_cast<size_t>(i)].label_ref.empty() &&
            !effective_disabled(mv, i))
            return refused(BpmSweepRefusal::LabelRefInSpan);
    }
    if (!bpm_popup_eligible_marker(mv[static_cast<size_t>(owner)]))
        return refused(BpmSweepRefusal::OwnerIneligible);
    if (warp_red_flag_set_cached(a, audio.sample_rate(),
                                 static_cast<long>(audio.total_frames()))
            .collapsed.count(owner))
        return refused(BpmSweepRefusal::OwnerCoincident);
    for (int i = owner + 1; i <= last_sel; ++i) {
        const GuiWarpMarker& m = mv[static_cast<size_t>(i)];
        if (m.tempo_inherits)      continue;
        if (effective_disabled(mv, i)) continue;
        if (m.tempo_cents != mv[static_cast<size_t>(owner)].tempo_cents ||
            m.tempo_scale != mv[static_cast<size_t>(owner)].tempo_scale)
            return refused(BpmSweepRefusal::MixedTempos);
    }

    BpmSweepPlan plan;
    plan.owner    = owner;
    plan.boundary = boundary;
    return plan;
}

// The DIRECTIONAL half of the Up / Down face, forking exactly where
// adjust_tempo_cents forks — the group scan above at 2+, the focused OWNER's
// own clamped landing at a singleton. The full ruling, the deliberate
// grey-and-card pairing on the group arm, and the value-shaped tails this
// deliberately answers TRUE for are at the declaration (app_state.h).
bool tempo_cent_step_direction_actionable(const AppState& a,
                                          const GuiAudio& audio,
                                          int64_t delta_cents) {
    if (a.selected_markers.size() >= 2)
        return tempo_cent_step_group_actionable(a, audio, delta_cents);
    const auto& mv = a.warpmarkers.markers();
    const int   f  = a.last_selected_marker;
    if (f < 0 || f >= static_cast<int>(mv.size())) return true;  // belt
    const GuiWarpMarker& m = mv[static_cast<size_t>(f)];
    // A PASS ALWAYS FREEZES and so always changes; a LABEL REF and the target
    // view's payload refusals keep a live face and a card of their own. Only
    // an OWNER can rest on a wall, and its wall is the landing owner's own
    // answer.
    if (m.tempo_inherits || !m.label_ref.empty()) return true;
    return tempo_cent_step_landing(m.tempo_cents, delta_cents) != m.tempo_cents;
}

GuiOpRefusal GuiWarpMarkersOps::adjust_tempo_cents_group(
        int64_t delta_cents, bool synthesized_repeat) {
    // THE WALL SCAN IS THE CONST OWNER ABOVE since 2026-08-31
    // (tempo_cent_step_group_actionable — the wall set, the red-flag cache
    // read and the direction's bracket edge, all of it): ANY walled member
    // refuses the whole press, and the Up / Down buttons now grey on that same
    // answer.
    // AND IT RUNS AHEAD OF THE COALESCE VERDICT (2026-08-31, converting codex
    // round A's MED finding; it sat behind it from 2026-07-29): A WALL NO-OP
    // TOUCHES NOTHING. The verdict's call invalidates the coalescing stamp on a
    // physical press, so with the scan behind it a refused KEY press split the
    // previous undo run while the same press on the GREYED button never
    // dispatched and left it whole — one wall, two undo behaviours. The
    // discriminator is the FACE and not the card: this refusal KEEPS its card,
    // and it moves anyway because the button greys on exactly this predicate.
    // A press that PASSES the scan is unaffected: the scan is const, reads no
    // coalescing state, and the verdict it then computes is the one it always
    // was.
    // IT STILL SAYS SO, AND THAT PAIRING IS DELIBERATE (architect 2026-08-31,
    // the refinement arc's rule pair): a command whose effect spreads across
    // the screen keeps its card even where its button greys — a group step
    // would have moved every selected flag's value, so it is not the
    // one-dimensional already-at-its-state refusal that went silent that day.
    // The sentence is one for the whole wall set (a pass, a ref, a
    // coincident-collapse member, a marker at the bracket edge) because what
    // the press needs to know is that the GROUP could not move as a group, not
    // which member walled — naming the member would be a second act's worth
    // of detail for a press that changed nothing. THE EMPTY STEP HAS THE
    // SINGLETON'S SENTENCE (R-12, 2026-09-02): a selection whose every member
    // is effectively disabled is invisible whole, and "the step wants an
    // enabled warp marker and has none" is the empty-selection answer
    // tempo_cent_step_actionable already gives — one sentence for the one
    // fact, and the Up / Down pair greys on the same verdict.
    switch (tempo_cent_step_group_verdict(app, audio, delta_cents)) {
    case TempoCentStepGroupVerdict::Steps:
        break;
    case TempoCentStepGroupVerdict::Walled:
        return "One of the selected markers cannot take this tempo change";
    case TempoCentStepGroupVerdict::Empty:
        return "Select a warp marker to change its tempo";
    }
    // THE COALESCE VERDICT, now past the one refusal this act has. It is
    // computed BEFORE the invalidate inside the call (the hybrid's order rule
    // at coalesce_gesture), and `merge` is consumed below; order-independent of
    // the mutation otherwise. On the REPEAT arm coalesce_gesture keys on the
    // kind + the repeat bit and NOT on any marker index,
    // so a held key over the SAME group collapses to one entry (a selection change
    // requires a command, and a command ends the hold). On the TAP arm the
    // selection IS compared (architect 2026-08-01), which is what keeps a rapid
    // re-tap over a RE-MADE group from merging into the previous group's entry —
    // the group is coalesce-eligible unchanged either way, and the Up/Down step is
    // the only route reaching here with kind TempoStep.
    const bool merge =
        undo.coalesce_gesture(GestureKind::TempoStep, synthesized_repeat);
    const auto& mv = app.warpmarkers.markers();
    const int n = static_cast<int>(mv.size());
    std::vector<GuiWarpMarker> pre_state = mv;
    // Apply the press's own signed cent count to each SURVIVING selected
    // member — ±1 bare, ±3 shifted, ±10 with ctrl (plain integer arithmetic —
    // the structural producer discipline). An effectively disabled member is
    // skipped on the SAME call the scan skipped it on (effective_disabled —
    // the store is unchanged between the two, so the two walks see one
    // survivor set), every field untouched; none of the survivors is walled
    // (checked above, and the scan asks whether the member can take the WHOLE
    // step), so every add stays in-bracket and actually changes the value;
    // positions untouched, so no reorder/remap. A member's iteration bracket
    // is never CLEARED by a tempo change, but it RIDES the new base per member
    // (the retroactive clamp, owner clamp_iter_bracket_to_tempo_bracket in
    // warpmarkers.h): the group's rigidity is about the stepped VALUES, and
    // each member's cells are its own, so clamping member by member deforms
    // no group relationship. The undo entry's touched hints are the
    // survivors alone — the skipped members changed nothing.
    std::vector<int> touched;
    for (int idx : app.selected_markers) {
        if (idx < 0 || idx >= n) continue;
        if (effective_disabled(mv, idx)) continue;   // invisible to the act
        GuiWarpMarker* m = app.warpmarkers.marker_mut(idx);
        if (!m) continue;
        m->tempo_cents = m->tempo_cents + delta_cents;
        clamp_iter_bracket_to_tempo_bracket(*m);
        touched.push_back(idx);
    }
    // Defensive (a fully-stale selection): a belt against an invariant the
    // selection layer keeps, so it says nothing (GuiOpRefusal's contract). The
    // all-invisible selection never reaches it — that is the Empty verdict,
    // refused ahead of the coalesce stamp above.
    if (touched.empty()) return std::nullopt;
    // ONE undo entry per press, with identity hints: no reorder happens
    // (positions untouched), so touched_snapshot == touched_live == the stepped
    // indices. A coalesced repeat skips the push: THE BURST'S OPENER owns the
    // pre-burst snapshot and its hints — a held KEY's own physical press, a
    // held arrow BUTTON's first fire, the two-surface rule stated once at
    // Undo::coalesce_gesture. NO SELECTION-DRIVEN STEM WORK IS
    // OWED HERE: since row 5 every enabled marker stems always, in its CLASS
    // colour alone, so a selection neither creates nor moves a stem — the
    // group's cue is its members' brightened flags plus the landed cursor.
    // (The skip is the whole per-press action: note_coalesced_commit died with
    // the hover popup in row 5.)
    if (!merge) undo.push_undo_warp(std::move(pre_state),
                                    /*affects_persistence=*/true,
                                    touched, touched);
    // Settle the burst, POST-mutation: the stamp, or — on a merged press that
    // stepped every member back to the burst entry's own snapshot — the
    // BYTE-EQUAL POP of that entry (the rule is at Undo::record_gesture). The
    // group's all-or-nothing rigidity is what makes the whole store the honest
    // subject of that compare.
    undo.record_gesture(GestureKind::TempoStep, merge);
    undo.recompute_dirty();
    viewport.invalidate_top_strip();
    // AND THE WAVEFORM, for the STEMS (row 5): a tempo step can move a marker in
    // or out of the RED set (a value that normalizes to the 1.00 fallback), and
    // the stem carries its class's colour now — #da4453 for red, the calm purple
    // otherwise. In SOURCE view nothing else here damages the waveform at all, so
    // without this the stem would keep its old colour until some unrelated
    // repaint. In target view the synchronous re-warp below repaints anyway; this
    // is the cheaper honest owner for both.
    viewport.invalidate_waveform_area();
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
        // NOTHING TO DO FOR THE REGION HERE (architect 2026-07-30, with the SPAN
        // FORM retired): the region IS THE TRIM, not this selection's extent,
        // so the group step no longer maintains it — the re-derive that stood
        // below the kick is deleted with its owner. RE-DERIVED 2026-08-18 and
        // now true for a stronger reason: the overlay is DERIVED from the trim
        // every frame (trim_overlay_span, app_state.h), so a step that re-warps
        // the target simply re-derives it in the new domain on the next frame.
        // (The old belt — "a region cannot even rest beside this selection
        // anyway, both formers deselecting at press" — is retired: bare `[`
        // shows the overlay and writes no selection, so a shown overlay may now
        // rest beside any selection. And the kick's live-domain reclamp no
        // longer clears anything of the region's: it had validated the stored
        // ACTIVE-domain endpoints, which no longer exist, and it is deleted at
        // its own site, viewport.cpp.)
        viewport.kick_waveform_sync();
        const int f = app.last_selected_marker;
        if (f >= 0 && f < n) {
            // THROUGH THE RESEAT, the singleton arm's twin and for its reason:
            // a map-change re-land is a translation, not a movement, so it must
            // leave the trim region overlay standing (reseat_playhead_to,
            // viewport.h; the rule at clear_region_highlight, input_handler.h).
            viewport.reseat_playhead_to(source_frame_to_active_domain(
                app, audio, app.warpmarkers.markers()[f].time_frame));
        }
        // No selection-driven stem work here either: stems are class-colored
        // and always on, so the re-selection moves none (the members'
        // brightened flags plus the re-landed cursor are the group's cue). The
        // kick_waveform_sync above already repainted the moved images, stems
        // included.
    }
    target_render.trigger();
    return std::nullopt;
}

// -- THE ITERATION BOUND STEP (architect 2026-09-04) --------------------------
//
// The vertical arrows' second body. On the road he does not type, and grid
// iterations was the one mode that needed the keyboard — the bracket was
// typed into the flag editor as text — so the two bounds became CELLS on
// the flag, each addressable by a click and stepped by the arrows (and since
// 2026-09-05 each with its own editor, the flag editor carrying no bracket
// at all). What
// follows mirrors adjust_tempo_cents' shape one clause at a time: the leading
// refusal block named whole in a predicate the face reads, the 2+ fork onto an
// all-or-nothing group arm, the wall asked through the directional face AHEAD
// of the coalesce stamp, the value-shaped kind refusal behind it on a card,
// the mutation through the one landing owner, the entry and the settle. The
// contracts are at the declarations (warpmarkers_ops.h, app_state.h's bound
// step block); what is argued here is only what differs from the tempo step.
//
// WHAT DIFFERS. The subject is a bound in the bracket's delta domain, not the
// base tempo, so the walls are the clamp window and the partner bound
// (iter_bound_step_landing). The eligibility is the sweep's own
// (iter_popup_eligible_marker): a marker without a tempo of its own has no
// bracket to step, and a disabled owner's bracket is dormant — so the group
// arm SKIPS those members as the tempo arm skips a disabled one, and the
// singleton REFUSES them on a card. A blank bracket starts at [0, 0] and the
// first step authors it, both bounds written through the one write site
// (iter_bound_step_write, app_state.h) — which is also where the step and
// the cell editor's commit meet one rule, that a pair of two zeroes is the
// cleared bracket, so a step can author a bracket but never come to rest on
// the blank one. AND
// NOTHING
// RENDERS: a bracket is not a map input (excluded from build_warp_frame_map
// and from the render recipe alike, the flag editor's bracket-only commit
// being the precedent), so there is no trigger, no target-view re-warp, no
// re-land and no target-view kind refusal — the step is target-legal as the
// bracket is. The entry is the bracket-only kind (affects_persistence false,
// so the dirty dot never lights for it) and the damage is the marker lane's.

// THE GROUP BOUND STEP'S WALL SCAN — the contract is at the declaration
// (app_state.h). A const walk, extracted for the same reason the tempo scan
// was: the act reads the verdict and the Up/Down face reads its boolean
// wrapper, so the wall set has one spelling.
IterBoundStepGroupVerdict iter_bound_step_group_verdict(const AppState& a,
                                                        MarkerCell side,
                                                        int64_t delta_cents) {
    const auto& mv = a.warpmarkers.markers();
    const int   n  = static_cast<int>(mv.size());
    int survivors = 0;
    for (int idx : a.selected_markers) {
        if (idx < 0 || idx >= n) continue;   // defensive; stale indices skipped
        if (!iter_popup_eligible_marker(mv, idx)) continue;   // invisible
        ++survivors;
        const GuiWarpMarker& m = mv[static_cast<size_t>(idx)];
        // CAN THIS MEMBER TAKE THE WHOLE STEP — the tempo scan's own test in
        // the delta domain: the landing owner clamps at the window and at the
        // partner, and the clamp bites iff the member cannot take the full
        // step, which is what GROUP RIGIDITY refuses on.
        const int64_t start = side == MarkerCell::Upper
                                  ? m.iter_end_cents.value_or(0)
                                  : m.iter_start_cents.value_or(0);
        if (iter_bound_step_landing(m, side, delta_cents) != start + delta_cents)
            return IterBoundStepGroupVerdict::Walled;
    }
    return survivors > 0 ? IterBoundStepGroupVerdict::Steps
                         : IterBoundStepGroupVerdict::Empty;
}

// The DIRECTIONAL half of the Up/Down face with a bound addressed — the
// contract is at the declaration (app_state.h). Forks where the act forks.
bool iter_bound_step_direction_actionable(const AppState& a,
                                          MarkerCell side,
                                          int64_t delta_cents) {
    if (a.selected_markers.size() >= 2)
        return iter_bound_step_group_actionable(a, side, delta_cents);
    const auto& mv = a.warpmarkers.markers();
    const int   f  = a.last_selected_marker;
    if (f < 0 || f >= static_cast<int>(mv.size())) return true;  // belt
    // An INELIGIBLE focus keeps a live face and a card of its own (the kind
    // refusal below); only an eligible marker's bound can rest on a wall, and
    // its wall is the landing owner's own answer.
    if (!iter_popup_eligible_marker(mv, f)) return true;
    const GuiWarpMarker& m = mv[static_cast<size_t>(f)];
    const int64_t start = side == MarkerCell::Upper
                              ? m.iter_end_cents.value_or(0)
                              : m.iter_start_cents.value_or(0);
    return iter_bound_step_landing(m, side, delta_cents) != start;
}

// THE SINGLETON'S KIND REFUSAL — the contract and the readers are at the
// declaration (app_state.h). Two sentences for the three ineligible kinds: a
// pass and a label ref share one fact (no tempo of their own, so no bracket
// to ride it), and a disabled owner's is that its bracket is dormant.
const char* iter_bound_step_kind_refusal(const AppState& a) {
    if (a.selected_markers.size() >= 2) return nullptr;
    const auto& mv = a.warpmarkers.markers();
    const int   f  = a.last_selected_marker;
    if (f < 0 || f >= static_cast<int>(mv.size())) return nullptr;
    const GuiWarpMarker& m = mv[static_cast<size_t>(f)];
    if (!iter_bracket_carrier(m))
        return "Only a marker that owns its tempo has a range";
    if (effective_disabled(mv, f))
        return "A disabled marker's range is dormant";
    return nullptr;
}

GuiOpRefusal GuiWarpMarkersOps::adjust_iter_bound_cents(
        MarkerCell side, int64_t delta_cents, bool synthesized_repeat) {
    // THE LEADING REFUSAL BLOCK, named whole (iter_bound_step_actionable) and
    // read by the Up/Down face too, so no lift reaches it. One sentence for
    // the mode, the column, an empty selection and a missing focus: the step
    // wants a focused warp marker's range and has none.
    if (!iter_bound_step_actionable(app))
        return "Select a warp marker to change its range";
    if (app.selected_markers.size() >= 2)
        return adjust_iter_bound_cents_group(side, delta_cents,
                                             synthesized_repeat);
    // THE WALL, AHEAD OF THE COALESCE STAMP — the face greys on it, so the
    // key must leave the stamp exactly as the greyed button does (the rule at
    // Undo::coalesce_gesture). Silent: a benign one-dimensional refusal
    // already at its state, the cell's own value being the place to glance.
    if (!iter_bound_step_direction_actionable(app, side, delta_cents))
        return std::nullopt;
    const bool merge =
        undo.coalesce_gesture(GestureKind::IterBoundStep, synthesized_repeat);
    // THE KIND REFUSAL, behind the stamp with a live face and a card, as the
    // tempo step's value-shaped tails are.
    if (const char* refusal = iter_bound_step_kind_refusal(app))
        return refusal;
    const auto& mv_const = app.warpmarkers.markers();
    const int f = app.last_selected_marker;
    // A stale focus is a belt against the selection layer's own invariant
    // (in range by iter_bound_step_actionable above), so it says nothing.
    if (f < 0 || f >= static_cast<int>(mv_const.size())) return std::nullopt;
    std::vector<GuiWarpMarker> proposed = mv_const;
    GuiWarpMarker& m = proposed[static_cast<size_t>(f)];
    const int64_t landing = iter_bound_step_landing(m, side, delta_cents);
    // Both bounds go through the one write site (iter_bound_step_write): a
    // blank bracket becomes [0, 0] with the step applied to its addressed
    // side, a set one keeps its partner as it was, and a pair that lands on
    // two zeroes clears — the blank rule, which the group arm below and the
    // cell editor's commit take from the same owner. The landing owner
    // already holds lo <= hi and
    // the clamp window, so the retroactive clamp has nothing to do here and is
    // not called.
    iter_bound_step_write(m, side, landing);
    // Unreachable past the wall test above: the directional face admitted
    // this press only because the landing differs from the resting bound, and
    // the write moves the store either way — it sets the addressed side to a
    // value the bracket did not hold, or it clears a bracket that was set (an
    // already-blank one cannot land on [0, 0]: its start is 0 and the partner
    // walls the landing there). Kept as the belt it is.
    if (m.iter_start_cents == mv_const[static_cast<size_t>(f)].iter_start_cents &&
        m.iter_end_cents   == mv_const[static_cast<size_t>(f)].iter_end_cents)
        return std::nullopt;
    std::vector<GuiWarpMarker> pre_state = mv_const;
    app.warpmarkers.markers_mut() = std::move(proposed);
    // The bracket-only entry: session-only fields, never serialized, so the
    // dirty dot stays where it is (recompute_dirty honours the flag), and it
    // carries the ADDRESSED CELL, so undoing this step brightens the bound it
    // moved (push_undo_iter_bracket, undo.h). A coalesced repeat skips the
    // push, the burst's opener owning the pre-burst snapshot — and its cell,
    // which the coalesce verdict has already found equal to this press's.
    if (!merge) undo.push_undo_iter_bracket(std::move(pre_state));
    // Settle the burst, POST-mutation: the stamp, or the byte-equal pop of a
    // merged press that stepped the bound back to the burst entry's own
    // snapshot (the rule at Undo::record_gesture; the row comparator reads
    // the iter fields).
    undo.record_gesture(GestureKind::IterBoundStep, merge);
    undo.recompute_dirty();
    // The marker lane repaints its cells — the store's generation moved, so
    // the flag cache rebuilds under the top strip's damage. No waveform
    // damage: a stem reads the class, and a bound changes no class; no map
    // moved, so no image moved.
    viewport.invalidate_top_strip();
    return std::nullopt;
}

GuiOpRefusal GuiWarpMarkersOps::adjust_iter_bound_cents_group(
        MarkerCell side, int64_t delta_cents, bool synthesized_repeat) {
    // THE WALL SCAN, ahead of the coalesce verdict, carded AND greyed — the
    // group pairing the tempo step argues (adjust_tempo_cents_group): a group
    // step would have moved every selected cell, so it is not the
    // one-dimensional refusal that went silent. THE EMPTY STEP HAS THE
    // SINGLETON'S SENTENCE: a selection whose every member is ineligible has
    // no range to step, which is the empty-selection answer.
    switch (iter_bound_step_group_verdict(app, side, delta_cents)) {
    case IterBoundStepGroupVerdict::Steps:
        break;
    case IterBoundStepGroupVerdict::Walled:
        return "One of the selected markers cannot take this range change";
    case IterBoundStepGroupVerdict::Empty:
        return "Select a warp marker to change its range";
    }
    const bool merge =
        undo.coalesce_gesture(GestureKind::IterBoundStep, synthesized_repeat);
    const auto& mv = app.warpmarkers.markers();
    const int n = static_cast<int>(mv.size());
    std::vector<GuiWarpMarker> pre_state = mv;
    // Every SURVIVOR steps its addressed bound by the full delta — none is
    // walled (checked above through the landing owner, so the add lands
    // exactly where the landing says) — through the same write site the
    // singleton uses, so a blank bracket is authored at [0, 0] plus the step
    // and the blank rule reaches every member alike. An ineligible member is
    // skipped on the same predicate the scan skipped it on; the store is
    // unchanged between the two walks, so the survivor set is one.
    std::vector<int> touched;
    for (int idx : app.selected_markers) {
        if (idx < 0 || idx >= n) continue;
        if (!iter_popup_eligible_marker(mv, idx)) continue;   // invisible
        GuiWarpMarker* m = app.warpmarkers.marker_mut(idx);
        if (!m) continue;
        const int64_t landing = iter_bound_step_landing(*m, side, delta_cents);
        iter_bound_step_write(*m, side, landing);
        touched.push_back(idx);
    }
    // Defensive (a fully-stale selection): the all-ineligible selection never
    // reaches it — that is the Empty verdict above.
    if (touched.empty()) return std::nullopt;
    // ONE bracket-only entry per press with its identity hints (no reorder —
    // positions untouched — so the push fills both coordinate spaces from the
    // one list) and with the ADDRESSED CELL, which the restore puts back on
    // the focus; a coalesced repeat skips the push, the burst's opener owning
    // the snapshot.
    if (!merge)
        undo.push_undo_iter_bracket(std::move(pre_state), std::move(touched));
    undo.record_gesture(GestureKind::IterBoundStep, merge);
    undo.recompute_dirty();
    viewport.invalidate_top_strip();
    return std::nullopt;
}

// Nudge the FOCUSED warp marker by exactly one on-screen pixel column per press.
// direction: -1 for earlier (up/left), +1 for later (down/right).
//
// HORIZONTAL MOVEMENT IS A FOCUS ACT — GROUPS ARE NEVER MOVED (architect
// 2026-07-29): a 2+ selection COLLAPSES TO ITS FOCUS in the shared prologue (which
// also lands the playhead there) and the focus takes this one step, so the body
// below is the singleton op unconditionally. The doctrine, the group-verb rule it
// instances, and the dead per-member machinery are recorded once at the head of
// position_nudge.h.
//
// SOURCE HOME VIEW ONLY (the home-view binding, architect 2026-07-22 — restored
// 2026-07-24 second pass: the same-day "third exception", a both-views warp
// position branch, was a misreading and is DELETED; there is no warp position
// authoring in target view at all). In W+TARGET bare Left/Right is a CONSUMED
// REFUSAL as of 2026-07-29 — it dispatched the tempo-image step there until that
// whole family was deleted (marker_drag.h), and there is no fallback — with the
// dispatch site in input_handler.cpp owning the routing.
//
// The wall regime over the identity map, one shape: the marker steps the
// press's own count of PAINTED columns — one bare, three under shift, ten under
// ctrl since 2026-08-31 (the step ladder, arrow_step_magnitude in gui_input.h)
// — through stepped_anchor_frame (the guarantee and its numeric rationale live
// in the comment there), and its delta is CLAMPED into its own wall headroom, walls
// exactly reachable — the unified wall policy, stated once at the head of
// position_nudge.h. Crossing a neighbor is legal and goes through the
// reorder-and-remap below; the render boundary collapses an exact-frame tie to one
// 1.00 owner.
GuiOpRefusal GuiWarpMarkersOps::nudge_selected_markers(
        int step_columns, bool synthesized_repeat) {
    // Shared guard prologue: the WHOLE refusal set as one predicate (the Left /
    // Right buttons' own marker_nudge_actionable — the state and geometry
    // guards, the focused-index belt and THE WALL, all of it ahead of the
    // coalesce stamp), then the coalesce verdict, then THE COLLAPSE + LAND that
    // makes this a focus act — which carries the collapse's own playback stop
    // (the playhead-follows / lead-in rationale lives at the declaration). ITS
    // refusals say NOTHING and that is the reason channel's own rule
    // (GuiOpRefusal, warpmarkers_ops.h): every one of them is either an OUTER
    // gate's card already (the loading gate's, the dispatch's home-view card),
    // a belt against an invariant the selection layer keeps, or the wall, whose
    // silence is paired with a greyed button — the prologue's own declaration
    // names each. `step_columns` is passed for the wall term alone.
    const PositionNudgePrologue pro = position_nudge_prologue(
        app, audio, playback_lifecycle, selection, viewport, undo,
        GestureKind::WarpNudge, synthesized_repeat, step_columns);
    if (!pro.ok) return std::nullopt;
    const bool merge = pro.merge;
    const auto& mv = app.warpmarkers.markers();
    const int   f  = pro.focused;   // validated in [0, mv.size()) by the prologue

    const int64_t orig_f = mv[f].time_frame;

    // THE WALL-REGIME MIDDLE, through the shared landing owner since
    // 2026-08-31 (position_nudge_landing, position_nudge.h — the painted-column
    // step, the headroom clamp that makes the wall exactly reachable, and the
    // walls-win belt, all three argued at its declaration): the twins' two
    // verbatim copies became ONE when the Left / Right buttons' face needed the
    // landing as a const owner to compare against, a caller-side copy being the
    // drift the truthful-buttons rule exists to prevent. The anchoring basis is
    // the DISPLAYED map there, which in warp's SOURCE home view is the empty
    // identity map — every commit a plain integer frame, and the working-zoom
    // authoring-grid bit-exactness claims (all source-view) hold. Crossing a
    // neighbor is legal and goes through the reorder-and-remap below.
    const int64_t committed_f =
        position_nudge_landing(app, audio, orig_f, step_columns);
    // POST-CLAMP IDENTITY IS A SILENT NO-OP: a press already resting on its wall
    // (or one whose column step resolved to the same frame) writes NOTHING — no
    // undo push, no damage, no playback stop. This is what makes the keyboard stop
    // rule's refusal gating exact for the nudges.
    // AND IT IS SILENT AGAIN SINCE 2026-08-31 (architect, retiring the
    // one-day card "The marker is already at the edge"): a benign
    // one-dimensional refusal already at its state says nothing — the marker
    // is one flag in one place, and the glance that asks whether it moved is
    // the glance that answers. (A zero-step press cannot arise off a wall
    // anyway, the authored frame grid being finer than the pixel grid — the
    // one-column-per-press guarantee at stepped_anchor_frame.)
    // WHAT STILL REACHES IT is the 2+ press whose FOCUS rests on a wall: the
    // prologue asks the same landing ahead of the coalesce stamp now, so a
    // SINGLETON at its wall never gets this far (the wall no-op touches
    // nothing — the rule is at the prologue), while a group press passes that
    // term unconditionally, collapses, lands, and then finds its wall here.
    if (committed_f == orig_f)
        return std::nullopt;
    // THE SINGLETON PRESS'S STOP, past every refusal and immediately ahead of the
    // first write (the keyboard stop rule's refusal gating, at
    // stop_playback_if_playing's declaration in playback_lifecycle.h): a position
    // nudge collapses the selection to point form and takes the playhead with it.
    // On a 2+ press the prologue's collapse arm already stopped; this second call
    // early-returns on the stopped session, so the double call is free.
    playback_lifecycle.stop_playback_if_playing();

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
    // Coalesce a held key or button: THE BURST'S OPENER pushed the pre-burst
    // snapshot with the identity hints — a held KEY's own physical press, a
    // held arrow BUTTON's first fire, the two-surface rule stated once at
    // Undo::coalesce_gesture; each synthesized repeat behind it skips the
    // redundant push and instead REFRESHES the surviving entry's touched_live
    // to this fire's post-reorder index (touched_snapshot stays the opener's pre-burst
    // coordinates — a restore produces that snapshot, and the whole hold moves the
    // same marker, since a selection change needs a command and a command ends the
    // hold). The post-mutation re-record happens in the shared tail.
    if (merge) {
        undo.refresh_coalesced_touched_live(std::move(touched_live));
    } else {
        // The warp POSITION NUDGE (the receiving marker's own image slides). A
        // restore owes no stem bit: stems key on the MARKER (always on,
        // class-colored), never on the selection.
        undo.push_undo_warp(std::move(pre_state),
                            /*affects_persistence=*/true,
                            std::move(touched_snapshot),
                            std::move(touched_live));
    }
    // Shared commit tail: record/dirty/invalidate (its full-waveform damage moves
    // the nudged marker's always-on stem), playhead follow (committed_f is
    // reorder-independent, and hiding the overlay through the movement owner
    // it writes with), and the
    // view-independent target trigger (no synchronous re-warp — source-view warp
    // pixels don't depend on the map). Ordering rationale at the declaration.
    finish_position_nudge(app, audio, viewport, undo,
                                GestureKind::WarpNudge, merge, committed_f,
                                target_render);
    return std::nullopt;
}
