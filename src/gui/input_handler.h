#pragma once

#include "app_state.h"
#include "async_renderer.h"
#include "audio.h"
#include "flag_editor.h"
#include "history_commit_worker.h"
#include "history_prefetch.h"
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

#include <atomic>
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
// cannot rest (the commit and load crossed-resets), and an ambiguous trim at
// render time falls back to the full, untrimmed deliverable — so after the
// normalizing resolver, entry gates only on the tripwire-class build
// failures. On success the built map is returned so the toggle can reuse
// it for its viewport/playhead translation (no second build); on failure
// the first owner's error string is returned verbatim.
std::expected<std::vector<WarpFrameMapSegment>, std::string>
validate_target_view_entry(const std::vector<GuiWarpMarker>& markers,
                           double scale, int sample_rate, long total_frames);

// Dissolve a resting region-select highlight, damaging the waveform so the
// recolored ground repaints away. A no-op when no region is active.
//
// WHAT THE REGION IS, so the clear list reads as one rule: TRIM SCRATCH
// (architect 2026-07-30 — the SPAN FORM retired), and a READING MARK in the `h`
// view, which has no trim to aim and no audition to preview. It is formed by ONE
// gesture wearing three entries — the waveform PLACEMENT PRESS and the drag it
// arms: plain in the upper half, SHIFT-exact at either height (2026-08-05, when
// the former's anchor moved to the clicked column), and the `h` view's own
// full-height press. The two live entries DESELECT at press; the view's does not
// need to, its spans being VIEW-LOCAL. It is previewed by the SCRUB press
// (either entry — the lower-half left one or the bare right one, neither of
// which exists inside the view) and CONSUMED by `x` (a live-view act; `x` is
// consumed in the view). It is not a playhead form, not a selection visual, not
// a trim-window display — the cursor playhead paints straight across it and the
// singleton stem is never suppressed. So a span rests beside an EMPTY selection
// wherever the editor can see one (the inventory and the view-local rule are at
// RegionState, app_state.h).
//
// EVERYTHING THAT MOVES THE PLAYHEAD OR REPLACES THE SELECTION TAKES THE SPAN
// WITH IT, unconditionally — never gated on whether the playhead actually moved.
// The scratch belongs to the gesture that drew it; a command that means something
// else invalidates it. CALL SITES, BY CLASS (re-derived by grep 2026-08-05; other
// sites state their own class and point here):
//   * NAVIGATION jumps: jump_playhead_to_focused_marker (the whole Tab
//     family plus `c`, through its own clear tail), `c`'s OWN up-front clear
//     before that jump (run_center_command, input_handler.cpp — the no-focus arm
//     never reaches the jump's tail, so `c` owns one of its own; both of that
//     command's recipes clear, and since 2026-08-05 bare `0` AT FULL ZOOM OUT
//     reaches this clear by running the command), the
//     bare Left/Right waveform-lane playhead step, Home/End (the trim-bound
//     jumps), and the
//     settings editor's `playhead_cursor=` GUI key (settings_editor.cpp — the
//     typed navigation-jump twin, no exemptions);
//   * EVERY PLACEMENT PRESS (arm_region_drag_at, input_pointer.cpp — the plain
//     upper half, the shift-exact press at either height, the empty
//     flag/triangle-lane parity press, and the `h`
//     view's own full-height press; membership re-derived 2026-08-06, the
//     authoritative inventory being RegionState's in app_state.h): dissolves any
//     resting highlight at
//     mouse-down, before the gesture is known to be a click or a fresh region
//     drag — all four share this exact dissolve shape;
//   * MARKER CLICKS, all three (the plain single-select and both multi-select
//     clicks), UNCONDITIONALLY — the result-size split the multi-select pair
//     carried died with the extent owner, so every marker click clears;
//   * enter_text_edit, the one chokepoint of every flag/bpm editor open and
//     retarget;
//   * the POSITION NUDGES, both columns (finish_position_nudge);
//   * the MARKER DROPS: one clear per column, at the drop chokepoints
//     drop_marker (warp) and drop_phase_reset_at_position (phase reset), which
//     both entry routes (bare `s`, the empty-lane double-click) converge on,
//     placed past every refusal;
//   * the THREE MEMBERSHIP-WHOLESALE routes: the UNDO/REDO restore's visual tail,
//     the `p` W/P swap (toggle_active_markers_view) and the propagate paste's
//     target-view tail. The undo tail's clear runs for
//     EVERY entry, settings-only ones included — it is the ONE part of the tail
//     that sits above the 'S' gate (which forbids SELECTING and LANDING) —
//     because a settings restore rebuilds the map under the span exactly as a
//     marker restore does;
//   * the SETTINGS ENGINE-COMMIT chokepoint: the one committed
//     tail every canonical engine key shares (GuiSettingsEditor::commit, past
//     the unknown-key / invalid-value / collision / unchanged returns), because
//     the scale among those keys is a warp-map input and the commit rebuilds the
//     map under any resting highlight. That tail CLEARS THE SELECTION beside the
//     region. The settings editor's TRIM keys never
//     reach it — they return through commit_gui_setting, whose active-tab arms
//     clear through the TRIM class below instead;
//   * EVERY TRIM WRITE, through the ONE park owner at the shared commit tail
//     (park_playhead_at_trim_start, input_trim.cpp — architect 2026-08-05):
//     each of these parks the playhead at the new trim start and is therefore a
//     playhead-moving command like the rest of this list. The membership is
//     stated once, at the head of input_trim.cpp, and is `x`, the ctrl /
//     ctrl+shift bound-set clicks, the endcap/bridge drag AT ITS RELEASE, the
//     settings editor's active-tab `:trim_*=` commits, the crossed/coincident
//     resets, and `Shift+X` inside its identity guard. `x` is the special one:
//     for it the clear is also the CONSUMPTION of the span it just read, the
//     trim it set being what the span was for;
//   * THE `h` HISTORY MODE's OWN CURSOR-MOVING ROUTES (2026-08-05, added when
//     the re-grep for this retell found the class absent; membership re-derived
//     2026-08-06): the three keyboard arms — the diff-flag Tab cycle, the
//     absolute Home/End and `c` — and, SINCE 2026-08-06, THE MODE'S FLAG CLICKS,
//     BOTH BODIES: the plain focus click over either surface, the flag box or
//     the stem (focus_history_diff_flag), and the lane's shift/ctrl selection
//     pair (select_history_diff_flags_modified). All of them are the live arms'
//     region regime read against the mode's data, and the flag clicks are the
//     live MARKER CLICKS' class above, at the same UNCONDITIONAL strength — the
//     empty-lane click, which lands nothing, clears too. Their day-old exemption
//     was a fossil of the view having had no region to clear; the placement
//     press gave it one, and the architect's live pass caught the gap. The
//     MODE'S PLACEMENT PRESS clears through arm_region_drag_at with the live
//     presses instead, that press being the live one whole since playback left
//     the view;
//   * THE `h` HISTORY MODE'S OWN EDGES (architect 2026-08-05, THE VIEW-LOCAL
//     REGION RULE): the mode's EXIT (close_history_mode, the one exit owner) and
//     each `,` / `.` STEP and each WALK-OR-READING SWITCH (the switching-
//     commits family, input_key_dispatch.cpp — set_history_reading is the one
//     switch owner behind the tabs, Ctrl+Tab and bare `u` alike). A span drawn in the view is a reading mark on the
//     checkpoint it was drawn against, so it does not outlive that checkpoint —
//     and cannot rest in the editor at all, which is what keeps "a region rests
//     only beside an EMPTY selection" true out there while the view's own press
//     deselects nothing. The ENTRY deliberately clears nothing: a span drawn
//     before `h` is the user's, and it rests beside the empty selection its own
//     former left;
//   * BARE ESC, the one route that clears a span and NOTHING ELSE (architect
//     2026-07-30, live-test refinement): no playhead move, no selection change,
//     no trim write. It is ranked under the editors and prompts and over the
//     render cancel, and a DRAG IN FLIGHT never reaches it — the drag-modal gate
//     swallows the key first, so Esc clears a rested span but cancels no gesture.
//     The full Esc enumeration lives at its dispatch point in on_key
//     (input_handler.cpp).
// DELIBERATELY NOT CLEARED, the whole list (the scrub membership re-derived by
// grepping scrub_press_at, 2026-08-01): BOTH SCRUB ENTRIES — the waveform
// LOWER-HALF PLAIN LEFT press and the BARE RIGHT press anywhere in the waveform
// area, full height — which run one shared body and are together the region's
// PREVIEW gesture (click inside a span to audition it, the span resting
// untouched), SPACE (which touches no region at all and always toggles
// from the playhead), and PURE VIEWPORT MOVES (PageUp/PageDown, zoom steps,
// pans, and bare `0`'s ZOOM-OUT ARM — its other arm, the one taken with the zoom
// already at full out, IS the `c` command and clears in the list above; the two
// arms sit on opposite sides of this line and the command they share is why).
// The remaining pre-existing clear sites (file
// load, Ctrl+Tab, and the S/T switch) keep their own in-place clears, pairing the
// reset with a domain flip or a full-window repaint rather than this exact damage
// shape; so does the kick validator's live-domain reclamp.
void clear_region_highlight(AppState& app, Viewport& viewport);

// LAND the playhead exactly onto marker `hit` of the ACTIVE column with NO
// viewport move (the two-step placement basis source_frame_to_active_domain then
// clamp_playhead_to_live_domain, a direct cursor write). A PURE PLAYHEAD WRITE:
// it touches no region and no selection — a caller that wants the scratch span
// cleared calls clear_region_highlight itself. Read-only allowed. Definition in
// input_pointer.cpp, whose comment is the AUTHORITATIVE statement of the
// marker-lane-owns-the-playhead rule and the one
// enumeration of the landing sites — do not restate either here.
void land_playhead_on_marker(AppState& app, const GuiAudio& audio,
                             Viewport& viewport, int hit);

// The same land with the store lookup taken off the front: place the playhead on
// an authored SOURCE frame directly, through the identical two-step basis and
// the identical damage. It exists for the ONE caller holding a frame that
// belongs to no store entry — the `h` history mode's focus click, whose removed
// diff flags name frames the session no longer has — and land_playhead_on_marker
// is its other caller, so the marker route and the frame route cannot drift.
// Same contract otherwise: pure playhead write, no region, no selection,
// read-only allowed.
void land_playhead_on_source_frame(AppState& app, const GuiAudio& audio,
                                   Viewport& viewport, int64_t src_frame);

// COINCIDENCE AUTO-SELECT — the entry counterpart of the never-park rule
// (architect 2026-07-29): the selection is never stashed, so an ENTRY re-acquires
// it from the playhead instead of from memory. Scans the ACTIVE column's store in
// order and SINGLE-SELECTS the first marker whose land value is EXACTLY the
// resting playhead. Definition in input_pointer.cpp, beside the land whose
// formula it reuses; that comment states the rule, the exactness, and the
// first-in-store tie-break. SIX CALL SITES (re-derived 2026-08-08 by grep), each
// stating only its own class and pointing there: the source load's tail
// (file_loader.cpp), the `p` column entry (toggle_active_markers_view) and the
// Ctrl+Tab tab entry (switch_active_tab_view_to), both in active_views.cpp, and
// ALL THREE LOAD-IN-PLACE BODIES' tails (input_key_dispatch.cpp) — the `'`
// render-entry load (load_render_entry_in_place, joined 2026-07-30), the `h`
// view's commit load (load_history_commit_in_place) and that view's LOCAL-tab
// load (load_history_local_entry_in_place, 2026-08-08), each because a
// load-in-place is specified 1:1 with a source load, and the
// load has always run this. No match leaves the selection exactly as the caller
// left it — every caller clears first, so that means empty.
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
// run_span_framing_command (all three arms) and the GROUP undo/redo
// restore's offscreen framing. Definition in input_handler.cpp.
void frame_span_into_view(AppState& app, const GuiAudio& audio,
                          Viewport& viewport, int64_t lo, int64_t hi,
                          bool margin);

// THE `h` HISTORY MODE'S TWO PURE KEY PREDICATES (bodies in
// input_key_dispatch.cpp, beside the mode's other keyboard work; the mode itself
// is stated at AppState::HistoryMode). They are free rather than members because
// each has a SECOND reader that holds no press and no handler: the redesign
// roster's mode-scoped disabled-face partition
// (history_mode_disables_button, app_state.h) asks both of them about a table of
// chords.
//   * history_mode_owns_key — the mode's own keys: bare `h` (the toggle), bare
//     `u` (the CUMULATIVE reading's toggle, 2026-08-08), bare `,` / `.` (the
//     walk), bare Tab / Shift+Tab / IsoLeftTab (the diff-flag cycle), Ctrl+Tab
//     and Ctrl+Shift+Tab (the row-3 tab cycle over the WALK SOURCES, forward
//     and reverse — the two ctrl-carrying shapes), bare Home / End and bare
//     `c`. The definition carries the derivation. handle_history_mode_key consumes exactly these,
//     one line ABOVE the allowlist, which is why a face derivation has to ask
//     this first.
//   * history_mode_key_blocked — the allowlist gate, read_only_key_blocked's
//     shape: true when the press is not admitted while the mode stands. Its
//     admitted membership is enumerated at the definition.
//
// THE SECOND TAKES THE WHOLE AppState, THE FIRST TAKES NOTHING BUT THE PRESS,
// and the asymmetry is the membership's own: the mode's keys are a fixed keymap,
// while THREE allowlist admissions are conditional on state they are asked about
// (re-derived 2026-08-07) — the commit act's, which is CTRL+S since 2026-08-08,
// on head_delta_empty (a view whose
// newest checkpoint already carries the session's authoring content has nothing
// to commit) and on history_checkpoint_in_flight (one checkpoint at a time), and
// the revert act's, on a subject standing
// (history_mode_revert_subject_standing — a selected diff flag, else the focused
// one). Both readers hand it the
// same `app` and neither restates a term of it, which is what
// keeps the key and the face one decision. It took the HistoryMode struct alone
// until the in-flight bit joined, that bit living on AppState because the act
// outlives the view it was launched from.
bool history_mode_owns_key(GuiKey key, GuiInputState mods);
bool history_mode_key_blocked(GuiKey key, GuiInputState mods,
                              const AppState& app);

// THE TRIM SETTER-DESELECT RULE, stated here where the retired trim-highlight
// sync used to declare it. THE SYNC ITSELF IS DELETED (architect 2026-07-30, Q3):
// the trim window no longer publishes itself as a region highlight at all — the
// region is TRIM SCRATCH, an input to `x` and nothing else (its contract is at
// clear_region_highlight above and at RegionState, app_state.h). What survives is
// the DESELECT half (architect 2026-07-29, "agree" 2026-07-30): EVERY TRIM SETTER
// CLEARS THE SELECTION as it commits — the trim-bar click is the sibling of the
// plain waveform click's deselect-all: clicking trim means ready to move on.
//
// THE SETTER CLASS IS DEFINED BY WHAT A ROUTE DOES, and since the publish died the
// definition TIGHTENS to the write alone: a route is a SETTER iff a USER COMMAND
// runs it and, past that route's own refusals, it WRITES A BOUND of the LIVE tab's
// trim window. ("Claiming the resting pair" left the definition with the highlight
// it was claiming for.) Membership RE-DERIVED 2026-08-01 by grepping every
// `selection.clear_selection()` call site against the live-tab trim-bound writers
// (app.trim.* / the settings arms' active branch) — SIX call sites, FIVE in
// input_trim.cpp and ONE in settings_editor.cpp:
//   * bare `x` (region -> trim, handle_trim_x), which deselects after its span
//     is read and then CONSUMES the span;
//   * the ctrl (BEGIN) and ctrl+shift (END) BOUND-SET clicks on the trim bar, ONE
//     function (set_trim_bound_at_click) and so one deselect — REINSTATED
//     2026-08-01 with the strictly-inside guard, which is simply a fourth refusal
//     ahead of the same deselect;
//   * the trim endcap/bridge DRAG — update_trim_drag's two motion arms and
//     commit_trim_drag — which also carries the drag's PLAYBACK STOP, relocated
//     there 2026-07-30 from the press when the press's highlight-only publish
//     retired (the keyboard stop rule is at stop_playback_if_playing's
//     declaration, playback_lifecycle.h);
//   * the settings editor's tab_X_trim_begin= / tab_X_trim_end= keys committed on
//     the ACTIVE tab (one value form now — a whole source frame; the `-1` unset
//     arm died with the unset state) — TWO KEYS THROUGH ONE SHARED ARM, hence one
//     call site, and the count above is call sites (a re-derivation correction:
//     the list has read "two in settings_editor.cpp" since 2026-07-30, counting
//     the keys). JOINED
//     2026-07-30, architect: "a typed commit is a commit", the sibling
//     playhead_cursor= key having already cleared selection and region under the
//     no-exemptions rule. Their INACTIVE-tab arm is not a member and never was:
//     it writes a parked band and changes nothing visible. (settings_editor.cpp's
//     other two clear_selection calls — the playhead_cursor= navigation jump and
//     the engine-key commit — write no trim bound and are not members.)
// Each deselects PAST ITS OWN REFUSALS (the refusal-gating rule these routes
// already hold their playback stop under):
// degenerate geometry, a bound-set click not strictly inside its partner, a drag
// event that moved no bound, and
// a settings commit rejected for an out-of-wall value / an
// unchanged value all write no bound and so deselect nothing. (The read-only
// arms of that list — the refused bound set and the refused settings commit —
// are gone with the 2026-08-07 reclassification of trim as band.)
// THE NON-SETTER IS EXACTLY ONE ROUTE: Shift+X, the dedicated trim MAXIMIZER,
// which widens the window to the whole song rather than claiming one, so there is
// no window for a deselect to hand the user (the architect's 2026-07-29
// exemption, kept verbatim through the 2026-07-30 re-pose). Everything else
// that touches a trim bound is not a command in this sense: auto_clear_crossed_trim
// is a shared commit tail every setter already runs inside its own body, and the
// ENTRY / RESTORE routes (file load, the Ctrl+Tab band pull, the settings-file
// tab-band pull, `'` load-in-place) install a trim wholesale.
// A PLAIN TRIM-BAR CLICK IS NO LONGER A TRIM ROUTE AT ALL (architect 2026-07-30):
// its three arms existed only to publish the highlight, so with the publisher gone
// they retire outright — a click that never becomes a drag is a consumed nothing,
// stopping no audition and destroying no selection.

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
    // The checkpoint act's background worker (2026-08-07). ONE user:
    // run_history_commit, which dispatches the captured job onto it; the
    // completion comes back through main.cpp's eventfd wiring into
    // on_history_checkpoint_complete.
    GuiHistoryCommitWorker&  history_commit_worker;
    // THE HISTORY WALK'S PREFETCH STORE (2026-08-07). The `h` visit BINDS to
    // it (GuiHistoryDiff::init) instead of running git itself, and this handler
    // owns the three kick sites' one funnel — kick_history_prefetch, which is
    // also what defers a kick that would land while the view stands.
    GuiHistoryPrefetch&      history_prefetch;
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
    // BEGIN CARRIES THE GESTURE'S OWN CURSOR KIND, which is the kind the capture
    // release hands back: Zoom for the strip drag (both entries arm inside the
    // Zoom zones), Pan for the alt-pan. A capture hides the cursor and makes the
    // GUI's pointer position virtual, so the platform cannot re-derive what to
    // restore and must not guess from what was showing at press time — the
    // reasoning, and why the stamp rides the lock-REQUEST path only, are at
    // GuiPlatform::begin_pointer_capture. It is the same shape as the live trim
    // cue: read the gesture's own record, never the pointer's position.
    std::function<void(GuiCursorKind)> begin_strip_pointer_capture =
        [](GuiCursorKind){};
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
                    GuiHistoryCommitWorker&  history_commit_worker_,
                    GuiHistoryPrefetch&      history_prefetch_,
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
          history_commit_worker(history_commit_worker_),
          history_prefetch(history_prefetch_),
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
    // (handle_active_audio_view_toggle, apply_gui_scale,
    // commit_trim_mutation); the friendship lets the editor reach them
    // through its back-pointer without a parallel writer.
    friend struct GuiSettingsEditor;
    // The propagate's paste tail lands in target view through the same
    // handle_active_audio_view_toggle chokepoint; the friendship lets it
    // reach that private method through its back-pointer.
    friend struct PhaseResetPropagate;
    // (NO GuiPrompt FRIENDSHIP. The prompt needed one while the history mode's
    // commit confirmation lived there — its `y` reached the private act through
    // a back-pointer — and both went with the prompt on 2026-08-07: the act is
    // now run by the commit-title editor's Enter, which is a private method of
    // this same struct.)

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

    // Re-derive and apply the pointer cursor at the REMEMBERED pointer position
    // with the modifier state handed in. The zone map it consults, and every
    // rule about WHAT the cue means, are at pointer_cursor_kind below.
    //
    // ONE CALLER, AND IT IS THE CURSOR'S SOLE OWNER: main.cpp wires it to the
    // platform's per-run-loop-iteration hook (set_loop_settled_hook), which fires
    // at the tail of the loop body — after the display dispatch, the tick and
    // both worker completions — so this runs against a fully settled state, once
    // per poll wakeup, and nothing else in the product pushes a cursor kind.
    // The HOOK has a second consumer beside this one (the open dropdown's item
    // faces, the same class of answer for the same reason); this function does
    // not, and its own job stays exactly the cursor.
    // WHY THAT IS THE SHAPE (architect 2026-08-03, replacing a per-site model
    // with twenty-three push sites): the map reads about ten independent fact
    // families, so a push was owed by EVERY writer of any of them — a set that
    // could not be enumerated and kept enumerated. Two review rounds each found
    // a class the previous derivation had missed, and whole classes had no event
    // to hang a call on at all (a wheel zoom moving the trim endcaps under a
    // resting pointer, the zoom and navigation keys, `x`/`Shift+X`, an undo
    // restoring trim, `o`, a gui_scale relayout, every keyboard editor open and
    // close, the dropdown item click). A LOOP BOUNDARY IS AFTER EVERY SETTLE BY
    // DEFINITION, so the "call this after your state has settled" rule that each
    // site owed is gone with the sites.
    // IT IS A NO-OP WHILE THE POINTER IS OUTSIDE THE WINDOW, where the
    // remembered coordinates mean nothing — app.pointer_in_window is written
    // true only in on_motion, which seeds those coordinates in the same breath,
    // so a session in which the pointer never entered is covered by the same
    // guard. It does nothing else at all: no hover recompute, no damage, no
    // gesture logic.
    // IT IS NOT ALWAYS THE LAST WORD, and that is deliberate rather than a hole:
    // the platform DROPS a kind named while it has no real pointer position (the
    // span a capture opens — GuiPlatform::set_cursor_kind), so a call made from
    // the remembered virtual coordinates cannot put up a confidently wrong cue.
    void refresh_pointer_cursor(GuiInputState mods);

    // THE REDESIGNED BUTTONS' HOVER FACES, in two entries over one transition
    // writer serving the WHOLE roster — row 1's Quit / Navigation / Settings and
    // the view bar's three, row 2's four, row 3's two tabs and row 4's sixteen
    // (definitions beside on_motion in
    // input_pointer.cpp).
    // recompute_
    // re-resolves the cursor's last position against the painter's stashed rects
    // and is called from on_motion's no-gesture tail and from the run loop's
    // TICK; it REFUSES OUTRIGHT while the pointer is outside the window (its own
    // first lines), which is what keeps the tick's call inert in both directions
    // out there. clear_ is the pointer-LEAVE
    // / capability-loss drop, wired in main.cpp on the pointer-leave hook,
    // because a face is an answer to "where is the pointer" and the pointer is
    // gone: capability loss ends that stream outright, and an ordinary leave has
    // no motion only WHILE the pointer stays outside — long enough for a lit
    // pill to sit there unowned until a re-entry's synthesized motion recomputes
    // it. THAT CALL IS CONDITIONAL (architect 2026-08-08): an ORDINARY leave
    // through ROW 1's band with the menu row's mode armed KEEPS the faces, so the
    // hovered row-1 button stays lit while the pointer rests on the titlebar —
    // the same leave that keeps the mode itself, argued at the hook, and scoped
    // there to the soft edge, capability loss clearing unconditionally. Both damage ONLY on a
    // real transition, and at most one invalidate_top_strip per call however many
    // faces moved.
    void recompute_redesign_button_hover();
    void clear_redesign_button_hover();

    // THE MENU ROW'S DROPDOWNS — two state writers and one hover, over the ONE
    // popup state both menus share (AppState::Dropdown). toggle_ is the whole
    // action of BOTH non-chord buttons, Settings and Navigation: it closes the
    // named menu if it is the open one and otherwise opens it, so pressing the
    // other button SWITCHES menus and "never two at once" is structural rather
    // than a rule. Its ONE refusal is the `h` history view's, and it is
    // MENU-SCOPED since 2026-08-08: Settings does not open in there, Navigation
    // does (the reasoning is at the definition). close_ is what every dismissal route calls — an outside
    // press, a wheel, bare Esc, Ctrl+Q, an item click, and any full relayout.
    // Both damage the top strip AND the popup's published rect, because the
    // popup hangs below the strip. toggle_ does NOT record the press claim that
    // the anchor-press gesture needs: two of its callers carry no press at all
    // (the menu-row hover open, the hover switch), so the claim is the press
    // site's, written from this toggle's outcome (AppState::Dropdown::
    // press_began_on_item). recompute_ resolves the item hover while it is open
    // AND, under a live press CLAIMED BY THE POPUP — one that went down on an
    // item, or on the anchor whose menu it opened — the ARMED item with it, one
    // walk, one hit, because a menu lights exactly one item and the press only
    // decides which face it wears (the rule, and why the arm cannot double as the
    // liveness test, are at the definition); a DISABLED row resolves to NO item
    // there, so neither face can ever name one. IT HAS TWO CALLERS AND BOTH ARE
    // LOAD-BEARING: on_motion's open-dropdown branch runs it per DELIVERED
    // MOTION, because a dispatch batch can carry a motion and then the PAINT that
    // reads these faces with no loop tail in between, and main.cpp's settled hook
    // runs it once per RUN-LOOP ITERATION, because the walk's inputs — the
    // painter-published item rects — can move with no pointer event under them.
    // WHAT IT WRITES SERVES THE FACES; the RELEASE derives its own item from the
    // release coordinates rather than depending on either caller having run last
    // (finish_dropdown_release). It refuses while the pointer is outside
    // the window (its own first lines), so the per-iteration caller cannot
    // re-light what the pointer-leave drop cleared.
    // They are also the mode's two writers: toggle_'s open
    // ARMS the menu row and close_ DISARMS it — unconditionally, ABOVE its own
    // "nothing is open" return, since the mode outlives the popup and a
    // dismissal must reach it in that state too (the one close that re-arms is
    // named at close_'s definition).
    void toggle_dropdown(DropdownMenu menu);
    void close_dropdown();
    void recompute_dropdown_hover(GuiInputState mods);

    // THE MENU ROW'S MODE — the three entries that maintain the armed bit outside
    // toggle_ (which sets it) and close_ (which clears it on every dismissal);
    // the contract, and the authoritative list of what ends the mode, are at the
    // field, AppState::Dropdown::menu_row_armed.
    // THE TWO MOTION HALVES ARE SPLIT BECAUSE THEIR GUARD LISTS DIFFER, and that
    // is the whole reason there are two functions rather than one:
    //   * open_menu_row_anchor_on_hover is the COLD ROW'S motion answer, called
    //     from on_motion's no-gesture tail and nowhere else: armed and over an
    //     anchor OPENS that menu through toggle_dropdown. It PRESUMES NO MENU IS
    //     OPEN and no modal or gesture owns the pointer, which that placement
    //     guarantees — the open-dropdown branch returns far above the tail, and
    //     so do the prompt, the bottom-strip editors and every live gesture,
    //     which is exactly the reachability the anchors' own PRESS claim has;
    //   * update_menu_row_exit is "the pointer left row 1, go cold", called from
    //     the TOP of on_motion so that it runs under every one of those branches
    //     too. A modal owning the pointer is a reason not to open a menu, and no
    //     reason to forget that the pointer left the row.
    // disarm_ is the mode's end, called from both of those, from the
    // pointer-leave hook (main.cpp, beside the row's other face clears — a
    // pointer that has left the window has left the VISIT, which is the same
    // reason the band exit disarms, at a coarser edge; it is not a claim that no
    // motion can follow, since a re-entry synthesizes one — and that call is
    // skipped when an ORDINARY leave went out through ROW 1's own band, a step
    // onto the titlebar the mode survives; a capability loss makes it always),
    // and from the top of
    // on_button_press and
    // on_key (any press, any chord). It carries the "no menu open" gate, because
    // leaving the WINDOW is not a dismissal, a menu left standing is still the
    // mode, and while one is up the POPUP's own routes own the mode.
    void open_menu_row_anchor_on_hover(int mouse_x, int mouse_y);
    void update_menu_row_exit(int mouse_x, int mouse_y);
    void disarm_menu_row();
    // Which item is at (x, y), or -1 — the painter's published boxes. PURE
    // GEOMETRY: a DISABLED row keeps its box and answers here like any other,
    // and whether it may be hovered, armed or activated is the separate question
    // each of the three callers asks for itself (dropdown_item_enabled,
    // app_state.h).
    int  dropdown_item_at(int x, int y) const;
    // The dropdown's RELEASE body: the redesign's one act-on-release surface.
    // Returns true when the popup owned the release. It TRIGGERS THE ITEM UNDER
    // THE POINTER — CLOSE FIRST, then the menu's own action (settings: the modal
    // stop and the prefilled editor; navigation: the item's chord through on_key).
    // IT TAKES THE RELEASE'S OWN (x, y) and, while the press CLAIM is live,
    // DERIVES that item with dropdown_item_at — the arm's own defining
    // expression, read at delivery — instead of trusting the recorded arm, which
    // a paint publishing the item rects later in the same dispatch batch can
    // leave one step behind. It is NOT the old position compare: that one refused
    // when the two disagreed; this one acts, and cannot disagree with an arm that
    // is current (the equivalence, and the batch that motivates it, are at the
    // definition). With nothing derived (the claimed press stands over the
    // separator, the chrome, the anchor button or off the box) nothing runs, the
    // release is consumed and the menu stays open — dismissal is a PRESS act here
    // and a release never dismisses, which is what makes the plain anchor click
    // open-and-stay-up by construction. A DISABLED row takes that same
    // consumed-and-still-open answer (2026-08-08): the derive reads raw geometry,
    // so the enablement gate is applied on this side of it. An UNCLAIMED release
    // derives nothing and takes the recorded arm, which is -1 in every state that
    // can reach it.
    bool finish_dropdown_release(int x, int y);
    // Drop the popup's POINTER-DERIVED state — the hovered face, the armed
    // face and the press claim — at the hook fired by both the pointer-leave
    // and capability-loss edges (its only caller). Only capability loss ends
    // that pointer stream outright; an ordinary leave has no event WHILE the
    // pointer stays outside, and may re-enter (a synthesized motion) with a
    // still-held button releasing normally afterward — clearing the press
    // claim here is what leaves that later motion/release owning nothing, not
    // an inability of those events to arrive. Both faces answer "where is the
    // pointer" and the pointer is
    // gone; the painter lights the hovered item with no in-window term of its
    // own, so dropping the arm alone would leave an item lit outside the
    // window. The menu stays OPEN and the row stays ARMED — leaving the window
    // is not a dismissal, and the mode is disarm_menu_row's question, asked
    // beside this call and asked only of an ORDINARY leave that did NOT go out
    // through row 1's band (a capability loss disarms whatever the position).
    void clear_dropdown_pointer_state();

    // THE HOVER TOOLTIP's hide — the hint's job ends the moment the user acts,
    // by whatever means, the two floating surfaces never coexist, and a pointer
    // that has left cannot be hovering anything. Its callers, re-derived by
    // grep: the hover recompute (hover ended), every pointer press, every KEY
    // press, every wheel, the dropdown's open edge, and two main.cpp hooks —
    // the pointer-leave / capability-loss edge, which needs it because the hover
    // clear beside it damages the STRIP only while this box hangs below it (the
    // full argument is at the definition), and the compositor close, which is
    // the key-press hide's own no-hint-over-a-modal rule at the one modal opener
    // no key press reaches. Showing is NOT here: the run loop's
    // tick owns the dwell, comparing AppState::redesign_tooltip.hover_ms against
    // the delay. Damages the strip and the box's last painted rect.
    // A MODAL SURFACE NEEDS NO HIDE OF ITS OWN beyond the key press that opened
    // it: recompute_redesign_button_hover refuses to run a dwell at all while a
    // prompt or a keyboard-modal editor is up (the rule is stated there), so a
    // tooltip cannot come back under one, which the per-tick recompute and the
    // hover that stays live under modals would otherwise let it do.
    void hide_shift_tooltip();

    // THE ONE CHORD-DISPATCH BODY shared by every redesigned band claim (rows 1
    // through 4). Hit-tests the painter-published rects against the chord table
    // and, on a hit, applies that button's shift / enabled / radio rules, arms
    // the click face where the row has one, and dispatches the chord through
    // on_key. Returns true when a rect claimed the press — a refusal still
    // claims it, a refusal being a consumed nothing. Row 1's Quit is inside it;
    // the two buttons outside it are Settings and Navigation, whose action is a
    // dropdown toggle — not a chord, since no keyboard chord opens or closes a
    // popup.
    // Arm the dual-axis strip drag — ONE body shared by the gesture's TWO
    // entries: the ctrl-exact waveform press and row 5's plain ruler-band press.
    // The arm PAINTS THE ANCHOR STEM from the press (2026-08-05, the one
    // surviving piece of the rolled-back strip-drag playhead arc) and owes that
    // first frame's damage; the gesture itself stays NAVIGATION-CLASS, touching
    // neither playhead nor selection.
    void arm_strip_drag_at(int x, int y);
    bool dispatch_redesign_chord(int x, int y, GuiInputState mods);

    // THE TOP FLAG EDITOR'S GUARD-FREE CLOSE, ONE OWNER FOR BOTH MOUSE BUTTONS
    // (2026-08-01, when the right press joined): a press anywhere OUTSIDE the
    // published editor box tears an open FlagPayload edit down without
    // committing — exactly Esc's teardown (pending dropped; Enter is the only
    // commit route, so closing is cheap and non-destructive) — and the caller
    // then goes on to act normally. Self-contained: it tests both "is an edit
    // open" and "is the press outside the box", so neither caller carries a
    // guard and the two can never drift. Inside the box the press belongs to
    // the field (caret / drag-select for the left button, nothing for the
    // right), so it returns having done nothing.
    void close_top_flag_editor_for_outside_press(int x, int y);

    // True when the open dropdown swallowed `key` — the popup-modal gate,
    // ranked directly under the prompt at the top of on_key. Bare Esc closes,
    // Ctrl+Q closes and falls through to the close route, everything else is
    // swallowed inert so no command can run under an open popup.
    bool dropdown_key_blocked(GuiKey key, GuiInputState mods);

    // THE CLICK FACE, dropped — the third face's only writer besides the
    // press claim. Called from the top of on_button_release (the physical
    // release) and from the pointer-leave hook beside clear_redesign_button_hover
    // (the button-LOST edge as far as the FACE is concerned: the hold stops being
    // the pointer's to show). Only capability loss actually guarantees no later
    // release; after an ordinary leave one may well arrive, and it finds the face
    // already cleared and does nothing, which is what makes clearing early safe
    // rather than the events being unable to come. Transition-gated, one
    // invalidate_top_strip when it fires. Rows 1 and 3 never set the state it
    // clears (their buttons carry click_face=false); rows 2 and 4 do.
    void clear_redesign_button_press();

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
    // NO CALLER OWES THE POINTER CURSOR ANYTHING — the cue has one owner, which
    // runs at the run loop's iteration boundary, past everything a caller does
    // after this returns (the prompt goes up, the layout is rebuilt). The three
    // callers each carried a re-resolve of their own until 2026-08-03; the
    // reasoning is at the definition.
    void finalize_active_drags();

    // Arm the region-select drag at a press — THE ONE ARM since 2026-08-05, when
    // the shift former's anchor moved to the clicked column and its
    // non-dissolving twin died with the span it used to preserve.
    // `anchor_frame` is the active-domain frame the press just placed the
    // playhead at; (x, y) is the press position for the press-becomes-drag
    // threshold. Dissolves the resting region at mouse-down, so a motionless
    // release rests nothing at all. FOUR CALLERS, all of them placement
    // presses (re-derived 2026-08-06): the plain UPPER-HALF waveform press, the
    // SHIFT-exact waveform press at either height, the empty
    // flag/triangle-lane parity press, and the `h` history view's own
    // full-height plain press (the three live ones through
    // place_playhead_and_arm_region). The plain
    // LOWER half is the scrub surface, whose press is a one-shot scrub act
    // arming nothing and leaving the region alone.
    void arm_region_drag_at(int64_t anchor_frame, int x, int y);

    // THE PLACEMENT PRESS'S PLAYHEAD HALF, and the whole of what the live press
    // and the `h` history mode's own placement press have in common: drop the
    // playhead at the clicked column, reseek a live scanner to it (keeping the
    // session alive) and override follow for that session. NO selection, NO
    // region, NO drag arm — each caller owns those, which is what lets the mode
    // reuse this recipe without inheriting a region former it must not have.
    // `click_rel_x` is x - waveform_area.x; the gutter (click_rel_x outside
    // [0, area.w)) seats nothing and returns -1, a value no seated frame can
    // take (the clamp's floor is 0). `was_playing` / `playhead_at_entry` are the
    // snapshot the caller captures AT PRESS ENTRY, ahead of every branch.
    // Neither is a pre-stop reading: the playback stops are claim-keyed and sit
    // at the branches that claim a gesture, and every press reaching this body
    // is a stop-free one, so no stop stands between the capture and either
    // reader. What each parameter really predates is a write of its own —
    // playhead_at_entry predates move_playhead_to's cursor write, and
    // was_playing predates the stop that reseek_keeping_alive may run internally
    // on an out-of-range position — and together they fire the reseek only on a
    // real move of a live session.
    int64_t place_playhead_at_click_column(int click_rel_x, bool was_playing,
                                           int64_t playhead_at_entry);

    // The waveform placement press BODY, shared by the plain UPPER-HALF waveform
    // press, the SHIFT-exact waveform press at either height (2026-08-05) and
    // the empty flag/triangle-lane parity press (architect 2026-07-23): clear
    // the marker selection, run the body above, and arm the region drag (which
    // dissolves any resting highlight at mouse-down). The clear runs ahead of
    // the body's gutter return, so an inert-gutter click still deselects but
    // seats no playhead and arms no drag.
    void place_playhead_and_arm_region(int click_rel_x, int x, int y,
                                       bool was_playing,
                                       int64_t playhead_at_entry);

    // The empty flag/triangle-lane double-click marker CREATE: the bare-`s`
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
    // editor, the load editor, or the top-strip flag editor in EITHER kind
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

    // Per-iteration promotion check for the archival status message, wired from
    // main.cpp's on_tick beside the preview label's own tick (the reason the
    // tick is the observer is stated at both sites). The message is composed and
    // PARKED at dispatch and written to the status slot only once the worker
    // reports that synthesis actually began, so a render served by one of
    // do_render's reuse rungs says nothing at all. Cheap: one empty-string test
    // per tick when nothing is parked.
    void tick_promote_render_status();

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

    // THE DEFERRED ARCHIVAL STATUS MESSAGE — the three fields that implement it,
    // written by park_render_status / tick_promote_render_status /
    // finalize_render_run and by nothing else.
    //
    // pending_status_text_ is the composed message ("Rendering..." for a single
    // render, "Rendering N of M (label)..." for a sweep entry) waiting for
    // permission to appear. Parked by park_render_status at the two archival
    // dispatch sites instead of being written to app.queue_progress_text, copied
    // into the slot by tick_promote_render_status, and cleared there and at
    // finalize_render_run — so a render served by a reuse rung, which never
    // fires the signal, simply drops its message at the completion with nothing
    // ever painted.
    //
    // status_promoted_ says the text currently in the SHARED slot is ours, and
    // exists so a park can retract it (a sweep cell's "3 of 8" must not linger
    // over the reuse cells that follow) without ever erasing another owner's
    // message — the preview's "Updating..." lives in the same slot.
    //
    // synthesis_started_ is the flag do_render stores true at its synthesis
    // boundary (RenderRequest::synthesis_started carries its address; the
    // ownership argument is at that field). The GUI thread resets it at each
    // dispatch — before the worker can run, so a previous session's true can
    // never promote the next session's message, which is what keeps a sweep's
    // reuse cells silent after a synthesis cell — and again at finalize.
    //
    // The signal says synthesis BEGAN, never that it will finish, so the
    // promotion also asks the dispatcher whether the parked message's session is
    // still alive (GuiAsyncRenderer::current_session_cancelled): a killed session
    // crosses the boundary and fires this flag on its way out, and its message
    // must not land on a newer owner's. Full rationale at the check.
    std::string       pending_status_text_;
    std::atomic<bool> synthesis_started_{false};
    bool              status_promoted_ = false;

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
    // strip, and drop the deferred status message with its signal. The summary
    // log is the caller's concern.
    void finalize_render_run();

    // Arm the deferred status message for an entry about to be dispatched, and
    // retire the outgoing one's: retract a message THIS owner promoted (a sweep
    // cell's count must not outlive its cell), reset the synthesis signal, park
    // the new text. Called by both archival dispatch sites immediately before
    // async_renderer.dispatch, which is what makes the reset's
    // before-the-worker-runs ordering structural. Full rationale at the
    // definition.
    void park_render_status(std::string text);

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
    // renders/ mutation (sweep batch-folder creation, the load-in-place
    // wipe) runs on
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

    // Clipboard: handle a Copy/Cut/Paste editor action against editor `s` and
    // report whether it handled one. THE CLIPBOARD IS THE SYSTEM ONE (the
    // Wayland CLIPBOARD selection, GuiPlatform::clipboard_set_text /
    // clipboard_get_text), and the PLATFORM HOLDS THE ONLY COPY of the payload:
    // copy and cut hand the selected text straight to the compositor and keep
    // nothing here (cut then deletes the text); paste takes whatever the system
    // clipboard holds — our own payload while we still own the selection,
    // another application's otherwise — and inserts it, doing NOTHING at all
    // when there is nothing to paste. Returns false for any other action so the
    // caller can fall through to its remaining branches.
    //
    // This and the readout's Ctrl+C (handle_key, input_handler.cpp) are the
    // whole of the GUI's clipboard reach; no other site copies or pastes.
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
    // playback and move the playhead onto the newly focused marker, recenter —
    // AT THE CURRENT ZOOM LEVEL, which the cycle never changes (architect
    // 2026-08-05, "no zoom on Tab", reverting the same-day working-zoom landing
    // this carried for one commit; `c` and `0`'s second arm remain the only
    // routes to kWorkingZoomLevel). The recenter is unconditional — follow mode
    // does not gate the cycle. A step that focuses nothing does nothing at all.
    // The WHOLE Tab family comes through here: the three bare chords and the
    // Ctrl+Shift+Tab lockstep march, which calls this once per tab.
    // Mode-aware: reads from phaseresetmarkers in 'P' mode, warpmarkers
    // otherwise. The history mode's diff-flag cycle is the mode-local mirror of
    // this rule, over its own list (handle_history_mode_key).
    void cycle_marker_focus(bool forward);

    // Jump the playhead directly onto the currently focused marker
    // (app.last_selected_marker), stopping playback and recentering the
    // viewport on it AT THE LEVEL IT IS CALLED AT — the zoom belongs to the
    // caller, and the callers differ in it: `c` sets the working zoom right
    // after this returns, the Tab family sets nothing (2026-08-05).
    // Returns true when a marker was
    // focused and the jump happened, false (leaving the playhead alone) when
    // there is none. This is the shared jump tail of cycle_marker_focus (the
    // Tab family) and the `c` gesture, both of which recenter the viewport; a
    // plain marker click is the other land-onto-marker route (its own direct
    // write in on_button_press — same two-step placement basis, but NO viewport
    // move). Both leave the playhead coincident with the focus, and a later
    // nudge/drag re-lands it on the focused marker as that marker moves.
    bool jump_playhead_to_focused_marker();

    // THE BARE `c` COMMAND, and the ONE owner of both its recipes: the working
    // zoom centered on the playhead, with a focused stop re-landed under it
    // first. THE MODE FORK IS INSIDE — the live recipe walks the live stores
    // (repair_last_selected + jump_playhead_to_focused_marker), the history
    // mode's re-expression walks its own diff-flag list and its own focus — so
    // the three callers (the live `c` arm, the mode's `c` claim, and
    // run_overview_command's already-full-out arm) share one decision instead of
    // spelling it each. Rationale at the definition.
    void run_center_command();

    // The bare `0` key: FULL ZOOM OUT FIRST, THE `c` COMMAND WHEN ALREADY THERE
    // (architect 2026-08-05, replacing the working-zoom toggle; the second arm
    // was a bare center for one day). Below the per-file effective ceiling →
    // jump to it (whole song visible); already at it → run_center_command, so
    // `0` twice is overview then working zoom on the focus. The FIRST arm is a
    // PURE VIEWPORT MOVE (architect 2026-07-30): it writes neither the selection
    // nor the region nor the playhead; the second carries `c`'s regime, stated
    // at that command. The rationale is at the definition.
    void run_overview_command();

    // THE SPAN-FRAMING command, run by the TRIM BAR LANE's DOUBLE-CLICK:
    // ZOOM TO A SPAN, never the working zoom. Span priority — a live region
    // (wins over trim) → a proper trim
    // SUB-WINDOW (expressed in the active domain) → the whole
    // song (full zoom-out, which the FULL trim window also takes). The region/trim span is framed with a 2.5%-per-side
    // margin; the fit level and span-start are set through the clamp chokepoints
    // via Viewport::apply_zoom_to_start (NOT apply_zoom_change — no playhead
    // recenter). Idempotent: a second click with the viewport unchanged no-ops.
    void run_span_framing_command();

    // Esc-cancel handlers: while a render or queued batch is in flight, BARE Esc
    // cancels it. Returns true if it consumed the key (on_key then returns).
    // Routed after the editor modal (which cancels an active edit on Esc
    // first) and before the rest of the key handlers. Takes the modifiers
    // because a modified Escape must not cancel a running render, as no modified
    // chord may do anything anywhere.
    // This SURVIVES the 2026-07-29 Esc unbinding as its own binding class
    // (render-work cancel, not a ladder rung) — ARCHITECT-CONFIRMED 2026-07-29
    // ("esc should cancel render"), the one flag the ruling-5 conversion carried.
    bool handle_escape_cancels(GuiKey key, GuiInputState mods);

    // Render-trigger chords: Ctrl+Alt+R and Ctrl+Alt+Shift+R, and nothing else.
    // Both read the ITERATION-MODE bit (architect 2026-08-02): mode off, they
    // are the single render beside the source and the `_miscellaneous` cell as
    // ever; mode ON, the plain chord becomes the ITERATION SWEEP and the shift
    // chord is a consumed no-op. Returns true if key+mods matched one (on_key
    // then returns), false otherwise.
    bool handle_render_dispatch_keys(GuiKey key, GuiInputState mods);

    // The iteration sweep's body, called from the one place its chord lives —
    // handle_render_dispatch_keys' Ctrl+Alt+R arm, with the mode on. Its
    // preconditions and its own refusals are stated at the definition.
    void run_iteration_sweep_render();

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
    // the load prompt, the commit-title editor, the bpm bracket editor, and
    // (architect 2026-07-28) the top-strip flag editor. The modal contract is stated once
    // at the definition; returns true if the editor consumed the key (on_key
    // then returns), false on Ctrl+Q so on_key runs the close routing.
    // `autocomplete` is the ONLY OPTIONAL hook — the bare-Tab one, empty for the
    // commit-title, bpm and flag editors, and bare Tab never arrives for them at
    // all: the
    // on_key gate (modal_editor_key_blocked) swallows it before this route ever
    // sees it. Every OTHER hook is REQUIRED and called unmodified: commit /
    // cancel / Ctrl+Q teardown are the per-editor bodies, and `repaint` is the
    // editor's own damage for a text change — the four bottom-strip surfaces
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

    // Load prompt (bare `'`). A bottom-strip modal editor, structural
    // sibling of the settings editor: it takes a render entry's identifier
    // relative to renders/ and, on Enter, loads that render's frozen sidecar
    // recipe in place as the new authoring baseline through
    // load_render_entry_in_place.
    //
    // THE `h` HISTORY MODE GIVES THE SAME EDITOR TWO MORE SUBJECTS, one per walk
    // (2026-08-04 for the commit, 2026-08-08 for the local member): on the COMMIT
    // tabs it takes a COMMIT SPELLING, opens prefilled with the viewed commit's
    // SHA and loads it through load_history_commit_in_place; on the LOCAL tabs it
    // takes a MEMBER NUMBER, opens prefilled with the viewed member's displayed
    // `n`, and loads that timeline state through
    // load_history_local_entry_in_place. The mode is the discriminator for the
    // pair against the renders side and the walk SOURCE is the discriminator
    // between them, both tested at the opener and at the commit (the autocomplete
    // speaks neither vocabulary and no-ops on the mode alone); every other line
    // of the editor — its keys, its modality, its painted cell, its Esc — is the
    // same one behaviour for all three subjects.
    //
    // open_load_editor: bare `'` opener (no-op with no source loaded; outside
    // the mode also refuses over a running/parked render and over an empty
    // renders/, both guards being renders-side).
    // load_editor_autocomplete:
    // bare-Tab longest-common-prefix completion over the entry identifiers; a
    // no-op in the mode, whose vocabulary it does not speak.
    // load_editor_commit: resolve the pending — to exactly one render entry,
    // or in the mode to a commit — and load it in place.
    // load_editor_exit_no_commit: Esc / Ctrl+Q teardown. handle_load_editor_key:
    // the key router, through route_modal_editor_key like the settings editor.
    void open_load_editor();
    void load_editor_autocomplete();
    void load_editor_commit();
    void load_editor_exit_no_commit();
    bool handle_load_editor_key(GuiKey key, GuiInputState mods);

    // THE COMMIT-TITLE EDITOR (architect 2026-08-07) — the load editor's exact
    // pattern for the history view's OTHER act. Ctrl+S while the view
    // stands opens it prefilled with `Update <id>`; Enter runs the
    // Save-and-Commit act under whatever the buffer holds; Esc abandons with
    // nothing written; an empty or whitespace-only buffer red-flashes and stays
    // open, since a checkpoint with no message is not a thing to write.
    // It REPLACED the act's confirmation prompt, and a bare Enter over the
    // prefill is that prompt's `y` — the pause is the same, and the editor uses
    // it to ask something worth asking.
    //
    // open_history_commit_editor: the opener, reached from ONE place —
    // Ctrl+S's own arm, which the mode bit re-aims (on_key's `s` handler,
    // input_handler.cpp; it was Ctrl+Alt+R's arm until 2026-08-08, when the
    // architect moved the act onto the SAVE button's chord).
    // commit_title_editor_commit: Enter — validate non-blank, close the editor,
    // run the act (run_history_commit, which owns the save, the close and the
    // dispatch).
    // commit_title_editor_exit_no_commit: Esc / Ctrl+Q teardown.
    // handle_commit_title_editor_key: the key router, through
    // route_modal_editor_key like the three editors before it. It passes NO
    // autocomplete hook — there is no vocabulary here to complete against, a
    // commit title being free text — so bare Tab drops at the modal gate exactly
    // as it does for the bpm and flag editors.
    void open_history_commit_editor();
    void commit_title_editor_commit();
    void commit_title_editor_exit_no_commit();
    bool handle_commit_title_editor_key(GuiKey key, GuiInputState mods);

    // load_render_entry_in_place: apply render entry `e`'s frozen sidecar recipe
    // (.settings + the marker pair) as the new authoring baseline, view-
    // agnostic (source OR target authoring view). Reads and validates the wav's
    // existence and all three sidecars BEFORE mutating any store, and returns
    // false leaving authoring untouched on any missing/malformed input — each
    // such genuine-failure arm naming its cause and path on stderr since
    // 2026-08-02, while the caller's unknown-id refusal (a typo) stays silent
    // behind its red flash; otherwise applies the recipe wholesale, wipes
    // renders/, and returns true.
    bool load_render_entry_in_place(const AppState::RenderEntry& e);

    // load_history_commit_in_place: the same act with the COMMITTED HISTORY as its
    // source — apply the three sidecars commit `spelling` carried (whatever
    // `git rev-parse` resolves: the SHA the `'` editor prefilled in the `h`
    // mode, a short SHA, a branch) as the new authoring baseline, in memory,
    // with the disk untouched. Validate-before-mutate like its sibling: the
    // resolve, the three-sidecar presence and all three STRICT whole-file parses
    // run before any store is written, each failure returning false with one
    // stderr line naming the cause, the committed path and the SHA. No wav is
    // compared (the corpus stores no audio — the loaded source is the source),
    // no renders/ wipe and so no running-render guard, and the mode itself
    // closes as part of the act. Gated on the mode standing: the sidecar base
    // name is the session's. Full behaviour paragraph at the definition.
    bool load_history_commit_in_place(const std::string& spelling);

    // load_history_local_entry_in_place: the same act with A STATE OF THIS
    // SESSION'S OWN UNDO/REDO TIMELINE as its source (architect 2026-08-08) —
    // apply the member `text` names, by its displayed NUMBER in [1, N], as the
    // new authoring baseline. Validate-before-mutate like both siblings: a
    // non-numeric, zero, out-of-range or unreadable member is one stderr line
    // and a false return with nothing touched. It restores exactly what an undo
    // entry carries — the two marker columns and the engine block — and nothing
    // a sidecar set would add (no tab bands, no prefs, no trim), applies it ON
    // TOP as ONE new undo entry rather than as a rollback, writes no disk and
    // closes the mode as part of the act. Full behaviour paragraph at the
    // definition.
    bool load_history_local_entry_in_place(const std::string& text);

    // Bare x is SET-ONLY (architect 2026-07-25): it branches on the highlight
    // (no context-awareness beyond that) — a live region trims to it, begin at
    // the span's lo, end at its hi, overwriting any existing bounds, then
    // DESELECTS (it is a trim SETTER — the rule is at the setter-deselect block
    // above), CONSUMES the span, clearing the region at its tail, and LANDS THE
    // PLAYHEAD ON THE COMMITTED TRIM START (all architect 2026-07-30: the scratch
    // existed to aim this gesture, and the cursor comes to rest at the start of
    // the window the drag just walked out). TWO SILENT REFUSALS, both writing
    // nothing at all: NO region, and a DEGENERATE result — the inverse-mapped,
    // wall-clamped pair coming out end <= begin, which auto_clear_crossed_trim would
    // read as crossed and reset to the song edges (ARCHITECT-CONFIRMED 2026-07-29;
    // the derivation is at the definition). x never MAXIMIZES either way
    // (that arm moved to Shift+X).
    // No read-only check, and nothing left for one to do: the keyboard gate
    // ADMITS bare `x` and Shift+X since 2026-08-07 (trim is band, not authored
    // content), so a locked tab runs both exactly as a writable one does. The
    // sole dispatch entry for the bare-x key.
    void handle_trim_x();

    // Shift+X MAXIMIZES the trim to the full window [0, total-1] (architect
    // 2026-07-25 for the binding, re-posed 2026-07-30 under always-set: the full
    // window IS the old unset state — it renders untrimmed and plays to the
    // natural end). Read-only admits it (see handle_trim_x above);
    // the body then delegates WHOLE to handle_trim_clear_both. A trim MAXIMIZER,
    // not a setter: it does NOT deselect, and it touches no region at all (the
    // gated region re-sync it carried died with the trim-window highlight,
    // architect 2026-07-30 — a scratch span is the user's).
    void handle_trim_shift_x();

    // Write the FULL window [0, total-1]. Silent no-op when the window is
    // ALREADY full (the identity guard that replaced the old has-a-bound refusal
    // gate — an already-maximized Shift+X stops nothing, repaints nothing and
    // triggers nothing). The caller is handle_trim_shift_x.
    void handle_trim_clear_both();

    // Field-reset core shared by handle_trim_clear_both (the Shift+X maximizer)
    // and the crossed-commit reset below: write the canonical full pair for the
    // loaded source through the one seeding owner, full_trim_window
    // (app_state.h). No invalidation and no trigger — callers own their repaint
    // tail. One implementation so the two can never drift.
    void reset_trim_to_full_window();

    // Crossed/equal trim cannot REST (ruling comment at the definition): when
    // end_frame <= begin_frame — exact integer compare — at a trim COMMIT, RESET
    // both bounds to the song edges, silently (the endcaps jumping there are
    // the signal). Recognizes the already-full window first, so the one-frame
    // canonical pair [0, 0] is left alone rather than reset every commit. Called
    // by every trim commit site after its mutation and before its
    // invalidations, so the repaint shows the reset state.
    void auto_clear_crossed_trim();

    // EVERY TRIM WRITE PARKS THE PLAYHEAD AT THE NEW TRIM START (architect
    // 2026-08-05, generalizing `x`'s own 2026-07-30 land). Reads the COMMITTED
    // begin out of the store — never a caller's local — so a crossed pair that
    // the commit tail has just RESET to the full window parks the playhead at
    // frame 0, which is that window's start: a reset is a trim write like any
    // other and needs no arm of its own. Lands through
    // land_playhead_on_marker's placement basis (source frame → active domain →
    // live-domain clamp) with NO viewport move, so a trim start offscreen
    // leaves the view where the user left it, and CLEARS A RESTING REGION,
    // this being a playhead-moving command (the clear-site rule at
    // clear_region_highlight). Callers own the refusals above it: a route that
    // writes no bound must not call this. The full per-route inventory is at
    // the head of input_trim.cpp.
    void park_playhead_at_trim_start();

    // THE SHARED TRIM COMMIT TAIL, in code rather than in prose: every
    // trim-SETTING commit runs the same acts in the same order
    // (auto_clear_crossed_trim, the waveform + timestamp repaints, the
    // target-render trigger, then the playhead park above), and this member is
    // their one spelling. FOUR
    // CALLERS — `x`'s set-from-region (handle_trim_x), the drag release
    // (commit_trim_drag) and the bound-set click (set_trim_bound_at_click), all
    // input_trim.cpp, plus the settings editor's `:trim_*=` active-tab arm
    // (settings_editor.cpp, reaching it through the friendship above; its
    // timestamp repaint rides applied() as well, harmlessly twice).
    // Callers own everything around it: their refusals, the playback stop and
    // the setter's deselect, which differ per route by
    // ruling. ONE DELIBERATE NON-CALLER, a different tail by design:
    // handle_trim_clear_both (the Shift+X maximizer resets rather than
    // auto-clears — a non-setter), which calls the park directly instead.
    void commit_trim_mutation();

    // Plain trim-bar press routing — the PLAIN press's route into a trim
    // drag, and ONE OF TWO since the bound-set clicks came back 2026-08-01 (the
    // other is set_trim_bound_at_click_then_arm_drag, which arms the same
    // single-bound pending on the bound it has just written; the Alt pointer
    // gesture retired wholesale, and the waveform stem
    // grab with it; bounds are grabbed by their top-strip ENDCAPS / the bar's
    // inter-cap bridge only). Arms a PendingTrimDrag (the pending+threshold
    // pattern): the
    // press CLAIMS the cap/bridge geometry, but the trim-drag machinery begins
    // only once the pointer crosses kDragMovedThresholdPx. A full ordered pair
    // always rests (the unset state died 2026-07-30), so the claim is purely
    // GEOMETRIC. Returns true iff the press landed on trim
    // geometry (an endcap-rect single hit, or the trim bar lane's inter-cap
    // bridge span) — so the caller claims with no
    // fallback; false lets the caller fall through to its ruler / marker flag
    // handling. Read-only no longer refuses anywhere on this route
    // (2026-08-07). Trim drags are SETTERS, so they DESELECT
    // and STOP a live audition at their first ACCEPTED bound change (the press
    // carries neither since 2026-07-30 — a trim-bar press that never becomes a
    // drag is a consumed nothing); the PLAYHEAD is what they never touch.
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

    bool route_trim_bar_press(int mouse_x, int mouse_y);
    // Arm the pending trim endcap/bridge drag (pending+threshold): the begin runs
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

    // Set ONE trim bound (begin or end) at the clicked column, REINSTATED on the
    // redesigned TRIM BAR (architect 2026-08-01, after a one-day retirement) —
    // the trim-drag release-snap basis (authored_frame_at_column over the
    // displayed paint map), walls [0, total-1], then the shared commit tail.
    // ADJUST-ONLY is now a statement about what the click DOES — it moves one
    // bound of the window that always rests — rather than a condition it tests,
    // the pair gate having died with the unset state (2026-07-30).
    // THE STRICTLY-INSIDE GUARD is the new half (architect 2026-08-01): a value
    // resting EQUAL TO or PAST its partner is a CONSUMED NO-OP — no write, no
    // deselect, no stop, no drag — so this route never produces a crossed pair
    // and never hands one to auto_clear_crossed_trim. History-less like every
    // trim mutation; repaint + target_render.trigger() like the drag release.
    // Read-only refuses silently (trim authoring). OWNS the press's playback
    // stop, placed past those refusals and just ahead of the bound write, so the
    // ctrl / ctrl+shift press carries none of its own and a refused click leaves
    // a live audition playing. RETURNS whether a bound was written (the wrapper's
    // arm gate). is_begin picks the bound: ctrl sets begin, ctrl+shift sets end.
    // The full derivation lives at the definition, input_trim.cpp.
    bool set_trim_bound_at_click(bool is_begin, int mouse_x);

    // WHAT THAT CLICK WOULD WRITE, or nullopt when it refuses — the whole of the
    // decision half of set_trim_bound_at_click above (its
    // degenerate-geometry gate, the column clamp, the map + authored_frame_at_column
    // derivation, the absolute walls, and the STRICTLY-INSIDE guard), leaving that
    // function nothing but the write and its tail. It is a shared owner for the
    // same reason the two trim-bar hit predicates are: the pointer CURSOR asks
    // whether the ctrl / ctrl+shift click at this column would set a bound
    // (pointer_cursor_kind), and a cue that promised a consumed no-op is exactly
    // what the strictly-inside guard exists to prevent. Const, because deciding is
    // not acting: the answer is a pure function of the store, the audio and the
    // painted geometry.
    std::optional<int64_t> trim_bound_click_frame(bool is_begin,
                                                  int mouse_x) const;

    // The ctrl / ctrl+shift trim-bar bound-set press: sets the bound at the
    // clicked column (set_trim_bound_at_click, above) AND arms the EXISTING
    // single-bound trim drag on it through arm_pending_trim_drag — the same
    // pending an ENDCAP press arms, so motion past the threshold drags the bound
    // live under the drag's own unchanged rules, while a motionless release rests
    // the click-set. NOTHING is stashed: the click-set is committed when made
    // (trim is history-less) and pointer gestures have no cancel. A REFUSED set
    // arms nothing at all, the arm riding the setter's return value rather than a
    // second copy of its guard ladder.
    // is_begin picks the bound (ctrl=begin, ctrl+shift=end).
    void set_trim_bound_at_click_then_arm_drag(bool is_begin, int mouse_x,
                                               int mouse_y);

    // One scrub ACT at an active-domain frame: STOP, THEN START ON THE NEXT
    // CLICK (architect 2026-07-27, superseding the 2026-07-23 kill-and-revive).
    // A click while audio PLAYS is a pure stop — the frame is ignored and
    // nothing relaunches; a click on a stopped session runs the launch path
    // (the target-view is_updating gate + scrub_launch_at) at the given frame,
    // capturing its end_sample freshly there and playing once to it. Sole caller:
    // the one-shot scrub press body (scrub_press_at).
    void scrub_act_at(int64_t frame);

    // The scanner scrub press body. TWO CALLERS, both in on_button_press: the
    // waveform lower-half PLAIN LEFT press and the BARE RIGHT press over the
    // waveform's FULL HEIGHT (architect 2026-08-01) — the enumeration and the
    // per-caller gates live at the definition, input_pointer.cpp. The
    // marker-text lane's empty-spot scrub is DELETED (architect
    // 2026-07-27). Given
    // the click's waveform-relative column, run ONE scrub act (scrub_act_at —
    // stop a live session, else launch) at that column's frame — the scrub is
    // ONE-SHOT per click (architect 2026-07-23, the Ableton model): the
    // press arms NOTHING, a held press does nothing further, and motion over
    // the scrub surface is inert (the scrub drag is removed, so each click
    // pays AT MOST one stop quiescence fence — a stopped session's launch pays
    // none). A gutter/invalid column
    // (outside [0, area.w)) is a silent no-op (no launch position). Touches
    // NOTHING else — no selection, region, cursor, follow, or double-click seed.
    // THAT is what makes it the REGION'S PREVIEW GESTURE (architect 2026-07-30,
    // Q2): clicking inside a resting scratch span auditions from the clicked
    // frame and leaves the span standing, which is why Space no longer carries a
    // region launch of its own.
    // The caller keeps playback alive across the press (no waveform press stops
    // playback, and the top-strip stops belong to the top-strip claims), so
    // the act sees the LIVE session — load-bearing for the
    // stop-then-start ruling: a press that let the session die first would turn
    // the interrupting click into a launch.
    void scrub_press_at(int click_rel_x);

    // THE POINTER CURSOR'S ZONE MAP — the kind the pointer should be showing at
    // (x, y) with `mods` held right now. THE ONE OWNER of that question; do not
    // scatter set-cursor calls through the handlers. EXACTLY ONE CALLER,
    // refresh_pointer_cursor above, which hands the answer straight to
    // GuiPlatform::set_cursor_kind and is itself called once per run-loop
    // iteration from the platform's settled-state hook. That single owner is why
    // this map may be a pure function of state: it is asked at a loop boundary,
    // so it never has to be defended against being asked too early.
    //
    // THE CURSOR PROMISES THE GESTURE (architect 2026-08-03). That is the whole
    // rule, and it is why every zone here is DERIVED FROM THE PRESS PATH rather
    // than written as a list of situations: each arm is a branch that actually
    // claims (or swallows, or diverts) the press in on_button_press and the band
    // routers, re-derived from them and in their order. If the press path grows a
    // new swallow over the waveform, this grows the same one.
    //
    // THE ZONES, each with the press branch it is taken from:
    // - Scrub: the waveform's LOWER half, plain — the audition scrub press,
    //   sharing its half test through waveform_lower_half. GONE WHILE THE `h`
    //   HISTORY VIEW STANDS (2026-08-05): playback is removed from that view
    //   whole, so its lower half is the placement press and there is no audition
    //   to promise.
    // - Pan: the waveform, EITHER half, ALT-exact — the captured grab-pan, which
    //   arms anywhere inside the waveform, so the cue covers the full height.
    // - Zoom: the waveform, EITHER half, CTRL-exact — the dual-axis strip drag;
    //   and the RULER band, plain — the SAME gesture through the same hoisted
    //   arm (arm_strip_drag_at's two entries), which is why the two surfaces
    //   share a cursor. The `h` view's trim-bar framing wore this cue for the
    //   day it was a single click and does not now: the act is a DOUBLE-click,
    //   and a double-click carries no cursor promise anywhere in the product.
    // - TrimResize: the trim bar's inter-cap BRIDGE, plain — the pair drag, which
    //   moves BOTH bounds together, and the only trim gesture that does.
    // - TrimBoundBegin / TrimBoundEnd: EXTENDING ONE BOUNDARY, in the two routes
    //   that do it — the trim bar's BEGIN / END endcap on a plain hover (the
    //   single-bound drags), and the bound-set clicks that write the same two
    //   bounds, ctrl for begin and ctrl+shift for end. Both of those arm a
    //   single-bound drag as well, so the cue is one shape for one act.
    // - Arrow: everything else, the marker lane and the four button rows
    //   included.
    // THE TRIM BAR'S THREE ZONES READ THE ROUTER'S OWN OWNERS and re-derive
    // nothing: hit_test_trim_endcap and point_in_trim_bridge_span for the plain
    // hover (exactly what route_trim_bar_press calls, in its order), and
    // trim_bound_click_frame for the two ctrl clicks (exactly what
    // set_trim_bound_at_click decides on). So a point on the band that would arm
    // NOTHING — the bar's outside on a trimmed-in window, or a ctrl click the
    // STRICTLY-INSIDE guard would consume — shows the Arrow, and the cue cannot
    // drift from the gesture because there is no second copy to drift.
    // ALL THREE ARE MODE-SCOPED, and per zone rather than per band (2026-08-05):
    // the `h` history view consumes the endcap/bridge drags and both ctrl clicks,
    // so those three cues go while it stands and the whole band answers Arrow
    // there. (That answer was the LOCKED TAB'S too until 2026-08-07, when trim
    // became read-only-legal; the view is the only zone consumer now.) The one gesture the view DOES give
    // that band is a DOUBLE-click (its diff-span framing), and a double-click
    // carries no cue anywhere in the product, the live band's own span framing
    // included. THE SCRUB ZONE IS MODE-SCOPED ON THE SAME MODEL, at its own arm:
    // the view consumes both scrub entries, so its lower half answers Arrow too.
    // THE MODIFIER ARMS OUTRANK THE PLAIN ZONES ON THE WAVEFORM, exactly as the
    // press path ranks them: alt or ctrl held means the pan or the zoom drag, not
    // the scrub. SHIFT IS NOT IN THE MAP over the waveform — it is the PLACEMENT
    // PRESS since 2026-08-05 (the region former's anchor moved to the clicked
    // column), and the placement press carries no cue on either half, so shift
    // takes the Arrow like everything unnamed and still refuses the Scrub cue as
    // it always did; the one place a shift combination IS named is ctrl+shift on
    // the trim bar, which is a real bound-set claim rather than an unbound stray.
    //
    // READ-ONLY IS NOT IN THIS MAP AT ALL SINCE 2026-08-07, and the change is a
    // deletion rather than a move: read-only protects the authored musical
    // content, trim is BAND, and so every zone this map answers — the strip
    // drag, the pan, the scrub, the endcap and bridge drags, the two ctrl
    // bound-set clicks — runs unrefused in a locked tab. The per-zone read-only
    // record that stood here (navigation live, TRIM refusing through the band
    // gate, the two ctrl cues through trim_bound_click_frame's first gate) is
    // RETIRED with those two gates; the `h` history view is the sole per-zone
    // consumer left, above. Neither this map nor its callers test the bit.
    //
    // WHAT IT IS BLIND TO, deliberately and by ruling:
    // - The BARE RIGHT press scrubs the waveform's FULL HEIGHT, and this marks
    //   only the LEFT press's lower half. The cursor is a cue for the lower-half
    //   SURFACE, not a map of every route into scrub_act_at — a scrub cue over
    //   the whole waveform would promise the left button something the upper half
    //   does not do.
    // - The FLAG editor does not refuse — it is pointer-transparent by ruling, so
    //   a scrub still acts under an open one and the cursor must not lie about
    //   that. The three BOTTOM-STRIP modal editors DO refuse, because they really
    //   do swallow the press (modal_bottom_strip_editor_active).
    //
    // THE CUES ARE HOVER-ONLY WITH ONE NAMED EXCEPTION (architect 2026-08-03):
    // a LIVE TRIM GESTURE — pending or past the threshold; an endcap drag, the
    // bridge drag, or a ctrl bound-set's armed drag — OWNS the cursor for as
    // long as it lasts, wherever the pointer is: a begin-bound drag keeps
    // TrimBoundBegin, an end-bound drag TrimBoundEnd, the bridge TrimResize.
    // The kind is read from the drag's own record of what it grabbed, never
    // re-derived from the pointer's position, so the cue neither flickers as
    // the pointer leaves the band nor reverts to the Arrow mid-drag. Trim can
    // be the one exception because on this gesture alone the thing being
    // dragged is the thing the cursor names, so the cue stays true throughout.
    // EVERY OTHER gesture keeps the uniform refusal — no cursor changes during
    // the marker, region, strip or pan drags (the captured two hide the cursor
    // anyway).
    //
    // THE ACCEPTED STALENESS IS ONE POLL WAKEUP WIDE, and that is the whole of
    // it since the cursor became a per-iteration answer (2026-08-03). Every
    // state this map reads is written from inside a dispatched event — a wayland
    // event, the tick, or a worker completion — and the owner runs at that same
    // iteration's tail, so a change is on screen in the frame it happens in.
    // The classes that used to be listed here as stale (a load completing under a
    // resting pointer, a menu closing by Esc, a zoom moving the trim endcaps out
    // from under a still pointer, a gesture ending on a key) are simply correct
    // now, and none of them cost a call site.
    // THE CAPTURED GESTURES ARE THE ONE REMAINING MEMBER, and they are a
    // deliberate deferral rather than a hole: while the platform has no real
    // pointer position it DROPS the kinds this map names (the reason is at
    // GuiPlatform::set_cursor_kind), so a modifier released BEFORE the button, or
    // a modal raised by a force-end mid-capture, shows at the compositor's next
    // absolute position — one mouse movement away, and strictly better than a
    // confidently wrong cue derived from virtual coordinates.
    GuiCursorKind pointer_cursor_kind(int x, int y, GuiInputState mods) const;

    // Bare `t` toggle: flip app.active_audio_view between Source and Target.
    // Stops any current playback before switching domains. Source → Target
    // translates app.viewport_start_sample / playhead_cursor_sample /
    // zoom_level through the current warp_frame_map in place and enters target
    // view only when target view is available. target-view playback is
    // allowed once the target buffer is ready; target render
    // update-in-progress gates playback elsewhere.
    void handle_active_audio_view_toggle();

    // Apply a new GUI scale (percent), running the shared live sequence:
    // assign app.gui_scale, push it to the renderer
    // (set_gui_scale_percent), full-window invalidate, then the resize-path
    // geometry-and-cache rebuild — the redesigned rows' lane heights come from
    // this value, so a change re-lays-out the whole window. The settings
    // editor's `gui_scale=` commit is the sole caller and gates the no-op case
    // (the file-load and load-in-place paths push straight through
    // set_gui_scale_percent, not through here). It is the ONE such applier since
    // row 7 deleted apply_font_size with the font_size key.
    void apply_gui_scale(int percent);

    // THE LANE MODEL (architect 2026-07-28, KEPT and re-justified 2026-07-30):
    // true when the arrows currently address the MARKER lane. The bare
    // horizontal arrows step one painted column per press; the lane decides WHAT
    // moves. A selection IS the marker lane: the step moves the FOCUSED MARKER
    // and the always-visible cursor RIDES ALONG, because both marker-lane routes
    // (the two position nudges) re-land the playhead on their committed focus.
    // The tempo-image step was a third marker-lane route
    // until 2026-07-29 and W+target is now a consumed refusal — see
    // marker_drag.h. With no selection the arrows are in the WAVEFORM
    // lane and step the cursor alone. THE JUSTIFICATION CHANGED, THE BEHAVIOUR
    // DID NOT: this used to be argued from a SUPPRESSION — the cursor stopped
    // painting under a selection and the focused flag's ink triangle "was" the
    // playhead — and the cursor now always paints, so the model stands on the
    // marker-nudge behaviour itself. LANE EXIT IS ANY DESELECTING
    // ROUTE (architect 2026-07-29, replacing the explicit Esc collapse — Esc is
    // unbound here now): Home/End, a waveform click, a trim setter's deselect, an
    // undo restore that empties the selection, and so on. There is still no
    // gesture fallback, so a marker-lane step that refuses stays a consumed no-op.
    // Distinct from the AUDITION SCRUB, which is untouched by all of this: that
    // is the waveform one-shot press (scrub_act_at / scrub_press_at — the
    // lower-half left press and the bare right press),
    // a pointer gesture on its own surface that starts or stops a scanner and
    // never moves the resting cursor. "Scrub" names that and only that.
    // TWO READERS, one owner: the on_key dispatch (which picks the lane) and
    // read_only_key_blocked's is_playhead_step entry (which admits the bare
    // horizontal arrows only while this is FALSE — in the marker lane they
    // author, and this gate is their sole read-only defense).
    bool playhead_in_marker_lane() const;

    // Source-view read-only allowlist. Returns true if key+mods is NOT on the
    // allowlist of navigation / playback / zoom / view-switch / close-prompt /
    // band / save / render keys honored in a read-only source tab — i.e. should
    // be dropped.
    // READ-ONLY PROTECTS THE AUTHORED MUSICAL CONTENT — the two marker stores
    // and the engine settings — AND NOTHING ELSE (architect 2026-08-07,
    // superseding the old "blocks persistent mutation" standard); the definition
    // carries the ruling, and it is the model's ONE authoritative home.
    // Authoring-mutation chords (Delete, undo/redo, the propagate commands, `;`,
    // `i`, `'`) are blocked here at the gate, while Ctrl+S, the two Ctrl+Alt+R
    // renders and the `x` / Shift+X trim gestures are ADMITTED — a save writes
    // the state the tab already holds, a render reads it, and trim is band.
    // One entry is
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
    // load editor, the commit-title editor and the bpm bracket editor (plus the
    // prompts, gated separately). TWO CALLERS, and they ask the same question about two pointer
    // facts: wheel_context's swallow (input_handler.cpp), because wheel zoom and
    // Alt+wheel pan are NAVIGATION, not chords, so they still punch through an
    // open top-strip flag editor; and pointer_cursor_kind (2026-08-03), because
    // these four editors are exactly the ones that SWALLOW a pointer press, so
    // they are exactly the ones over which no cursor may promise a gesture. The
    // flag editor's exemption is the same fact in both: it is
    // pointer-transparent, so the wheel reaches the viewport under it and a
    // scrub reaches the audio under it.
    // IT IS NOT A PLAYBACK-STOP PREDICATE and never was one in code. The stop is
    // not decided here — but it is no longer scattered either: since 2026-07-28
    // it has ONE owner, GuiPlaybackLifecycle::stop_playback_for_modal_open, which
    // every open site calls and which records the whole decision table (the four
    // bottom-strip editors and the prompts stop; the top-strip flag editor is
    // explicitly EXEMPT and keeps a live audition playing). So a new modal
    // surface inherits the wheel swallow from this predicate and its playback
    // answer from that owner — it grows neither by hand.
    // The gate is the sibling of read_only_key_blocked's allowlist shape: true
    // when key+mods should be dropped while a keyboard-modal editor is open
    // (admits only the keys the active editor consumes, bare Esc, Ctrl+S, and
    // Ctrl+Q). It serves all five editors, top strip included.
    bool modal_bottom_strip_editor_active() const;
    bool modal_editor_key_blocked(GuiKey key, GuiInputState mods);

    // THE `h` HISTORY MODE's entry points (bodies in
    // input_key_dispatch.cpp, except the pointer one in input_pointer.cpp). The
    // mode itself — what it shows, what opens and closes it, what it refuses and
    // why its frozen diff cannot go stale — is stated ONCE at
    // AppState::HistoryMode (app_state.h); each body states only its own
    // membership.
    //   * handle_history_mode_key owns the mode's whole keyboard vocabulary —
    //     the toggle, the walk, the diff-flag cycle, the absolute Home/End and
    //     `c` — and returns true when it consumed the press. The membership is
    //     re-derived at history_mode_owns_key; its position in on_key IS its
    //     entry-gate list.
    //   * history_mode_key_blocked is the allowlist gate, read_only_key_-
    //     blocked's shape: true when the press is not admitted while the mode
    //     stands. The redesigned buttons and the Navigation menu's items reach it
    //     through their synthesized chords, so it covers them too — and since
    //     2026-08-04 it also DECIDES THEIR FACES (history_mode_disables_button,
    //     app_state.h). It is a FREE function beside this class, with
    //     history_mode_owns_key (that vocabulary's shape), for that second reader:
    //     both are pure, the face derivation having no press and no handler in
    //     hand. Declarations above the class, where the one conditional
    //     admission — and why only this one takes the session — is stated.
    //   * handle_history_mode_press is the pointer half, and it both refuses and
    //     acts: true when the press was consumed (as one of the mode's own acts
    //     or as a refusal), false for the navigation gestures the mode lets
    //     through untouched. Its own comment carries the admitted list and the
    //     four acts. It takes the press's DOUBLE-CLICK SNAPSHOT because one of
    //     those acts is a double-click (the trim bar's framing) and on_button_-
    //     press clears the shared field before this is reached.
    //   * focus_history_diff_flag is the PLAIN focus click's body, shared by its
    //     two surfaces — the flag box in the lane and the flag's STEM in the
    //     waveform's upper half — so the two cannot answer differently. It
    //     clears the mode's multi-selection: a plain click replaces it.
    //   * select_history_diff_flags_modified is the SHIFT and CTRL clicks' body,
    //     over the MARKER LANE ALONE — the range extend and the membership
    //     toggle, both then focusing the clicked flag and landing on it. The
    //     flag's stem is a PLAIN surface only (architect 2026-08-06, the
    //     symmetry ruling: over the waveform a modifier names a gesture, not a
    //     selection, in this view exactly as in the regular ones).
    //   * close_history_mode is the ONE exit owner; every closer calls it, and
    //     since 2026-08-05 it is also the ONE site that puts the editor's parked
    //     navigation band back (the snapshot's own record is at
    //     AppState::HistoryMode).
    //   * frame_viewed_commit_diff_span frames the viewed checkpoint's whole
    //     delta. Since 2026-08-05 it is an ON-DEMAND ACT with ONE caller — the
    //     trim bar's plain DOUBLE-CLICK, the regular views' span-framing gesture
    //     with this act as its command — rather than an edge effect. Its own
    //     comment carries the recipe and the span rule.
    //   * frame_history_view_whole_song is what the ENTRY does instead: a fresh
    //     visit opens at FULL ZOOM OUT. It is the mode's ONLY viewport write
    //     since 2026-08-08 — the `,` / `.` step and the compare switch stopped
    //     calling it, the window being the USER'S for the whole visit and
    //     unified across every walk and reading — so its callers are the entry
    //     owner and
    //     the framing act above, whose empty-delta arm falls through to it.
    //   * open_history_mode_fresh is the ONE entry owner, and "fresh" is the
    //     whole of it: a new session, a new commit walk, a now side captured at
    //     this instant, and the head delta measured once. ONE CALLER since
    //     2026-08-05 — bare `h` — the commit act having stopped re-entering when
    //     it began closing the view instead. False (with init's own stderr line
    //     already printed) when there is no history to show; the mode is then
    //     left exactly as it was.
    //   * drop_lane_stash_across_history_edge empties the marker lane's
    //     published content — the two pointer stashes and the diff-flag list
    //     their indices name — at every mode edge: the entry, the exit, each
    //     walk step and each WALK-OR-READING SWITCH (four call sites, re-derived by
    //     grep 2026-08-06). Its own comment carries the argument and is the
    //     authoritative statement of the edge set.
    //   * republish_history_lane_now REFILLS it in the same press, at ALL FOUR
    //     edges (entry, step, reading switch, exit — the last one below its
    //     parked-band restore, where the mode is down and the lane it publishes
    //     is the LIVE one). It is the view switch's own synchronous route, and it
    //     is what makes an edge swap the lane's content atomically instead of
    //     blanking it for a frame (architect 2026-08-07). The drop's comment
    //     carries both arguments.
    //   * set_history_reading is the ONE switch owner for WHAT THE LANE SHOWS
    //     (2026-08-05 as the two compare readings' owner, generalized
    //     2026-08-07 to the (walk source, reading) PAIR): row 3's repurposed
    //     tabs SELECT THE WALK through it — the mode's only pointer surface
    //     outside the waveform and the lane — Ctrl+Tab CYCLES the two walks
    //     through it, and bare `u` FLIPS THE READING through it (2026-08-08,
    //     when the reading left the row for row 4's own toggle). A switch is a MODE EDGE with the `,` / `.` step's own
    //     shape, and the owner is idempotent, which is what makes a press on the
    //     already-shown tab a consumed nothing at its call sites.
    // THE COMMIT ACT'S GUI HALF is the last pair, and the act itself lives in
    // the diff module (commit_history_checkpoint, history_diff.h):
    //   * the COMMIT-TITLE EDITOR asks for the message (its cluster is declared
    //     above, beside the load editor whose pattern it takes).
    //   * run_history_commit is that editor's Enter: save, rebuild the bytes,
    //     close the view, and hand the captured job to the background worker.
    //     Its body owns the close partition (THE VIEW CLOSES IFF THE SAVE
    //     LANDED, architect 2026-08-07) and the capture list.
    //   * on_history_checkpoint_complete is the worker's completion, back on
    //     the main thread: it clears the in-flight bit and, for the three
    //     reporting outcomes, raises the acknowledge notice — deferring it into
    //     AppState::pending_history_notice while another modal stands.
    // THE REVERT ACT is the odd one out and deliberately so:
    //   * run_history_revert applies the SELECTED diff flags backwards into the
    //     live store of the active column and then closes the view. Its chord,
    //     Ctrl+H, is NOT part of the mode's own vocabulary — it is admitted by
    //     the allowlist (conditionally, on a subject standing) and dispatched
    //     from on_key's ordinary body BELOW the read-only gate, so a locked tab
    //     refuses it exactly as it refuses `'` — but no longer as it refuses the
    //     checkpoint act, which authors nothing and runs from a locked tab
    //     (2026-08-07's band ruling; the act's chord is Ctrl+S since
    //     2026-08-08, admitted by that same gate). Its body owns the
    //     per-class inverse, the always-force rule and the one undo entry.
    bool handle_history_mode_key(GuiKey key, GuiInputState mods);
    bool open_history_mode_fresh();
    void frame_viewed_commit_diff_span();
    void frame_history_view_whole_song();
    void drop_lane_stash_across_history_edge();
    void republish_history_lane_now();
    void set_history_reading(GuiHistoryWalkSource source,
                             GuiHistoryCompare    compare);
    bool handle_history_mode_press(GuiMouseButton button, int x, int y,
                                   GuiInputState mods,
                                   const DoubleClickCandidate& dc_at_press);
    void focus_history_diff_flag(int hit);
    void select_history_diff_flags_modified(int hit, bool extend);
    void close_history_mode();
    void run_history_commit(const std::string& title);
    void on_history_checkpoint_complete(GuiHistoryCommitOutcome outcome);
    void run_history_revert();
    // THE HEAD DELTA'S ONE MEASUREMENT SITE (2026-08-07). Called at the entry
    // and again at every prefetch arrival while the view stands; it measures
    // exactly once, when member 0 first exists, and is a no-op forever after.
    void measure_history_head_delta();
    // A KICK THAT WOULD LAND WHILE THE VIEW STANDS IS DEFERRED to the exit —
    // the visit's list must not be swapped underneath it. Set here, flushed by
    // close_history_mode.
    bool deferred_history_prefetch_kick_ = false;

public:
    // -- THE HISTORY PREFETCH'S THREE PUBLIC EDGES (2026-08-07) -------------
    //
    // START A FRESH SCAN of the loaded source's committed history — the ONE
    // funnel for all three kickers (main.cpp's startup load tail, the
    // checkpoint completion's re-warm, and the `h` entry's staleness kick), and
    // the one place the deferred-while-active rule lives.
    void kick_history_prefetch();
    // The same question with the staleness test in front of it: kick only when
    // the store describes another source, another projects_repo, or a branch tip
    // that has moved. Public for symmetry with the funnel; its one caller is the
    // mode's entry owner.
    void kick_history_prefetch_if_stale();
    // THE ARRIVAL HOOK, from the platform's prefetch ready fd (main.cpp's
    // wiring): drain the worker's queue into the store and, while the view
    // stands, react to what arrived — measure the head delta the moment member 0
    // exists, and damage the window so the lane and the `n/N` corner catch up
    // with a walk that just grew.
    void on_history_prefetch_ready();

    // THE DEFERRED NOTICE'S POLL, called once per tick from main.cpp: if a
    // checkpoint failure report is parked and the bottom strip is free, open it.
    // Public for that one caller, like repeat_eligible above.
    void maybe_open_pending_history_notice();
};
