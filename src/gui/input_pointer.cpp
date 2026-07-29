#include "input_handler.h"

#include "gui_display_context.h"
#include "warp_frame_map_view.h"
#include "marker_drag.h"
#include "paint_handler.h"
#include "render.h"
#include "text_editor.h"
#include "warpmarkers.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

// GuiInputHandler pointer-gesture handlers (on_button_press,
// on_button_release, on_motion) and the editor-text drag finalizer
// (finalize_editor_text_drag), lifted
// verbatim from input_handler.cpp. The methods are declared on
// GuiInputHandler in input_handler.h, and the platform layer's pointer
// callbacks dispatch to them unchanged, so this is a pure definition move.
//
// The file-local ActiveEditorText / active_editor_text /
// set_editor_caret_from_x helpers live here because the press / motion /
// release editor-text drag-select is the only consumer; press, motion, and
// release all resolve active_editor_text so they agree on the editor's text
// origin and which strip to repaint.
//
// apply_editor_clipboard is intentionally NOT here — it is a keyboard
// clipboard helper used only by on_key, and stays in input_handler.cpp.

// F2.1: mouse drag-to-select for the three text editors. The selection
// highlight is already painted from the editor State's selection_anchor /
// cursor_pos, so the whole gesture is input-side: a press sets the anchor
// and arms the drag, motion moves cursor_pos (extending the highlight),
// release finalizes. The only per-editor geometry the mouse path needs is
// each editor's char-0 text origin; advance is the shared monospace cell.
namespace {

// monotonic_ms() (the press-driven CLOCK_MONOTONIC ms time base for double-click
// detection) is now the shared reader declared in app_state.h — one owner, no
// per-TU clock copy.

// Active-domain playhead frame at click column `col`. SOURCE view: the exact
// source grid (source_grid_position_at_column via painter q), matching marker
// commits so a drop-at-playhead lands where a drag/nudge would. TARGET view:
// the domain spp form — the source-frame commit routes through the inverse map,
// so there is no source-grid claim there.
int64_t playhead_frame_at_click_column(const AppState& app,
                                       const GuiAudio& audio, int col) {
    const GuiDisplayContext& ctx = active_display_context(app, audio);
    if (ctx.domain == GuiDisplayDomain::Source) {
        const double q = painter_samples_per_pixel(app, audio, waveform_area(app));
        if (q > 0.0)
            return static_cast<int64_t>(std::nearbyint(
                source_grid_position_at_column(app.viewport_start_sample, col, q)));
    }
    const double spp = current_samples_per_pixel(app, audio);
    return app.viewport_start_sample +
        static_cast<int64_t>(std::nearbyint(static_cast<double>(col) * spp));
}

// The active editor's resolved text geometry, valid only while exactly one
// editor is active (and, for the flag editor, on-view). Press / motion /
// release all resolve this so they agree on origin and which strip to
// repaint.
struct ActiveEditorText {
    bool                valid        = false;
    text_editor::State* ed           = nullptr;  // the active editor
    double              text_left    = 0.0;       // char-0 origin (px)
    double              advance      = 0.0;
    bool                bottom_strip = false;      // which strip to repaint
};

ActiveEditorText active_editor_text(AppState& app, const GuiAudio& audio) {
    ActiveEditorText g;
    const double adv = monospace_advance();
    if (adv <= 0.0) return g;
    if (text_editor::is_active(app.settings_editor)) {
        g.ed = &app.settings_editor;
        g.text_left = editor_text_glyph0_x(
            static_cast<double>(timestamp_pad_x()), kSettingsEditorPrefix);
        g.bottom_strip = true;
    } else if (text_editor::is_active(app.commit_editor)) {
        g.ed = &app.commit_editor;
        g.text_left = editor_text_glyph0_x(
            static_cast<double>(timestamp_pad_x()), kCommitEditorPrefix);
        g.bottom_strip = true;
    } else if (text_editor::is_active(app.top_flag_editor) &&
               app.top_flag_editor.kind == text_editor::Kind::BpmBracket) {
        g.ed = &app.top_flag_editor;
        g.text_left = editor_text_glyph0_x(
            static_cast<double>(timestamp_pad_x()), kBpmEditorPrefix);
        g.bottom_strip = true;
    } else if (text_editor::is_active(app.top_flag_editor)) {
        // FlagPayload — marker-text lane. text_left is the lane run's left
        // edge (flag_pending_text_left_x); it is -1 only for an invalid editor
        // target (a valid off-view marker still yields a clamped onscreen
        // origin, so the caret math keeps working while the text stays visible).
        const double tl = flag_pending_text_left_x(
            app, audio, app.top_flag_editor.target);
        if (tl < 0.0) return g;   // invalid target: leave invalid
        g.ed = &app.top_flag_editor;
        g.text_left = tl;
    } else {
        return g;
    }
    g.advance = adv;
    g.valid = true;
    return g;
}

void set_editor_caret_from_x(const ActiveEditorText& g, int mouse_x) {
    const int idx = text_editor::byte_index_from_click_x(
        static_cast<double>(mouse_x), g.text_left, g.advance,
        static_cast<int>(g.ed->pending.size()));
    g.ed->cursor_pos = idx;
}

// Region-drag end: dissolve a resting region whose on-screen span is under the
// arm gate. The press-becomes-drag gate (kDragMovedThresholdPx) latches once
// and never re-engages, so a hand-jitter drag that crosses the gate then
// releases near the press — or wanders back toward it — can rest a sliver
// region a pixel or two wide. That was never an intentional window: a
// sub-threshold rest reads as a click, so it dissolves exactly as a plain
// click's would, clearing the highlight and the split playhead (the cursor
// playhead
// returns when the region deactivates, which the same damage covers). The
// resting-region minimum floor SCALES with the gate (deliberate): reading
// kDragMovedThresholdPx here means the sliver floor rose to 8px when the
// architect unified the gate 2026-07-24, so the smallest region that can rest
// tracks the smallest press that becomes a drag. Called
// from both region-drag end points (release and button-lost). Only the REST is
// gated — the live mid-drag extension paints slivers freely. Under
// SELECTION-FLOWS-DOWNWARD-ONLY (architect 2026-07-23) the drag never touches
// the selection: the press's deselect/demote was the committed act, and the
// drag holds no selection to clear — so this dissolve drops the region alone.
void end_region_drag_min_size_check(AppState& app, const GuiAudio& audio,
                                    Viewport& viewport) {
    if (!app.region.active) return;
    const double spp = current_samples_per_pixel(app, audio);
    if (spp <= 0.0) return;   // no geometry -> leave the region as-is
    const double span_px =
        std::abs(static_cast<double>(app.region.a_frame - app.region.b_frame)) /
        spp;
    if (span_px < kDragMovedThresholdPx) {
        app.region = RegionState{};
        viewport.invalidate_waveform_area();
    }
}

} // namespace

// THE MARKER LANE OWNS THE PLAYHEAD (architect 2026-07-28) — the rule this
// helper serves, stated ONCE here; the other landing sites carry only their own
// class plus a pointer back to this comment. With a non-empty selection the
// cursor playhead stops painting and the FOCUSED flag's ink triangle IS the
// playhead (the lane model, GuiInputHandler::playhead_in_marker_lane), so:
//   * any route that changes WHICH marker is focused while the lane is active
//     LANDS the playhead on the new focus — otherwise a flag asserts it is the
//     playhead while playhead_cursor_sample rests somewhere else entirely, and
//     Space plays from that stale spot;
//   * any route that LEAVES the lane (empties the selection) leaves the playhead
//     exactly where it is, for the cursor to paint again — Home/End clear rather
//     than land for precisely that reason.
// A LAND IS ALWAYS ON THE FOCUS (architect 2026-07-28, closing the rule): the
// three MULTI-SELECT clicks used to land at the earliest selected while focusing
// elsewhere, and no longer do — the shift-range click lands on the clicked range
// END, and both ctrl-toggle arms land on whichever marker the toggle leaves
// focused. So there is no such thing as a focus set whose land went somewhere
// else: the "towed" category is empty, and the nudge/drag follow tows nothing.
// Auditioning a range FROM ITS START is unaffected and never depended on this:
// Space launches from an active region's LEFT BOUND, and those same clicks set
// the extent region (set_region_to_selection_extent, below).
// The landing sites are enumerated below (they are this function's callers, plus
// the group-image gestures that re-land through move_playhead_to at their own
// commits: the two group position nudges, the tempo-image step, and both tempo
// steps' target-view re-warp tails).
//
// THE PLAYHEAD HAS TWO FORMS, POINT AND SPAN, AND EXACTLY ONE IS EVER VISIBLE
// (architect 2026-07-29) — the frame this land now sits inside. The POINT form
// is the cursor (empty selection) or the singleton stem / focused flag; the SPAN
// form is the REGION, which is why an active region dissolves the cursor into
// split half-triangles, suppresses the singleton stem (paint_handler.cpp), and
// is where Space launches from. The region therefore OUTRANKS every point cue,
// and a command that asserts the point form must END the span.
// THE LAND IS A PURE PLAYHEAD WRITE: it has no region side effect whatsoever.
// Each POINT COMMAND owns its own clear_region_highlight AT ITS OWN SITE, and
// owns it UNCONDITIONALLY — never gated on whether the playhead actually moved.
// (The one-day-old 2026-07-28 rule that a land ended a span only when it MOVED
// the playhead retired with the land's clear it existed to decide: conditioning
// the collapse on motion is exactly what left a frozen span resting under a
// marker the arrows then moved, with Space still launching from that stale
// span.) The collapse sites are the plain marker click and its four
// deferred completions, the two multi-select clicks whenever the RESULT is <2
// selected, enter_text_edit (the one chokepoint of every editor open and
// retarget), a SINGLETON position nudge, and the navigation jumps that always
// owned theirs (the list at clear_region_highlight, input_handler.h). The
// SPAN-PRESERVING routes each earn it: the group gestures and the whole TEMPO
// family re-derive or re-sync the highlight they move, Space plays the span, `x`
// consumes it into the trim, `m` re-derives its extent right after the open, and
// pure viewport moves leave it alone. The other half of the model is the
// MEMBERSHIP REPLACE, which clears a SelectionExtent span outright
// (clear_region_on_membership_replace, app_state.h) — that is what covers the
// ops which change WHO is selected without moving anything: sanitize/prune and
// the Esc clear. (The Ctrl+N collapse is NOT one of them — it LANDS the playhead
// on the surviving focus, warpmarkers_ops.cpp, which makes it a point command
// like the clicks; it simply owes no span clear of its own, the membership
// replace already taking a SelectionExtent one.) THREE ops go further and clear ANY
// resting span, provenance and all (architect 2026-07-29): the undo/redo
// restore, the `p` W/P swap, and the propagate paste — each hands the lane a
// selection it took in wholesale, so a span it did not build has nothing left to
// describe (the list lives at clear_region_highlight, input_handler.h). THE
// COROLLARY, recorded there too with its own site list: a 2+ SELECTION NEVER
// RESTS WITHOUT A SPAN — a group whose span is cleared with nothing re-deriving
// one collapses to its focus, because the extent span is a group's only point
// cue and a focused flag must never claim a playhead the group does not own.
//
// LANDS the playhead exactly onto marker `hit` (active column's store), with
// NO viewport move — the sole difference from Tab (which recenters) and `c`
// (which re-zooms and recenters), so the view holds perfectly still while the
// playhead seats. THE CALLER INVENTORY, THE ONE AUTHORITATIVE ENUMERATION,
// re-derived by grep 2026-07-29 (other sites state their own class and point
// here; do not copy this list, re-derive it):
//   * THE POINTER CLICKS — the plain marker click (input_pointer.cpp) and its
//     FOUR deferred-click completions on the release / lost-button paths, the
//     shift RANGE click, and the ctrl TOGGLE click. Each lands on the FOCUS its
//     own path just set: the clicked marker, the clicked range end, the
//     toggled-in marker, or the focus repaired after a toggle-out (an empty
//     post-toggle selection lands nothing);
//   * THE KEYBOARD POINT COMMANDS — the no-region + singleton Esc rung of
//     handle_escape_selection_region (deselect + land on the marker) and the
//     Ctrl+N inherit toggle's collapse (warpmarkers_ops.cpp);
//   * EVERY TEXT-EDITOR OPEN — flag_editor.cpp's enter_text_edit, the one
//     chokepoint of the three open routes;
//   * THE RESTORES, which hand the lane a focus it did not have: BOTH undo/redo
//     marker arms (undo.cpp's visual tail — the singleton lands on its touched
//     marker, the group on its focus, which IS the earliest touched member),
//     BOTH propagate-paste arms (phase_reset_propagate.cpp — the created arm on
//     the FIRST created reset, the no-created arm on whatever focus the restored
//     P slot leaves), and the `p` W/P swap (active_views.cpp);
//   * THE VIEW / TAB SWITCHES, which re-express a focus into a new domain: the
//     `t` S<->T flip (input_handler.cpp) and Ctrl+Tab's tab restore
//     (active_views.cpp). Both land only on a NON-EMPTY selection — with no lane
//     the cursor is the playhead in its own right and keeps its own value;
//   * THE MAP CHANGERS, target view only, which move the focused marker's image
//     out from under a resting cursor: the settings engine-commit
//     (settings_editor.cpp) and the settings-only 'S' undo/redo arm (undo.cpp),
//     each landing AFTER its kick_waveform_sync so the conversion reads the new
//     map — the ordering the singleton tempo step's label-coupling re-land
//     established (that step re-lands through move_playhead_to rather than here,
//     the one map-change re-land that wants the keep-visible scroll).
// The two-step placement
// basis the Tab family lands with (source_frame_to_active_domain then
// clamp_playhead_to_live_domain), against the active column's store, so the
// placement is exactly coincident for a subsequent nudge/drag ride. Direct
// cursor write mirroring jump_playhead_to_focused_marker's non-recenter part —
// NOT move_playhead_to, whose keep-visible edge-align could scroll for a
// half-offscreen flag, and the ruling is NO viewport write of any kind (the
// playhead may rest at a slightly offscreen column when the clicked flag hung
// half off the edge — accepted). Read-only allowed (selection + playhead are
// navigation). Some callers stop playback first (each marker click owns that
// stop at its own site, Tab-family symmetry; the restores and the bpm-editor
// open stop too); the others — the Esc rung, the bare-Return flag-editor open,
// the `p` swap, the Ctrl+N collapse — may land DURING playback, safe because the
// land is a direct RESTING-cursor write and a live scanner is untouched by
// cursor writes (move_playhead_to's scanner-inactive
// convention). External linkage (declared in input_handler.h) so undo.cpp and
// the ops/views TUs can reach it.
void land_playhead_on_marker(AppState& app, const GuiAudio& audio,
                             Viewport& viewport, int hit) {
    int64_t src_frame = 0;
    bool valid = true;
    if (app.active_markers_view == 'P') {
        const auto& tv = app.phaseresetmarkers.markers();
        if (hit < 0 || hit >= static_cast<int>(tv.size())) valid = false;
        else src_frame = tv[hit].time_frame;
    } else {
        const auto& mv = app.warpmarkers.markers();
        if (hit < 0 || hit >= static_cast<int>(mv.size())) valid = false;
        else src_frame = mv[hit].time_frame;
    }
    if (valid) {
        int64_t sample = source_frame_to_active_domain(app, audio, src_frame);
        sample = clamp_playhead_to_live_domain(sample, app, audio);
        // IDEMPOTENCE ONLY, carrying no semantics: a land onto the sample the
        // playhead already holds writes the same value and moves no pixel, so
        // there is nothing to damage. It decides nothing about the region — the
        // caller's own clear (when the caller is a point command) runs either
        // way, before or after this call. Compared AFTER the clamp, because the
        // clamp is what decides where the land actually seats.
        if (sample == app.playhead_cursor_sample) return;
        const double old_px = playhead_pixel_x(app, audio);
        app.playhead_cursor_sample = sample;
        viewport.invalidate_playhead_columns(
            old_px, playhead_pixel_x(app, audio));
        viewport.invalidate_timestamp_area();
    }
}

// The DOWNWARD coupling — the selection defines the extent region,
// SELECTION-FLOWS-DOWNWARD-ONLY (architect 2026-07-23): a
// multi-select CLICK that leaves 2+ markers selected sets the region to the
// selection's active-domain position extent [earliest, latest], so the highlight
// and Space's play-from-region-left-bound agree by construction — auditioning a
// range from its START is the REGION's doing, not the land's (the clicks land on
// the FOCUS since 2026-07-28; see land_playhead_on_marker). A selection of <=1
// sets NOTHING — a result under two markers is a POINT, and the click's own
// clear_region_highlight (its else-arm, run whatever the land did) is what
// collapses the span there. Endpoints
// are clamped through clamp_playhead_to_live_domain (the region domain's
// playable-frame invariant, as every other former clamps). FOUR caller CLASSES:
//   (1) CREATORS — the shift-range and ctrl-toggle click paths, plus the `m`
//       bpm-mode open (input_key_dispatch.cpp), which is the same pattern one
//       gesture over: the open collapses the selection to the span owner and
//       lands there, then the handler restores the span membership and re-derives
//       the extent, so the span cue survives the open. Each MUST run
//       this AFTER its own point-form clear (the click's else-arm
//       clear_region_highlight, or the open chokepoint's clear inside
//       enter_text_edit) — a reorder would let that clear kill this fresh
//       highlight;
//   (2) MAINTAINERS — the group image-moving commits that re-derive an
//       ALREADY-ACTIVE SelectionExtent highlight from the post-commit,
//       reordered/remapped store (never creating one). Every one of them gates on
//       region.active && provenance == SelectionExtent, so a Free / TrimWindow /
//       inactive region is untouched. The class is the group image-movers —
//       position and tempo alike — and its MEMBERSHIP is not enumerated here: the
//       authoritative inventory of the image-follow sites lives at RegionState in
//       app_state.h, and an inline copy here is exactly the list that goes stale;
//   (3) the two MASS-MARKER PROGRAMMATIC selections, which unlike the
//       maintainers MAY CREATE a region: the GROUP undo/redo RESTORE (undo.cpp's
//       visual tail, architect 2026-07-25) and the PROPAGATE PASTE's target-view
//       tail (phase_reset_propagate.cpp, architect 2026-07-29 — reversing its
//       earlier no-region rule; a mass creation should read like the other mass
//       event). Both share one shape: wholesale region clear, select the set,
//       land on its FIRST/earliest member, then derive the extent here.
//   (4) the ESC DEMOTE (handle_escape_selection_region's no-region + 2+ rung) —
//       the one caller that immediately FLIPS the fresh span to Free and drops
//       the selection, deriving the bounds through this owner rather than by
//       hand. The span it leaves has no selection at all, so it is a former, not
//       an extent.
// Touches ONLY the region, never shift_range_anchor, so the shift-range path's
// anchor survives a downward selection->extent set. The REMAINING programmatic
// selections (the drops, Tab/`c`) do NOT call this. Declared in input_handler.h
// so the ops TUs, undo.cpp and the propagate tail can reach it.
void set_region_to_selection_extent(AppState& app, const GuiAudio& audio,
                                    Viewport& viewport) {
    if (app.selected_markers.size() < 2) return;
    const bool phase_reset = (app.active_markers_view == 'P');
    const auto& warp_vec = app.warpmarkers.markers();
    const auto& phase_reset_vec = app.phaseresetmarkers.markers();
    const int n = phase_reset
        ? static_cast<int>(phase_reset_vec.size())
        : static_cast<int>(warp_vec.size());
    int64_t lo = 0, hi = 0;
    bool have = false;
    for (int idx : app.selected_markers) {
        if (idx < 0 || idx >= n) continue;   // defensive; stale indices skipped
        const int64_t src_f = phase_reset
            ? phase_reset_vec[idx].time_frame
            : warp_vec[idx].time_frame;
        const int64_t pos = clamp_playhead_to_live_domain(
            source_frame_to_active_domain(app, audio, src_f), app, audio);
        if (!have) { lo = hi = pos; have = true; }
        else { if (pos < lo) lo = pos; if (pos > hi) hi = pos; }
    }
    if (!have) return;   // every selected index was stale (degenerate)
    app.region.active     = true;
    app.region.a_frame    = lo;
    app.region.b_frame    = hi;
    // The ONE site that sets SelectionExtent provenance: this region IS the
    // selection's extent, so the image-follow gestures may track it. Every
    // legitimate extent re-derive (the multi-select clicks, the position-drag
    // commit, the tempo follows) routes through here, so provenance self-maintains
    // (clear_region_on_membership_replace takes the whole region away on any
    // membership replace).
    app.region.provenance = RegionProvenance::SelectionExtent;
    viewport.invalidate_waveform_area();
}

// R3 Esc ladder (architect 2026-07-23): the selection/region collapse rung of
// the Escape chain, placed after the drag / editor / render cancels and in place
// of the old plain region clear. Defined here beside land_playhead_on_marker /
// set_region_to_selection_extent (external linkage, declared in input_handler.h
// — undo.cpp reaches them there). Returns true
// iff it consumed the Esc. Navigation-class, read-only allowed.
bool GuiInputHandler::handle_escape_selection_region() {
    // DOWN-ONLY ladder (round 4, architect 2026-07-23): markers -> region ->
    // playhead, and a region is already the LOWER rung, so an active region
    // collapses toward the playhead — never into a subregion. This is why
    // region.active is tested FIRST, regardless of selection: the old rung (b)
    // re-derived the region from the selection extent, which SHRANK an active
    // region to the selection's subregion. The ladder walks ONE rung per Esc:
    // for a MULTIMARKER selection resting under an active region, the first Esc
    // clears the SELECTION ONLY (the region stays), and a SECOND Esc then takes
    // the region collapse below.
    if (app.region.active) {
        // First rung under an active region: 2+ selected markers -> clear the
        // SELECTION ONLY; the span's PIXELS REST (architect 2026-07-29, the
        // two-step Ableton walk, reversing the one-Esc-takes-both of that
        // morning). The ladder walks ONE rung per press, so the next Esc reaches
        // the collapse below. The playhead is untouched.
        //
        // ESC IS THE EXPLICIT DEMOTE — the ONE sanctioned route to a resting
        // demoted span, and it is legal under the two-forms model because the
        // SELECTION IS EMPTY afterward: what the model abolished is a span
        // resting beside a SURVIVING selection (the demoted-but-visible extent
        // that asserted a playhead nobody was at), and Esc never produces that.
        // The resting state here is exactly the drag-former's — a Free span with
        // nothing selected. The MEMBERSHIP-CLEAR machinery stays untouched: no
        // demotion route re-enters clear_region_on_membership_replace, which
        // still clears a SelectionExtent slot outright at every membership site.
        // MECHANISM: clear_selection's membership clear kills a SelectionExtent
        // slot, so capture the span first and RE-FORM it at the same bounds with
        // Free provenance afterward. A TrimWindow or Free span is untouched by
        // that clear, so it needs no branch — and must not get one, since
        // overwriting a TrimWindow provenance would unhook the highlight from the
        // chips it tracks.
        if (app.selected_markers.size() >= 2) {
            const RegionState before = app.region;
            selection.clear_selection();
            if (before.provenance == RegionProvenance::SelectionExtent) {
                app.region.active     = true;
                app.region.a_frame    = before.a_frame;
                app.region.b_frame    = before.b_frame;
                app.region.provenance = RegionProvenance::Free;
            }
            viewport.invalidate_waveform_area();
            return true;
        }
        // Region active (0 or 1 selected): collapse to its start (PROVENANCE-BLIND
        // — any active region collapses, SelectionExtent / TrimWindow / Free
        // alike). The region is the stretched-out playhead, so clear it AND the
        // selection (a singleton's coupled extent region dies with the selection
        // here) and land the playhead at lo.
        const int64_t lo = std::min(app.region.a_frame, app.region.b_frame);
        app.region = RegionState{};
        selection.clear_selection();
        viewport.invalidate_waveform_area();
        viewport.move_playhead_to(lo);
        return true;
    }
    const size_t nsel = app.selected_markers.size();
    if (nsel >= 2) {
        // No region + MULTIPLE selected (a PROGRAMMATIC multi-select — a
        // click-made one always rests with its extent region, handled above):
        // DROP THE SELECTION TO ITS SPAN (architect 2026-07-29, restoring the
        // drop-to-region rung). The group's cue survives the deselect as a
        // resting Free span, and a second Esc collapses it to its start — the
        // same two-step walk the rung above performs. This is Esc's explicit
        // demote (the principle is stated at that rung), legal because the
        // selection ends EMPTY.
        // MECHANISM: derive through the ONE extent owner
        // (set_region_to_selection_extent — it clamps the endpoints playable,
        // handles the degenerate all-stale set by resting nothing, and owns its
        // damage), then flip the fresh span to Free BEFORE clear_selection, so
        // the membership clear skips it (it takes SelectionExtent only) and the
        // span survives the deselect. Deriving here by hand instead would
        // duplicate that owner's per-column store walk and clamps for no gain.
        set_region_to_selection_extent(app, audio, viewport);
        app.region.provenance = RegionProvenance::Free;
        selection.clear_selection();
        return true;
    }
    if (nsel == 1) {
        // No region + SINGLETON: deselect + land the playhead ON the marker (the
        // playhead is usually already coincident from the re-coupling land, so the
        // land is a safety re-affirm; deselecting then flips the playhead form back
        // to the waveform's own cursor focus). Land BEFORE the clear so the
        // marker index is still resolvable. Full waveform damage: the deselect
        // un-shows a wider WAVEFORM overlay than the playhead-column / top-strip
        // damage covers — the phase-reset lead-in overlay (P+target) and the
        // selected-marker stem.
        const int idx = *app.selected_markers.begin();
        land_playhead_on_marker(app, audio, viewport, idx);
        selection.clear_selection();
        viewport.invalidate_waveform_area();
        return true;
    }
    return false;
}

// One scrub ACT: STOP, THEN START ON THE NEXT CLICK (architect 2026-07-27,
// superseding the kill-and-revive of 2026-07-23). A click on a scrub surface
// WHILE AUDIO PLAYS is a pure STOP — it does not relaunch, so the audition
// ends where the user interrupted it. The NEXT click then lands on a stopped
// session and launches a fresh one from wherever it fell, re-capturing the
// loop verdict and end bound at that launch — so a scrub after a mid-session
// trim edit auditions the NEW window instead of riding a stale capture.
// The old exact-same-frame skip is GONE with the relaunch it existed to
// avoid: it kept an in-place audition uninterrupted, and the playing case now
// always stops, so there is no in-place audition left to preserve — and it
// cannot migrate to the stopped case, whose scanner fields are stale by
// contract (a stopped scanner is deactivated immediately; no non-playing
// validity window exists). A refused launch (out-of-window frame; target
// update in flight) leaves playback stopped, silently — the "nothing to
// audition" family; a later click at a launchable frame launches.
void GuiInputHandler::scrub_act_at(int64_t frame) {
    if (playback.is_playing()) {
        // The pure stop, through the standing stop machinery — side-effect-
        // clean here (the scrub never moved the cursor) and the owner of the
        // scanner's visible-identity teardown. `frame` is deliberately unused
        // on this arm: a click over a live session says only "stop", never
        // where to play from.
        playback_lifecycle.stop_playback_if_playing();
        return;
    }
    // Outer is_updating gate, mirroring the two Space handlers: a NEW launch
    // while a target update is in flight would audition the stale target
    // buffer, which Space refuses — so the scrub launch refuses it too,
    // silently.
    if (!(app.active_audio_view == 'T' && target_render.is_updating()))
        playback_lifecycle.scrub_launch_at(frame);
}

// The scanner scrub press body. SOLE CALLER: the waveform lower-half plain
// press — the marker-text lane's empty-spot scrub is DELETED (architect
// 2026-07-27; that lane touches playback in neither direction now), so the
// waveform's lower half is the whole scrub surface. See the declaration for
// the full contract. ONE-SHOT per click (architect 2026-07-23, the Ableton
// model): derive the clicked column's frame and run one scrub act — the press
// arms NOTHING, a held press does nothing further, and motion over the scrub
// surface is inert (the scrub drag is removed; each click pays scrub_act_at's
// stop quiescence fence AT MOST once — a stopped session's launch pays none —
// and the per-column fence cadence class is structurally gone). The caller
// keeps playback alive across the press (no waveform press stops playback, and
// the top-strip stops belong to the top-strip claims), so the
// scrub act sees the LIVE session — load-bearing for the stop-then-start
// ruling: a caller that let the session die before the act would turn the
// interrupting click into a launch, which is precisely the behaviour the
// ruling removed.
void GuiInputHandler::scrub_press_at(int click_rel_x) {
    const GuiRect area = waveform_area(app);
    // Gutter / invalid column: no launch position exists, silent no-op.
    if (click_rel_x < 0 || click_rel_x >= area.w) return;
    const int64_t frame = clamp_playhead_to_live_domain(
        playhead_frame_at_click_column(app, audio, click_rel_x), app, audio);
    scrub_act_at(frame);
}

// Button-press handler. Verbatim from the lambda at the original
// main.cpp:1483; the captured operation-struct lambdas (begin_drag,
// drop_marker, drop_phase_reset_at_position, set_single_selection, etc.)
// are rewritten to direct method calls on the appropriate operation
// struct ref. The four hit_test_* lambdas are now free functions taking
// (app, audio, ...) explicit args. The handle_wheel lambda is now a
// private method on this struct.
void GuiInputHandler::apply_strip_drag_at(int x, int y, bool final_event) {
    // Dual-axis strip drag, INCREMENTAL (the v6 model). Every event reads the
    // LIVE zoom level and viewport and applies its own dx/dy on top — there is no
    // press baseline to go stale across composed pan/zoom phases (the earlier
    // axis-lock model died of exactly that staleness). The song anchor
    // (anchor_sample) is the zoom focus; the pan re-derives its drifted column
    // each event and the edge trick rebinds it when it leaves the screen.
    StripDragState& sd = app.strip_drag;

    // (1) Per-event deltas from the previous motion position. The crossing event
    // folds the whole accumulated delta since the press (last_x/last_y were
    // seeded there and no sub-threshold event advanced them).
    const double dx = static_cast<double>(x - sd.last_x);
    const double dy = static_cast<double>(y - sd.last_y);
    sd.last_x = x;
    sd.last_y = y;

    // (2) The old spp is read from the LIVE level (never stored).
    const double spp_old = current_samples_per_pixel(app, audio);
    const GuiRect wf_area = waveform_area(app);
    const double W = static_cast<double>(wf_area.w);
    const int64_t total = live_total_frames(app, audio);

    // (3) Pan at the old level, in the double domain: grab sign — drag right
    // (dx>0) reveals earlier content, so the viewport moves left. The result is
    // WALL-CLAMPED here, at the old level, to the SAME right wall the downstream
    // clamp_viewport_start rests at — the shared max_viewport_start_grid owner
    // (the level mid-gesture is the live level, so it reads exactly the state the
    // chokepoint would). step (5) derives the anchor column and rebinds
    // anchor_sample from vp, so both must see the viewport that will actually
    // REST. The earlier `total − W·spp_old` form sat up to a pixel short of the
    // legal grid rest — pressing at the flush-right rest first pulled vp back to
    // that off-grid wall, the anchor column clamped at W−1, and the edge rebind
    // PERMANENTLY rewrote anchor_sample, so the at-wall no-op proof failed exactly
    // at the legal rest; sharing the wall makes vp derive at the true rest and the
    // no-op proof hold. The grid snap of arbitrary INTERIOR vp values is
    // deliberately NOT reproduced (the sub-pixel residue there self-heals on the
    // following event, exactly as step (5)'s live re-read does — only the WALL had
    // to be exact because the edge rebind is a lasting mutation).
    double vp = static_cast<double>(app.viewport_start_sample) - dx * spp_old;
    const double vp_lo = 0.0;
    const double vp_hi = static_cast<double>(max_viewport_start_grid(app, audio));
    if (vp < vp_lo) vp = vp_lo;
    if (vp > vp_hi) vp = vp_hi;

    // (4) Zoom INCREMENTALLY off the live level: this event's dy applies to the
    // current level (drag DOWN, dy>0, lowers the level → zooms in). No press
    // baseline, so a wall reversal responds immediately — the older absolute-dy
    // formula had a dead zone after a clamp (dy had to unwind all the way back
    // before the level moved); this incremental form has none.
    double new_level = app.zoom_level - dy / kZoomStripPxPerLevel;
    const double max_l = effective_max_zoom_level(
        W, total, audio.sample_rate());
    if (new_level < kMinZoom) new_level = kMinZoom;
    if (new_level > max_l)    new_level = max_l;

    // (5) The anchor's drifted column under the wall-clamped post-pan viewport,
    // with the Ableton EDGE TRICK: clamp the column into [0, W-1] (the effective
    // waveform width), and when the clamp engages REBIND anchor_sample to that
    // edge pixel's frame — the zoom focus never leaves the screen; a pan that
    // pushes it to an edge PINS it there and it becomes the edge's content.
    // Deriving the column from the wall-clamped vp is what makes pure pan an
    // exact identity (see below) even at a saturated wall, and reading the live
    // viewport each event is what lets the sub-pixel grid snap self-heal on the
    // following event.
    double anchor_col = (sd.anchor_sample - vp) / spp_old;
    const double col_max = W > 0.0 ? W - 1.0 : 0.0;
    double clamped_col = anchor_col;
    if (clamped_col < 0.0)     clamped_col = 0.0;
    if (clamped_col > col_max) clamped_col = col_max;
    if (clamped_col != anchor_col) {
        sd.anchor_sample = vp + clamped_col * spp_old;
        anchor_col = clamped_col;
    }

    // Drive the capture's release-restore x to the anchor stem's surface x — the
    // identical column->x math render_strip_anchor_stem paints at (area.x +
    // col + 0.5), so on release the cursor lands dead on the stem rather than at
    // its raw traveled position (which the edge rebind leaves past a pinned
    // stem). Fired every event; the last before release wins. A motionless
    // strip press-release never reaches here, so its override stays unset and it
    // restores at the press point.
    if (set_strip_capture_restore_x)
        set_strip_capture_restore_x(
            static_cast<double>(wf_area.x) + anchor_col + 0.5);

    // (6) Apply: place anchor_sample at anchor_col under the new level's spp and
    // clamp. IDENTITY PROOFS: pure pan (dy=0) is EXACT — new_level == old, so
    // apply reproduces vp = anchor_sample - anchor_col·spp_old bit-for-bit (the
    // column was derived from that same vp), and the level-unchanged dispatch
    // takes the same synchronous full rebuild. Off the walls the pan arithmetic
    // is unchanged, so the identity holds as before; AT a wall the clamped vp
    // equals the viewport that will rest, the anchor column re-derives against it
    // consistently, and apply reproduces the wall value — a saturated pan is a
    // true no-op. Pure zoom (dx=0) leaves the anchor's current (possibly
    // edge-pinned) column fixed and pivots the rescale around it. A both-unchanged
    // event (level and viewport identical after the clamp) is a true no-op the
    // entry point skips.
    viewport.apply_strip_drag_zoom(new_level, sd.anchor_sample, anchor_col,
                                   final_event);
}

void GuiInputHandler::on_button_press(GuiMouseButton button, int x, int y,
                                      GuiInputState mods) {
    // Prompt-modal input handling: while the bottom-strip prompt is
    // active, all mouse events are swallowed. Responses go through
    // the keyboard.
    if (app.prompt.active) return;

    // A double-click is two CONSECUTIVE clicks: snapshot the pending candidate
    // and clear the shared field here, so ANY intervening press invalidates it.
    // The consume checks below read this snapshot; each surface then re-seeds
    // its own fresh candidate (ZoomRow / EditorText at a motionless release,
    // Marker at the press). One closed instrumentation point — the clear covers
    // every non-consuming press (a strip/region/chip arm, a modal swallow)
    // without a clear scattered on each path.
    const DoubleClickCandidate dc_at_press = app.double_click;
    app.double_click = DoubleClickCandidate{};

    // F2.1: mouse drag-to-select inside the active text editor. A press on
    // the active editor's text region places the caret and arms a selection
    // drag (anchor == caret until the pointer moves). Resolved before the
    // per-editor modal swallows below so the gesture reaches the settings /
    // BPM bottom-strip editors too. A press outside the active editor's
    // region falls through: the bottom-strip editors stay modal and swallow
    // it, while the top flag editor closes guard-free below and the press
    // then acts normally.
    if (button == GuiMouseButton::Left) {
        const ActiveEditorText g = active_editor_text(app, audio);
        if (g.valid) {
            bool in_region = false;
            if (g.bottom_strip) {
                const GuiRect bs = bottom_strip_area(app);
                in_region = x >= bs.x && x < bs.x + bs.w &&
                            y >= bs.y && y < bs.y + bs.h;
            } else {
                // FlagPayload: the editable text lives in the marker-text lane,
                // centered on the target marker's column. A press within the
                // rendered run's x-extent (in the lane's y-band) repositions the
                // caret and arms the drag; g.text_left is that run's left edge
                // (flag_pending_text_left_x, the one caret-origin owner). Any
                // OTHER press is a non-caret click, which closes the editor
                // below and then routes normally (the guard-free lifecycle).
                const GuiRect lane = top_marker_text_row_area(app);
                const double run_w = static_cast<double>(
                    app.top_flag_editor.pending.size()) * g.advance;
                in_region = y >= lane.y && y < lane.y + lane.h &&
                    static_cast<double>(x) >= g.text_left &&
                    static_cast<double>(x) <= g.text_left + run_w;
            }
            if (in_region) {
                // Double-click: a second click within the window on this
                // editor's text selects the RUN of the clicked character class
                // (word / punctuation / whitespace) under the click — select_
                // word_at's own classifier, not just a word — arming no drag.
                // The surface tag keeps it from consuming a marker / zoom-row
                // candidate.
                const DoubleClickCandidate& dc = dc_at_press;
                if (dc.surface == DoubleClickSurface::EditorText &&
                    monotonic_ms() - dc.time_ms <= kDoubleClickMs &&
                    std::abs(x - dc.press_x) <= kDoubleClickSlackPx &&
                    std::abs(y - dc.press_y) <= kDoubleClickSlackPx) {
                    const int idx = text_editor::byte_index_from_click_x(
                        static_cast<double>(x), g.text_left, g.advance,
                        static_cast<int>(g.ed->pending.size()));
                    text_editor::select_word_at(*g.ed, idx);
                    if (g.bottom_strip) viewport.invalidate_timestamp_area();
                    else                viewport.invalidate_top_strip();
                    return;
                }
                set_editor_caret_from_x(g, x);
                // Collapsed anchor — extends to a real selection only if the
                // pointer then moves.
                g.ed->selection_anchor = g.ed->cursor_pos;
                app.editor_text_drag.active = true;
                if (g.bottom_strip) viewport.invalidate_timestamp_area();
                else                viewport.invalidate_top_strip();
                return;
            }
            // A bottom-strip editor stays modal: a press outside its row is
            // swallowed without arming. A flag-editor press that isn't on the
            // lane text falls through to the guard-free close below.
            if (g.bottom_strip) return;
        }
    }

    if (text_editor::is_active(app.settings_editor)) return;
    if (text_editor::is_active(app.commit_editor)) return;
    if (text_editor::is_active(app.top_flag_editor) &&
        app.top_flag_editor.kind == text_editor::Kind::BpmBracket) {
        // The BPM editor is a bottom-strip modal owner (like the settings
        // editor). Mouse input does not interact with it beyond its own
        // click-to-cursor region; the session ends only through Esc or the
        // Enter dispatch path (`m` is just a typed character now). Swallow
        // the press so it cannot drive a region drag / marker click / or
        // tear the editor down through the top-strip flag-edit routine
        // below.
        return;
    }
    if (app.loading || audio.total_frames() <= 0) return;
    const GuiRect area = waveform_area(app);
    const GuiRect top  = top_strip_area(app);
    // The waveform BAND spans the full window width (top.w), not the effective
    // width (area.w): the <=15 px inert right gutter counts as a waveform click
    // by the user's lights, so a plain press there still reaches the waveform
    // branch — the upper half clears the selection (it has no column to seat a
    // playhead, so that is all it does), the lower (scrub) half returns
    // silently (no launch position exists, and a scrub press touches no
    // selection anyway). The gutter is 0 px at the deployment widths
    // (1920/2560/3840 are multiples of 16), so this only matters off-deployment.
    const bool inside_waveform =
        x >= area.x && x < top.w &&
        y >= area.y && y < area.y + area.h;
    const bool inside_top =
        x >= top.x && x < top.x + top.w &&
        y >= top.y && y < top.y + top.h;
    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;
    const bool alt   = mods.alt;

    // Defensive: a second press during a drag is ignored (left button
    // should still be held down for a drag to exist).
    if (app.drag.active) return;
    if (app.tempo_drag.active) return;
    if (app.trim_drag.active) return;

    // Mouse authoring is home-view gated like the keyboard: a plain flag
    // press in W+target arms the TEMPO drag on an eligible marker instead
    // of the reposition drag (marker_drag.tempo_drag_predecessor; an
    // ineligible press still selects and LANDS the playhead, only the drag
    // arm is refused) — the pointer half of the home-view binding's one tempo
    // exception — while placement arming everywhere else is gated by
    // active_column_authoring_allowed, off-home selecting and landing but
    // arming nothing. The click-playhead / region-drag family below is
    // navigation, not authoring, and stays view-independent.

    if (button == GuiMouseButton::Left) {
        // Editor lifecycle, guard-free. A press in the editor's rendered lane
        // text already repositioned the caret / armed the text drag above (the
        // F2.1 block) and returned; ANY other left press with the top flag
        // editor open CLOSES it without committing — exactly Esc's teardown
        // (pending dropped; Enter is the only commit route, so closing is cheap
        // and non-destructive) — and then FALLS THROUGH so the press acts
        // normally (arm a strip drag, select a marker, arm a marker drag, land,
        // place the playhead, ...). Placed ahead of the zoom-row claim so the
        // close really is unconditional. Consequence: a double-click on the
        // open editor's own marker is close-then-reopen — the first click
        // closes + selects + seeds a Marker candidate (+ arms the pending drag
        // on the flag part, writable), and the second consumes into a fresh
        // open. That IS the documented "double-click opens the editor"; there
        // is no own-marker special case.
        if (text_editor::is_active(app.top_flag_editor)) {
            flag_editor.exit_top_flag_edit_no_commit();
            // Re-resolve hover NOW, before the fall-through arms anything. While
            // the editor was open, hover_popup stayed cleared (recompute is
            // suppressed by an open editor, and motion kept it clear), so the
            // hover-driven POPUP / lane-text surfaces would be stale after a
            // fall-through marker click. Recomputing here reflects the pointer's
            // true position so the popup/lane text settle correctly (one of the
            // 57f7196 hover-settling fixes; the always-on stem does not key on
            // hover). Placed before any pending-drag arm on purpose:
            // a recompute AFTER an arm would hit the drags-suppress-hover rule
            // (any_pointer_gesture_active) and clear it again; here no gesture is
            // active yet, so this is a genuine resolve.
            viewport.recompute_hover_at_cursor();
        }

        // Live top zoom-strip row (Ableton-style navigation), claimed ahead of
        // the top-strip playback-stop and the click routing below. It claims
        // ONLY the plain unmodified left press inside its exact half-open row
        // band; a modified press there is a strict no-op (nothing else lives on
        // the row). The claim is immediate — no motion threshold — and a
        // motionless press-release commits nothing. Navigation-class like the
        // wheel pan: allowed in read-only, never touches the playhead or
        // selection, does not stop playback, and does not override follow. It is
        // DUAL-AXIS (vertical motion zooms, horizontal motion pans, freely
        // composed — see apply_strip_drag_at). All modal gates (prompt,
        // bottom-strip editors, the loading/empty guard) sit above this point, so
        // a modal surface blocks the claim exactly as it blocks every other
        // pointer target.
        {
            const GuiRect zoom_row = top_zoom_row_area(app);
            const bool in_zoom_row =
                x >= zoom_row.x && x < zoom_row.x + zoom_row.w &&
                y >= zoom_row.y && y < zoom_row.y + zoom_row.h;
            if (in_zoom_row) {
                if (ctrl || shift || alt) return;  // modified: strict no-op
                // Double-click detection, BEFORE arming the drag: a candidate
                // seeded by the previous motionless zoom-row release, within
                // kDoubleClickMs and kDoubleClickSlackPx of the recorded x,
                // consumes this press as a one-shot zoom command — no drag armed,
                // no pointer capture, playhead and selection untouched, allowed
                // in read-only (all modal gates sit above this claim). The
                // surface tag (ZoomRow) means a marker / editor candidate can
                // never consume here. The double-click DIVERGES from the bare
                // `0` key:
                // it runs run_zoom_double_click_command (zoom to the region /
                // trim / whole-song span), not run_zoom_toggle_command. The
                // candidate's first click briefly captured and hid the cursor at
                // its press and restored it at the motionless release; this
                // second press never captures.
                const DoubleClickCandidate& dc = dc_at_press;
                if (dc.surface == DoubleClickSurface::ZoomRow &&
                    monotonic_ms() - dc.time_ms <= kDoubleClickMs &&
                    std::abs(x - dc.press_x) <= kDoubleClickSlackPx) {
                    run_zoom_double_click_command();
                    return;
                }
                const double spp = current_samples_per_pixel(app, audio);
                app.strip_drag = StripDragState{};
                app.strip_drag.active    = true;
                // The press position: the drag-threshold reference, and the seed
                // for the incremental per-event deltas (last_x/last_y). Not a
                // baseline — every event reads the live level and viewport.
                app.strip_drag.press_x   = x;
                app.strip_drag.press_y   = y;
                app.strip_drag.last_x    = x;
                app.strip_drag.last_y    = y;
                // The song position painted under the press at press time — the
                // anchor the zoom pivots around. Rebindable: a pan that drives
                // its column offscreen re-pins it to the nearest visible pixel.
                app.strip_drag.anchor_sample =
                    static_cast<double>(app.viewport_start_sample) +
                    static_cast<double>(x) * spp;
                // Zoom-row origin: a motionless release seeds the ZoomRow
                // double-click candidate (the ctrl-exact waveform arm sets this
                // false so its release seeds nothing).
                app.strip_drag.double_click_seed = true;
                // Ableton-style pointer capture: hide and lock the cursor at
                // the press so motion feeds the gesture as unbounded virtual
                // coordinates (infinite pan/zoom travel). Self-guarding no-op
                // on a degraded compositor. The capture is shared by three
                // gestures (this zoom strip, the ctrl-exact waveform strip drag,
                // and the alt-exact pan); every exit path of each calls the
                // matching end hook exactly once (it is idempotent).
                begin_strip_pointer_capture();
                return;
            }
        }

        // Unified marker hit, computed ONLY on the path that consumes it. The
        // marker is ONE pointer item — flag shape OR rendered lane run
        // (marker_hit_at, the shared resolver in render.cpp the hover recompute
        // also reads) — and the TOP-STRIP hit feeds the plain/Shift/Ctrl
        // marker-click branches (plain = single-select + land the playhead on
        // the marker + double-click seed / consume + arm the pending marker
        // drag on the flag part, Shift = file-manager inclusive RANGE select
        // from the interaction's anchor (shift-held, else the adopted focus)
        // to the clicked marker + land on that range END, Ctrl = the individual
        // membership toggle + land on the resulting focus), so it is resolved
        // once here — every one of the three lands on its own focus.
        // The editor lifecycle block above already closed any open flag editor,
        // so the lane run this resolves is the committed (non-editor) run. The
        // WAVEFORM never SELECTS a marker by HIT — a plain press splits by half
        // (upper: deselect-all + playhead placement + region-drag arm; lower:
        // the scanner scrub, which touches no selection at all), and a Shift
        // press FORMS a region waveform-wide (from the playhead, or a marker
        // DEMOTE that clears the selection; see the waveform block below) — so
        // no marker scan runs on the waveform at all (the invisible stem is
        // not a grab target). The plain DRAG never selects markers either
        // (SELECTION FLOWS DOWNWARD ONLY, architect 2026-07-23 — the region no
        // longer selects its contents; it leaves the selection empty).
        // Trim bounds are grabbed only by their top-strip chips /
        // the inter-chip bridge on a PLAIN chip-row press (route_trim_chip_press
        // below); a click over a bound's waveform stem is an ordinary waveform
        // click (the stem grab retired), so no trim hit test runs on the
        // waveform at all. Resolved ONCE here, ahead of every branch that
        // consumes it: the ctrl-exact membership toggle, the plain / Shift
        // marker click, and the empty-top-strip fallthrough (mh.index < 0 is
        // what makes a spot EMPTY) all read this one hit.
        MarkerHit mh;
        if (inside_top) mh = marker_hit_at(app, audio, x, y);

        // A top-strip press stops playback WHEN IT CLAIMS SOMETHING, never
        // merely because it landed in the strip (architect 2026-07-27). The
        // stop is the price of an authoring or a navigation act — a marker
        // select, a trim bound set, a chip-row consume — and continuing audio
        // during authoring / text editing is the wrong default, so each of
        // those acts calls stop_playback_if_playing ITSELF at its own site
        // below. THE STOP IS INTENTIONAL, NOT POSITIONAL: a press that claims
        // NOTHING changes no state at all, so there is nothing for a stop to
        // protect and a live audition survives it. A claim that can still
        // REFUSE goes one step further (architect 2026-07-27): its stop sits
        // INSIDE the refusal gate, at the latest point before the mutation, so
        // a claimed-but-refused press (a bound set with no resting pair or in a
        // read-only tab, a chip-row consume with no trim window to highlight)
        // is as playback-inert as an unclaimed one. That covers every modified
        // combination the branches below reject (alt on a marker, ctrl or alt
        // on an empty flag/triangle spot, ctrl+alt, shift+alt, ...), a
        // SHIFT-exact chip-row press (trim is transparent to shift), a
        // SHIFT-exact empty flag/triangle-lane press (shift binds nothing on
        // that lane — the waveform's shift region former does not extend to
        // it), every empty marker-text-lane spot, and the inter-lane gaps — all
        // of which end at the inert top-strip return far below.
        // The EMPTY FLAG/TRIANGLE lane's PLAIN press is the one ACTING press
        // that still does not stop: it is the waveform-upper-half's twin (R6
        // parity, architect 2026-07-23), and a live session RESEEKS there
        // rather than dying, through place_playhead_and_arm_region. That
        // exemption is stated at the branch itself and is PLAIN-ONLY by
        // construction — the branch matches no modified press at all, so shift
        // sits with ctrl and alt in the inert list above and stops nothing
        // there either.
        // Waveform clicks keep playback alive as ever — the per-press reseek to
        // the click sample happens at the playhead-drag press site below, gated
        // on was_playing && sample != playhead_at_entry. Capture the entry
        // state up front, AHEAD OF EVERY STOP below, so all the downstream
        // branches see the same snapshot.
        const bool was_playing = playback.is_playing();
        const int64_t playhead_at_entry = app.playhead_cursor_sample;

        // Clicks in iter/BPM mode route through the unified marker
        // hit-test below.

        // Only presses inside the waveform or the top strip do anything.
        if (!inside_waveform && !inside_top) return;

        // Alt-exact left press: on the waveform it arms the captured grab-pan;
        // alt-exact anywhere else (a top-strip marker included) does nothing
        // further HERE — the land now lives on the PLAIN marker click below.
        // On a markerless (or top-strip) spot that "nothing" is a STRICT no-op,
        // playback included: alt claims no top-strip gesture, so no stop runs
        // over it and a live audition keeps playing; alt+drag from a marker is
        // inert by ruling.
        //
        // The waveform grab-pan: continuous 1:1 pan of the viewport by the
        // per-event pixel delta (see on_motion). It CAPTURES the pointer
        // (begin_strip_pointer_capture, the same cursor-hide + lock the zoom
        // strip uses) so the pan travels infinitely under unbounded virtual
        // coordinates while the viewport clamps at the song walls — pan-only, no
        // zoom axis and no anchor stem (the stem is the zoom pivot affordance,
        // gated on strip_drag.active). Navigation-class: allowed in read-only,
        // never touches the playhead or selection. It deliberately does NOT
        // override follow — a pan during playback moves the view along with the
        // audio rather than signaling a stop, unlike the marker / trim / playhead
        // gestures. A motionless Alt press-release commits nothing but the brief
        // cursor hide/reappear (the scroll happens on motion).
        if (alt && !ctrl && !shift) {
            if (inside_waveform) {
                app.scroll_drag = ScrollDragState{};
                app.scroll_drag.active = true;
                app.scroll_drag.last_x = x;
                begin_strip_pointer_capture();
            }
            return;
        }

        // Ctrl-exact left press splits by surface. On a top-strip MARKER it is
        // the individual membership toggle + land on the resulting focus (the
        // marker claim below). On the WAVEFORM it arms the dual-axis strip drag — the SAME
        // gesture the zoom row arms (StripDragState / apply_strip_drag_at),
        // triggered here for reach: it gets the cursor capture ("swallow"), the
        // anchor stem, the edge clamp, and dual-axis zoom+pan for free. That arm
        // is byte-identical to the zoom-row arm (anchor_sample from the click
        // song position, press/last seeds, begin_strip_pointer_capture),
        // diverging at ONE point — double_click_seed=false, so a motionless
        // ctrl+waveform press-release seeds no ZoomRow double-click candidate
        // (that affordance stays zoom-row-only). The waveform strip-drag half is
        // navigation-class: allowed in read-only, never touches the playhead or
        // selection — and a MOTIONLESS ctrl+waveform press-release commits
        // nothing at all (the R3.4 selection clear is RETIRED, architect
        // 2026-07-23: ctrl is purely the zoom modifier on the waveform).
        // Ctrl-exact on a MARKERLESS top-strip spot is a strict no-op except
        // the chip row's BEGIN bound set (the claim below).
        if (ctrl && !alt && !shift) {
            // Ctrl-exact on a top-strip MARKER is the individual membership
            // toggle (the former shift behavior) — this AMENDS the "Ctrl keeps
            // only the letter chords" rule for the one marker surface (architect
            // 2026-07-23). It arms no drag, seeds/consumes no double-click, opens
            // no editor. Whether the toggle ADDED or REMOVED, the playhead lands
            // on the FOCUS the toggle leaves behind (architect 2026-07-28,
            // replacing the earliest-selected land): an ADD focuses the clicked
            // marker, a REMOVE of the focused member repairs the focus to the
            // largest remaining index, and a REMOVE of any other member leaves
            // the focus alone — so app.last_selected_marker is the one expression
            // for all three, and it is always a live member here (a non-empty
            // selection always carries a focus after either arm). Auditioning
            // from the selection's START survives regardless: that is Space
            // launching from the extent region's LEFT BOUND, set below. THE
            // RESULT SIZE DECIDES THE PLAYHEAD'S FORM (architect 2026-07-29):
            // 2+ left selected is a SPAN, so the click re-owns the region as the
            // selection's [earliest, latest] extent under the DOWNWARD
            // selection->extent coupling (SELECTION-FLOWS-DOWNWARD-ONLY,
            // architect 2026-07-23); a result of <2 — one survivor, or the
            // emptied selection that lands nothing — is a POINT, so any resting
            // region collapses through clear_region_highlight, unconditionally
            // and whatever the land did or did not move. Read-only allowed
            // (selection + playhead are navigation). The ctrl-exact WAVEFORM
            // press keeps the zoom-strip drag below (different surface, no
            // collision); a markerless top-strip ctrl press claims only the
            // chip row (BEGIN bound set, next block) and is a strict no-op on
            // every other lane — no-op in the playback sense too, since only a
            // CLAIM stops playback.
            if (inside_top && mh.index >= 0) {
                // The toggle is an act, so it owns its stop: selecting while a
                // session plays is the authoring case the top-strip stop
                // exists for. It runs AHEAD of the toggle and the land, like
                // every other claim's stop — and it stays at the TOP of the
                // branch because this claim cannot refuse: read-only is allowed
                // (selection is navigation), the hit index is >= 0 by the gate
                // above (so the mutator's idx < 0 guard is unreachable here),
                // and every path below changes membership.
                playback_lifecycle.stop_playback_if_playing();
                selection.toggle_selection_membership(mh.index);
                // The selected-marker stem is always-on for a singleton;
                // toggle_selection_membership owns its appear/move/disappear
                // damage via the subject-change owner, so no explicit stem
                // damage is needed here.
                if (!app.selected_markers.empty())
                    land_playhead_on_marker(app, audio, viewport,
                                            app.last_selected_marker);
                // Span or point, decided by the RESULT (see above). Land first,
                // then own the region: the extent must follow the land at a span
                // result, and the clear is the point result's own act.
                if (app.selected_markers.size() >= 2)
                    set_region_to_selection_extent(app, audio, viewport);
                else
                    clear_region_highlight(app, viewport);
                return;
            }
            // Markerless top-strip ctrl-exact press: the CHIP ROW sets the
            // BEGIN trim bound at the click (R4.6 as corrected by R5 — ctrl is
            // BEGIN and ctrl+shift is END, the architect's intended pair;
            // set_trim_bound_at_click refuses read-only AND a missing pair
            // silently — the clicks ADJUST an existing window, never create
            // one — and runs the
            // coupling sync). EVERY other lane — an empty flag or triangle
            // lane included — is a strict no-op, falling through to the return
            // below (the R3.2 ctrl-clear is RETIRED, architect 2026-07-23:
            // ctrl-click in Ableton is just click, and ctrl stays the zoom
            // modifier here; the PLAIN empty flag/triangle-lane press below is
            // the surviving lane gesture — R6 waveform parity, not a bare
            // clear). The zoom row (lane 0) was claimed above and never reaches
            // here.
            if (inside_top) {
                const GuiRect chip_row = top_upper_row_area(app);
                if (y >= chip_row.y && y < chip_row.y + chip_row.h) {
                    // NO stop here: the bound set has its own refusals
                    // (read-only, no resting pair, a degenerate audio/geometry
                    // state), and a refused press changes nothing, so there is
                    // nothing for a stop to protect. The stop lives INSIDE
                    // set_trim_bound_at_click, past every refusal and
                    // immediately ahead of the bound write.
                    // R3: set the BEGIN bound AND arm the single-bound drag on it,
                    // so motion drags it live (a motionless release rests the set).
                    set_trim_bound_at_click_then_arm_drag(/*is_begin=*/true, x, y);
                    return;
                }
            }
            if (inside_waveform) {
                const double spp = current_samples_per_pixel(app, audio);
                app.strip_drag = StripDragState{};
                app.strip_drag.active  = true;
                app.strip_drag.press_x = x;
                app.strip_drag.press_y = y;
                app.strip_drag.last_x  = x;
                app.strip_drag.last_y  = y;
                app.strip_drag.anchor_sample =
                    static_cast<double>(app.viewport_start_sample) +
                    static_cast<double>(x) * spp;
                // Waveform origin: never seeds a zoom-row double-click candidate.
                app.strip_drag.double_click_seed = false;
                begin_strip_pointer_capture();
            }
            return;
        }

        // Ctrl+Shift-exact: the chip row is its ONE claim — set the END trim
        // bound at the click (R5: ctrl is BEGIN, ctrl+shift is END;
        // set_trim_bound_at_click refuses read-only AND a missing pair
        // silently — adjust-only — and runs the
        // coupling sync). Everywhere else Ctrl+Shift stays a strict no-op,
        // playback included, falling to the return below.
        if (ctrl && shift && !alt && inside_top) {
            const GuiRect chip_row = top_upper_row_area(app);
            if (y >= chip_row.y && y < chip_row.y + chip_row.h) {
                // NO stop here either: like the BEGIN set above, the stop sits
                // inside set_trim_bound_at_click past that act's refusals, so a
                // refused END set leaves a live audition alone.
                // R3: set the END bound AND arm the single-bound drag on it.
                set_trim_bound_at_click_then_arm_drag(/*is_begin=*/false, x, y);
                return;
            }
        }

        // Strict modifier matching: the marker reposition arm lives on the plain
        // flag press and trim's chip/bridge drags on the plain chip-row press, so
        // every remaining modified combination — Ctrl+Alt (now a strict no-op),
        // Ctrl+Shift off the chip row (its one claim is the END bound set
        // above), Shift+Alt, Ctrl+Alt+Shift, ... — no-ops here. Only a plain
        // or Shift-on-the-top-strip base press proceeds (Shift adjusts the
        // marker selection). Alt is POINTER-ONLY vocabulary: the Alt+wheel
        // stepped pan and the Alt+drag captured grab-pan are untouched (separate
        // handlers). On the keyboard alt survives only in the FIVE Ctrl+Alt
        // render / propagate chords (Ctrl+Alt+R, Ctrl+Alt+Shift+R, Ctrl+Alt+I,
        // Ctrl+Alt+P, Ctrl+Alt+Shift+P) — every other alt keybinding was retired
        // 2026-07-28, so nothing here defers to one.
        // Discarding a press here is TOTAL: it claimed
        // nothing, so it stopped no playback on the way down either — the stops
        // live at the claims above and below, never on the route to this gate.
        if (ctrl || alt) return;

        // Plain or Shift press. In the waveform area a plain press splits by
        // HALF: the UPPER half clears the marker selection (deselect-all),
        // places the playhead at the clicked column, and arms the
        // region-select drag; the LOWER half is the scrub surface (one scrub
        // act, nothing else) — neither ever SELECTS a marker. A Shift press on
        // the waveform instead FORMS a region waveform-wide
        // (the former / marker demote, one-shot — see the waveform block). In the top strip a plain
        // CHIP-ROW press arms a trim chip/bridge drag (claimed ahead of the
        // marker select); otherwise (a marker click on EITHER part — flag shape
        // or lane run) selection is the whole interface, BOTH views. Plain click:
        // single-select, LAND the playhead on the marker (below), and — on the
        // FLAG part only — ARM a pending marker drag (moves the marker if the
        // pointer crosses the threshold, else a pure click). Shift+click: a
        // file-manager INCLUSIVE RANGE select from the interaction's anchor
        // (shift-held, else the adopted focus) to the
        // clicked marker (the range end = FOCUS), which LANDS the playhead THERE.
        // The individual membership TOGGLE moved to Ctrl+click (the ctrl-exact
        // marker claim in the earlier branch; whether it adds or removes it lands
        // on the focus it leaves, an empty selection landing nothing). The
        // plain / shift / ctrl land makes every such marker click a land route
        // alongside the Tab family and `c` (which additionally recenter /
        // re-zoom), and all three land ON THEIR FOCUS (architect 2026-07-28), so
        // the playhead and the focused flag are coincident before any subsequent
        // drag or nudge — nothing is towed.
        if (inside_top) {
            // The chip row (top_upper_row_area, lane 1) is trim's lane and is
            // claimed BEFORE the marker single-select. The chip row, the marker
            // text lane, and the flag/triangle lanes are disjoint y-bands, so
            // this contends with nothing: a marker-part press falls to the marker
            // handling below. The PLAIN click either arms a chip/bridge drag or,
            // on an unclaimed spot, selects + highlights the trim window (R4.5);
            // the bound-set clicks are the ctrl (BEGIN) / ctrl+shift (END)
            // claims above (R5). A SHIFT-exact chip-row press claims nothing —
            // trim is transparent to it, and the shift fall-through below is
            // inert here (marker_hit_at's y-bands exclude the chip row), so it
            // ends at the inert top-strip return, having touched nothing at
            // all: it is the only press that reaches the chip row without
            // claiming it (ctrl and alt were discarded at the gate above), and
            // it stops no playback — that stop belongs to the plain claim.
            const GuiRect chip_row = top_upper_row_area(app);
            const bool in_chip_row =
                (y >= chip_row.y && y < chip_row.y + chip_row.h);
            if (!shift && in_chip_row) {
                // Plain chip-row press (R4.5). Read-only cannot ARM a trim drag,
                // but the region-highlight sync is navigation, so it runs directly
                // there (route_trim_chip_press would claim-without-arming and
                // skip it). In a writable tab a chip/bridge hit arms the drag (a
                // motionless release then runs this same R4.5 click action at
                // on_button_release), and an UNCLAIMED chip-row spot runs the
                // R4.5 region-highlight sync now. With BOTH bounds set the window
                // is taken; lone/no trim is a silent no-op. Either way the chip
                // row consumes the press — it never falls to the marker handling.
                // The consume claims the press, but a RESTING FULL PAIR is what
                // makes it ACT: with the pair set every arm commits something
                // (read-only syncs the highlight, a chip/bridge hit arms the
                // drag, an unclaimed spot syncs the highlight), and with a lone
                // or absent trim every arm is a silent no-op — route_trim_chip_press
                // refuses at its own pair gate and both sync arms are gated on
                // the pair too. So the stop is gated on the pair and runs ahead
                // of all three acting arms; a press with nothing to highlight
                // leaves a live audition playing. One predicate, read once —
                // nothing on any arm mutates the trim (the drag arms, it does
                // not commit), so the three arms and the stop all read the same
                // verdict.
                const bool have_window =
                    (app.trim.has_begin && app.trim.has_end);
                if (have_window) playback_lifecycle.stop_playback_if_playing();
                if (active_view_state(app).read_only) {
                    if (have_window) sync_highlight_to_trim_window();
                    return;
                }
                if (route_trim_chip_press(x, y)) return;
                if (have_window) sync_highlight_to_trim_window();
                return;
            }
            if (mh.index >= 0) {
                // A marker click — plain or Shift, flag shape or lane run — is
                // an act (select, land, arm, open), so it owns the stop for the
                // whole branch: selecting or editing under a live audition is
                // the case the top-strip stop was written for. One site ahead
                // of every arm below (the range select, the two group
                // deferrals, the plain single-select + land + editor open),
                // and it belongs at the TOP because NO arm here refuses:
                // read-only still selects and lands, the hit is >= 0 by the
                // gate (the mutators' idx < 0 guards are unreachable), the two
                // deferrals arm a pending drag and seed a candidate, and the
                // plain arm always single-selects and lands. Only the editor
                // OPEN can decline (read-only / P view / off home), and that is
                // a second act layered on a click that already committed.
                playback_lifecycle.stop_playback_if_playing();
                const int hit = mh.index;
                if (shift) {
                    // Shift+click is a file-manager INCLUSIVE RANGE select
                    // (architect 2026-07-23): the click ranges from the
                    // interaction's anchor — a live shift-held anchor, else the
                    // ADOPTED FOCUS (architect labwc round 2, 2026-07-23:
                    // plain-click A then shift+click B selects A..B, and a
                    // re-started shift interaction ranges from the previous
                    // click's focus; with nothing focused the click anchors on
                    // its own marker, selection {hit}) — each
                    // successive shift-click replaces the selection with the
                    // inclusive index range between that anchor and
                    // hit. The clicked marker becomes the range end = FOCUS
                    // (last_selected), and the playhead LANDS THERE (architect
                    // 2026-07-28, replacing the earliest-member land): focus and
                    // land no longer diverge on any click, so nothing is towed
                    // onto the focus by a later nudge. A Space after a range
                    // select still auditions from the range's START — that is the
                    // extent region's LEFT-BOUND launch (set below), which is
                    // what owned that behavior all along. On an anchoring
                    // focus-less first click the selection is {hit} and hit is
                    // the focus, so the land is unchanged. Ctrl+click is the
                    // individual membership toggle (above, landing on its own
                    // resulting focus). It arms no drag, seeds/consumes no
                    // double-click, opens no editor. Allowed in read-only (selection + playhead are
                    // navigation). The downward selection->extent coupling
                    // (SELECTION-FLOWS-DOWNWARD-ONLY): a range leaving
                    // 2+ selected sets the region to the [earliest, latest]
                    // extent (set_region_to_selection_extent, AFTER the land),
                    // so the highlight and Space's left-bound launch agree; the
                    // anchoring first click ({hit}) leaves <=1 selected, which is
                    // a POINT result and collapses any resting region instead —
                    // the ctrl toggle's identical span-or-point split.
                    selection.select_range_from_anchor(hit);
                    // A range leaving exactly one selected shows its always-on stem;
                    // select_range_from_anchor owns the subject-change damage (a 2+
                    // range shows the extent region's ground, no stem).
                    // The land target is the FOCUS the mutator just set, which is
                    // `hit` on both its arms — spelled as last_selected_marker so
                    // the three multi-select clicks read as one rule.
                    if (!app.selected_markers.empty())
                        land_playhead_on_marker(app, audio, viewport,
                                                app.last_selected_marker);
                    // Span or point by the RESULT size, land-then-region order.
                    if (app.selected_markers.size() >= 2)
                        set_region_to_selection_extent(app, audio, viewport);
                    else
                        clear_region_highlight(app, viewport);
                } else {
                    // GROUP-drag deferral (architect 2026-07-23, file-manager
                    // convention): when THIS press would arm a REPOSITION drag
                    // (the home-view flag-press arm condition at the tail of this
                    // branch — mh.on_flag, tab writable,
                    // active_column_authoring_allowed) AND the clicked marker is
                    // already a member of a 2+ selection, DEFER the click's
                    // committed act (single-select + land) to a motionless
                    // release. Committing it now would collapse the
                    // multi-selection to {hit} before begin_drag could seed the
                    // whole group, so the press holds its fire. The tempo-drag
                    // surface (W + target view) has active_column_authoring_allowed
                    // false, so it is excluded HERE — but it has its OWN group
                    // deferral in the next branch (an eligible grab on a selected
                    // member defers likewise, so the group tempo drag seeds intact).
                    // Every other plain marker press keeps the immediate
                    // single-select + land below.
                    const bool would_arm_reposition =
                        mh.on_flag && !active_view_state(app).read_only &&
                        active_column_authoring_allowed(app);
                    if (would_arm_reposition &&
                        app.selected_markers.size() >= 2 &&
                        app.selected_markers.count(hit) > 0) {
                        // Keep the selection; seed the Marker double-click
                        // candidate exactly as the immediate path (press-time,
                        // target = hit) and arm the reposition drag flagged
                        // deferred. The double-click-consume check is SKIPPED —
                        // it can never fire on a still-multi selection: a prior
                        // release either collapsed the selection to a singleton
                        // (routing the next press through the immediate path) or
                        // never happened. If the pointer never crosses
                        // kDragMovedThresholdPx the deferred single-select
                        // + land runs at release / lost button; Esc ABANDONS it,
                        // leaving the multi-selection intact.
                        app.double_click = DoubleClickCandidate{
                            .surface = DoubleClickSurface::Marker,
                            .time_ms = monotonic_ms(),
                            .press_x = x, .press_y = y,
                            .target  = hit};
                        app.pending_marker_drag = PendingMarkerDrag{};
                        app.pending_marker_drag.active         = true;
                        app.pending_marker_drag.marker         = hit;
                        app.pending_marker_drag.press_x        = x;
                        app.pending_marker_drag.press_y        = y;
                        app.pending_marker_drag.deferred_click = true;
                        return;
                    }
                    // GROUP TEMPO-drag deferral: the same file-manager deferral
                    // for the W + target view tempo surface. That surface has
                    // active_column_authoring_allowed FALSE, so the reposition
                    // check above excluded it; defer here when the grabbed marker
                    // is tempo-drag-ELIGIBLE (its predecessor set is walkable) AND
                    // a member of a 2+ selection, so begin_tempo_drag seeds the
                    // whole participant set intact. An INELIGIBLE grab arms no
                    // drag, so there is nothing to defer — it collapses at press
                    // below, as before.
                    const bool would_arm_tempo =
                        mh.on_flag && !active_view_state(app).read_only &&
                        app.active_markers_view == 'W' &&
                        app.active_audio_view == 'T' &&
                        marker_drag.tempo_drag_predecessor(hit) >= 0;
                    if (would_arm_tempo &&
                        app.selected_markers.size() >= 2 &&
                        app.selected_markers.count(hit) > 0) {
                        // Keep the selection; seed the Marker double-click
                        // candidate (press-time, target = hit) and arm the tempo
                        // drag flagged deferred. A motionless release / lost
                        // button runs the held single-select + land; Esc abandons.
                        app.double_click = DoubleClickCandidate{
                            .surface = DoubleClickSurface::Marker,
                            .time_ms = monotonic_ms(),
                            .press_x = x, .press_y = y,
                            .target  = hit};
                        app.pending_tempo_drag = PendingTempoDrag{};
                        app.pending_tempo_drag.active         = true;
                        app.pending_tempo_drag.marker         = hit;
                        app.pending_tempo_drag.press_x        = x;
                        app.pending_tempo_drag.press_y        = y;
                        app.pending_tempo_drag.deferred_click = true;
                        return;
                    }
                    // Plain marker click single-selects (both views; W's
                    // click-to-edit is retired — the editor now opens on Enter or
                    // this double-click). Selection is navigation, allowed in
                    // read-only.
                    selection.set_single_selection(hit);
                    // The clicked marker's always-on focus stem appears/moves
                    // here through set_single_selection's
                    // subject-change owner — including the click-an-already-selected
                    // no-op case (same subject, no stem damage needed, the stem is
                    // already painting there). Covers the double-click-consume path
                    // below too (this single-select ran first).
                    // ...and LANDS the playhead exactly onto the marker (shared
                    // helper; see land_playhead_on_marker). Runs on EVERY plain
                    // marker click — the double-click-consume path below (whose
                    // first click already landed, leaving the second land a
                    // same-sample no-op) and the plain-select path; both parts of
                    // the unified item land (flag shape or lane run — hit is the
                    // one index).
                    land_playhead_on_marker(app, audio, viewport, hit);
                    // THE POINT COMMAND OWNS ITS COLLAPSE (architect 2026-07-29):
                    // a single-select click asserts the playhead's POINT form, so
                    // any resting span ends here — unconditionally, of any
                    // provenance, and whether or not the land moved anything. A
                    // re-click of the already-selected marker therefore collapses
                    // a resting TrimWindow or Free highlight too; that is the
                    // ruling and not an accident (the click says "the playhead is
                    // HERE, at this point"). The double-click-consume path rides
                    // this same site, so an editor open through it finds no span.
                    clear_region_highlight(app, viewport);
                    // Double-click: a Marker candidate for the SAME index within
                    // the window opens the flag editor, exactly like Enter on the
                    // focused marker (the click above already single-selected it).
                    // The surface + target tag prevents any zoom-row / editor
                    // candidate from consuming here, and a candidate seeded on
                    // one part consumes on the other — one surface. Read-only,
                    // P view (phase resets have no per-flag editor), and the
                    // off-home column (active_column_authoring_allowed false —
                    // the warp editor is source-view-only) refuse silently,
                    // matching Enter's allowlist / view refusal — the candidate
                    // is cleared and the press stays a plain second select. On a
                    // consumed open nothing is armed and no fresh candidate
                    // seeds (the editor now owns input).
                    bool opened_editor = false;
                    const DoubleClickCandidate& dc = dc_at_press;
                    if (dc.surface == DoubleClickSurface::Marker &&
                        dc.target == hit &&
                        monotonic_ms() - dc.time_ms <= kDoubleClickMs &&
                        std::abs(x - dc.press_x) <= kDoubleClickSlackPx &&
                        std::abs(y - dc.press_y) <= kDoubleClickSlackPx) {
                        if (app.active_markers_view != 'P' &&
                            !active_view_state(app).read_only &&
                            active_column_authoring_allowed(app)) {
                            // Every open route opens fully SELECTED (open-
                            // selected), so there is no clicked-glyph caret to
                            // seat — the flag-shape vs lane-run press are the
                            // same open. A specific caret spot is a click inside
                            // the already-open editor (the F2.1 path).
                            flag_editor.enter_top_flag_edit(hit);
                            opened_editor = true;
                        }
                    }
                    if (!opened_editor) {
                        // SEED a Marker candidate at this PRESS — the one seed
                        // timing for the whole marker surface (the release-time
                        // seeding is retired). Press-seeding is safe for the
                        // flag part because a press that becomes a real drag
                        // drops the candidate at the threshold crossing (see
                        // on_motion's pending-marker-drag branch).
                        app.double_click = DoubleClickCandidate{
                            .surface = DoubleClickSurface::Marker,
                            .time_ms = monotonic_ms(),
                            .press_x = x, .press_y = y,
                            .target  = hit};
                        // A writable tab arms a pending drag on a plain FLAG
                        // press only (the flag is the sole drag handle; the
                        // lane run selects but never arms); read-only selects
                        // but never arms (marker mutation refused). WHICH drag
                        // arms is the home-view split: in the column's home
                        // view the press arms the marker REPOSITION drag; in W
                        // view + TARGET view exactly, it instead arms the
                        // TEMPO drag on an eligible marker — the pointer half
                        // of the home-view binding's tempo exception
                        // (architect 2026-07-22; the keyboard half is the
                        // owner-only bare Up/Down step), an Ableton-style
                        // stretch that rewrites the GROUP PREDECESSOR's tempo.
                        // Coincident groups drag as ONE — dragging any member
                        // stretches against the marker before the stack (the
                        // walk in tempo_drag_predecessor). An ineligible W+T
                        // press (marker at the store's earliest frame, non-owner
                        // predecessor, or a coincident-collapsed predecessor
                        // whose tempo is render-inert — tempo_drag_predecessor
                        // returns -1), and the P column off ITS home (P view in
                        // source view), select and LAND the playhead but arm
                        // nothing — the silent read-only convention, marker
                        // motion / tempo authoring being authoring.
                        if (mh.on_flag && !active_view_state(app).read_only) {
                            if (active_column_authoring_allowed(app)) {
                                app.pending_marker_drag = PendingMarkerDrag{};
                                app.pending_marker_drag.active  = true;
                                app.pending_marker_drag.marker  = hit;
                                app.pending_marker_drag.press_x = x;
                                app.pending_marker_drag.press_y = y;
                            } else if (app.active_markers_view == 'W' &&
                                       app.active_audio_view == 'T') {
                                // The walk is purely the eligibility test here
                                // (>= 0 means armable); the crossing re-walks in
                                // begin_tempo_drag, equivalent because nothing
                                // mutates the store between press and crossing.
                                if (marker_drag.tempo_drag_predecessor(hit)
                                        >= 0) {
                                    app.pending_tempo_drag = PendingTempoDrag{};
                                    app.pending_tempo_drag.active      = true;
                                    app.pending_tempo_drag.marker      = hit;
                                    app.pending_tempo_drag.press_x     = x;
                                    app.pending_tempo_drag.press_y     = y;
                                }
                            }
                        }
                    }
                }
            } else {
                // Empty top-strip spot — no marker run/flag under the point (the
                // chip row already returned above; mh.index < 0 here).
                const GuiRect flag_lane = top_flag_row_area(app);
                const GuiRect tri_lane  = top_triangle_row_area(app);
                const bool in_flag_or_tri =
                    (y >= flag_lane.y && y < flag_lane.y + flag_lane.h) ||
                    (y >= tri_lane.y  && y < tri_lane.y  + tri_lane.h);
                if (in_flag_or_tri && !shift) {
                    // R6 empty flag/triangle-lane parity (architect 2026-07-23):
                    // the empty lane works like the waveform upper half. A
                    // DOUBLE-CLICK consume creates a marker at the clicked
                    // position — the AUGMENTED drop, the same and only drop bare
                    // `s` performs (architect 2026-07-28: the lane double-click
                    // reuses the keyboard's machinery, so it follows it); the
                    // FIRST press seeds the candidate AND runs the placement body
                    // (deselect + playhead + region arm).
                    // PLAIN ONLY: a SHIFT press on the lane claims nothing at all
                    // and falls to the inert return below, exactly like the
                    // SHIFT-exact chip-row press — shift has no meaning here (the
                    // waveform's shift region former does not extend to the lane),
                    // so it seeds nothing, places nothing, and leaves a shift+drag
                    // inert. Ctrl and alt never reach this branch (discarded at the
                    // strict-modifier gate above).
                    // NO STOP HERE, deliberately, and this is the one ACTING
                    // top-strip press that omits one: PARITY is the whole reason —
                    // a live session RESEEKS to the placed playhead
                    // (place_playhead_and_arm_region's was_playing arm) exactly as
                    // it does on the waveform upper half, so a double-click drop
                    // lands over a live session without cutting it off. Do not add
                    // a stop to make this branch look like its siblings — the
                    // omission IS the ruling.
                    const int click_rel_x = x - area.x;
                    const DoubleClickCandidate& dc = dc_at_press;
                    if (dc.surface == DoubleClickSurface::EmptyLane &&
                        monotonic_ms() - dc.time_ms <= kDoubleClickMs &&
                        std::abs(x - dc.press_x) <= kDoubleClickSlackPx &&
                        std::abs(y - dc.press_y) <= kDoubleClickSlackPx) {
                        create_marker_at_empty_lane(click_rel_x);
                        return;
                    }
                    // SEED an EmptyLane candidate at this PRESS (position-keyed;
                    // target unused). Cleared at the region-drag threshold
                    // crossing so a moved drag never carries one (on_motion).
                    app.double_click = DoubleClickCandidate{
                        .surface = DoubleClickSurface::EmptyLane,
                        .time_ms = monotonic_ms(),
                        .press_x = x, .press_y = y,
                        .target  = -1};
                    place_playhead_and_arm_region(
                        click_rel_x, x, y, was_playing, playhead_at_entry);
                    return;
                }
                // Every other empty top-strip spot: NOTHING AT ALL — no
                // playhead, no marker, no selection or region change, and no
                // playback effect either, because this press claimed nothing
                // and the stops all live at the claims. That covers the whole
                // marker-text lane, plain or modified: its empty spots are
                // inert by ruling (architect 2026-07-27, the scrub that lived
                // there deleted), leaving it a pure display surface for the
                // rendered runs — a run under the point is a marker hit and
                // never reaches this branch. The inter-lane gaps, the SHIFT-exact
                // chip-row press, and the SHIFT-exact flag/triangle-lane press
                // land here too, equally inert.
                return;
            }
            return;
        }

        // Waveform-area press: marker-blind for SELECTION (it never SELECTS a
        // hit marker — the invisible stem is not a grab target; marker_hit_at
        // runs only for top-strip presses). The PLAIN press splits by HALF
        // (architect 2026-07-23): the UPPER half keeps the placement press —
        // CLEARS the selection (the deselect-all: a waveform click dismisses
        // the marker selection, the Ableton behaviour), drops the playhead at
        // the clicked column (no marker snap — the 3px marker-snap magnet
        // already died with the retired plain-drag scrub), reseeks a live
        // scanner to it, overrides follow, and arms the region drag — which
        // also DISSOLVES any resting highlight at this mouse-down
        // (arm_region_drag_at clears it), so the highlight vanishes on press
        // whether the gesture becomes a click or a fresh drag. The LOWER half
        // is the SCRUB surface (the branch below): the press runs one
        // scrub act — stopping a live session, else starting a fresh SCANNER
        // session from the clicked
        // frame — arming nothing (the one-shot Ableton model) and
        // touching nothing else. ONLY the plain press splits — a Shift press
        // instead FORMS a region waveform-wide (the demote path below), never
        // a plain press's playhead placement, and ctrl/alt already claimed
        // their waveform-wide gestures above.
        {
            const int click_rel_x = x - area.x;
            if (shift) {
                // Waveform shift+click: the region former / the DEMOTE (this is
                // one of the DROP formers — it clears the selection; the downward
                // selection->extent derivation lives on the multi-select clicks
                // and the SelectionExtent maintainers (group drags / tempo
                // follows), never on a drag-formed region — the plain waveform
                // drag makes a FREE region and selects nothing, and this shift
                // former likewise clears; architect 2026-07-23, replacing
                // the reserved strict no-op). With NO markers
                // selected it forms a region from the PLAYHEAD to the clicked
                // column; with markers selected it DEMOTES — the selection
                // clears and the region spans from the selected marker FURTHEST
                // from the click to the click (one rule: furthest =
                // argmax |pos - click| over the selection in active-domain
                // frames, which covers the between-the-series case as the
                // longest side). It moves NO playhead, stops NO playback,
                // reseeks nothing, overrides no follow, seeds no double-click.
                // Read-only allowed (the region is
                // transient navigation; the demote's deselect is selection =
                // navigation). ctrl/alt returned earlier, so this is
                // shift-exact — a shift+modified combination never reaches here.
                // The deselect runs FIRST, before the gutter early-return, so
                // an inert-gutter shift+click (no column to form a region from)
                // still drops the marker selection — every waveform click drops
                // it, mirroring the plain branch's gutter clear.
                //
                // NEW (architect 2026-07-24 second pass): the former now also
                // ARMS a region drag anchored at the FAR endpoint (the playhead,
                // or the demote's furthest-marker image). So the press is
                // one-shot ONLY on a motionless release: the formed region rests
                // exactly as today, and on the sliver rule whatever the deselect
                // below left standing does; motion past the shared gate drags the
                // CLICK-side endpoint live through the region-drag motion path,
                // the far endpoint fixed, and Esc mid-drag CLEARS the region
                // outright — the cancel restores nothing, so the press's
                // pre-press state is not preserved anywhere. GUTTER presses arm
                // NOTHING (there is no column to anchor a drag against).
                if (click_rel_x < 0 || click_rel_x >= area.w) {
                    selection.clear_selection();
                    return;
                }
                // Region endpoints hold PLAYABLE live-domain frames only: the
                // display-state validator (clamp_display_state_to_live_domain)
                // defines an endpoint >= total as invalid and clears the whole
                // highlight, and the forward map rounds unclamped, so an EOF
                // item's image (a marker at total-1 under a fast map) can land
                // one past the wall — clamping here through the land's own
                // helper keeps every former inside the one region domain. The
                // click_frame is clamped for the same conformance (the plain
                // press path clamps it through move_playhead_to; the region
                // former stored it raw).
                const int64_t click_frame = clamp_playhead_to_live_domain(
                    playhead_frame_at_click_column(app, audio, click_rel_x),
                    app, audio);
                int64_t endpoint = app.playhead_cursor_sample;
                if (!app.selected_markers.empty()) {
                    // Demote: the region's far endpoint is the selected marker
                    // whose active-domain position is furthest from the click.
                    // Stale indices are skipped defensively; if every index was
                    // stale (degenerate) the playhead endpoint stands.
                    int64_t best_dist = -1;
                    for (int idx : app.selected_markers) {
                        int64_t src_frame;
                        if (app.active_markers_view == 'P') {
                            const auto& tv = app.phaseresetmarkers.markers();
                            if (idx < 0 || idx >= static_cast<int>(tv.size()))
                                continue;
                            src_frame = tv[idx].time_frame;
                        } else {
                            const auto& mv = app.warpmarkers.markers();
                            if (idx < 0 || idx >= static_cast<int>(mv.size()))
                                continue;
                            src_frame = mv[idx].time_frame;
                        }
                        // Clamp the forward-map image into the live domain (see
                        // the click_frame comment above): an EOF marker's image
                        // can round to total, which the display-state validator
                        // rejects — the land's own helper keeps this endpoint a
                        // playable frame.
                        const int64_t pos = clamp_playhead_to_live_domain(
                            source_frame_to_active_domain(app, audio, src_frame),
                            app, audio);
                        const int64_t dist = pos > click_frame
                                                 ? pos - click_frame
                                                 : click_frame - pos;
                        if (dist > best_dist) {
                            best_dist = dist;
                            endpoint  = pos;
                        }
                    }
                    // Deselect — this demote is a DROP former (the shift-click
                    // waveform demote drops the selection by explicit ruling,
                    // unlike the plain drag and the multi-select clicks that
                    // promote). This also dissolves the shift-range anchor,
                    // correct here: this shift interaction is on a DIFFERENT
                    // surface (the waveform) than the marker range select, so no
                    // range is being extended.
                    selection.clear_selection();
                }
                // Sliver rule (mirrors end_region_drag_min_size_check): a span
                // narrower than the drag threshold — a click at the playhead,
                // hand jitter — leaves no region window.
                const double spp = current_samples_per_pixel(app, audio);
                if (spp <= 0.0) return;
                if (std::abs(static_cast<double>(endpoint - click_frame)) / spp
                        < kDragMovedThresholdPx) {
                    // Sliver: form NO region — app.region is untouched HERE, so
                    // a motionless release keeps whatever the deselect above left
                    // standing: a Free or TrimWindow pre-press region rests
                    // bit-for-bit, while an ex-SelectionExtent one is already
                    // gone (the deselect's membership clear took it). An Esc
                    // mid-drag from here clears the region like any other, this
                    // gesture keeping no snapshot to restore. ALSO
                    // ARM the drag anchored at the far endpoint so dragging out
                    // past the gate still grows a fresh region live from it (e.g.
                    // shift-click AT the playhead, then drag out).
                    arm_region_drag_preserving(endpoint, x, y);
                    return;
                }
                // RegionState endpoints are unordered — the painter and Space
                // normalize lo/hi. Damage the waveform (the region paints as a
                // direct overlay), matching the region-drag extend.
                app.region.active     = true;
                app.region.a_frame    = endpoint;
                app.region.b_frame    = click_frame;
                // The shift-click former / demote DROPS the selection, so this
                // region is FREE — tempo gestures skip it. ORDER: the deselect
                // above ran FIRST, so its membership clear cannot reach this
                // span — and a Free span is outside that clear's reach in any
                // case (it takes SelectionExtent only).
                app.region.provenance = RegionProvenance::Free;
                viewport.invalidate_waveform_area();
                // ARM the drag anchored at the FAR endpoint, PRESERVING the
                // just-formed region: a motionless release keeps it (today's
                // one-shot region, bit-for-bit), motion past the gate drags the
                // click-side endpoint live (the motion handler fixes a_frame =
                // anchor = endpoint and tracks b_frame to the pointer).
                arm_region_drag_preserving(endpoint, x, y);
                return;
            }
            // THE HALF TEST, first thing on the plain path — before the
            // deselect, because a scrub press must not deselect. Lower half of
            // the waveform area (y >= a.y + a.h/2) = the SCRUB surface: the
            // press drives the SCANNER (not the cursor) — the scanner fields
            // are meaningful only while active (the standing contract), and
            // this gesture is exactly the launches-the-scanner-independently-
            // of-the-cursor consumer that contract anticipated. Every scrub
            // act is STOP-THEN-START-ON-THE-NEXT-CLICK (scrub_act_at, architect
            // 2026-07-27): a click over a LIVE session just stops it, and the
            // following click launches a fresh audition from the frame it lands
            // on — never a positional seek inside an old session. A refused
            // launch is a silent no-op, exactly Space's conventions. The scrub
            // is ONE-SHOT per click (architect 2026-07-23, the Ableton model):
            // the press arms NOTHING, a held press does nothing further, and
            // motion over the surface is inert — each click pays at most one
            // stop quiescence fence. It touches
            // NOTHING else: no selection change, no region change, no cursor
            // write, no follow override, no double-click seed, no drag
            // arm. Read-only allowed (playback is navigation). The gutter
            // (click_rel_x outside [0, area.w)) returns silently — no launch
            // position exists there, unlike the upper half's
            // deselect-then-return.
            if (y >= area.y + area.h / 2) {
                // The scrub press body (scrub_press_at): gutter no-op, clamped
                // frame from the column, one scrub act (stop a live session,
                // else launch), nothing armed. This is its only caller — the
                // scrub is the waveform lower half and nothing else.
                scrub_press_at(click_rel_x);
                return;
            }
            // Upper half: the placement press body, shared verbatim with the
            // R6 empty flag/triangle-lane parity press
            // (place_playhead_and_arm_region): deselect-all, drop the playhead
            // at the clicked column, reseek a live session, override follow, arm
            // the region drag. The clear runs FIRST inside the helper, so an
            // inert-gutter click still deselects.
            place_playhead_and_arm_region(click_rel_x, x, y,
                                          was_playing, playhead_at_entry);
        }
    }
    // Wheel events no longer reach on_button_press; they arrive coalesced
    // per pointer frame through on_wheel -> handle_wheel.
}

void GuiInputHandler::finalize_editor_text_drag() {
    const ActiveEditorText g = active_editor_text(app, audio);
    if (g.valid) {
        // A press that never moved leaves a plain caret and no selection,
        // matching the existing click-to-caret.
        if (g.ed->selection_anchor == g.ed->cursor_pos)
            g.ed->selection_anchor = -1;
        if (g.bottom_strip) viewport.invalidate_timestamp_area();
        else                viewport.invalidate_top_strip();
    }
    app.editor_text_drag.active = false;
}

void GuiInputHandler::arm_region_drag_at(int64_t anchor_frame, int x, int y) {
    app.region_drag = RegionDragState{};
    app.region_drag.active       = true;
    app.region_drag.anchor_frame = anchor_frame;
    app.region_drag.press_x      = x;
    app.region_drag.press_y      = y;
    // Clear any resting region immediately at press: a plain upper-half
    // waveform press dissolves an existing highlight on mouse-down (the plain
    // canvas ground repaints back now, not at release; a lower-half scrub press never
    // reaches here — it arms no region drag and leaves the region alone). A
    // moved drag rebuilds a fresh region live; a
    // motionless press-release simply leaves it cleared, and an Esc mid-drag
    // clears whatever the drag had grown — nothing here is snapshotted, the
    // dissolve being final. Same dissolve shape as
    // the navigation clears, so it shares clear_region_highlight.
    clear_region_highlight(app, viewport);
}

void GuiInputHandler::arm_region_drag_preserving(int64_t anchor_frame, int x,
                                                 int y) {
    // The SHIFT-exact former's arm (labwc 2026-07-24 second pass). Same drag
    // state as arm_region_drag_at — active, anchored at the FAR endpoint (the
    // playhead, or the demote's furthest-marker image), press coordinates for
    // the shared Chebyshev gate — but it does NOT dissolve app.region: the
    // former has already left it exactly as it should REST for a motionless
    // release (the freshly formed region, or on the sliver rule the pre-press
    // region untouched), so preserving it keeps today's one-shot behaviour
    // bit-for-bit. That no-dissolve-at-press property is this function's whole
    // reason to exist; an Esc mid-drag clears the region either way, both arms
    // sharing the one cancel. Past the gate the SHARED region-drag motion
    // handler re-establishes
    // app.region from this anchor (it fixes a_frame = anchor_frame and tracks
    // b_frame to the pointer column on each column change), so the click-side
    // endpoint drags live while this far endpoint stays put — no motion/release/
    // Esc handler change needed, the anchor semantic is identical to the plain
    // drag's.
    app.region_drag = RegionDragState{};
    app.region_drag.active       = true;
    app.region_drag.anchor_frame = anchor_frame;
    app.region_drag.press_x      = x;
    app.region_drag.press_y      = y;
}

void GuiInputHandler::place_playhead_and_arm_region(int click_rel_x, int x,
                                                    int y, bool was_playing,
                                                    int64_t playhead_at_entry) {
    // The waveform-upper-half placement press body, shared by the plain waveform
    // press and the empty flag/triangle-lane parity press (R6). The clear runs
    // FIRST, before the gutter early-return, so an inert-gutter click (no column
    // to seat a playhead) still deselects.
    const GuiRect area = waveform_area(app);
    selection.clear_selection();
    if (click_rel_x < 0 || click_rel_x >= area.w) return;
    // Clamp the click column's frame into the live domain ONCE and pass that
    // same clamped value to both the playhead move and the region arm:
    // move_playhead_to clamps internally, but the region former stored the raw
    // value. At a fractional flush-right zoom the painter-quantized wall
    // (q = nearbyint(spp*W)/W) differs from the click conversion's
    // current_samples_per_pixel, so the last visible column's frame can compute
    // to domain_total — one past [0, domain_total-1], which the display-state
    // validator would clear wholesale — so both formers clamp.
    const int64_t sample = clamp_playhead_to_live_domain(
        playhead_frame_at_click_column(app, audio, click_rel_x), app, audio);
    viewport.move_playhead_to(sample);
    if (was_playing && sample != playhead_at_entry)
        playback_lifecycle.reseek_keeping_alive(sample);
    if (was_playing) app.follow_overridden_for_session = true;
    arm_region_drag_at(sample, x, y);
}

void GuiInputHandler::create_marker_at_empty_lane(int click_rel_x) {
    // R6 empty flag/triangle-lane double-click marker create: the bare-`s` drop
    // equivalent, and like bare `s` it is the AUGMENTED drop in both columns —
    // the copy-previous owner in W, the lead-in reset in P. Gated exactly like
    // the keyboard `s`: home-view (active_column_authoring_allowed) and read-only
    // refuse SILENTLY. Place the playhead on the clicked column first — the
    // double-click's first press already moved it there, so this is a harmless
    // same-value repeat that also covers any first press whose placement was
    // undone in between. The standing _at_playhead drops then author at that
    // playhead, taking the full create path (walls, undo, selection, and the
    // point command's own region collapse at the drop chokepoint) — the
    // phase-reset lead-in additionally offset N/2 before the playhead and landing
    // the playhead per that drop's rule.
    const GuiRect area = waveform_area(app);
    if (click_rel_x < 0 || click_rel_x >= area.w) return;
    if (active_view_state(app).read_only) return;
    if (!active_column_authoring_allowed(app)) return;
    const int64_t sample = clamp_playhead_to_live_domain(
        playhead_frame_at_click_column(app, audio, click_rel_x), app, audio);
    viewport.move_playhead_to(sample);
    // active_column_authoring_allowed guarantees the column is in its HOME audio
    // view: W -> source (warp drops legal), P -> target (phase-reset drops legal,
    // the lead-in's target requirement satisfied), so the view dispatch below
    // needs no extra audio-view guard.
    if (app.active_markers_view == 'P')
        phase_resets.drop_phase_reset_lead_in_at_playhead();
    else
        warpops.drop_copy_previous_at_playhead();
}

void GuiInputHandler::on_button_release(GuiMouseButton button, int x,
                                        int y, GuiInputState /*mods*/) {
    if (app.prompt.active) return;
    // F2.1: a left release ending an editor-text drag finalizes the
    // selection (or collapses to a caret) before the modal swallow below.
    if (button == GuiMouseButton::Left && app.editor_text_drag.active) {
        const ActiveEditorText g = active_editor_text(app, audio);
        finalize_editor_text_drag();
        // Double-click seeding: a MOTIONLESS release (a pure click that left a
        // caret, no selection) seeds an editor-text candidate so a second click
        // within the window selects the clicked character class's run (word /
        // punctuation / whitespace). A drag that made a selection seeds nothing.
        if (g.valid && !text_editor::has_selection(*g.ed)) {
            app.double_click = DoubleClickCandidate{
                .surface = DoubleClickSurface::EditorText,
                .time_ms = monotonic_ms(), .press_x = x, .press_y = y,
                .target = -1};
        }
        return;
    }
    if (text_editor::is_active(app.settings_editor)) return;
    if (text_editor::is_active(app.commit_editor)) return;
    if (button != GuiMouseButton::Left) return;
    if (app.strip_drag.active) {
        // Terminating event: if the drag moved, run the final apply with
        // final=true and the one synchronous rebuild (resync + kick_waveform_sync,
        // inside apply_strip_drag_zoom's final path) so the rest state is exact. A
        // motionless press-release finalizes nothing.
        // Double-click seeding: a MOTIONLESS zoom-row release records a
        // candidate (this release x equals the press x); a release that MOVED
        // records nothing and clears any candidate, so a drag can never seed the
        // second click of a double-click. Only the ZOOM-ROW arm seeds
        // (double_click_seed): a motionless ctrl+waveform press-release commits
        // NOTHING — no seed, no selection change (the R3.4 clear is RETIRED,
        // architect 2026-07-23: the ctrl-waveform press is purely the zoom-strip
        // drag).
        if (app.strip_drag.moved) {
            apply_strip_drag_at(x, y, /*final_event=*/true);
            app.double_click = DoubleClickCandidate{};
        } else if (app.strip_drag.double_click_seed) {
            app.double_click = DoubleClickCandidate{
                .surface = DoubleClickSurface::ZoomRow,
                .time_ms = monotonic_ms(), .press_x = x, .press_y = y,
                .target = -1};
        }
        app.strip_drag = StripDragState{};
        // reappear the cursor at the anchor-stem column (y frozen at the press
        // row) — the restore x override the drag set each event.
        end_strip_pointer_capture();
        return;
    }
    if (app.scroll_drag.active) {
        // Alt+drag grab-pan end: the pan applied incrementally during motion, so
        // there is nothing to finalize but the predictor. The continuous pan
        // deferred per-event resyncs, so re-anchor the predictor once here. The
        // pan captured the pointer at its arm, so end the capture (reappear the
        // cursor at the raw traveled virtual_pointer_x_, y frozen at the press
        // row — the pan sets no anchor-stem override); idempotent, so a degraded
        // compositor that never captured is unharmed.
        if (playback.is_playing()) playback.resync_predictor();
        app.scroll_drag = ScrollDragState{};
        end_strip_pointer_capture();
        return;
    }
    // (No scrub branch: the scrub is one act at the PRESS — it arms nothing,
    // so its release is an ordinary fall-through.)
    if (app.region_drag.active) {
        // The region is extended live during the drag (see on_motion); a drag
        // that moved rests the region at its final extent. A MOTIONLESS
        // press-release (never crossed the threshold) needs no collapse here:
        // the press already cleared any resting highlight at mouse-down (see
        // arm_region_drag_at), so a plain click leaves the region cleared and
        // there is nothing to do at release but disarm. A jitter drag that
        // crossed the gate but rests a sub-threshold sliver dissolves like a
        // click (end_region_drag_min_size_check) — but ONLY a MOVED drag runs
        // that check (codex second-pass round-1 MEDIUM): the SHIFT-exact former's
        // preserving arm (arm_region_drag_preserving) does NOT dissolve
        // app.region, so a motionless shift-sliver press-release rests a legal
        // narrow region (a TrimWindow or Free highlight, NOT subject to the
        // drag-rest minimum — a SelectionExtent one cannot reach this rest: the
        // press's own deselect is a membership replace that clears it, and an
        // extent span cannot rest beside an empty selection either), and an
        // unconditional min-size check would delete
        // it. Capture `moved` BEFORE the state reset (the reset zeroes it). Plain
        // path unaffected: a plain MOVED drag still checks; a plain motionless
        // release left the region cleared at arm (arm_region_drag_at), so the
        // check would early-return anyway — the gate changes nothing there.
        const bool moved = app.region_drag.moved;
        app.region_drag = RegionDragState{};
        if (moved) end_region_drag_min_size_check(app, audio, viewport);
        return;
    }
    if (app.tempo_drag.active) {
        // Tempo-drag release: the final synchronous re-warp already ran on the
        // last committed cent step, so the finalize only settles history (the
        // one undo entry + dirty + the deferred preview trigger, net-change
        // gated inside end_tempo_drag).
        marker_drag.end_tempo_drag();
        return;
    }
    if (app.pending_tempo_drag.active) {
        // The pending tempo drag never crossed the threshold: a pure flag click.
        //  - IMMEDIATE arm (deferred_click false): the press already
        //    single-selected its marker AND seeded the Marker candidate, so there
        //    is nothing to commit — just disarm.
        //  - DEFERRED arm (deferred_click true): the press held the click's
        //    committed act back (a group member pressed with the multi-selection
        //    intact). A motionless release IS that click now — collapse to {m}
        //    and land the playhead on it, exactly the reposition pending's
        //    deferred completion, its own clear_region_highlight collapsing any
        //    resting span exactly as the immediate path's does (the point command
        //    owns the collapse; see land_playhead_on_marker). Bounds-check m
        //    against the warp store (the tempo surface is W view — nothing
        //    mutates it between press and release, the pending gate swallows
        //    keys).
        const bool deferred = app.pending_tempo_drag.deferred_click;
        const int  m        = app.pending_tempo_drag.marker;
        app.pending_tempo_drag = PendingTempoDrag{};
        if (deferred) {
            const int n = static_cast<int>(app.warpmarkers.markers().size());
            if (m >= 0 && m < n) {
                selection.set_single_selection(m);
                // Deferred click completes on marker m: set_single_selection owns
                // the always-on stem's subject-change damage.
                land_playhead_on_marker(app, audio, viewport, m);
                clear_region_highlight(app, viewport);
            }
        }
        // Settle hover to the pointer's ACTUAL position. Sub-threshold motion
        // during the pending drag never recomputed hover, so hover_popup still
        // holds the press-time hit — stale if the pointer slid off the flag rect
        // (the hover POPUP / lane text would then show a marker no longer under the
        // pointer). Recompute when the pointer is still here (a clean release always
        // is); covers the immediate arm too. (If it had left, the pointer-leave hook
        // already cleared — but a clean release means it is here, so this is the
        // resolve.)
        if (gui.pointer_focused()) viewport.recompute_hover_at_cursor();
        return;
    }
    if (app.trim_drag.active) {
        commit_trim_drag();
        return;
    }
    if (app.pending_trim_drag.active) {
        // The pending trim drag never crossed the threshold: a motionless
        // chip/bridge press. Under the lane-click model that is the trim-lane
        // CLICK (R4.5) — highlight the trim window with the REGION (which can
        // collapse a 2+ selection when the sync's arm leaves no span; a resting
        // click is a commit, so nothing restores it — see
        // sync_region_to_trim_window). The pending only arms on the full pair
        // (route_trim_chip_press gates it), so the window exists; the sync takes
        // it. (A crossed pending became app.trim_drag and
        // commits through the branch above; read-only never armed a pending, so
        // this branch is writable-only — the read-only R4.5 click ran at press.)
        app.pending_trim_drag = PendingTrimDrag{};
        sync_highlight_to_trim_window();
        return;
    }
    if (app.pending_marker_drag.active) {
        // The pending marker drag never crossed the threshold: a pure flag
        // click, in one of two shapes.
        //  - IMMEDIATE arm (deferred_click false): the press already
        //    single-selected its marker AND seeded the Marker double-click
        //    candidate (press-time seeding), so there is nothing to commit —
        //    just disarm.
        //  - DEFERRED arm (deferred_click true): the press held the click's
        //    committed act back (a group member was pressed, the
        //    multi-selection intact). A motionless release IS that click now —
        //    collapse to {m}, land the playhead on it and collapse any resting
        //    span, exactly the immediate path's press-time act (the point command
        //    owns its clear — see land_playhead_on_marker). Bounds-check m against
        //    the active column's store defensively (nothing mutates it between
        //    press and release — the pending gate swallows keys).
        // (A crossed pending became app.drag — dropping the candidate at the
        // threshold crossing — and commits through the branch below.)
        const bool deferred = app.pending_marker_drag.deferred_click;
        const int  m        = app.pending_marker_drag.marker;
        app.pending_marker_drag = PendingMarkerDrag{};
        if (deferred) {
            const int n = (app.active_markers_view == 'P')
                ? static_cast<int>(app.phaseresetmarkers.markers().size())
                : static_cast<int>(app.warpmarkers.markers().size());
            if (m >= 0 && m < n) {
                selection.set_single_selection(m);
                // Deferred click completes on marker m: set_single_selection owns
                // the always-on stem's subject-change damage.
                land_playhead_on_marker(app, audio, viewport, m);
                clear_region_highlight(app, viewport);
            }
        }
        // Settle hover to the pointer's ACTUAL position (see the tempo pending's
        // twin above): a sub-threshold slide off the flag left hover_popup stale,
        // so re-resolve while the pointer is still here (a clean release always
        // is) — the hover popup / lane text then reflect the true pointer position,
        // immediate arm too.
        if (gui.pointer_focused()) viewport.recompute_hover_at_cursor();
        return;
    }
    if (!app.drag.active) return;
    marker_drag.commit_drag();
}

// Motion handler. Drives the active pointer gesture: editor-text drag,
// strip-row zoom/pan drag, trim drag (or
// a pending trim drag arming past the threshold), region-select drag, or
// marker reposition drag (or a pending
// marker drag); with no gesture it recomputes hover at the cursor. The
// marker drag applies the pointer delta to the grabbed marker; the playhead
// follows the grabbed marker unconditionally (apply_drag_motion owns that —
// the arming plain click already landed the playhead on the marker, so the
// drag tows it by construction).
void GuiInputHandler::on_motion(int mouse_x, int mouse_y, GuiInputState mods) {
    // Record latest cursor coords so viewport mutators can re-evaluate hover
    // at the cursor's last position.
    app.last_mouse_x = mouse_x;
    app.last_mouse_y = mouse_y;
    if (app.prompt.active) {
        viewport.clear_hover_popup();
        return;
    }
    // F2.1: editor-text drag motion. Handled before the settings swallow
    // (which returns) so the gesture reaches the bottom-strip editors, and
    // before the trim / playhead branches. A lost button finalizes like
    // release, mirroring those handlers.
    if (app.editor_text_drag.active) {
        if (!mods.primary_button_held) {
            finalize_editor_text_drag();
            return;
        }
        const ActiveEditorText g = active_editor_text(app, audio);
        if (g.valid) {
            // The anchor set at press stays put; moving cursor_pos extends
            // the selection.
            set_editor_caret_from_x(g, mouse_x);
            if (g.bottom_strip) viewport.invalidate_timestamp_area();
            else                viewport.invalidate_top_strip();
        }
        // !g.valid (only an invalid editor target — the lane text stays
        // onscreen even off-view): no-op this frame, leaving the caret put.
        viewport.clear_hover_popup();
        return;
    }
    if (text_editor::is_active(app.settings_editor) ||
        text_editor::is_active(app.commit_editor)) {
        viewport.clear_hover_popup();
        return;
    }
    // Dual-axis strip drag (the incremental v6 model; see apply_strip_drag_at).
    // Each event pans by its dx at the live level and zooms by its dy off the
    // live level, pivoting the zoom around the (edge-rebindable) song anchor. The
    // repaint is SYNCHRONOUS (final_event=false): one full rebuild whenever the
    // level or the viewport moved, a true
    // no-op when neither did — affordable because the platform coalesces captured
    // motion to one event per pointer frame. The release runs the one synchronous
    // rebuild plus the predictor resync. A lost button finalizes like release.
    if (app.strip_drag.active) {
        if (!mods.primary_button_held) {     // button lost -> end like release
            if (app.strip_drag.moved) {
                apply_strip_drag_at(mouse_x, mouse_y, /*final_event=*/true);
            }
            // A motionless press ends with NO click action from either origin
            // (the R3.4 ctrl-waveform clear is RETIRED — a motionless
            // ctrl+waveform press-release commits nothing on the clean release
            // too, so this abnormal end matches it for free), and a motionless
            // zoom-row press seeds NOTHING on this abnormal end (unlike the
            // clean release), matching the double_click clear below.
            app.strip_drag = StripDragState{};
            // An abnormal termination (button lost, not a clean release) seeds
            // no double-click candidate and drops any pending one.
            app.double_click = DoubleClickCandidate{};
            end_strip_pointer_capture();
            return;
        }
        // Sub-pixel capture jitter must not promote a click to a drag: while the
        // press has not yet become a drag, apply nothing until the pointer has
        // travelled at least the Chebyshev threshold from the press, leaving the
        // drag armed but unmoved. This gate decides only WHETHER the press
        // becomes a drag — once moved it never re-engages, so dragging back near
        // the press mid-drag has no dead zone. last_x/last_y stay at the press
        // until the crossing, so the crossing event folds the whole accumulated
        // delta since the press and no travel is lost.
        if (!app.strip_drag.moved &&
            std::max(std::abs(mouse_x - app.strip_drag.press_x),
                     std::abs(mouse_y - app.strip_drag.press_y)) <
                kDragMovedThresholdPx) {
            return;
        }
        app.strip_drag.moved = true;
        apply_strip_drag_at(mouse_x, mouse_y, /*final_event=*/false);
        viewport.clear_hover_popup();
        return;
    }
    // Alt+drag grab-pan (continuous 1:1). The viewport snaps to whole pixels in
    // clamp_viewport_start (reached through scroll_viewport), so a per-event pan
    // re-anchored by that snap tracks the cursor 1:1 without drift — no carried
    // sample remainder. scroll_viewport renders the plate synchronously, so
    // per-event work is one full-width render — the cost zoom already paid per
    // pointer frame, and the reason a panning plate looks identical to a resting
    // one (architect 2026-07-26). A
    // lost button ends it like release (re-anchor the predictor once). The
    // wheel keeps its quantized detent step; only the drag is continuous.
    if (app.scroll_drag.active) {
        if (!mods.primary_button_held) {     // button lost -> end like release
            if (playback.is_playing()) playback.resync_predictor();
            app.scroll_drag = ScrollDragState{};
            end_strip_pointer_capture();     // reappear the cursor (idempotent)
            return;
        }
        const double spp = current_samples_per_pixel(app, audio);
        const int    dx  = mouse_x - app.scroll_drag.last_x;
        app.scroll_drag.last_x = mouse_x;
        const int64_t delta =
            static_cast<int64_t>(std::nearbyint(static_cast<double>(dx) * spp));
        if (delta != 0) {
            // Grab-pan: drag right (dx>0) reveals earlier content, viewport
            // moves left. The mid-gesture guarantee this gesture needed — never
            // painting over a plate from an older basis, a staleness mechanism
            // the async deferral was once convicted of — now comes free: every
            // scroll renders synchronously and drains a busy worker, so it no
            // longer has to ask for a distinct pan mode.
            viewport.scroll_viewport(-delta, /*continuous=*/true);
        }
        viewport.clear_hover_popup();
        return;
    }
    // (No scrub motion branch: the scrub is a ONE-SHOT act at the press — the
    // held-drag per-column re-scrub is REMOVED (architect
    // 2026-07-23, the Ableton model), so motion over the scrub surfaces is
    // inert and the per-column stop-fence cadence is structurally gone.)
    // Trim-boundary drag motion. Handled before the marker-drag branch;
    // active in BOTH views (begin_trim_drag has no view gate, and
    // update_trim_drag / commit_trim_drag carry the target-view cached-map
    // machinery). A lost button commits at the current position, mirroring the
    // marker-drag motion handler.
    if (app.trim_drag.active) {
        viewport.clear_hover_popup();
        if (!mods.primary_button_held) {
            commit_trim_drag();
            return;
        }
        update_trim_drag(mouse_x);
        return;
    }
    // Pending trim drag (armed by a plain chip-row press): the trim reposition
    // begins only once the pointer travels past the shared Chebyshev threshold.
    // A lost button before the crossing ends it as a motionless click (nothing
    // committed). Placed after the trim_drag branch above: on the crossing this
    // begins the drag AND applies its first update inline, so it does not fall
    // back into that branch this event.
    if (app.pending_trim_drag.active) {
        viewport.clear_hover_popup();
        if (!mods.primary_button_held) {   // button lost -> just the click
            // The motionless chip/bridge press is the trim-lane CLICK (R4.5):
            // run the same region-highlight sync the clean release does, so the
            // same physical click cannot rest differently depending on which
            // path ended it. The pending only arms on the full pair (writable),
            // so the window exists.
            app.pending_trim_drag = PendingTrimDrag{};
            sync_highlight_to_trim_window();
            return;
        }
        if (std::max(std::abs(mouse_x - app.pending_trim_drag.press_x),
                     std::abs(mouse_y - app.pending_trim_drag.press_y)) <
                kDragMovedThresholdPx) {
            return;   // still a click; leave the pending armed, do nothing
        }
        // Threshold crossed: begin the trim drag anchored at the PRESS column so
        // the bound(s) track from the grab, this first update folding the whole
        // press->crossing delta (the marker-pending / strip catch-up pattern).
        // begin_trim_drag captures the anchor at press_x now — exact, since
        // nothing mutated the trim store between press and crossing — and sets
        // app.trim_drag.active.
        const bool is_begin = app.pending_trim_drag.is_begin;
        const bool both     = app.pending_trim_drag.both;
        const int  press_x  = app.pending_trim_drag.press_x;
        // R3: a bound-set-armed pending carries the PRE-PRESS pair — capture it
        // before clearing the pending, so the drag's Esc-restore origin can undo
        // the whole set+drag gesture.
        const bool    set_click = app.pending_trim_drag.set_click;
        const int64_t preset_begin = app.pending_trim_drag.preset_begin_frame;
        const int64_t preset_end   = app.pending_trim_drag.preset_end_frame;
        // The PRE-GESTURE selection + region travel with them, UNCONDITIONALLY
        // (a plain chip drag needs them as much as a bound-set one: its motion
        // syncs can collapse a 2+ selection just the same). Captured at the
        // arming press, applied by the drag's Esc-cancel.
        SelectionSnapshot pre_selection =
            std::move(app.pending_trim_drag.pre_gesture_selection);
        const RegionState pre_region = app.pending_trim_drag.pre_gesture_region;
        app.pending_trim_drag = PendingTrimDrag{};
        begin_trim_drag(is_begin ? TrimHit::Begin : TrimHit::End, press_x, both);
        if (!app.trim_drag.active) return;  // begin refused (no pair / no audio)
        app.trim_drag.pre_gesture_selection = std::move(pre_selection);
        app.trim_drag.pre_gesture_region    = pre_region;
        if (set_click) {
            // The drag BASE (orig_frame) stays the click-set value begin_trim_drag
            // captured, so the bound tracks smoothly from the clicked column; only
            // the Esc-restore origin (orig_begin/orig_end) moves back to the
            // pre-press pair, so an Esc undoes the click-set too. set_click makes
            // that restore unconditional (an unmoved drag still has the click-set
            // to undo).
            app.trim_drag.set_click        = true;
            app.trim_drag.orig_begin_frame = preset_begin;
            app.trim_drag.orig_end_frame   = preset_end;
        }
        update_trim_drag(mouse_x);
        return;
    }
    // Motion just continues whatever the press already armed — the
    // home-view gate (active_column_authoring_allowed, plus the tempo
    // drag's own eligibility check) ran once at arm time in
    // on_button_press, so nothing here re-checks view or column; the
    // region drag below is navigation, not authoring, and was never
    // gated. Per-site translation (drag anchor capture, motion delta
    // conversion, hit tests) lives in the handlers below.
    if (app.region_drag.active) {
        viewport.clear_hover_popup();
        // Left button must still be held; if not, the release was lost —
        // end the gesture, resting the region at its current extent (as a
        // clean release would). Modifier changes mid-drag are ignored. A
        // sub-threshold sliver rest dissolves like a click, exactly as the
        // clean release branch does (end_region_drag_min_size_check) — and
        // identically gated on `moved` (codex second-pass round-1 MEDIUM): a
        // MOTIONLESS lost button on the shift-exact preserving arm must not delete
        // the legal narrow region it left resting. Capture `moved` before the
        // reset zeroes it; plain path unaffected for the same reasons as the clean
        // release branch.
        if (!mods.primary_button_held) {
            const bool moved = app.region_drag.moved;
            app.region_drag = RegionDragState{};
            if (moved)
                end_region_drag_min_size_check(app, audio, viewport);
            return;
        }
        const GuiRect area = waveform_area(app);
        if (area.w <= 0) return;
        // Sub-threshold: the press has not yet become a drag. Below the shared
        // Chebyshev gate nothing extra happens — the press already did the
        // click and cleared any resting region at mouse-down. Once a drag,
        // always a drag (moved never re-engages). `crossing` = this event is the
        // transition to moved (captured before we set the flag); it force-installs
        // the anchored span below, past the same-column short-circuit.
        const bool crossing = !app.region_drag.moved;
        if (crossing &&
            std::max(std::abs(mouse_x - app.region_drag.press_x),
                     std::abs(mouse_y - app.region_drag.press_y)) <
                kDragMovedThresholdPx) {
            return;
        }
        app.region_drag.moved = true;
        // A moved region drag drops any double-click candidate: this press became
        // a drag, not the first click of a double-click. Only the R6 empty
        // flag/triangle-lane press seeds a candidate before arming this drag (a
        // waveform press seeds none), so this keeps a lane drag from later
        // consuming as an EmptyLane marker-create double-click — the standing
        // moved-drag clear route.
        app.double_click = DoubleClickCandidate{};
        // Far endpoint at the pointer column, through the same click->frame
        // basis as the anchor, clamped to the visible strip like the other
        // drags' live tracking. Endpoints are active-domain frames, so the
        // span survives pan/zoom mid-drag and at rest. Also clamped into the
        // live domain: at a fractional flush-right zoom the painter-quantized
        // wall differs from the click conversion, so the last visible column's
        // frame can land at domain_total — one past [0, domain_total-1] — which
        // the display-state validator would clear wholesale (same rule as the
        // press-site anchor; the round-3 no-clamp provenance is disproven).
        int rel = mouse_x - area.x;
        if (rel < 0) rel = 0;
        if (rel >= area.w) rel = area.w - 1;
        const int64_t far_frame = clamp_playhead_to_live_domain(
            playhead_frame_at_click_column(app, audio, rel), app, audio);
        // Column-change gate: the span changes only when the far endpoint moves
        // to a new frame. A same-frame motion event (sub-pixel jitter within one
        // column) is a no-op — skip the repaint. The anchor is fixed for the
        // gesture, so the far endpoint alone decides the span. The CROSSING event
        // ALWAYS installs {anchor, pointer}, bypassing this short-circuit (codex
        // second-pass round-1 MEDIUM): the plain arm cleared the region
        // (active == false) so it would proceed anyway, but the SHIFT-exact
        // preserving arm leaves the OLD region active, and its stale b_frame can
        // coincide with the first crossed column's frame — without the bypass the
        // crossing marks moved yet returns before installing, resting the stale
        // span indefinitely. The short-circuit's purpose (redundant-damage
        // avoidance on same-column motion) survives for every LATER event.
        if (!crossing && app.region.active && far_frame == app.region.b_frame)
            return;
        app.region.active     = true;
        app.region.a_frame    = app.region_drag.anchor_frame;
        app.region.b_frame    = far_frame;
        // Drag-formed, so FREE provenance: no tempo gesture re-derives it (it has
        // an empty selection anyway).
        app.region.provenance = RegionProvenance::Free;
        // SELECTION FLOWS DOWNWARD ONLY (architect 2026-07-23): highlighting a
        // region does NOT select the markers it contains (the reverse coupling —
        // a region selecting its contents — was tried and retired; do not
        // re-propose) — the press already deselected all and the drag
        // leaves the selection EMPTY throughout. The reverse direction stands:
        // when markers ARE selected the region is set to their extent (the
        // multi-select clicks via set_region_to_selection_extent — provenance
        // SelectionExtent; the trim window via sync_highlight_to_trim_window —
        // provenance TrimWindow). So a drag-formed region always rests with an
        // empty selection (Free) — and whenever 2+ markers rest selected WITH an
        // active SelectionExtent region, that region IS their extent by
        // construction (a TrimWindow region may rest beside any selection).
        viewport.invalidate_waveform_area();
        return;
    }
    // Target-view tempo drag motion: each event re-solves the pointer's
    // target position to a predecessor-tempo candidate and commits changed
    // candidates live with a synchronous re-warp (apply_tempo_drag_motion —
    // vertical motion is ignored there). A lost button finalizes like
    // release, mirroring the marker-drag motion handler.
    if (app.tempo_drag.active) {
        viewport.clear_hover_popup();
        if (!mods.primary_button_held) {
            marker_drag.end_tempo_drag();
            return;
        }
        marker_drag.apply_tempo_drag_motion(mouse_x);
        return;
    }
    // Pending tempo drag (armed by a plain flag press in W + target view on
    // an eligible marker): the tempo drag begins only once the pointer
    // travels past the shared Chebyshev threshold — the SAME
    // kDragMovedThresholdPx grab slop the reposition drag uses. A lost
    // button before the crossing ends it as a plain click. Placed after the
    // tempo_drag branch above: on the crossing this begins the drag AND
    // applies its first solve inline (the solve is absolute — pointer x ->
    // tempo — so applying at the CURRENT x needs no press-anchor catch-up
    // fold), and does not fall back into that branch this event.
    if (app.pending_tempo_drag.active) {
        if (!mods.primary_button_held) {   // button lost -> just the click
            // A lost button before the crossing IS the click, matching the
            // release path: a DEFERRED arm completes the held single-select +
            // land + span collapse now; an immediate arm just disarms.
            // Bounds-check m (W store).
            const bool deferred = app.pending_tempo_drag.deferred_click;
            const int  m        = app.pending_tempo_drag.marker;
            app.pending_tempo_drag = PendingTempoDrag{};
            if (deferred) {
                const int n = static_cast<int>(app.warpmarkers.markers().size());
                if (m >= 0 && m < n) {
                    selection.set_single_selection(m);
                    // Deferred click completes on marker m: set_single_selection
                    // owns the always-on stem's subject-change damage.
                    land_playhead_on_marker(app, audio, viewport, m);
                    clear_region_highlight(app, viewport);
                }
            }
            // Settle hover to the pointer's ACTUAL state instead of the old
            // unconditional clear (which erased a legitimately-hovering popup):
            // if the pointer is still here, re-resolve — a slid-off pointer drops
            // the popup/lane text, a still-hovering one keeps it; if focus is gone
            // (the usual lost-button cause), the pointer-leave hook already cleared,
            // so this is a no-op. Same observable end state as the clean release
            // above.
            if (gui.pointer_focused()) viewport.recompute_hover_at_cursor();
            return;
        }
        if (std::max(std::abs(mouse_x - app.pending_tempo_drag.press_x),
                     std::abs(mouse_y - app.pending_tempo_drag.press_y)) <
                kDragMovedThresholdPx) {
            return;   // still a click; leave the pending armed, do nothing
        }
        const int marker = app.pending_tempo_drag.marker;
        app.pending_tempo_drag = PendingTempoDrag{};
        // A moved tempo drag drops any double-click candidate: this press is
        // a drag, not a click of a marker double-click — the same
        // load-bearing clear the pending marker drag's crossing does (the
        // arming press seeded a Marker candidate at press time).
        app.double_click = DoubleClickCandidate{};
        if (!marker_drag.begin_tempo_drag(marker)) {
            viewport.clear_hover_popup();
            return;   // begin refused (eligibility re-check): drop the gesture
        }
        marker_drag.apply_tempo_drag_motion(mouse_x);
        return;
    }
    // Pending marker drag (armed by a plain flag press): usually the marker was
    // single-selected at press; the reposition begins only once the pointer
    // travels past the shared Chebyshev threshold (kDragMovedThresholdPx, now
    // one generic 8px gate for every press-becomes-drag surface). Handled before the hover
    // fallthrough below and after the other drag branches (a pending drag and
    // any other pointer gesture are mutually exclusive — the arming press does
    // no other work). A lost button before the crossing ends it as a plain
    // click.
    if (app.pending_marker_drag.active) {
        if (!mods.primary_button_held) {   // button lost -> just the click
            // A lost button before the crossing IS the click, matching the
            // release path: a DEFERRED arm (deferred_click — a group member
            // pressed with the multi-selection held) completes the held
            // single-select + land + span collapse now; an immediate arm already
            // committed that at press and just disarms. Bounds-check m
            // defensively.
            const bool deferred = app.pending_marker_drag.deferred_click;
            const int  m        = app.pending_marker_drag.marker;
            app.pending_marker_drag = PendingMarkerDrag{};
            if (deferred) {
                const int n = (app.active_markers_view == 'P')
                    ? static_cast<int>(app.phaseresetmarkers.markers().size())
                    : static_cast<int>(app.warpmarkers.markers().size());
                if (m >= 0 && m < n) {
                    selection.set_single_selection(m);
                    // Deferred click completes on marker m: set_single_selection
                    // owns the always-on stem's subject-change damage.
                    land_playhead_on_marker(app, audio, viewport, m);
                    clear_region_highlight(app, viewport);
                }
            }
            // Settle hover to the pointer's ACTUAL state (see the tempo twin
            // above): re-resolve while the pointer is here, else the pointer-leave
            // hook owns the clear. Replaces the old unconditional clear that broke
            // its own "matches the clean release" promise by erasing a hovering
            // popup. Same observable end state as the clean release.
            if (gui.pointer_focused()) viewport.recompute_hover_at_cursor();
            return;
        }
        if (std::max(std::abs(mouse_x - app.pending_marker_drag.press_x),
                     std::abs(mouse_y - app.pending_marker_drag.press_y)) <
                kDragMovedThresholdPx) {
            return;   // still a click; leave the pending armed, do nothing
        }
        // Threshold crossed: begin the drag anchored at the PRESS column so the
        // marker tracks the pointer 1:1, this first apply folding the whole
        // press->crossing delta (the strip/region catch-up pattern). begin_drag
        // captures the pre-drag snapshot / selection / walls now — exact, since
        // nothing mutated the store between press and crossing — and sets
        // app.drag.active. Fall through (no return) so this same motion event
        // applies the first delta through the marker-drag branch below.
        const int marker  = app.pending_marker_drag.marker;
        const int press_x = app.pending_marker_drag.press_x;
        app.pending_marker_drag = PendingMarkerDrag{};
        // A moved marker drag drops any double-click candidate: this press is a
        // drag, not a click of a marker double-click. The arming press seeded a
        // Marker candidate (press-time seeding), so this clear is what keeps a
        // moved drag from carrying one — the load-bearing half of seeding at
        // the press.
        app.double_click = DoubleClickCandidate{};
        if (!marker_drag.begin_drag(marker, press_x)) {
            viewport.clear_hover_popup();
            return;   // begin refused (bad index / no audio): drop the gesture
        }
        // No follow override needed: the marker drag always begins from a
        // top-strip flag press, which already stopped playback (the marker
        // click owns that stop), so there is no live playhead to chase.
    }
    if (!app.drag.active) {
        // No active gesture: hover recomputation is owned by
        // recompute_hover_at_cursor (one implementation for motion and
        // viewport mutation), suppressions included. The branches above
        // already returned on the suppressions it re-checks (prompt, the
        // editors, the other drags), so those re-checks are harmless; its
        // W-mode / iter-mode / top_flag_editor / queue_running arms clear
        // the popup exactly like this path's own else-clear did.
        viewport.recompute_hover_at_cursor();
        return;
    }
    // A drag is active — drop any pending popup.
    viewport.clear_hover_popup();
    // Left button must still be held down — otherwise release was lost.
    if (!mods.primary_button_held) {
        marker_drag.commit_drag();
        return;
    }
    const int sr = audio.sample_rate();
    if (sr <= 0) return;
    const GuiRect area = waveform_area(app);
    const double spp = current_samples_per_pixel(app, audio);
    // The delta handed to apply_drag_motion is an ACTIVE-domain frame delta:
    // mouse_frame is the pointer's plain active-domain position — one
    // expression, both views, no inverse map anywhere in its derivation.
    // The displayed-map hops that carry the delta into the source domain
    // live inside apply_drag_motion, which anchors the proposal in the
    // DISPLAYED target domain so the painted flag tracks the pointer 1:1.
    const double mouse_frame = static_cast<double>(app.viewport_start_sample) +
        static_cast<double>(mouse_x - area.x) * spp;
    marker_drag.apply_drag_motion(mouse_frame - app.drag.anchor_mouse_time_frame);
    // Playhead rule: the playhead follows the grabbed marker through the drag
    // inside apply_drag_motion (the arming click landed it on the marker, so
    // the drag tows it by construction — the DragState ruling). The focus
    // transfer onto the grabbed marker already ran at the THRESHOLD CROSSING in
    // begin_drag (a single-marker drag re-asserts the single selection, a group
    // drag focuses without collapsing), so it holds even for a wall-saturated
    // drag with no moved motion; apply_drag_motion here only latches
    // app.drag.moved and slides the playhead. Nothing further tracks here.
}
