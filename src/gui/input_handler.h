#pragma once

#include "app_state.h"
#include "async_renderer.h"
#include "audio.h"
#include "flag_editor.h"
#include "playback.h"
#include "playback_lifecycle.h"
#include "prompt.h"
#include "render_pipeline.h"
#include "renders_dir.h"
#include "save_ops.h"
#include "selection.h"
#include "active_views.h"
#include "settings_editor.h"
#include "target_render.h"
#include "phase_reset_propagate.h"
#include "phaseresetmarkers_ops.h"
#include "marker_drag.h"
#include "undo.h"
#include "value_format.h"
#include "viewport.h"
#include "warp_frame_map.h"
#include "warpmarkers.h"
#include "warpmarkers_ops.h"
#include "gui_input.h"
#include "platform_wayland.h"

#include <cmath>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

struct GuiPaintHandler;

// Keyboard input handler. Owns the on_key callback body; lifetime is the
// same scope as the other operation structs.
//
// Also provides on_button_press / on_button_release as public methods plus
// the shared wheel handler as a private helper, and on_motion the same way.

// -- BPM-sweep math primitive -------------------------------------------
//
// Promoted out of main.cpp's anonymous namespace so input_handler.cpp can
// reach it. render_bpm_sweep() is the sole caller; if a future TU needs it the
// home is open for relocation.
//
// Given a span's measured duration (seconds), the user-asserted beat count
// for that span, and a target BPM, return the (base_tempo_cents, scale)
// pair the engine needs so that one cell of the BPM sweep renders at
// exactly the target tempo. The derivation is a double-domain ENTRY into
// the integer-cents tempo domain: the ratio rounds ONCE to int64 cents via
// banker's rounding (std::nearbyint with FE_TONEAREST), is bracket-checked
// in cents, and is assigned as cents; scale stays full double precision
// (the shortest round-trip serializer keeps in-memory and on-disk values
// bit-identical). The bash-script port uses an epsilon nudge before
// rounding to work around shell-level numerics — that nudge does not apply
// in C++ and is intentionally omitted here. The C++ port may diverge from
// the bash script on tie cases; this is documented behavior.
struct BaseTempoScale {
    int64_t base_tempo_cents;
    double  scale;
};

inline std::optional<BaseTempoScale> compute_base_tempo_scale(
    double duration_seconds, int beats, double target_bpm) {
    if (!(duration_seconds > 0.0)) return std::nullopt;
    if (beats <= 0)                return std::nullopt;
    if (!(target_bpm > 0.0))       return std::nullopt;

    const double desired_duration =
        static_cast<double>(beats) * 60.0 / target_bpm;
    if (!std::isfinite(desired_duration) ||
        desired_duration == 0.0) return std::nullopt;

    const double ratio = duration_seconds / desired_duration;
    if (!std::isfinite(ratio)) return std::nullopt;

    // Tempo bracket (value_format.h): a derived base tempo lands in marker
    // stores and .warpmarkers sidecars exactly like a typed tempo, so an
    // out-of-bracket derivation refuses here — never clamps, which would
    // silently mistune the span. The bracket check runs on the rounded
    // cents count BEFORE the int64 cast, so a non-finite or overflowing
    // cents value can never reach the cast (in-bracket doubles convert
    // exactly). The BPM editor commit checks the bracket's two ends through
    // this refusal (the derivation is monotone in bpm), and the sweep's
    // per-cell loop rejects any refused cell to stderr.
    const double cents_d = std::nearbyint(ratio * 100.0);
    if (!std::isfinite(cents_d) ||
        cents_d < static_cast<double>(kTempoMinCents) ||
        cents_d > static_cast<double>(kTempoMaxCents)) {
        return std::nullopt;
    }
    const int64_t base_tempo_cents = static_cast<int64_t>(cents_d);

    // Full-double derived scale, no decimal quantization: the value
    // serializer is shortest round-trip (value_format.h), so the exact
    // ratio / base_tempo survives the .settings write bit-identically, so
    // the in-memory value and the on-disk text agree with no decimal grid.
    // tempo_from_cents here is the derivation's own cents-to-double
    // boundary — the divisor is bit-identical to the double the cell
    // sidecar's N.NN spelling parses to.
    const double scale = ratio / tempo_from_cents(base_tempo_cents);
    if (!std::isfinite(scale)) return std::nullopt;
    // Scale bracket (value_format.h): the derived scale is stamped into the
    // cell's .settings exactly like the derived base tempo is stamped into
    // its markers, so an out-of-bracket scale refuses here for the same
    // reason — never clamps. Without this it could rest in a .settings the
    // GUI could never author.
    if (scale < kScaleMin || scale > kScaleMax) return std::nullopt;

    return BaseTempoScale{base_tempo_cents, scale};
}

// -- Target-view entry validity predicate --------------------------------
//
// The single definition of "may this state enter target view", shared by
// the two entry surfaces: the keyboard S → T toggle
// (GuiInputHandler::handle_active_audio_view_toggle) and the load-time
// restore of active_audio_view=T from .settings (GuiFileLoader::load_file).
// If a keystroke entry would be blocked, a load restore is blocked
// identically — the predicate changes in one place or not at all.
//
// The walk: resolve_warp_markers_for_render over `markers` (the resolver
// normalizes ambiguous arrangements to tempo 1.00 — one stderr line per
// timestamp — so it never refuses an authored store), then
// build_warp_frame_map with `scale` (the whole-song map — trim is
// render-time, not view-time). Trim plays no part: crossed/equal bounds
// cannot rest (the commit and load auto-clears), and an ambiguous trim at
// render time falls back to the full, untrimmed deliverable — so after the
// normalizing resolver, entry gates only on the tripwire-class build
// failures. On success the built map is returned so the toggle can reuse
// it for its viewport/playhead translation (no second build); on failure
// the first owner's error string is returned verbatim.
std::expected<std::vector<WarpFrameMapSegment>, std::string>
validate_target_view_entry(const std::vector<GuiWarpMarker>& markers,
                           double scale, int sample_rate, long total_frames);

// Dissolve a resting region-select highlight, damaging the waveform so the
// recolored ground and split playhead repaint away and the cursor playhead
// returns under
// the same damage. A no-op when no region is active. Shared by every POINT
// COMMAND — the playhead has two forms, POINT and SPAN, and exactly one is ever
// visible (the rule and its rationale live at land_playhead_on_marker), so a
// command that asserts the point form ends the span here, UNCONDITIONALLY: never
// gated on whether the playhead moved, and blind to provenance. Call sites, by
// class:
//   * NAVIGATION jumps: jump_playhead_to_focused_marker (the whole Tab family
//     plus `c` through the one tail), run_zoom_toggle_command (bare `0`), the
//     bare Left/Right playhead step, Home/End (the trim-bound jumps);
//   * MARKER CLICKS: the plain single-select click (its four deferred completions
//     died 2026-07-29 with the group drag — every marker press commits its click at
//     PRESS time now, the doctrine at the head of position_nudge.h), plus
//     the two MULTI-SELECT clicks whenever their RESULT is under two selected
//     (at 2+ they instead re-own the region through
//     set_region_to_selection_extent — the DOWNWARD coupling, the selection
//     defines the extent region, SELECTION-FLOWS-DOWNWARD-ONLY, a separate call
//     that must run AFTER any clear);
//   * enter_text_edit, the one chokepoint of every flag/bpm editor open and
//     retarget;
//   * the POSITION NUDGES, both columns (finish_position_nudge, which clears
//     unconditionally: horizontal movement is a FOCUS ACT since 2026-07-29 — a 2+
//     selection collapses to its focus in the shared prologue, so a nudge is always
//     a point command and the span-preserving group arm is gone);
//   * the MARKER DROPS (architect 2026-07-29, overruling their earlier keep):
//     a drop seats the playhead on the marker it creates and single-selects it,
//     so it is a point command like the rest — one clear per column, at the
//     drop chokepoints drop_marker (warp) and drop_phase_reset_at_position
//     (phase reset), which both entry routes (bare `s`, the empty-lane
//     double-click) converge on, placed past every refusal;
//   * the THREE MEMBERSHIP-WHOLESALE routes (architect 2026-07-29, the
//     maximally greedy collapse): the UNDO/REDO restore's visual tail, the `p`
//     W/P swap (toggle_active_markers_view — which since the parked selections
//     died replaces the membership by EMPTYING it), and the propagate paste's
//     target-view tail. None of them builds the span it would be resting beside,
//     so all three collapse it, any provenance. The undo tail's clear runs for
//     EVERY entry, settings-only ones included — it is the ONE part of the tail
//     that sits above the 'S' gate (which forbids SELECTING, WRITING a region, and
//     LANDING, exceptionlessly again since ruling 6 — that arm now also CLEARS the
//     selection, which is what killed the one land exception it briefly had),
//     because a settings restore rebuilds the map under the
//     span exactly as a marker restore does; for a group entry the extent derive
//     then runs AFTER this clear;
//   * the SETTINGS ENGINE-COMMIT chokepoint (same ruling): the one committed
//     tail every canonical engine key shares (GuiSettingsEditor::commit, past
//     the unknown-key / invalid-value / collision / unchanged returns), because
//     the scale among those keys is a warp-map input and the commit rebuilds the
//     map under any resting highlight. That tail CLEARS THE SELECTION beside the
//     region (ruling 6, below). The settings editor's TRIM keys never
//     reach it — they return through commit_gui_setting, which re-syncs a
//     TrimWindow highlight to the edited bounds, a maintainer by ruling.
// A 2+ SELECTION MAY REST WITHOUT A SPAN — THE NEVER-SPAN-LESS ENFORCEMENT IS
// RETIRED (architect 2026-07-29, ruling 6, superseding the corollary this
// declaration used to carry and the distributed collapse protocol that served it:
// nine sites at its peak, six after the parked selections died, all deleted, along
// with sync_region_to_trim_window's injected Selection& parameter). Nothing is
// added in its place. The two-forms model itself STANDS unchanged
// (land_playhead_on_marker, input_pointer.cpp): while a span is ACTIVE it outranks
// every point cue and suppresses both the cursor and the singleton stem; with no
// span the SELECTION's own cues — the selected flags' ink triangles — are the
// visible state, and the resting cursor is the playhead of record for Space. What
// the enforcement bought was one extra guarantee on top of that, "a group always
// has a drawn playhead form", and the architect withdrew it as not worth a protocol
// spread across the tree.
// THE LAST TWO PRODUCERS CLOSED SYMMETRICALLY instead, at the two sites where a
// map rebuild invalidates every index and image at once: the engine-key settings
// COMMIT and the settings-only ('S') undo/redo RESTORE each clear the selection
// beside the region (GUI-kind keys are history-less, so 'S' is the only settings
// entry kind and the pair is complete). Both #19 map-changer RE-LANDS died with
// them — nothing selected means no focus to re-land — so the 'S' gate is simple
// again: no select, no region write, no land.
// THREE COMMANDS DO produce a resting spanless 2+ selection, verified against the
// tree rather than assumed, and each records the state at its own site: the S/T
// domain flip (the span's endpoints are active-domain frames and cannot survive
// it, while the selection is carried across — reachable through the bare `t` key
// AND the settings editor's `active_audio_view=` GUI key, which is not covered by
// the engine-commit clear), bare `0`, and `c` (both pure viewport/playhead commands
// that clear the span and touch no membership). In that
// state no playhead form is DRAWN — the cursor yields to a non-empty selection and
// the stem is singleton-only — while Space still launches from the resting cursor;
// that is the accepted cost of the retirement, and paint_playheads states it.
// TWO CANDIDATES ARE NOT PRODUCERS, by derivation: the TRIM-SYNC no-window arm
// (all three of its non-setter callers gate on provenance == TrimWindow, and a
// TrimWindow region rests only beside an EMPTY selection — the derivation lives at
// that function), and the kick validator's DOMAIN-SHRINK clear (the only
// total-changers that keep a 2+ selection are the group cent step, which re-derives
// its extent unconditionally AFTER its kick, and the group undo restore, which
// derives its extent from the already-restored domain and clamps into it, so the
// validator's out-of-domain test cannot fire on it).
// The land itself clears NOTHING — it is a pure playhead write, and the
// motion condition that briefly rode its clear died with that clear
// (land_playhead_on_marker). DELIBERATELY NOT
// cleared: the region Space launch (the
// region IS the launch point there — and since 2026-07-29 it moves no cursor
// either, launching through the scrub's own entry), the nudge/drag playhead
// follow, and pure
// viewport moves (PageUp/PageDown, zoom
// steps, pans) — and the lower-half scrub press, which touches no region at
// all. The plain upper-half waveform press (arm_region_drag_at) shares this
// helper — same dissolve shape. ESC IS NOT A CLEAR SITE AT ALL (architect
// 2026-07-29): the selection/region ladder is deleted, so bare Esc touches neither
// the region nor the selection anywhere — the whole Esc story is stated at its
// dispatch point in on_key. The remaining pre-existing clear sites (file load,
// Ctrl+Tab, and the S/T switch) keep their own in-place clears, pairing the reset
// with a domain flip or a full-window repaint rather than this exact damage shape.
void clear_region_highlight(AppState& app, Viewport& viewport);

// The DOWNWARD coupling (the selection defines the extent region,
// SELECTION-FLOWS-DOWNWARD-ONLY): set the region to the
// current selection's active-domain [earliest, latest] position extent when 2+
// markers are selected (a <=1 selection sets nothing). MUST run AFTER its
// caller's own point-form clear_region_highlight.
// Definition lives in input_pointer.cpp; declared here
// so the group TEMPO gestures, undo.cpp's restore and the propagate paste can
// reach the same owner (the class-level contract is at the definition).
void set_region_to_selection_extent(AppState& app, const GuiAudio& audio,
                                    Viewport& viewport);

// LAND the playhead exactly onto marker `hit` of the ACTIVE column with NO
// viewport move (the two-step placement basis source_frame_to_active_domain then
// clamp_playhead_to_live_domain, a direct cursor write). A PURE PLAYHEAD WRITE:
// it touches no region and no selection — a caller that wants the span to
// collapse calls clear_region_highlight itself. Read-only allowed. Definition in
// input_pointer.cpp, whose comment is the AUTHORITATIVE statement of the two
// playhead forms, the marker-lane-owns-the-playhead rule, and the one
// enumeration of the landing sites — do not restate any of them here.
void land_playhead_on_marker(AppState& app, const GuiAudio& audio,
                             Viewport& viewport, int hit);

// COINCIDENCE AUTO-SELECT — the entry counterpart of the never-park rule
// (architect 2026-07-29): the selection is never stashed, so an ENTRY re-acquires
// it from the playhead instead of from memory. Scans the ACTIVE column's store in
// order and SINGLE-SELECTS the first marker whose land value is EXACTLY the
// resting playhead. Definition in input_pointer.cpp, beside the land whose
// formula it reuses; that comment states the rule, the exactness, and the
// first-in-store tie-break. THREE CALL SITES, each stating only its own class and
// pointing there: the source load's tail (file_loader.cpp), the `p` column entry
// (toggle_active_markers_view) and the Ctrl+Tab tab entry
// (switch_active_tab_view_to), both in active_views.cpp. No match leaves the
// selection exactly as the caller left it — every caller clears first, so that
// means empty.
void auto_select_marker_at_playhead(AppState& app, const GuiAudio& audio,
                                    Selection& selection, Viewport& viewport);

// Frame an ACTIVE-domain span [lo, hi] into the viewport: compute the margined
// fit level (effective_max_zoom_level's formula over the span, clamped
// [kMinZoom, effective ceiling]) and CENTER the span in the window, then apply
// through Viewport::apply_zoom_to_start (pre-clamps the level, funnels through
// clamp_viewport_start, keeps the idempotent current-vs-target no-op, kicks one
// sync render). `margin` adds a 2.5%-per-side (region / trim / group cases); the
// whole-song case passes margin=false. The centering formula uses the UNROUNDED
// visible width (spp_t * W) — grid quantization is owned downstream by
// clamp_viewport_start, so NO painter-quantized pre-rounding is applied here
// (only the final start is rounded). A floor-saturated span rests CENTERED rather
// than left-aligned, and the unclamped case degenerates to the span's left edge
// (unrounded spp_t * W == the margined span by the solve). Shared by
// run_zoom_double_click_command (all three arms) and the GROUP undo/redo
// restore's offscreen framing. Definition in input_handler.cpp.
void frame_span_into_view(AppState& app, const GuiAudio& audio,
                          Viewport& viewport, int64_t lo, int64_t hi,
                          bool margin);

// TWO ARMS (architect 2026-07-29, the coincident middle arm deleted): both bounds
// set -> the region takes the trim window's active-domain extent with TrimWindow
// provenance, COINCIDENT IMAGES INCLUDED (a window whose two source bounds round
// onto one target frame under bracket-legal compression rests ACTIVE at that one
// column — paint_region_ground draws no ground at zero width and
// render_split_playhead has its own one-column form, the shape the undo group
// restore's all-coincident extent already rests at); no full pair -> clear the
// region. So a full pair DOES imply an active TrimWindow region.
// Definition lives in input_trim.cpp. It was made a FREE function so the group
// tempo gestures could re-sync a TrimWindow region across their own map change;
// those re-syncs are DELETED (2026-07-29, see below), so its only callers today are
// the two GuiInputHandler wrappers — sync_highlight_to_trim_window (the bare form,
// for the clearers and maintainers) and deselect_and_sync_trim_window_highlight
// (the SETTERS' form, which deselects ahead of the same call).
//
// A TrimWindow REGION RESTS ONLY BESIDE AN EMPTY SELECTION (architect 2026-07-29).
// Stated here once, at the sync that owns the provenance; every other site states
// its own class plus a pointer. EVERY TrimWindow SETTER CLEARS THE SELECTION — the
// trim-bar click is the span-form sibling of the plain waveform click's
// deselect-all: clicking trim means ready to move on. THE SETTERS (grep-derived
// over the callers of deselect_and_sync_trim_window_highlight, which pairs the
// deselect with the publish and is the only route that does):
//   * the PLAIN chip-row click, all three arms — the read-only press's direct sync,
//     the writable unclaimed-spot sync, and the deferred motionless completions
//     (clean release / lost button);
//   * the ctrl (BEGIN) and ctrl+shift (END) BOUND-SET clicks, one function
//     (set_trim_bound_at_click) and so one deselect;
//   * bare `x` (region -> trim), which deselects after its span is read;
//   * the trim chip/bridge DRAG's live sync — both motion arms and the release.
// Each deselects PAST ITS OWN REFUSALS (the refusal-gating rule these routes
// already hold their playback stop under): a read-only bound set, a chip-row claim
// with no resting pair, degenerate geometry, and a drag event that moved no bound
// publish no highlight and so deselect nothing.
// THE NON-SETTERS KEEP THE BARE SYNC and do not deselect: Shift+X (a trim
// CLEARER) and the settings editor's two trim maintainers. That is the whole list
// — the tempo family's TrimWindow re-syncs were DELETED by the same ruling (see
// the unreachability below), and the two trim Esc-cancel re-syncs died with the
// cancels themselves (pointer gestures have no cancel, 2026-07-29).
// The invariant's OTHER HALF is that nothing puts a selection back beside a
// resting TrimWindow region: every route that selects a marker either CLEARS the
// region (the plain/multi-select clicks, the Tab
// family and `c` through jump_playhead_to_focused_marker, the editor opens, the
// drops, and the three membership-wholesale routes) or REPLACES it with the
// selection's own extent (the multi-select clicks at 2+). So
// TrimWindow-under-a-selection is UNREACHABLE — which is what retired the tempo
// gestures' region-provenance branches, the singleton tempo step's trim re-sync,
// and the tempo drag's own grab-time trim-highlight bit (that drag is itself
// deleted since 2026-07-29 — see marker_drag.h).
// IT TAKES NO Selection AT ALL any more (architect 2026-07-29, ruling 6): the
// parameter existed for the never-span-less collapse its clear arm carried, that
// enforcement is retired (the retirement paragraph is at clear_region_highlight's
// declaration above), and the collapse was UNREACHABLE from here in the first
// place — all three non-setter callers gate on provenance == TrimWindow and the
// invariant above says such a region rests beside an EMPTY selection. So this
// function touches the REGION only, and the two wrappers differ exactly by whether
// the caller deselects first.
void sync_region_to_trim_window(AppState& app, const GuiAudio& audio,
                                Viewport& viewport);

// -- GuiInputHandler ----------------------------------------------------
//
// The batch render runner lives on this struct as private helper methods
// (start_render_batch and the ActiveBatch lifecycle), driven by the iteration
// and BPM sweeps; ActiveBatch is a nested type on the struct.
struct GuiInputHandler {
    AppState&                app;
    const GuiAudio&          audio;
    GuiPlatform&             gui;
    GuiPlayback&             playback;
    Viewport&                viewport;
    Selection&               selection;
    Undo&                    undo;
    GuiWarpMarkersOps&       warpops;
    GuiPhaseResetMarkersOps& phase_resets;
    MarkerDragOps&           marker_drag;
    GuiFlagEditor&           flag_editor;
    GuiRendersDir&           renders_dir;
    GuiActiveViews&          active_views;
    PhaseResetPropagate&     phase_reset_propagate;
    GuiAsyncRenderer&        async_renderer;
    GuiPlaybackLifecycle&    playback_lifecycle;
    GuiSaveOps&              save_ops;
    GuiPrompt&               prompt;
    GuiSettingsEditor&       settings_editor;
    GuiTargetRender&         target_render;
    GuiPaintHandler&         paint_handler;

    // Strip-drag pointer-capture hooks, seeded no-op and installed in main.cpp
    // to GuiPlatform::begin_pointer_capture / end_pointer_capture (the same
    // reverse-the-platform-boundary pattern as Viewport::kick_waveform_*).
    // ONLY the two strip-row drags fire them: begin after the press claim arms,
    // end on every strip-drag exit path (release, lost button, and the force-end
    // finalizer — there is no cancel path, 2026-07-29). Both
    // platform methods are self-guarding — begin no-ops when a capture is live
    // or the compositor lacks the managers, end is idempotent — so a strip drag
    // that never captured (degraded compositor) still calls end harmlessly.
    std::function<void()> begin_strip_pointer_capture = []{};
    std::function<void()> end_strip_pointer_capture   = []{};
    // Set the active capture's release-restore x to the anchor stem's surface
    // x. apply_strip_drag_at fires it each event (the last wins at release) so
    // the cursor reappears dead on the stem; the alt-pan never calls it, so its
    // release keeps the raw traveled-x restore.
    std::function<void(double)> set_strip_capture_restore_x = [](double){};

    GuiInputHandler(AppState&                app_,
                    const GuiAudio&          audio_,
                    GuiPlatform&             gui_,
                    GuiPlayback&             playback_,
                    Viewport&                viewport_,
                    Selection&               selection_,
                    Undo&                    undo_,
                    GuiWarpMarkersOps&       warpops_,
                    GuiPhaseResetMarkersOps& phase_resets_,
                    MarkerDragOps&           marker_drag_,
                    GuiFlagEditor&           flag_editor_,
                    GuiRendersDir&           renders_dir_,
                    GuiActiveViews&          active_views_,
                    PhaseResetPropagate&     phase_reset_propagate_,
                    GuiAsyncRenderer&        async_renderer_,
                    GuiPlaybackLifecycle&    playback_lifecycle_,
                    GuiSaveOps&              save_ops_,
                    GuiPrompt&               prompt_,
                    GuiSettingsEditor&       settings_editor_,
                    GuiTargetRender&         target_render_,
                    GuiPaintHandler&         paint_handler_)
        : app(app_),
          audio(audio_),
          gui(gui_),
          playback(playback_),
          viewport(viewport_),
          selection(selection_),
          undo(undo_),
          warpops(warpops_),
          phase_resets(phase_resets_),
          marker_drag(marker_drag_),
          flag_editor(flag_editor_),
          renders_dir(renders_dir_),
          active_views(active_views_),
          phase_reset_propagate(phase_reset_propagate_),
          async_renderer(async_renderer_),
          playback_lifecycle(playback_lifecycle_),
          save_ops(save_ops_),
          prompt(prompt_),
          settings_editor(settings_editor_),
          target_render(target_render_),
          paint_handler(paint_handler_) {
        // The worker-idle pump lives on GuiTargetRender (every completion
        // path funnels through maybe_dispatch_pending) while the parked
        // archival command's dispatch machinery lives here. Bridge them:
        // the pump offers each idle beat to the archival slot first
        // through this hook.
        target_render.dispatch_pending_archival =
            [this] { return dispatch_pending_archival_command(); };
    }

    // The settings editor is a keyboard front-end that funnels each `:`-typed
    // GUI-kind key into the SAME gesture chokepoint the key's gesture uses.
    // Three of those chokepoints are private methods here
    // (handle_active_audio_view_toggle, apply_font_size,
    // auto_clear_crossed_trim); the friendship lets the editor reach them
    // through its back-pointer without a parallel writer.
    friend struct GuiSettingsEditor;
    // The propagate's paste tail lands in target view through the same
    // handle_active_audio_view_toggle chokepoint; the friendship lets it
    // reach that private method through its back-pointer.
    friend struct PhaseResetPropagate;

    void on_key(GuiKey key, GuiInputState mods);
    void on_button_press(GuiMouseButton button, int x, int y, GuiInputState mods);
    void on_button_release(GuiMouseButton button, int x, int y,
                           GuiInputState mods);
    // Coalesced scroll-wheel entry. `count` is the net detent count for one
    // pointer frame (>= 1); the platform's set_on_wheel routes here.
    void on_wheel(GuiMouseButton dir, int count, int x, int y,
                  GuiInputState mods);

    // The SINGLE wheel routing predicate. Returns -1 when the wheel is
    // swallowed at (x, y), else a region code: 1 inside the waveform area,
    // 2 inside the top strip, 0 outside both. on_wheel's
    // completed-detent gate and the platform's per-frame sub-detent accumulator
    // probe both consult it so the two surfaces can never drift.
    int wheel_context(int x, int y) const;
    void on_motion(int mouse_x, int mouse_y, GuiInputState mods);

    // END every in-flight pointer gesture through its own RELEASE body — a
    // commit, never a cancel: pointer gestures have no cancel (the rule is stated
    // at the drag-modal gate in on_key). The marker drag commits its proposed
    // position with its undo entry, the trim drag keeps its live bounds and runs its
    // commit tail, the region drag rests its region, the strip / grab-pan drags
    // just end (they applied continuously), and the TWO PENDINGS disarm having
    // committed nothing. (The tempo drag was a fifth body here until 2026-07-29 —
    // the whole gesture is deleted, see marker_drag.h.)
    // No-op when nothing is live. Definition beside
    // on_button_release in input_pointer.cpp (same bodies, same order). Callers:
    // the Ctrl+Q hatch in on_key, and main.cpp's WM-close and resize callbacks
    // (close ends the gestures before raising the prompt, so none is left live
    // under it; resize ends them before the geometry rebuild, whose new
    // samples-per-pixel would otherwise make the next motion derive its delta
    // across two coordinate systems).
    void finalize_active_drags();

    // Arm the plain left-drag region-select gesture at a press. `anchor_frame`
    // is the active-domain frame the press just placed the playhead at; (x, y)
    // is the press position for the press-becomes-drag threshold. Dissolves the
    // resting region at mouse-down. Only the plain UPPER-HALF waveform press
    // calls this (the
    // lower half is the scrub surface, whose press is a one-shot scrub act
    // arming nothing and leaving the
    // region alone); the Shift-exact former arms through
    // arm_region_drag_preserving instead.
    void arm_region_drag_at(int64_t anchor_frame, int x, int y);

    // The SHIFT-exact former's arm (labwc 2026-07-24 second pass): same drag
    // state as arm_region_drag_at, anchored at the FAR endpoint, but it does NOT
    // dissolve app.region — the former has already left it exactly as it should
    // rest for a motionless release, so preserving it keeps that one-shot
    // behavior bit-for-bit. Esc mid-drag does nothing for either arm (no cancel).
    void arm_region_drag_preserving(int64_t anchor_frame, int x, int y);

    // The waveform-upper-half placement press BODY, shared by the plain waveform
    // press and the empty flag/triangle-lane parity press (R6, architect
    // 2026-07-23): clear the marker selection, drop the playhead at the clicked
    // column, reseek a live scanner to it (keeping the session alive, follow
    // overridden), and arm the region drag (which dissolves any resting highlight
    // at mouse-down). `click_rel_x` is x - waveform_area.x; the gutter
    // (click_rel_x outside [0, area.w)) still deselects but seats no playhead and
    // arms no drag. `was_playing` / `playhead_at_entry` are the snapshot the
    // caller captures AT PRESS ENTRY, ahead of every branch. Neither is a
    // pre-stop reading any more: the playback stops are claim-keyed and sit at
    // the branches that claim a gesture, and both presses reaching this body are
    // stop-free ones, so no stop stands between the capture and either reader.
    // What each parameter really predates is a write of its own —
    // playhead_at_entry predates move_playhead_to's cursor write, and
    // was_playing predates the stop that reseek_keeping_alive may run internally
    // on an out-of-range position — and together they fire the reseek only on a
    // real move of a live session.
    void place_playhead_and_arm_region(int click_rel_x, int x, int y,
                                       bool was_playing,
                                       int64_t playhead_at_entry);

    // The empty flag/triangle-lane double-click marker CREATE (R6): the bare-`s`
    // drop equivalent at the clicked column — the AUGMENTED drop in both columns,
    // exactly as bare `s` performs it — home-view and read-only gated silently.
    // Places the playhead on the clicked column first, then drops through the
    // standing _at_playhead paths so the create takes the full path (walls, undo,
    // selection, the lead-in offset) unchanged.
    void create_marker_at_empty_lane(int click_rel_x);

    // Pump half of the kill-and-park dispatch rule, reached through
    // GuiTargetRender's dispatch_pending_archival hook on every
    // worker-completion path. Dispatches the parked archival command
    // (app.pending_archival) when one is armed and the worker is idle: the
    // Ctrl+Alt+R shape through dispatch_single_archival_render, a batch
    // through start_render_batch. Returns true iff a session was started, so
    // the caller leaves its own pending preview queued behind it.
    bool dispatch_pending_archival_command();

    // The Esc-cancel body itself, key-free: cancel the running archival
    // session (worker cancel flag + batch finalize sentinel) and disarm the
    // parked archival command. Called by handle_escape_cancels. Returns true
    // when there was a session to cancel.
    bool cancel_archival_session();

    // True when ANY text editor is consuming printable keys — the settings
    // editor, the commit editor, or the top-strip flag editor in EITHER kind
    // (unlike modal_bottom_strip_editor_active, which names only the three
    // BOTTOM-STRIP surfaces and omits the FlagPayload editor). The platform's
    // press-time probe for kLeftClickKey: while an editor is open kLeftClickKey
    // types its normal letter instead of the button. Public because main.cpp's
    // probe lambda calls it. keyboard_modal_editor_active delegates to this —
    // see there for why the two concepts are necessarily the same set.
    bool any_text_editor_active() const;

    // Press-time key-repeat eligibility, the platform's repeat_eligible_probe_.
    // Repeat serves held-step gestures and editor typing; edge-triggered
    // commands (one-shot actions, toggles, editor openers) never repeat. Judged
    // under the PRESS-TIME context (the platform evaluates it before dispatch),
    // so a press that opens an editor is judged pre-open and does not arm, while
    // typing inside an already-open editor does. Public because main.cpp's
    // probe lambda calls it.
    bool repeat_eligible(GuiKey key, GuiInputState mods) const;

private:
    // ActiveBatch holds the batch render state machine (start_render_batch
    // and its lifecycle). Each entry is dispatched onto GuiAsyncRenderer and
    // the next entry fires from the worker-completion callback. The GUI
    // remains interactive between (and during) entries.
    struct ActiveBatch {
        std::vector<RenderRequest> reqs;
        std::string                label;
        int                        next_index = 0;
        int                        rendered   = 0;
        bool                       active     = false;
    };
    ActiveBatch batch_;

    // Result of one walk over the renders/ batch root: the highest
    // leading-index `<digits>_...` folder, and that folder's filename.
    struct RendersBatchScan {
        int         max_index = 0;             // 0 when none / dir missing
        std::string max_index_folder_name;     // filename of the max-index
                                               // folder; empty when none
    };

    // Scan `renders_dir` for the highest leading-index batch folder. The three
    // batch-dispatch sites share this one walk: the iteration and BPM sweeps
    // use `max_index + 1` for their next batch folder (a missing/empty dir
    // yields max_index 0, so the first folder is index 1 — the pre-factor
    // convention), and Ctrl+Alt+Shift+R additionally reads
    // `max_index_folder_name` to decide append-vs-new. A tie keeps the first
    // `<digits>_` folder at that index (strict `>` update), exact for the
    // always->=1 product folders.
    static RendersBatchScan max_renders_batch_index(
        const std::filesystem::path& renders_dir);

    // Start a multi-entry batch. Snapshots reqs + label, sets queue_running,
    // clears the cancel flag, dispatches the first entry. The on_done
    // callback advances the state machine. Empty reqs is a silent no-op.
    void start_render_batch(std::vector<RenderRequest> reqs,
                            std::string batch_label);

    // Worker-completion callback for batched entries. Increments counters,
    // observes cancellation, and either dispatches the next entry or
    // finalizes the batch (clear progress text, log summary).
    void on_batch_entry_complete(RenderOutcome outcome);

    // Dispatch reqs[batch_.next_index] (or finalize when out of range / on
    // cancel). Caller must have already mutated batch_ so next_index points
    // to the entry to run.
    void dispatch_next_batch_entry();

    // Finalize the current single-render-or-batch run on the GUI thread:
    // clear queue_running / queue_progress_text, invalidate the bottom
    // strip. The summary log is the caller's concern.
    void finalize_render_run();

    // Re-establish a cold/stale target buffer after a successful archival
    // completion. Shared by the single-archival success tail and the batch
    // terminal (see the definition for the full rationale). Internal gates
    // make the call safe in every worker/view state.
    void maybe_reestablish_target_buffer();

    // Dispatch a single archival render (the Ctrl+Alt+R shape) on an idle
    // worker: source-directory naming (empty batch_folder/basename inside
    // do_render), session bookkeeping, and an on_done that finalizes the
    // run and re-arms target view. Caller must have verified the worker is
    // idle.
    void dispatch_single_archival_render(RenderRequest req);

    // The busy half of the dispatch rule: kill the running render with the
    // Esc pair (request_cancel interrupts the current render mid-stream;
    // queue_cancel_requested stops a batch state machine from advancing
    // after the cancelled entry's on_done) and park the fully built
    // command in the one-slot app.pending_archival — a newer command
    // replaces an older parked one. Cancellation is cooperative: the
    // worker finishes acknowledging before going idle, so the slot waits
    // for the completion pump rather than dispatching here.
    void kill_running_render_and_park(AppState::PendingArchivalCommand cmd);

    // Authoring-state sidecar snapshot: active tab/audio view, active-tab
    // trim, and the live viewport/zoom/playhead fields managed by
    // switch_active_tab_view_to.
    AuthoringSnapshot snapshot_current_authoring_state() const;

    // Allocate the Ctrl+Alt+Shift+R miscellaneous output cell: derive renders/
    // from the (process-immutable) source path, decide append-into-the-most-
    // recent `_miscellaneous` folder vs. a new `<max+1>_miscellaneous`,
    // create the folder, and scan it for the next `<N>.wav` cell. Writes the
    // folder path into `out_folder` and `<max_cell+1>` into `out_basename`;
    // returns false after printing the one stderr line on directory-creation
    // failure. MUST only run when the render worker is idle: idle means
    // GuiAsyncRenderer::is_busy() is false, which spans the whole
    // CompletionPending interval, so the prior worker has finished do_render
    // and ALL publication into renders/ before the scan runs; every OTHER
    // renders/ mutation (sweep batch-folder creation, the adopt wipe) runs on
    // this same GUI event thread, so none can interleave with the scan
    // either; the only other writer thread, the master-floats cache writer,
    // writes solely under the cache tree, never renders/ — so an
    // idle-moment scan cannot race a publication. The
    // command-time scan it replaces could, because a cancelled render may
    // still publish into renders/ during its cancellation drain, after the
    // scan but before the cancel flag lands, stealing the scanned cell name.
    bool allocate_miscellaneous_cell(std::string& out_folder,
                                     std::string& out_basename);

    // Attach the process-wide render resources to an assembled request:
    // the single RenderCache (constructed in main, reached through
    // target_render's reference), the GUI's shared source buffer, and the
    // source's load identity. These are required request fields — do_render
    // dereferences the buffer and cache without fallbacks. Every archival
    // dispatch site must call this after build_render_request.
    void attach_shared_render_resources(RenderRequest& req);

    // Sweep every BPM in the BPM owner's [bpm_lo, bpm_hi] range,
    // computing (base_tempo, scale) per cell and rendering one .wav per
    // cell into `<source_parent>/renders/<N>_bpm/`. The
    // body is the former Ctrl+Alt+M block verbatim, minus the keystroke
    // gate; it is now fired by Enter in the bottom-strip BPM editor (after
    // a successful commit). Returns true if the batch was accepted —
    // dispatched, or (when the worker was busy) parked behind the killed
    // render's drain via kill_running_render_and_park; false on any guard
    // bail (wrong view / mode off / no owner / blank values /
    // zero-duration span / no valid cells / batch-folder creation failure)
    // — the Enter dispatch exits bpm mode on a bail, since the editor
    // already closed on commit and the mode is exactly its editor session.
    bool render_bpm_sweep();

    // F2.1: end an in-flight editor-text drag (motion-with-lost-button and
    // button release both route here). Collapses a never-moved anchor back
    // to a plain caret (no selection), repaints the active editor's strip,
    // and clears app.editor_text_drag. No-op on the strip repaint if no
    // editor is active (the editor closed out from under the drag); the
    // flag is cleared regardless.
    void finalize_editor_text_drag();

    // Clipboard: handle a Copy/Cut/Paste editor action against editor `s`
    // using the internal session clipboard (AppState::text_clipboard; there is
    // no Wayland clipboard), and report whether it handled one. Copy and cut
    // push the selection to the session clipboard (cut then deletes it); paste
    // pulls the clipboard text into the selection. Returns false for any other
    // action so the caller can fall through to its remaining branches.
    bool apply_editor_clipboard(text_editor::KeyAction action,
                                text_editor::State& s);

    // Shared wheel handler for source and target view; on_wheel is
    // its only caller. Exact-match modifiers: plain = zoom, Alt = pan (10% of
    // the visible span per detent) everywhere it is over the waveform or top
    // strip. Ctrl+Alt is no longer a wheel chord — it, like every other
    // combination (Shift+wheel, Ctrl+wheel, ...), no-ops. `inside_top` is true
    // anywhere over the top strip.
    void handle_wheel(GuiMouseButton button, int count, bool ctrl, bool shift,
                      bool alt, bool inside_waveform, bool inside_top);

    // Tab / Shift+Tab / IsoLeftTab dispatch: cycle marker focus, then stop
    // playback and move the playhead onto the newly focused marker. Always
    // recenters the viewport on it at the current zoom — follow mode does not
    // gate the cycle. Mode-aware: reads from phaseresetmarkers in 'P' mode,
    // warpmarkers otherwise.
    void cycle_marker_focus(bool forward);

    // Jump the playhead directly onto the currently focused marker
    // (app.last_selected_marker), stopping playback and recentering the
    // viewport on it at the current zoom. Returns true when a marker was
    // focused and the jump happened, false (leaving the playhead alone) when
    // there is none. This is the shared jump tail of cycle_marker_focus (the
    // Tab family) and the `c` gesture, both of which recenter the viewport; a
    // plain marker click is the other land-onto-marker route (its own direct
    // write in on_button_press — same two-step placement basis, but NO viewport
    // move). Both leave the playhead coincident with the focus, and a later
    // nudge/drag re-lands it on the focused marker as that marker moves.
    bool jump_playhead_to_focused_marker();

    // The bare `0` key zoom TOGGLE: at the working zoom → full zoom-out (the
    // per-file effective ceiling, whole song visible); anywhere else → the
    // working zoom (kWorkingZoomLevel), centered on the playhead via
    // apply_zoom_change.
    void run_zoom_toggle_command();

    // The zoom-strip DOUBLE-CLICK command: ZOOM TO A SPAN, never the working
    // zoom. Span priority — a live region (wins over trim) → a set trim
    // (completed to its extremes, expressed in the active domain) → the whole
    // song (full zoom-out). The region/trim span is framed with a 2.5%-per-side
    // margin; the fit level and span-start are set through the clamp chokepoints
    // via Viewport::apply_zoom_to_start (NOT apply_zoom_change — no playhead
    // recenter). Idempotent: a second click with the viewport unchanged no-ops.
    void run_zoom_double_click_command();

    // Esc-cancel handlers: while a render or queued batch is in flight, BARE Esc
    // cancels it. Returns true if it consumed the key (on_key then returns).
    // Routed after the editor modal (which cancels an active edit on Esc
    // first) and before the rest of the key handlers. Takes the modifiers
    // because a modified Escape must not cancel a running render, as no modified
    // chord may do anything anywhere.
    // This SURVIVES the 2026-07-29 Esc unbinding as its own binding class
    // (render-work cancel, not a ladder rung) — planner interpretation, architect
    // confirmation pending.
    bool handle_escape_cancels(GuiKey key, GuiInputState mods);

    // Render-trigger chords: Ctrl+Alt+R (single render), Ctrl+Alt+I
    // (render iteration sweep). Returns true if key+mods matched
    // one (on_key then returns), false otherwise.
    bool handle_render_dispatch_keys(GuiKey key, GuiInputState mods);

    // P / I / M / L letter-key handlers: Ctrl+P-family phase-reset clipboard
    // ops, `p` view toggle, `i` iteration mode, `m` bpm mode, `l` listen-to-
    // renders launcher. Returns true if key+mods matched one (on_key then
    // returns), false otherwise.
    bool handle_mode_keys(GuiKey key, GuiInputState mods);

    // Tab-key family: Ctrl+Tab / Ctrl+Shift+Tab switch A/B tabs; Tab /
    // Shift+Tab / IsoLeftTab cycle marker focus. Returns true if key+mods
    // matched one (on_key then returns), false otherwise.
    bool handle_tab_switch_keys(GuiKey key, GuiInputState mods);

    // Bare-key (no-modifier) dispatch: playhead move / zoom / follow / center /
    // Home-End / trim begin-end. Caller gates on no modifiers held. Its
    // Left/Right arms are the WAVEFORM-LANE half of the horizontal arrows: the
    // marker-lane half returns before reaching the tail, so these run only with
    // an empty selection (playhead_in_marker_lane false).
    void handle_plain_bare_keys(GuiKey key);

    // Shared key route for EVERY keyboard-modal editor — the settings prompt,
    // the render-commit prompt, the bpm bracket editor, and (architect
    // 2026-07-28) the top-strip flag editor. The modal contract is stated once
    // at the definition; returns true if the editor consumed the key (on_key
    // then returns), false on Ctrl+Q so on_key runs the close routing.
    // `autocomplete` is the ONLY OPTIONAL hook — the bare-Tab one, empty for the
    // bpm and flag editors, and bare Tab never arrives for them at all: the
    // on_key gate (modal_editor_key_blocked) swallows it before this route ever
    // sees it. Every OTHER hook is REQUIRED and called unmodified: commit /
    // cancel / Ctrl+Q teardown are the per-editor bodies, and `repaint` is the
    // editor's own damage for a text change — the three bottom-strip surfaces
    // pass the timestamp area, the flag editor the top strip. `repaint` is
    // invoked UNCONDITIONALLY on every consumed key, so an empty std::function
    // there would throw; the route carries no null check for it deliberately (a
    // caller that forgets it is a program bug, not a runtime condition to guard).
    bool route_modal_editor_key(text_editor::State& ed, GuiKey key,
                                GuiInputState mods,
                                const std::function<void()>& autocomplete,
                                const std::function<void()>& commit,
                                const std::function<void()>& cancel,
                                const std::function<void()>& ctrl_q_teardown,
                                const std::function<void()>& repaint);

    // Routes a key to the active top-flag editor. Returns true if the editor
    // consumed it (on_key then returns); false on Ctrl+Q so on_key runs the
    // close routing. BOTH kinds now take route_modal_editor_key: the bpm
    // bracket editor as ever, and the FlagPayload flag editor since it became
    // keyboard-modal — the two differ only in their commit/cancel bodies and in
    // which area they repaint. There is no longer a tail that cancels an edit to
    // let an unmatched key through: the gate means no unmatched key arrives.
    bool handle_top_flag_editor_key(GuiKey key, GuiInputState mods);

    // Routes a key to the active settings-prompt editor through
    // route_modal_editor_key, with bare Tab autocompleting the value side
    // of `key=` from the key's current stored value.
    bool handle_settings_editor_key(GuiKey key, GuiInputState mods);

    // Render-commit prompt (bare `'`). A bottom-strip modal editor, structural
    // sibling of the settings editor: it takes a render entry's identifier
    // relative to renders/ and, on Enter, adopts that render's frozen sidecar
    // recipe as the new authoring baseline through adopt_render_entry.
    //
    // open_commit_editor: bare `'` opener (no-op with no source loaded or a
    // running/parked render; refuses to open over an empty renders/).
    // commit_editor_autocomplete:
    // bare-Tab longest-common-prefix completion over the entry identifiers.
    // commit_editor_commit: resolve the pending to exactly one entry and adopt.
    // commit_editor_exit_no_commit: Esc / Ctrl+Q teardown. handle_commit_editor_key:
    // the key router, through route_modal_editor_key like the settings editor.
    void open_commit_editor();
    void commit_editor_autocomplete();
    void commit_editor_commit();
    void commit_editor_exit_no_commit();
    bool handle_commit_editor_key(GuiKey key, GuiInputState mods);

    // adopt_render_entry: apply render entry `e`'s frozen sidecar recipe
    // (.settings + the marker pair) as the new authoring baseline, view-
    // agnostic (source OR target authoring view). Reads and validates the wav's
    // existence and all three sidecars BEFORE mutating any store, and returns
    // false leaving authoring untouched (silent) on any missing/malformed
    // input; otherwise applies the recipe wholesale, wipes renders/, and
    // returns true.
    bool adopt_render_entry(const AppState::RenderEntry& e);

    // Bare x is SET-ONLY (architect 2026-07-25): it branches on the highlight
    // (no context-awareness beyond that) — a live region trims to it, begin at
    // the span's lo, end at its hi, overwriting any existing bounds, then KEEPS
    // and re-syncs the highlight — the coupling tail
    // (deselect_and_sync_trim_window_highlight) re-derives the REGION (provenance
    // TrimWindow) from the just-set window, so region and trim rest coupled (x is a
    // TrimWindow SETTER, so it DESELECTS as it publishes — the rule at
    // sync_region_to_trim_window's declaration). TWO SILENT REFUSALS, both writing
    // nothing at all: NO region, and a DEGENERATE result — the inverse-mapped,
    // wall-clamped pair coming out end <= begin, which auto_clear_crossed_trim would
    // read as crossed and destroy (planner-decided 2026-07-29 pending architect
    // confirmation; the derivation is at the definition). x never UNSETS either way
    // (that arm moved to Shift+X).
    // No read-only check: the keyboard gate — the ONE read-only guard
    // on that path — leaves x off its allowlist, and this pair is keyboard-only,
    // so a locked tab never reaches it. The sole dispatch entry for the bare-x key.
    void handle_trim_x();

    // Shift+X UNSETS the trim (architect 2026-07-25 — split off x, which is now
    // set-only). Read-only is the keyboard gate's (see handle_trim_x above), then
    // delegates to handle_trim_clear_both and, when the region it just tore down
    // was the trim's own TrimWindow highlight, re-syncs so that highlight cannot
    // outlive its window (a Free/SelectionExtent region is NOT trim's to clear).
    // A trim CLEARER, not a setter: it keeps the BARE sync and does NOT deselect.
    void handle_trim_shift_x();

    // Clear both trim bounds unconditionally. Silent no-op when neither bound
    // is set. The caller is handle_trim_shift_x (the Shift+X unset arm).
    void handle_trim_clear_both();

    // Field-reset core shared by handle_trim_clear_both (the Shift+X unset arm)
    // and the crossed-commit auto-clear below: unset both bounds, zero both
    // frames. No invalidation and no trigger — callers own their repaint tail.
    // One implementation so the two clears can never drift.
    void clear_trim_bounds();

    // Crossed/equal trim cannot REST (ruling comment at the definition):
    // when both bounds are set and end_frame <= begin_frame — exact integer
    // compare — at a trim COMMIT, destroy both bounds, silently. Called by
    // every trim commit site after its mutation and before its
    // invalidations, so the repaint shows the cleared state.
    void auto_clear_crossed_trim();

    // Plain chip-row press trim routing — the sole pointer route into a trim
    // drag (the Alt pointer gesture retired wholesale, and the waveform stem
    // grab with it; bounds are grabbed by their top-strip chips / the inter-chip
    // bridge only). Arms a PendingTrimDrag (the pending+threshold pattern): the
    // press CLAIMS the chip/bridge geometry, but the trim-drag machinery begins
    // only once the pointer crosses kDragMovedThresholdPx. Every trim drag needs
    // the FULL pair set; a lone bound is gesture-inert (transparent to the
    // press). Returns true iff both bounds are set AND the press landed on trim
    // chip geometry (a chip-rect single hit, or the chip-row inter-chip bridge
    // span) — armed or read-only-refused — so the caller claims with no
    // fallback; false lets the caller fall through to the marker flag handling.
    // Read-only claims without arming. Trim drags publish the region highlight to
    // the moving window through the SETTER's route
    // (deselect_and_sync_trim_window_highlight), so they DESELECT at their first
    // moved bound; the PLAYHEAD is what they never touch.
    // Dual-axis strip drag, INCREMENTAL: applies one motion event at (x, y).
    // Reads the LIVE zoom level and viewport (never a stored press baseline),
    // pans by the dx since the last event at the old level, zooms by the dy off
    // the live level (clamped into the numeric band and the shorter-file max),
    // and pivots the zoom around the song anchor — re-deriving the anchor's
    // drifted column each event and rebinding it to the nearest visible pixel
    // when a pan carries it offscreen (the edge trick). `final_event` is true on
    // the terminating event (release / button-lost) for the one synchronous
    // rebuild plus predictor resync; motion events pass false and repaint
    // synchronously too — one full rebuild per pointer frame whether the level
    // changed or only the viewport moved.
    void apply_strip_drag_at(int x, int y, bool final_event);

    bool route_trim_chip_press(int mouse_x, int mouse_y);
    // Arm the pending trim chip/bridge drag (pending+threshold): the begin runs
    // only once on_motion crosses kDragMovedThresholdPx from the press.
    void arm_pending_trim_drag(bool is_begin, bool both, int press_x,
                               int press_y);
    void begin_trim_drag(TrimHit which, int mouse_x, bool both = false);
    void update_trim_drag(int mouse_x);   // motion: writes the live store
    // mouse_x → source-domain frame double, the single conversion both the
    // drag anchor (begin) and the live cursor (update) read so they can never
    // diverge. Returns false (out untouched) when audio/zoom state is unusable.
    bool trim_mouse_x_to_source_frame(int mouse_x, double& out_frame);
    // mouse_x → active-domain frame: the cursor-column half of
    // trim_mouse_x_to_source_frame, shared with the move-both-bounds drag
    // so the gap is preserved in the domain the user actually sees (active),
    // not source. Returns false (out untouched) when audio/zoom state is
    // unusable.
    bool trim_mouse_x_to_active_frame(int mouse_x, int64_t& out_frame);
    void commit_trim_drag();               // release: trigger render if moved

    // The lane-click model's trim<->REGION coupling (architect 2026-07-23,
    // region-only under SELECTION FLOWS DOWNWARD ONLY): sync the region highlight
    // to the CURRENT trim window. Trim is region-related, so it touches the REGION
    // and nothing else — not the selection (the never-span-less collapse its clear
    // arm once carried is deleted, ruling 6) and not the PLAYHEAD, ever. Both
    // bounds set -> region = the window's active-domain extent (source bounds
    // through source_frame_to_active_domain, clamped playable), coincident images
    // included; lone / no trim -> clear the region. Navigation-class (the region is
    // navigation), so read-only-safe. Owns its own waveform-highlight damage,
    // raised only when the region's visible identity actually changed (a
    // per-motion-event path pays narrow damage, not full).
    // THIS BARE FORM IS THE NON-SETTERS' (Shift+X and the settings editor's two
    // trim maintainers): it leaves the selection alone, and all three gate their
    // call on a resting TrimWindow region, which by that provenance's own rule
    // means the selection is already empty. Every route that SETS the window calls
    // the setter form below instead.
    void sync_highlight_to_trim_window();

    // The SETTER's publish: deselect, then sync. EVERY TrimWindow setter goes
    // through here — the plain chip-row click and its deferred completions, both
    // bound-set clicks, bare `x`, and the trim drags' live sync — because a
    // TrimWindow region rests only beside an EMPTY selection (architect
    // 2026-07-29). The rule, the full setter list, the refusal-gating requirement
    // on each caller's placement, and the non-setter routes are stated once at
    // sync_region_to_trim_window's declaration above.
    void deselect_and_sync_trim_window_highlight();

    // R4.6: set ONE trim bound (begin or end) at the clicked column — the
    // trim-drag release-snap basis (authored_frame_at_column over the displayed
    // paint map), walls [0, total-1], then the auto_clear_crossed_trim commit
    // tail (a bound onto/across its partner dissolves both). History-less like
    // every trim mutation; repaint + target_render.trigger() like the drag
    // release. Read-only refuses silently (trim authoring), as does a missing
    // pair — ADJUST-ONLY (architect 2026-07-23): the clicks adjust an existing
    // window, never create one (region→x creates; the settings editor is the
    // deliberate lone-bound route). Runs the coupling
    // sync afterward. OWNS the press's playback stop, placed past those
    // refusals and just ahead of the bound write, so the ctrl / ctrl+shift
    // chip-row press carries none of its own and a refused click leaves a live
    // audition playing. is_begin picks the bound: the ctrl chip-row click sets
    // begin, ctrl+shift sets end (R5).
    void set_trim_bound_at_click(bool is_begin, int mouse_x);

    // The ctrl / ctrl+shift chip-row bound-set press (round 3, architect
    // 2026-07-23): sets the bound at the clicked column (set_trim_bound_at_click,
    // adjust-only + sync, above) AND, when the click-set kept a full writable
    // pair, arms the single-bound trim drag on the just-set bound — so motion
    // past the threshold drags it live exactly like a plain chip drag, while a
    // motionless release rests the click-set as before. NOTHING is stashed: the
    // click-set is committed when made (trim is history-less) and pointer gestures
    // have no cancel. Read-only / a missing pair / a crossed
    // dissolve all leave nothing armed (the set itself already refused or
    // dissolved). is_begin picks the bound (ctrl=begin, ctrl+shift=end).
    void set_trim_bound_at_click_then_arm_drag(bool is_begin, int mouse_x,
                                               int mouse_y);

    // One scrub ACT at an active-domain frame: STOP, THEN START ON THE NEXT
    // CLICK (architect 2026-07-27, superseding the 2026-07-23 kill-and-revive).
    // A click while audio PLAYS is a pure stop — the frame is ignored and
    // nothing relaunches; a click on a stopped session runs the launch path
    // (the target-view is_updating gate + scrub_launch_at) at the given frame,
    // capturing the loop verdict and end bound freshly there. Sole caller:
    // the one-shot scrub press body (scrub_press_at).
    void scrub_act_at(int64_t frame);

    // The scanner scrub press body. SOLE CALLER: the waveform lower-half plain
    // press — the marker-text lane's empty-spot scrub is DELETED (architect
    // 2026-07-27), so the lower half is the entire scrub surface. Given
    // the click's waveform-relative column, run ONE scrub act (scrub_act_at —
    // stop a live session, else launch) at that column's frame — the scrub is
    // ONE-SHOT per click (architect 2026-07-23, the Ableton model): the
    // press arms NOTHING, a held press does nothing further, and motion over
    // the scrub surface is inert (the scrub drag is removed, so each click
    // pays AT MOST one stop quiescence fence — a stopped session's launch pays
    // none). A gutter/invalid column
    // (outside [0, area.w)) is a silent no-op (no launch position). Touches
    // NOTHING else — no selection, region, cursor, follow, or double-click seed.
    // The caller keeps playback alive across the press (no waveform press stops
    // playback, and the top-strip stops belong to the top-strip claims), so
    // the act sees the LIVE session — load-bearing for the
    // stop-then-start ruling: a press that let the session die first would turn
    // the interrupting click into a launch.
    void scrub_press_at(int click_rel_x);

    // Bare `t` toggle: flip app.active_audio_view between Source and Target.
    // Stops any current playback before switching domains. Source → Target
    // translates app.viewport_start_sample / playhead_cursor_sample /
    // zoom_level through the current warp_frame_map in place and enters target
    // view only when target view is available. target-view playback is
    // allowed once the target buffer is ready; target render
    // update-in-progress gates playback elsewhere.
    void handle_active_audio_view_toggle();

    // Apply a new GUI font size (points), running the shared live sequence:
    // assign app.font_size, push it to the renderer (set_gui_font_size_pt),
    // full-window invalidate, then the resize-path geometry-and-cache rebuild.
    // The settings editor's `font_size=` commit is the sole caller and gates
    // the no-op case before calling (its same-value gate), so this assumes a
    // real change (the file-load path pushes font_size straight through
    // set_gui_font_size_pt, not through here).
    void apply_font_size(double pt);

    // THE LANE MODEL (architect 2026-07-28): true when the playhead currently
    // lives in the MARKER lane. The bare horizontal arrows are always a PLAYHEAD
    // STEP — one painted column per press; the lane decides what else rides
    // along. A selection IS the marker lane: the cursor playhead stops painting
    // and the focused flag's kWaveform-filled triangle IS the playhead (the
    // focus model), so the step moves that, carrying the marker under it (the
    // two position nudges, each re-landing the playhead on
    // its committed focus; the tempo-image step was a third marker-lane route
    // until 2026-07-29 and W+target is now a consumed refusal — see
    // marker_drag.h). With no selection the playhead is in the WAVEFORM
    // lane and the step moves the cursor alone. LANE EXIT IS ANY DESELECTING
    // ROUTE (architect 2026-07-29, replacing the explicit Esc collapse — Esc is
    // unbound here now): Home/End, a waveform click, a trim setter's publish, an
    // undo restore that empties the selection, and so on. There is still no
    // gesture fallback, so a marker-lane step that refuses stays a consumed no-op.
    // Distinct from the AUDITION SCRUB, which is untouched by all of this: that
    // is the waveform lower-half one-shot press (scrub_act_at / scrub_press_at),
    // a pointer gesture on its own surface that starts or stops a scanner and
    // never moves the resting cursor. "Scrub" names that and only that.
    // TWO READERS, one owner: the on_key dispatch (which picks the lane) and
    // read_only_key_blocked's is_playhead_step entry (which admits the bare
    // horizontal arrows only while this is FALSE — in the marker lane they
    // author, and this gate is their sole read-only defense).
    bool playhead_in_marker_lane() const;

    // Source-view read-only allowlist. Returns true if key+mods is NOT on the
    // allowlist of navigation / playback / zoom / view-switch / close-prompt
    // keys honored in a read-only source tab — i.e. should be dropped.
    // READ-ONLY BLOCKS PERSISTENT MUTATION — what can reach DISK or a RENDER —
    // not every store write; the definition carries that standard and the one
    // admitted key whose route writes a store under it (bare `t`).
    // Authoring-mutation chords (trim gestures, Delete, undo/redo, the
    // propagate commands) are blocked here at the gate, and Ctrl+S is not
    // admitted either — read-only means no save from a locked tab. One entry is
    // STATE-DEPENDENT: the bare horizontal arrows are admitted as navigation
    // only while playhead_in_marker_lane is false, since in the marker lane the
    // same press authors.
    bool read_only_key_blocked(GuiKey key, GuiInputState mods);

    // KEYBOARD MODALITY (architect 2026-07-28): true when an open editor owns
    // the keyboard, so every chord outside the admitted set is a silent no-op.
    // EVERY editor does — the two bottom-strip ones, the bpm bracket, and the
    // top-strip FlagPayload flag editor, which this ruling brought in, reversing
    // the old "commands punch through" design and deleting the tail that
    // discarded an edit on the way to a command.
    // ONE READER: the on_key gate (input_handler.cpp), paired with
    // modal_editor_key_blocked.
    // Modality here is CHORDS only, which is why the flag editor's OTHER
    // transparencies do not consult this predicate — see
    // modal_bottom_strip_editor_active below for what does and does not.
    bool keyboard_modal_editor_active() const;

    // Modal-editor predicate + key gate (bodies in input_key_dispatch.cpp).
    // THIS DECLARATION IS THE AUTHORITATIVE STATEMENT of what
    // modal_bottom_strip_editor_active is for; other sites carry a pointer here.
    // It names the BOTTOM-STRIP modal surfaces only — the settings editor, the
    // render-commit editor, and the bpm bracket editor (plus the prompts, gated
    // separately) — and it has exactly ONE CALLER: wheel_context's swallow
    // (input_handler.cpp), because wheel zoom and Alt+wheel pan are NAVIGATION,
    // not chords, so they still punch through an open top-strip flag editor.
    // IT IS NOT A PLAYBACK-STOP PREDICATE and never was one in code. The stop is
    // not decided here — but it is no longer scattered either: since 2026-07-28
    // it has ONE owner, GuiPlaybackLifecycle::stop_playback_for_modal_open, which
    // every open site calls and which records the whole decision table (the three
    // bottom-strip editors and the prompts stop; the top-strip flag editor is
    // explicitly EXEMPT and keeps a live audition playing). So a new modal
    // surface inherits the wheel swallow from this predicate and its playback
    // answer from that owner — it grows neither by hand.
    // The gate is the sibling of read_only_key_blocked's allowlist shape: true
    // when key+mods should be dropped while a keyboard-modal editor is open
    // (admits only the keys the active editor consumes, bare Esc, Ctrl+S, and
    // Ctrl+Q). It serves all four editors, top strip included.
    bool modal_bottom_strip_editor_active() const;
    bool modal_editor_key_blocked(GuiKey key, GuiInputState mods);
};
