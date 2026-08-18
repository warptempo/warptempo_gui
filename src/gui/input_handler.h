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
// gesture — the SHIFT REGION FORMER (the one mouse region gesture since
// 2026-08-12, the eighth glass ruling) — in two entries: the live shift press
// on the navigation surface (the WHOLE waveform since 2026-08-13 + the ruler +
// the marker lane's empty stretches; it DESELECTS at press) and the `h` view's
// own shift former (which clears the mode's pair instead, its spans being
// VIEW-LOCAL). It is previewed by the SCRUB — the lower half's motionless-
// release click act since 2026-08-13, its press-time entry and the bare right
// entry both retired, and no scrub exists inside the view — and CONSUMED by
// `x` (a live-view act; `x` is
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
//   * THE SHIFT FORMER'S PRESS (arm_region_drag_at, input_pointer.cpp — both
//     entries, the live navigation-surface press and the `h` view's own;
//     membership re-derived 2026-08-12, the authoritative inventory being
//     RegionState's in app_state.h): dissolves any resting highlight at
//     mouse-down, before the gesture is known to be a click or a fresh region
//     drag;
//   * THE DEFERRED CLICK ACT (run_nav_click_act, input_pointer.cpp — the
//     plain navigation-surface press's motionless release, live and `h`-view
//     arms alike, 2026-08-12): the placement is a point command, so the
//     release that runs it dissolves the span with it. THE PAN NEVER CLEARS —
//     a crossed plain drag is a pure viewport move — and NEITHER DOES THE
//     SCRUB ARM, which returns above the dissolve: those two are membership's
//     only deliberate non-members on this surface;
//   * THE MARKER CLICK ACT (run_marker_click_act, input_pointer.cpp), all
//     three arms (the plain single-select and both multi-select clicks) at ONE
//     site since 2026-08-15, UNCONDITIONALLY — the result-size split the
//     multi-select pair carried died with the extent owner, so every marker
//     click clears. It runs AT THE PRESS (2026-08-17), so a marker DRAG
//     dissolves the span too — its arming press cleared before the crossing;
//   * enter_text_edit, the one chokepoint of every flag/bpm editor open and
//     retarget;
//   * the POSITION NUDGES, both columns (finish_position_nudge);
//   * the MARKER DROPS: one clear per column, at the drop chokepoints
//     drop_marker (warp) and drop_phase_reset_at_position (phase reset), which
//     both entry routes (bare `s`, and the empty-lane double-click — at its
//     second PRESS since 2026-08-17) converge on,
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
//     BOTH BODIES: the plain focus click on the flag box
//     (focus_history_diff_flag — its stem surface died with the stems-inert
//     ruling, 2026-08-12), and the lane's shift/ctrl selection
//     pair (select_history_diff_flags_modified) — BOTH RUN AT THE PRESS
//     (2026-08-17, with the live marker clicks: the mode has no drag for a
//     flag press to become, so nothing there needs the lift), reached from
//     the mode router's flag claims. All of them are the live arms'
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
//     switch owner behind bare `g` and bare `u` alike). A span drawn in the view is a reading mark on the
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
// grepping scrub_press_at, 2026-08-13): THE SCRUB — the waveform LOWER-HALF
// plain press's motionless-release CLICK ACT, its one entry since the press-
// time dispatch moved to the lift — the region's
// PREVIEW gesture (click inside a span to audition it, the span resting
// untouched), THE REGION'S OWN EDITOR (2026-08-15 — the move and the two bound
// drags a plain press arms INSIDE a live span, apply_region_edit_drag_at: it
// EDITS the span, so clearing it would be the one thing it must never do; its
// UNMOVED release runs the deferred click act instead and clears through that,
// which is the degenerate full-window case's escape hatch), THE SHOW-REGION
// BUTTON (Ctrl+Shift+X, handle_show_region — it CREATES a span when none
// stands and otherwise only moves the viewport to the one that does; it clears
// a region in NO case, which is a ruling and not an omission: a
// clear-if-present arm was designed and the architect removed it, because a
// button that cleared what it was asked to show could strand a scrolled-away
// span. It was bare `x`'s no-region arm for the day between 2026-08-15 and
// 2026-08-16 and moved whole when he split show from commit), SPACE
// (which touches no region at all and always toggles
// from the playhead), and PURE VIEWPORT MOVES (PageUp/PageDown, zoom steps,
// pans — the plain-drag grab-pan and the plain wheel's stepped pan included,
// a crossed pan being a pure viewport move that pops nothing — and bare `0`'s
// ZOOM-OUT ARM — its other arm, the one taken with the zoom
// already at full out, IS the `c` command and clears in the list above; the two
// arms sit on opposite sides of this line and the command they share is why).
// The remaining pre-existing clear sites (file
// load, Ctrl+Tab, and the S/T switch) keep their own in-place clears, pairing the
// reset with a domain flip or a full-window repaint rather than this exact damage
// shape; so does the kick validator's live-domain reclamp.
void clear_region_highlight(AppState& app, Viewport& viewport);

// THE SEATED PINCH'S CLEAR, AND ITS DAMAGE — one body rather than the bare
// assignments it replaces (2026-08-14, when the pinch became the anchor stem's
// producer — its third when it joined, one of two since the overview strip
// drag's deletion; the stem's contract is at paint_strip_drag_anchor,
// paint_handler.cpp). It is a BODY for two reasons: (1) the EARLY RETURN makes
// the damage fire exactly ONCE per phase however often the clear is reached,
// and it is reached on every one-finger frame of the survivor's pan; (2) the
// damage is owed at all because a clear can land on a frame that APPLIES
// NOTHING and therefore rebuilds nothing — a survivor pan refused off the
// wheel's surfaces is exactly that frame, and it is the case the clear's own
// ordering rule already names (the clear leads apply_touch_nav_update's body,
// above the refusal). Full waveform-area damage, the discrete shape the mouse's
// own mode edges spell.
//
// FREE, AND BESIDE clear_region_highlight, SINCE codex round 20 — AND ON THE
// VIEW-STATE WRITERS RATHER THAN THE COMMANDS SINCE ROUND 21: the seat is an
// ACTIVE-DOMAIN song frame taken against a particular view, so A WRITE OF THE
// ACTIVE VIEW STATE KILLS IT. The one body cannot be a member of GuiActiveViews
// or GuiInputHandler because writers live in both, and in Undo and the file
// loader besides.
//
// THE MEMBERSHIP IS DERIVED FROM THE WRITES, NOT FROM THE COMMANDS, and this is
// the one place it is enumerated. Round 20 installed the clear at the three USER
// COMMANDS (`t`, Ctrl+Tab, `p`) and three routes still reached a switch without
// it: the propagate paste, which calls switch_active_markers_view_to directly;
// Undo's own inline W/P swap; and apply_settings_engine_and_prefs, which
// replaces S/T, W/P and A/B wholesale for BOTH load-in-places. So the rule now
// sits at every site that assigns app.active_audio_view / active_markers_view /
// active_tab_view — grep those three names and this list is what comes back:
//   * GuiInputHandler::handle_active_audio_view_toggle — the S/T writer (bare
//     `t`, the settings `active_audio_view=` key, the propagate paste's audio
//     half), below its own refusals.
//   * GuiActiveViews::switch_active_markers_view_to — the W/P writer, below its
//     same-mode early return (`p` through toggle_active_markers_view, the
//     settings key, and the propagate paste, which reaches this helper direct).
//   * GuiActiveViews::switch_active_tab_view_to — the A/B writer (Ctrl+Tab, the
//     settings `active_tab_view=` key, Undo's cross-tab restore).
//   * Undo's inline W/P swap in Undo::restore_history_entry — a deliberate copy
//     of switch_active_markers_view_to (Undo does not hold that cluster), so it
//     carries this rule by hand exactly as it already carries the column
//     switch's selection clear.
//   * apply_settings_engine_and_prefs (file_loader.cpp) — all three fields at
//     once, shared by the source load and by both load-in-places. It takes a
//     Viewport for this and for nothing else; its VALUES-ONLY contract is
//     otherwise intact, the clear being a lifecycle end rather than a side
//     effect the caller could time differently.
// load_file's own two direct writes (the pre-parse 'W' reset and the forced 'S'
// of a failed target-view restore) need no call of their own: it runs the
// routine above in the same body, and it is invoked once from the startup tick,
// before any input exists.
// THE COMMAND WRAPPERS DO NOT SPELL IT — `t` and Ctrl+Tab keep their calls
// because they ARE the writers, and toggle_active_markers_view lost its call to
// the helper it delegates to, so there is ONE spelling of the rule per write.
// The bare 1/2/3 selectors, the view bar's buttons, the S/T + W/P radios and the
// settings keys all compose those writers and inherit it.
//
// WHICH HALF IS CORRECTNESS AND WHICH IS THE FRESH-GRIP RULE, said plainly
// because the two read alike at the call site: an S/T write CHANGES WHAT THE
// NUMBER MEANS — the anchor is a song frame in the ACTIVE domain, so a SOURCE
// frame would go on being zoomed about as a TARGET one — and there the clear is
// a CORRECTNESS need. A W/P or A/B write leaves the stored number
// arithmetically valid (A/B restores another band, which moves the view under
// the anchor; W/P moves neither domain nor viewport), so there the clear is the
// FRESH-GRIP rule: a view switch replaces the view the fingers grabbed. Both are
// wanted, and one rule at every writer is worth more than a per-site judgment —
// but a reader should know which is which.
//
// REACHABILITY, so none of this reads as theoretical: a two-finger frame under a
// MODAL returns at apply_touch_nav_update's wheel_context refusal WITHOUT
// clearing anything (the only per-frame clear is the one-finger arm), so a
// seated pinch survives a whole load-editor session and resumes when the modal
// closes — which is exactly how a load-in-place selecting TARGET reaches a seat
// taken in SOURCE.
//
// AND THE TEMPTING FIX IS THE WRONG ONE, recorded here so it is not tried: DO
// NOT add touch navigation to any_pointer_gesture_active to make the above
// "impossible". apply_touch_nav_update asks wheel_context per frame, and
// wheel_context READS that predicate — a live pinch would refuse itself into a
// dead gesture.
//
// A live pinch simply re-seats on its next frame, which is the same fresh grip
// an upgrade takes.
// Its two non-writer callers are unchanged: the touch nav body's top (any frame
// that is not two-finger) and end_touch_nav (every end of the gesture).
void clear_touch_zoom_seat(AppState& app, Viewport& viewport);

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

// PREFER A SCROLL, ZOOM ONLY WHEN THE SPAN CANNOT FIT — the three-arm framing
// the GROUP undo/redo restore has taken since 2026-07-25, HOISTED into its own
// owner on 2026-08-16 when the show-region button needed the identical
// behaviour ("like undo in terms of zoom/viewport", architect). It is the
// framer above's caller, not its sibling: arm three IS
// frame_span_into_view(margin=true).
//
// [lo, hi] are ACTIVE-DOMAIN frames, the same domain frame_span_into_view
// takes and the same one BOTH callers already hold — the restore derives its
// extent through clamp_playhead_to_live_domain(source_frame_to_active_domain
// (...)) and the region's endpoints ARE active-domain frames by definition. A
// caller holding SOURCE frames (the trim bounds, say) converts before it calls,
// which is what the show-region seed does.
//
// It writes ONLY the viewport (level and start) and only through the family's
// clamp chokepoints; it damages nothing and kicks no render, exactly as the
// inline version did — each caller owns its own damage, which is why the
// restore's unconditional invalidate + kick tail is unchanged by the hoist.
// Order-agnostic: both arms read the pair symmetrically, so no swap is needed
// (the framer's own defensive swap still stands for its other callers).
//
// THE WHOLE ARGUMENT — the three arms, the painted-column fit contract, the
// ceiling/half-pixel exception the framer's no-op guard cannot cover, and the
// accepted duplicate render — lives at the DEFINITION in input_handler.cpp.
// Definition in input_handler.cpp.
void bring_span_into_view(AppState& app, const GuiAudio& audio,
                          Viewport& viewport, int64_t lo, int64_t hi);

// THE `h` HISTORY MODE'S TWO PURE KEY PREDICATES (bodies in
// input_key_dispatch.cpp, beside the mode's other keyboard work; the mode itself
// is stated at AppState::HistoryMode). They are free rather than members because
// each has a SECOND reader that holds no press and no handler: the redesign
// roster's mode-scoped disabled-face partition (history_mode_disables_button,
// input_pointer.cpp) asks them about a table of
// chords. (A THIRD predicate, history_mode_admission_is_momentary, served the
// 2026-08-12 collapse rule alone and is deleted with it, 2026-08-14.)
//   * history_mode_owns_key — the mode's own keys: bare `h` (the toggle), bare
//     `u` (the CUMULATIVE reading's toggle, 2026-08-08), bare `,` / `.` (the
//     walk), bare Tab / Shift+Tab / IsoLeftTab (the diff-flag cycle), bare `g`
//     (the WALK toggle over the two sources, the icon row's radio pair's chord
//     since 2026-08-18 — it was Ctrl+Tab and Ctrl+Shift+Tab, forward and
//     reverse over row 3's repurposed tabs, until then), bare Home / End and
//     bare `c`. NOTHING CTRL-CARRYING IS CLAIMED. The definition carries the
//     derivation. handle_history_mode_key consumes exactly these,
//     one line ABOVE the allowlist, which is why a face derivation has to ask
//     this first.
//   * history_mode_key_blocked — the allowlist gate, read_only_key_blocked's
//     shape: true when the press is not admitted while the mode stands. Its
//     admitted membership is enumerated at the definition.
//
// THE SECOND TAKES THE WHOLE AppState, THE FIRST TAKES NOTHING BUT THE PRESS,
// and the asymmetry is the membership's own: the mode's keys are a fixed keymap,
// while FOUR allowlist admissions are conditional on state they are asked about
// (re-derived 2026-08-09) — the commit act's, which is CTRL+S since 2026-08-08,
// on head_delta_empty (a view whose
// newest checkpoint already carries the session's authoring content has nothing
// to commit) and on history_checkpoint_in_flight (one checkpoint at a time), the
// revert act's, on a subject standing
// (history_mode_revert_subject_standing — a selected diff flag, else the focused
// one), and the load-in-place's, on the active walk carrying a member. Both
// readers hand it the
// same `app` and neither restates a term of it, which is what
// keeps the key and the face one decision — WITH ONE SCOPE SINCE 2026-08-15:
// the revert act's subject term decides the KEY alone, redesign_button_enabled
// lifting the four history companions over the derived partition, so Revert
// stays lit on an empty subject and its click is a consumed no-op (the
// architect's reversal of a grey that tracked the diff-flag selection and
// blinked at interaction cadence; the record is at that arm in app_state.h).
// It took the HistoryMode struct alone
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
//     ahead of the same deselect, and run AT THE LIFT since 2026-08-15 (the act
//     moved whole, this deselect with it);
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
    // ONLY ONE gesture fires them since 2026-08-15 — the one nav drag (the
    // overview lane's ctrl strip drag was the second and is deleted whole; its
    // record is above ScrollDragState, app_state.h): begin at the threshold
    // crossing,
    // end on every exit path (release, lost button, and the force-end
    // finalizer — there is no cancel path, 2026-07-29). Both
    // platform methods are self-guarding — begin no-ops when a capture is live
    // or the compositor lacks the managers, end is idempotent — so a drag
    // that never captured (degraded compositor) still calls end harmlessly.
    // BEGIN CARRIES THE GESTURE'S OWN CURSOR KIND, which is the kind the capture
    // release hands back: the nav drag's MODE AT ITS CROSSING — Pan or Zoom —
    // with every mid-gesture ctrl switch
    // re-stamping it through set_strip_capture_restore_kind below, so the
    // release restores the phase the gesture ended in. The overview lane's
    // drags (the box pan and the two edge drags — an outside press teleports
    // at the press and then arms that same pan) never capture —
    // absolute-position drags, the trim endcap model. A capture hides the cursor and makes the
    // GUI's pointer position virtual, so the platform cannot re-derive what to
    // restore and must not guess from what was showing at press time — the
    // reasoning, and why the stamp rides the lock-REQUEST path only, are at
    // GuiPlatform::begin_pointer_capture. It is the same shape as the live trim
    // cue: read the gesture's own record, never the pointer's position.
    std::function<void(GuiCursorKind)> begin_strip_pointer_capture =
        [](GuiCursorKind){};
    std::function<void()> end_strip_pointer_capture   = []{};
    // Set the active capture's release-restore x to the anchor stem's surface
    // x. The nav drag's zoom body (apply_nav_zoom_at, the ONE caller since the
    // overview strip drag's deletion) fires it each event (the last wins at
    // release) so the
    // cursor reappears dead on the stem; the pan phase never calls it, so its
    // release keeps the notional-position restore.
    std::function<void(double)> set_strip_capture_restore_x = [](double){};
    // The nav drag's TWO mode-switch riders (2026-08-14, the live-ctrl model —
    // contract at ScrollDragState, app_state.h). The capture itself is
    // untouched by a mode switch; only what the release restores moves:
    //   * clear_strip_capture_restore_x — drop the stem override at a
    //     zoom→pan switch, so the release goes back to the notional x
    //     (a later zoom phase re-sets it per event);
    //   * set_strip_capture_restore_kind — re-stamp the cursor kind the
    //     release restores (Pan or Zoom) so the cursor comes back as the
    //     phase the gesture ENDED in, not the one its capture began in.
    // Both are no-ops while no capture is live.
    std::function<void()> clear_strip_capture_restore_x = []{};
    std::function<void(GuiCursorKind)> set_strip_capture_restore_kind =
        [](GuiCursorKind){};
    // THE NAV DRAG'S LATERAL FREEZE (architect 2026-08-14, from the rig: the
    // zoom phase locks the pointer's x). True while the drag is zooming, false
    // while it pans: the platform then stops advancing the pointer's NOTIONAL
    // position with the zoom phase's lateral travel, leaving the travel ledger
    // and every delta untouched (contract at
    // GuiPlatform::set_notional_x_frozen). SINCE THE ROTATION THAT TRAVEL IS
    // THE ZOOM'S OWN INPUT rather than travel the phase discards, which is what
    // makes the freeze load-bearing rather than tidy: the level spends those
    // pixels, the position must not spend them again, and a position that did
    // would cap the zoom at the window's width (it clamps into the surface;
    // the ledger does not). Fired at the threshold crossing —
    // the ctrl edges a sub-threshold press took reached no capture — and at
    // every ctrl edge after it, from the one mode-sync body. A no-op while no
    // capture is live.
    std::function<void(bool)> set_strip_capture_notional_x_frozen =
        [](bool){};
    // THE CTRL-UP HANDOVER (2026-08-14): tell the platform that the pointer's
    // NOTIONAL X is now the zoom stem's surface x. The zoom phase froze that
    // position at the ctrl-down column while the stem slid with the song frame
    // it holds (wherever the viewport saturated), so the pan phase's release —
    // which restores at the notional x once the stem override is dropped —
    // would otherwise strand the cursor at the pre-zoom column. Stating the
    // position is what makes the drop honest, and it is FREEZE-INDEPENDENT BY
    // CLASS: the freeze gates the relative stream's ACCUMULATION, while this
    // states a real position exactly as the capture release's own write-back
    // does (contract at GuiPlatform::set_notional_pointer_x). Fired once, at
    // the zoom->pan edge, from the one mode-sync body; a no-op while no
    // capture is live.
    std::function<void(double)> set_strip_capture_notional_x = [](double){};
    // THE CAPTURED POINTER'S WRAP SPAN (2026-08-14): the waveform's left and
    // right bounds, between which the hidden pointer folds EDGE TO EDGE when
    // its travel would carry it past one — past the right bound it reappears at
    // the left, past the left at the right (contract at
    // GuiPlatform::set_capture_wrap_span). Fired IMMEDIATELY AFTER
    // begin_strip_pointer_capture at the ONE capture site — the nav drag's
    // threshold crossing (there were two until the overview strip drag's
    // deletion, 2026-08-15) — and nowhere else: the wrap is
    // a property of THE CAPTURED POINTER, not of a particular gesture, so every
    // capture supplies it uniformly by design. The bounds are the waveform's
    // rather than the window's so the behaviour is identical at every
    // resolution, and there is NO third value: an edge-to-edge fold has no
    // middle, so the centre column the one-commit centre form was handed here
    // is gone, and the even-width rounding question it raised is retired
    // with it.
    // A no-op while no capture is live.
    std::function<void(double, double)> set_strip_capture_wrap_span =
        [](double, double){};

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

    // KEYBOARD COMMANDS ACT AT THE PRESS. This is the one ranked key
    // dispatch: the platform delivers physical presses and synthesized
    // repeats to it (main.cpp's set_on_key hook), and three pointer surfaces
    // synthesize chords through this same body so every gate applies to them
    // identically — the chrome lift, the dropdown item, and the arrow
    // buttons' hold-repeat tick (input_pointer.cpp). Its boundaries each
    // live at their own homes: the five editors' modality
    // (route_modal_editor_key), strict modifier validation (an unbound
    // modifier combination is a consumed no-op; conventions.md), the Super
    // press drop (deliver_key, platform_wayland.cpp), the modal Enter/Space
    // act at the RELEASE — the product's one act-on-lift keyboard edge
    // (on_key_release, below) — bare `e` as the left mouse button, swallowed
    // at the platform boundary (kLeftClickKey, gui_input.h), and the held
    // arrow BUTTONS' repeat burst (AppState::ChromePress).
    void on_key(GuiKey key, GuiInputState mods);
    // THE KEY RELEASE, and the ONE thing in this product that acts on one
    // (2026-08-13): bare Enter / bare Space on a focused DIALOG BUTTON press it
    // down at the press and commit it here, the keyboard's own act-at-release
    // matching the pointer's. Every other release is a no-op — the platform
    // delivered releases to nothing at all before this, and the whole rest of
    // the keyboard contract is still edge-on-press. It takes no modifiers, for
    // the reason on_button_release names its own unused: the PRESS is what is
    // bare-exact. The arm is AppState::modal_dialog_key_pressed; the body is
    // in input_key_dispatch.cpp beside the ring that arms it.
    void on_key_release(GuiKey key);
    void on_button_press(GuiMouseButton button, int x, int y, GuiInputState mods);
    void on_button_release(GuiMouseButton button, int x, int y,
                           GuiInputState mods);
    // Coalesced scroll-wheel entry. `count` is the net detent count for one
    // pointer frame (>= 1); the platform's set_on_wheel routes here.
    void on_wheel(GuiMouseButton dir, int count, int x, int y,
                  GuiInputState mods);

    // The SINGLE wheel routing predicate. Returns -1 when the wheel is
    // swallowed at (x, y), else a region code: 1 inside the waveform area,
    // 2 inside the top strip, 3 the overview lane, 0 outside them all. It reads
    // POSITION AND STATE ONLY — never a modifier — so the plain pan and the
    // ctrl zoom step are live over exactly the same surfaces. on_wheel's
    // completed-detent gate and the platform's per-frame sub-detent accumulator
    // probe both consult it so the two surfaces can never drift.
    int wheel_context(int x, int y) const;
    void on_motion(int mouse_x, int mouse_y, GuiInputState mods);

    // THE TOUCH NAVIGATION BODY (touch phase 1, 2026-08-11; the phone
    // model's single-finger frames are back since the windowed model's
    // return, the sixth glass ruling 2026-08-12): the
    // platform's touch-nav update hook lands here — a PUBLIC entry point like
    // on_key and on_motion above, because main.cpp's hook wiring calls it (the
    // set_keyboard_intent_cancel_hook wiring precedent). TWO CALLERS' FRAMES,
    // one body: the two-finger gesture, and the phone model's SINGLE-FINGER
    // PAN — a one-finger drag whose down point lay on the waveform
    // (touch_point_in_pan_zone below), whose frames carry the finger as the
    // centroid and dist_ratio pinned at 1.0. The payload is a
    // GuiTouchNavFrame (gui_input.h): the CURRENT centroid, the centroid's
    // horizontal delta and the finger-distance ratio against the previous
    // delivered frame, the finger count, and the first finger's
    // thin-lane answer.
    //
    // THE THIN LANES TAKE NO NAV GESTURE AT ALL — the overview strip and the
    // trim bar — and the refusal is the frame's own down_on_thin_lane bit READ
    // ALONE, with no finger-count term (architect 2026-08-15: "once one finger
    // is down, the second finger is completely ignored, which is what we do with
    // three-finger gestures on the waveform"): a gesture BEGUN on such a lane
    // does nothing at all rather than falling through to the pinch below and
    // zooming the view from a strip the user was touching for another reason.
    // The one-finger case is no over-reach — a plain finger on these lanes never
    // reaches this body at all, so the only one-finger frames carrying the bit
    // are downgrade survivors, which are exactly what must not pan (the argument
    // is at the refusal). It reads the DOWN POINT and not the live centroid
    // because a 26 px lane whose drags are x-only cannot hold one, and the bit
    // is stream-constant, so the refusal cannot change under a live gesture.
    // THIS IS THE SECOND DOOR: the platform's own second-finger fork is the
    // first, ignoring a second contact on such a lane rather than upgrading a
    // live translation into a gesture this body would then refuse frame by
    // frame. Everything below describes the WAVEFORM's gesture — which keeps
    // its pinch for its whole life even if the fingers carry the centroid onto
    // a strip, the same down-point rule read the other way.
    //
    // ONE FINGER PANS, TWO FINGERS ZOOM — AND THE TWO-FINGER GESTURE NEVER
    // PANS (architect 2026-08-14, from the rig: "on the touch panel, two
    // fingers is zoom, one finger is pan... the two-finger gesture will never
    // scroll on the touch panel"). The body's whole model is that one fork:
    // a two-finger frame's centroid delta is discarded and its distance ratio
    // applies, a single-finger frame's delta pans and its ratio is 1.0 by
    // construction. There is nothing to classify — a pinch is the only thing
    // two fingers can mean — so the gesture carries NO segment state, no
    // verdict and no pause reset, and a same-direction two-finger slide
    // barely changes the finger gap and therefore does approximately nothing,
    // which is the intended answer rather than a classified outcome.
    //
    // THE APPLICATION IS ONE ANCHORED PLACEMENT PER FRAME, the strip drag's
    // own, and the two finger counts supply its anchor differently.
    // ONE FINGER — PER-FRAME ANCHORED AND STATELESS: the content under the
    // PREVIOUS centroid column is placed at the CURRENT centroid column under
    // the new level, which is the pan, and nothing survives the frame.
    // TWO FINGERS — A SEATED PIVOT, HELD FOR THE PHASE (architect 2026-08-14,
    // from the rig, carrying the mouse's own song-anchored pivot onto glass:
    // "when the two-finger touch is first registered, it picks the point on the
    // waveform, and the zoom pivot stays there no matter where the two fingers
    // move on the screen"). THE MODEL, stated here once (the state's lifecycle
    // is at TouchNavZoomState, app_state.h; the arithmetic in the body):
    //   * THE PIVOT IS THE POINT ON THE WAVEFORM THE PINCH GRABBED — a song
    //     frame seated at the centroid of the first two-finger frame that
    //     SURVIVES THE REFUSAL (not the first APPLIED one: seating is above the
    //     exact-no-op return, the ordering rule at the site) and held for the
    //     phase, its COLUMN re-derived against the live viewport every frame.
    //   * MOVING BOTH FINGERS TOGETHER STILL APPLIES NOTHING: the ratio is 1
    //     and the centroid's travel is discarded, the standing zoom-only
    //     ruling — the seat changes what the zoom pivots ABOUT, not what the
    //     gesture responds TO. Such a frame does SEAT, which is what makes the
    //     stem appear the instant the pinch registers and the grabbed point the
    //     pivot even when the fingers slid before the gap ever changed.
    //   * MOVING ONE FINGER zooms about the GRABBED point rather than about
    //     the moving midpoint between the fingers.
    //   * AT A WALL — the viewport saturated at frame 0 or the right edge — the
    //     held frame KEEPS ITS GRIP and its column slides across the glass,
    //     which is what makes the pinch reversible; a live centroid let the
    //     audio drift out from under the fingers, so pinching back out never
    //     returned what it came from. The mouse's own fix, and the parity is
    //     the point: the right hand plays the same part on both surfaces and
    //     ctrl is the left hand doing implicitly what the second finger does.
    //   * A column pushed off the waveform CLAMPS to the edge pixel and REBINDS
    //     the held frame to that pixel's content (apply_nav_zoom_at's edge
    //     trick), the one lasting mutation of the anchor.
    // WHAT THIS DOES NOT CHANGE: the fingers still zoom by their DISTANCE RATIO
    // ALONE. The level maps that ratio LOGARITHMICALLY (new_level = level -
    // log2(ratio)) — no feel constant: doubling the finger gap is exactly one
    // zoom level in, so the content between the fingers tracks the fingers.
    //
    // THE SUCCESSION, kept so the middle rung can be revived if the feel ever
    // wants it: (1) THE ACCORDION — pan and zoom applied simultaneously and
    // continuously every frame, and since two fingers moving together never
    // hold their separation exactly, the drift fed the zoom term and the view
    // breathed in and out during a drag ("if I move two fingers in the same
    // direction I get the old unrestricted zoom motion... during the drag it
    // squeezes in and out"); (2) THE FINGER-AGREEMENT SEGMENT LOCK
    // (2026-08-14, SUPERSEDED the same day) — the then-live mouse lock's
    // model brought
    // to glass: each motion segment classified ONCE on the two fingers'
    // cumulative travel vectors, PAN iff their horizontal components agreed
    // in sign, each vector lay within 60 degrees of flat, and
    // the LESSER finger had travelled at least half the 8 px classify distance
    // (the anchored-finger floor — a planted thumb's jitter could otherwise
    // pass the first two tests, while a panning hand moves as one rigid
    // unit), else ZOOM, with the off mode a literal zero, a
    // 75 ms rest re-aiming it, and the pre-classification
    // window HOLDING AND FOLDING rather than running both axes plain
    // (both-plain being the accordion in miniature); it worked, and it was
    // dropped because zoom-only makes it unnecessary, not because it failed;
    // (3) ZOOM ONLY, this shape. Damping was tried and rejected on the mouse
    // surface before either of them (the mouse ladder's closed record, now
    // below kNavZoomPxPerLevel, app_state.h) and is not re-proposed here.
    //
    // THE DESK MIRRORS THIS MODEL SINCE 2026-08-14 (the one-model ruling,
    // superseding the friction asymmetry this block used to record): the
    // navigation surface's drag PANS by default and CTRL is its live zoom
    // modifier — pressed or released mid-gesture, the second finger's part
    // played by a key — so "pan by default, add the zoom modifier at any
    // time, drop it at any time" is one sentence for both surfaces
    // (ScrollDragState, app_state.h). The friction argument that justified
    // the mouse keeping a pan axis inside its ctrl drag — "dropping it would
    // cost a modifier release mid-gesture and a re-press" — is ANSWERED
    // rather than violated: the modifier release mid-gesture IS the pan now,
    // costing no click, which is what dissolved the asymmetry and the mouse's
    // segment lock with it.
    //
    // IT DRIVES THE ZOOM FAMILY'S OWN APPLICATION CHOKEPOINT,
    // Viewport::apply_strip_drag_zoom (level clamp, viewport clamp, the one
    // synchronous per-frame rebuild, and the either-axis follow suppression
    // all come from it), and DELIBERATELY NOT any pointer-press arm — the
    // gesture enters BELOW every one of them.
    // The recorded justification for stopping short of a mouse arm, per the
    // fallback the phase-1 ruling names, is structural twice over and outlived
    // the particular arm it was written against (the overview lane's, deleted
    // 2026-08-15; it reads identically of the nav drag's): (1) the arm
    // CAPTURES THE REAL POINTER (begin_strip_pointer_capture
    // hides and locks the mouse cursor, and the release warps the mouse to the
    // gesture's restore x and rewrites the platform's tracked pointer position
    // to it — real mouse state corrupted from glass); (2) a live pointer
    // gesture is TERMINATED by on_motion's button-lost arm the moment any real
    // mouse motion arrives without a held button (mods.primary_button_held is
    // false during any touch nav gesture — neither finger count holds the
    // logical button), so a nudged mouse would kill a live
    // pinch. So the arms are pointer-coupled in exactly the
    // sense the ruling anticipated.
    //
    // THE REFUSAL ANSWER IS THE WHEEL'S, not the press path's, and per frame
    // rather than at entry: the gesture is the wheel's navigation-class
    // sibling (it never delivers pointer events, punches through the
    // keyboard-modal flag editor exactly as the wheel does, and navigates the
    // same two surfaces), so wheel_context — the SINGLE wheel routing
    // predicate — is asked at each frame's centroid, refusing under the
    // prompt, an open dropdown, the dialog modal editors, loading/empty
    // audio and any live pointer gesture, and applying only over the waveform
    // or the top strip. A modal opening MID-gesture therefore freezes it (the
    // wheel's own behavior), and a live mouse gesture freezes it too (no two
    // writers). The wheel's dismissal pair (dropdown close, tooltip hide) is
    // deliberately NOT copied — this is a refusal, not a press act — but the
    // DOUBLE-CLICK CANDIDATE IS CLEARED on every applied frame (the C8 rule: a
    // navigation that moves content between two clicks must not let the second
    // consume as a double-click).
    //
    // Navigation-class whole: no playhead, no selection, no region, allowed in
    // read-only, and a mid-audition frame suppresses follow through the
    // chokepoint's own either-axis line. Implemented beside the strip drag in
    // input_pointer.cpp.
    void apply_touch_nav_update(const GuiTouchNavFrame& frame);
    // The gesture's end (any end commits — a finger lifted, wl_touch.cancel,
    // or touch-capability loss; the platform fires this only if an update was
    // delivered): one predictor resync, the grab-pan release's own tail — each
    // applied frame already rebuilt synchronously — PLUS the pinch's seated
    // pivot cleared, the gesture's one GUI-side record since 2026-08-14
    // (TouchNavZoomState, app_state.h) through clear_touch_zoom_seat below, so
    // a later pair seats afresh instead of inheriting a dead pinch's anchor —
    // and so the anchor stem it gates is rubbed out at every end.
    void end_touch_nav();
    // (THE SEATED PINCH'S CLEAR is a FREE function since codex round 20 — the
    // view switches clear the seat too and they are not this class's:
    // clear_touch_zoom_seat(app, viewport), declared with clear_region_highlight
    // near the top of this header, which is the spelling it now sits beside at
    // those sites.)
    // THE PAN-ZONE QUERY (the phone model, second glass session 2026-08-11;
    // GROWN to the navigation surface by pan-primary's touch half, the
    // eighth glass ruling 2026-08-12, and to the WHOLE WAVEFORM by the
    // two-halves ruling of 2026-08-13): the platform asks whether a
    // touch-DOWN point lies on the one-finger PAN SURFACE — answered here as
    // THE NAVIGATION SURFACE through its one geometry owner
    // (point_on_nav_surface, input_pointer.cpp), which is literally the
    // predicate the press router reads: the WHOLE waveform, both halves and
    // every view, + the RULER + the MARKER lane MINUS its flag boxes (the
    // painter's published rects through hit_test_flag — a finger landing on
    // a flag resolves to the POINTER, so a quick flag drag is the immediate
    // marker drag, the mouse's own carve-out; in the `h` view the same
    // carve-out serves the diff flags). THE LOWER HALF JOINED THE ZONE with
    // the scrub's move to the lift: it is no longer a different surface, so
    // a one-finger drag pans there and the region hold reaches it, and a
    // motionless tap's press-release burst runs the deferred scrub — the
    // asymmetry the architect named ("finger down works in the upper half by
    // waiting for the finger up, but on the lower half it immediately
    // dispatches the scanner") is gone.
    // THE ZONE YIELDS INSIDE A LIVE REGION (2026-08-15): where
    // region_manipulation_hit answers non-None the zone answers FALSE, so the
    // finger resolves to the POINTER translation and reaches the region's own
    // move / bound drags — the flag box carve-out's exact shape, and it MUST be
    // here rather than beside it, because the surface this feature exists for is
    // the glass. It is the same one predicate the mouse press claim and the
    // cursor map read, so the two can never disagree about where a region
    // begins. THE CONSEQUENCE FOR THE REGION HOLD IS RECORDED AND ACCEPTED: the
    // hold-beat hold is a PAN-ZONE gesture, so a hold INSIDE an existing
    // region no longer reaches it — it is a pointer press on the region's
    // editor instead, and a motionless one lifts into the ordinary click act,
    // which destroys the
    // region. Drawing a fresh region over a standing one therefore needs one tap
    // to clear it first, or a hold started OUTSIDE it (the sweep may then run
    // through it freely — only the DOWN point is asked). SURFACE GEOMETRY ONLY:
    // every refusal (modal, prompt, dropdown, loading/empty audio, live
    // pointer gesture) deliberately stays downstream — at
    // apply_touch_nav_update's per-frame
    // wheel_context answer, so a refused pan FREEZES exactly as a refused
    // two-finger frame does rather than falling back to a pointer drag, and
    // in begin_touch_region's gate list for the hold. Asked ONCE at the
    // first finger's down; the answer also picks the window's DEADLINE (the
    // region-hold beat on the zone, the 60 ms window off it) and forks the
    // EXPIRY (the region hold vs the pointer unlock) — the platform state
    // block owns those edges. Wired at main.cpp's set_touch_nav_hooks call.
    bool touch_point_in_pan_zone(int x, int y) const;

    // THE THIN-LANE QUERY (2026-08-15) — the pan-zone query's twin, asked at
    // the same down: does this point lie on a THIN LANE? A CLASS, not a lane —
    // the body states what makes a lane a member and names the two (the OVERVIEW
    // STRIP and the TRIM BAR), so the next one joins on a rule. It names NO
    // gesture and forks no resolution here; the platform captures its answer and
    // carries it onto every nav frame (GuiTouchNavFrame::down_on_thin_lane),
    // where TWO readers refuse with it — apply_touch_nav_update drops every nav
    // frame carrying it, and the platform's own second-finger fork ignores a
    // second contact on such a lane outright, so once one finger is down the
    // second is completely ignored. Surface geometry only, like its twin. Wired
    // at main.cpp's set_touch_nav_hooks call.
    bool touch_point_on_thin_lane(int x, int y) const;

    // THE TOUCH REGION HOOKS (pan-primary's touch half, the eighth glass
    // ruling 2026-08-12 — "region select to be hold and then drag because
    // the pan is way more common": the pan zone's stretched window expires
    // at the region-hold beat into THE REGION FORMER, so
    // hold-then-drag sweeps a region on glass; the dead trim-move hooks'
    // exact pattern reborn — touch.md carries both records). The three ARE
    // the one region former's own machinery driven from the platform's
    // region hooks: no pending and no second former anywhere — the beat
    // already disambiguated, so the begin goes straight to the former's
    // press half and the drag rides the ONE motion path
    // (apply_region_drag_motion) with the playhead on the moving end.
    //
    // begin_touch_region(x, y) runs THE SHIFT FORMER'S OWN PRESS HALF at the
    // finger's DOWN point, forking on the `h` history mode exactly as the
    // shift press does: LIVE = the one placement body
    // (place_playhead_and_arm_region — deselect-all, playhead seated at the
    // down column, live-session reseek, dissolve-at-arm, the drag arm); the
    // MODE = the view-local former's recipe (clear the mode focus +
    // selection through the pair clearer, the shared placement body, the
    // same arm) — EVERY REGION FORMER DROPS THE SELECTION ITS SURFACE OWNS,
    // the family rule at RegionState (app_state.h).
    // THE REFUSALS LIVE IN THE BEGIN, mirroring the press path the gesture
    // bypasses (the dead begin_touch_trim_move's own list MINUS the `h`
    // view, which ADMITS this former as its own view-local vocabulary):
    // prompt, the five editors via keyboard_modal_editor_active (the flag
    // editor deliberately included though pointer-transparent — every
    // pointer press CLOSES an open flag editor before any claim runs, and
    // this begin, which skips the press path, must not become the first
    // gesture to run under one; the user's tap closes the editor as any
    // click does, and the next hold works), open dropdown, loading/empty
    // audio, a live pointer gesture. A refused begin arms nothing and the
    // update/end bodies no-op on the drag's own !active guard, so the
    // refused stream is dead rather than a fallback pointer drag.
    //
    // update_touch_region(x, y): the drag's one motion path — the shared
    // Chebyshev gate from the down point, then the span extension with the
    // playhead riding the moving end. end_touch_region(): the release
    // path's own body (a moved drag rests the span under the sliver gate; a
    // MOTIONLESS end rests nothing and leaves the playhead where the begin
    // seated it — the former's motionless-release rule, which is what makes
    // a long-press-then-lift a PLACEMENT). Any end commits — finger up,
    // wl_touch.cancel, capability loss; the platform's end split delivers
    // or drops the staged final frame, its record.
    //
    // ONE ACCEPTED CROSS-DEVICE EDGE, the dead trim-move's own class: the
    // drag state is app.region_drag itself, so a mouse release or a
    // lost-button motion arriving mid-gesture ends it early through the
    // pointer paths — the user's own two-handed act, every end a commit
    // either way.
    void begin_touch_region(int x, int y);
    void update_touch_region(int x, int y);
    void end_touch_region();

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
    // to hang a call on at all (a keyboard zoom moving the trim endcaps under a
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

    // THE NAV DRAG'S ZOOM/PAN MODE, synced from the live ctrl bit — the ONE
    // body behind the live-ctrl model, whose full contract (both re-seat
    // directions, the persistent screen pivot, what each edge damages and
    // re-stamps) is at ScrollDragState, app_state.h. Refuses immediately with
    // no nav drag standing or with the mode already agreeing, so both callers
    // cost a compare at rest.
    // TWO CALLERS, and neither is redundant. This one — main.cpp on the
    // settled-state hook, beside the cursor's owner above — answers the
    // MOTIONLESS edge: releasing ctrl must drop the zoom stem and re-stamp the
    // capture's restore THERE AND THEN, with no motion required (architect
    // 2026-08-14, from the rig). A loop boundary is after every write, so it
    // also covers the routes that move the modifier with no modifiers event at
    // all (a keyboard-focus loss forgetting the mask). The OTHER caller is the
    // top of the motion arm, which a dispatch batch carrying the modifiers
    // event and then a motion — with no loop tail in between — still needs, so
    // that the motion applies in the mode the user is already holding.
    void sync_nav_drag_mode(GuiInputState mods);

    // THE REDESIGNED BUTTONS' HOVER FACES, in two entries over one transition
    // writer serving the WHOLE roster — row 1's File / Settings and
    // the view bar's three, row 3's two tabs, row 4's twenty-six (the
    // toolbar four included since the 2026-08-12 relayout, the history group's
    // five closing it since 2026-08-18) and the bottom
    // row's fifteen — the transport three, then the right block's four marker
    // verbs with ADD TO SELECTION behind them (2026-08-18), three walk steps
    // and four cardinal arrows. EVERY ONE OF THEM
    // PUBLISHES A REAL RECT on every frame the roster paints: the bottom row's
    // cluster swap, which published zero rects for whichever four it hid, went
    // with the history companions on 2026-08-18 (definitions beside
    // on_motion in input_pointer.cpp).
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
    // popup state the menus share (AppState::Dropdown). toggle_ is the whole
    // action of BOTH non-chord buttons, File and Settings: it closes the
    // named menu if it is the open one and otherwise opens it, so pressing the
    // other button SWITCHES menus and "never two at once" is structural rather
    // than a rule. Its ONE refusal is the `h` history view's, and it is
    // MENU-SCOPED since 2026-08-08: Settings does not open in there, File
    // does (the scope was re-derived rather than inherited when the Navigation
    // menu was deleted 2026-08-15 — the reasoning is at the definition). close_ is what every dismissal route calls — an outside
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
    // liveness test, are at the definition). IT HAS TWO CALLERS AND BOTH ARE
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
    //     so do the prompt, the dialog editors and every live gesture —
    //     plus ONE condition the call site restates because nothing above
    //     returns on it: a HELD PRIMARY BUTTON refuses the open (codex round 2;
    //     the two held-motion producers are recorded at the call);
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
    // GEOMETRY, and since 2026-08-15 the whole answer: no item on either menu
    // can grey (the retired per-item disabled state's record is at the
    // definition, input_pointer.cpp).
    int  dropdown_item_at(int x, int y) const;
    // The dropdown's RELEASE body: the redesign's one act-on-release surface.
    // Returns true when the popup owned the release. It TRIGGERS THE ITEM UNDER
    // THE POINTER — CLOSE FIRST, then the menu's own action (settings: the modal
    // stop and the prefilled editor; a command menu: the item's chord through
    // on_key).
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
    // NO ROSTER DWELL RUNS UNDER A MODAL SURFACE:
    // recompute_redesign_button_hover refuses to stamp one while a prompt or a
    // keyboard-modal editor is up (the
    // rule is stated there), so a roster tooltip cannot come back under one,
    // which the per-tick recompute and the hover that stays live under modals
    // would otherwise let it do. THE MODAL'S OWN BUTTONS DO carry hints since
    // 2026-08-13 — a different surface in the same one dwell state, armed by
    // that dialog's hover walk.
    void hide_shift_tooltip();

    // THE DWELL'S ONE ARMING ROUTE, for both hover walks (the roster's and the
    // modal dialog's). Hides and re-stamps on any change of owner, keeps a
    // running dwell when the owner is unchanged, and hides when there is none.
    // The owner's two-surface encoding is at AppState::RedesignTooltip.
    void arm_tooltip_dwell(AppState::RedesignTooltip::Owner o);

    // THE CHROME ACT'S TWO HALVES (architect 2026-08-13, act-at-release — the
    // authoritative rule is kdenlive-redesign.md's act-at-release section;
    // the arm state and its contract are at AppState::ChromePress).
    // arm_redesign_press is every chord-bearing band claim's press half:
    // hit-test the painter-published rects against the chord table and, on a
    // hit, apply the button's shift / enabled / radio refusals, then ARM —
    // press-time shift carried with the arm — dispatching NOTHING. Returns
    // true when a rect claimed the press (a refusal still claims it, a refusal
    // being a consumed nothing). The two buttons
    // outside it are row 1's File and Settings, whose action is a
    // dropdown
    // toggle — the recorded press-time exception, stated at their claim (row 1
    // had a chord button, Quit, inside it until 2026-08-13, when its act became
    // the File menu's one item, and a THIRD anchor, Navigation, until its menu
    // was deleted 2026-08-15).
    // take_chrome_press consumes the arm whole (armed or not) at the top of
    // on_button_release, damaging the un-pressed face.
    // finish_chrome_press_release is the release half: re-hit the armed
    // target at the release's own coordinates and re-ask every press-time
    // gate — the modal veil FIRST, for every kind alike, then the roster's own
    // shift admission under the CARRIED shift, the enabled bit, the radio
    // rule, the Render cancel face — then run the act (the chord through
    // on_key). A lift anywhere else, or a
    // gate that no longer holds, dispatches nothing. It also owns THE SHIFT LONG PRESS: the hold
    // measured against the arm's press stamp and ORed into the one shift term
    // the chord is built from, on the shift-admitting buttons alone
    // (kChromeShiftHoldMs, app_state.h, carries the ruling).
    bool arm_redesign_press(int x, int y, GuiInputState mods);
    AppState::ChromePress take_chrome_press();
    void finish_chrome_press_release(const AppState::ChromePress& arm,
                                     int x, int y);

    // THE ARM'S THIRD BODY — the HOLD-REPEAT's firing tick (architect
    // 2026-08-16), driven from the run loop beside the hover recompute. While a
    // press stands on a `repeats` button — the bottom row's four cardinal
    // arrows — this synthesizes that button's chord on the keyboard's own
    // cadence, the first fire a hold beat after the press and the rest at the
    // compositor's advertised repeat rate. One kind compare and one integer
    // compare when idle; every firing condition lives in the body, and the
    // burst's state and authoritative edge inventory are at
    // AppState::ChromePress. Deliberately NOT gated on
    // any_pointer_gesture_active: the held button IS a live pointer act, and
    // the band claims arm no gesture that predicate names.
    void tick_chrome_press_repeat();
    // THE OVERVIEW LANE'S GESTURES (the lane rework 2026-08-12, redesigned
    // 2026-08-15 onto the box alone; the vocabulary's contract is at
    // OverviewDragState, app_state.h, and both bodies live together in
    // input_pointer.cpp).
    // run_overview_teleport: the centering an outside-the-box press runs AT
    // THE PRESS (2026-08-17) before arming the box pan (2026-08-18) — the
    // viewport centers on that column's whole-song position at the unchanged
    // zoom
    // level, a pure viewport move of the pan class through scroll_viewport's
    // funnel (follow suppression included; the exact arithmetic is at the
    // definition). apply_overview_drag_at: the one motion body for the box
    // pan and the two edge drags, X ONLY by construction (it takes no y).
    // seat_overview_edge_drag: the ONE writer of an edge drag's kind and its
    // FIXED partner bound, called by the press claim's ENDCAP hit — a bound is
    // reached through its own grab band and nowhere else since the outside-drag
    // extension's deletion (2026-08-15); false on degenerate geometry, where the
    // caller drops the arm.
    void run_overview_teleport(int x);
    void apply_overview_drag_at(int x, bool final_event);
    bool seat_overview_edge_drag(bool grabbed_begin);

    // THE STANDING REGION'S OWN GESTURES (2026-08-15 — the whole model, and why
    // it exists, are at RegionState / RegionEditDragState, app_state.h). Both
    // bodies live together in input_pointer.cpp.
    //
    // region_manipulation_hit: the ONE hit owner, and the ONE spelling of
    // "inside a live region" the whole product reads — THREE consumers, so it
    // cannot drift: the plain waveform press claim (which asks it BEFORE arming
    // the nav drag), the pointer cursor map's region arm, and THE TOUCH PAN
    // ZONE, which must answer false wherever this answers non-None or the
    // finger would become the phone-model pan and never reach the drag at all.
    // It is Y-GATED TO THE WAVEFORM RECT ALONE — the ruler and the marker lane
    // answer None even where the span covers their columns, the architect's
    // ruling and the reason a pan stays reachable while a full-width region
    // stands — and it answers None while the `h` history view stands, where the
    // press claim never reaches this arm and the view's spans are reading marks
    // rather than trim scratch (one gate, so the cue and the touch zone cannot
    // promise a gesture the router does not run).
    //
    // apply_region_edit_drag_at: the one motion body for all three kinds,
    // X ONLY by construction (it takes no y) and ABSOLUTE per event, the
    // overview box drag's own shape.
    //
    // region_edit_frame_at_column: the gesture's own column->frame conversion,
    // read by the press arm and by every motion event, ON THE DISPLAYED (PLATE)
    // BASIS — the basis the hit test and the painter share, so the span the hand
    // grabbed is the span it moves even while an async rebuild is in flight
    // (codex round 20; the full argument is at the definition, and the standing
    // rule it applies is at playhead_pixel_x, app_state.h). The caller passes
    // its own already-validated waveform rect.
    RegionHit region_manipulation_hit(int x, int y) const;
    int64_t region_edit_frame_at_column(const GuiRect& area, int mouse_x) const;
    void apply_region_edit_drag_at(int x);

    // THE TOP FLAG EDITOR'S GUARD-FREE CLOSE — the LEFT press's (a right press
    // is a consumed nothing everywhere since the button's unbinding,
    // 2026-08-12, so it closes nothing; the 2026-08-01 "both buttons" clause
    // served the right-click scrub that died with it): a press anywhere
    // OUTSIDE the
    // published editor box tears an open FlagPayload edit down without
    // committing — exactly Esc's teardown (pending dropped; Enter is the only
    // commit route, so closing is cheap and non-destructive) — and the caller
    // then goes on to act normally. Self-contained: it tests both "is an edit
    // open" and "is the press outside the box", so the caller carries no
    // guard. Inside the box the press belongs to
    // the field (caret / drag-select), so it returns having done nothing.
    void close_top_flag_editor_for_outside_press(int x, int y);

    // True when the open dropdown swallowed `key` — the popup-modal gate,
    // ranked directly under the prompt at the top of on_key. Bare Esc closes,
    // Ctrl+Q closes and falls through to the close route, everything else is
    // swallowed inert so no command can run under an open popup.
    bool dropdown_key_blocked(GuiKey key, GuiInputState mods);

    // THE ARMED CHROME PRESS, dropped — the pointer-leave / capability-loss
    // hook's clear, beside clear_redesign_button_hover: a pointer that has
    // left the window is on no button, and since the act moved to the release
    // (2026-08-13) the arm is a pending ACT, not just a face — it must not be
    // left waiting for a release that may never come. Only capability loss
    // actually guarantees no later release; after an ordinary leave one may
    // well arrive, and it finds the arm already dropped and dispatches
    // nothing, which is what makes clearing early safe rather than the events
    // being unable to come. Transition-gated on a painted face, one strip
    // damage when it fires (row 8's is its own bottom-strip lane, the
    // standing fork). The RELEASE does not come here — it consumes the arm
    // through take_chrome_press.
    void clear_redesign_button_press();

    // THE MODAL DIALOG'S ARMED BUTTON, dropped on that same edge (2026-08-13):
    // the dialog buttons act at the RELEASE, so a pointer that has left the
    // window is holding an act that must not still be waiting for a lift that
    // may never arrive. Its two siblings — the arm and the release's verdict —
    // are private, beside the modal's other pointer readers; the contract is at
    // the definition and the full edge list at AppState::modal_dialog_pressed.
    void clear_modal_dialog_press();

    // THE THREE RELEASE-TIME ARMS, DROPPED TOGETHER AT THE BUTTON-LOST EDGE
    // (codex round 20). THE FINDING IS WHY THIS EXISTS, and it is worth stating
    // before the mechanism: the touch upgrade's ABNORMAL END (round 19) ends
    // every MOTION-DRIVEN gesture correctly, because those have a button-lost
    // end — on_motion's `!primary_button_held` arms, one per drag state. THESE
    // THREE HAVE NONE. The armed chrome press, the modal dialog's armed button
    // and the dropdown's item claim are not button-lost consumers at all: they
    // are claims on a FUTURE RELEASE, and until now the only thing that dropped
    // them was the pointer-LEAVE hook. Calling the upgrade's end "the standing
    // lost-button shape" was therefore true of one family and FALSE of this
    // one — a concept reused across two families that do not share it, which is
    // exactly how a stale ChromePress came to survive a whole pinch (with a
    // mouse resting in the window the end takes its restore-motion branch, so
    // no leave hook ran) and swallow the next tap or dispatch its old command on
    // a later release over the original target.
    //
    // THE FIX GIVES THEM ONE, at the one place the edge is observable: a MOTION
    // that reports the primary button UNHELD while one of these stands. That
    // motion is the abnormal end's own delivery, so an upgrade now ends every
    // arm the vanished press could have committed — motion-driven and
    // release-time alike, because the finger that armed them is not going to
    // lift — and the mouse-focused case is no longer a different path for them.
    // A lost PHYSICAL button reaches it by the same sentence, which is the
    // point: the edge is the invariant, not the device.
    //
    // WHAT IT IS NOT: an end for anything the pointer's POSITION owns. The
    // roster's hover faces, the menu row's armed mode and the tooltip stay with
    // the leave hook, which is a different question ("where is the pointer") —
    // so that hook still calls its own three clears plus those, and this is a
    // strict subset of it rather than a replacement.
    // NOR IS IT AN END FOR THE PENDINGS, and the deferred bound-set click
    // (PendingClickAct) did NOT join it though it too acts at a
    // lift: a pending is a MOTION-DRIVEN member — it has a threshold crossing to
    // resolve and therefore an on_motion branch of its own, which carries its
    // button-lost arm exactly as every drag state's does. Adding it here would
    // be a second owner for an edge that already has one, which is the failure
    // this family's own finding was about.
    void clear_release_time_press_arms();

    // AND THE KEYBOARD'S OWN ARM, dropped on the KEYBOARD's equivalent edge —
    // the platform's keyboard-intent cancellation hook (keyboard leave,
    // keyboard-capability loss, a Super-swallowed press), wired in main.cpp,
    // which is why this one is public. Same reasoning as its pointer twin,
    // over the stream that owes the release. It is ALSO the owner of the
    // focus-move cancel since 2026-08-14 (the ring's walk, the pointer feint's
    // passive assignment, the editor act's return of the focus to the field —
    // three internal callers, each the site of a focus move): the contract is
    // at the definition and the full edge list at
    // AppState::modal_dialog_key_pressed.
    void clear_modal_dialog_key_press();

    // END every in-flight pointer gesture through its own RELEASE body — a
    // commit, never a cancel: pointer gestures have no cancel (the rule is stated
    // at the drag-modal gate in on_key). The marker drag commits its proposed
    // position with its undo entry, the trim drag keeps its live bounds and runs its
    // commit tail, the region drag rests its region, the strip / grab-pan drags
    // just end (they applied continuously), and the THREE PENDINGS disarm (the
    // third is PendingClickAct — the trim bound sets, the one deferred click
    // since 2026-08-17 — which really has committed nothing; the marker
    // pending's click already committed at its press, so only its drag and its
    // release-side seed die here — the split is stated at the definition).
    // (The tempo drag was one more body here until 2026-07-29 —
    // the whole gesture is deleted, see marker_drag.h.)
    // No-op when nothing is live. Definition beside
    // on_button_release in input_pointer.cpp (same bodies, same order). FOUR
    // CALLERS: the Ctrl+Q hatch in on_key, main.cpp's WM-close and resize
    // callbacks (close ends the gestures before raising the prompt, so none is
    // left live under it; resize ends them before the geometry rebuild, whose
    // new samples-per-pixel would otherwise make the next motion derive its
    // delta across two coordinate systems), and — since 2026-08-09 —
    // on_history_prefetch_ready's FAILED-SCAN closer, the product's one
    // asynchronous closer, which bypasses on_key's drag-modal gate and so has to
    // re-establish by hand the invariant every other closer gets from it (the
    // reasoning is at that site).
    // NO CALLER OWES THE POINTER CURSOR ANYTHING — the cue has one owner, which
    // runs at the run loop's iteration boundary, past everything a caller does
    // after this returns (the prompt goes up, the layout is rebuilt). The three
    // callers each carried a re-resolve of their own until 2026-08-03; the
    // reasoning is at the definition.
    void finalize_active_drags();

    // Arm the region-select drag at a press — THE ONE ARM, serving the ONE
    // region former's entries (shift+drag on the desk, the region hold on
    // glass — the deliberate act's two device forms since 2026-08-12, the
    // eighth glass ruling; the plain placement presses that used to arm it are
    // the pending pan now, and the one-day RULER arm with its deferred
    // dissolve is deleted). `anchor_frame` is the active-domain frame the
    // press just placed the playhead at; (x, y) is the press position for the
    // press-becomes-drag threshold. Dissolves the resting region at
    // mouse-down, so a motionless release rests nothing at all. THREE CALLERS
    // (re-derived 2026-08-12, the touch half): the LIVE former's press half
    // (place_playhead_and_arm_region — the shift press and the touch begin's
    // live arm both route through it), the `h` history view's own shift
    // former (handle_history_mode_press), and the touch begin's MODE arm
    // (begin_touch_region — the view-local recipe re-expressed at the down
    // point). The LOWER half is on the former's surface too since 2026-08-13
    // ("shift plus drag to map out a region should also be allowed in the
    // lower half, for consistency"); its PLAIN press is the pending click,
    // whose motionless release runs the one-shot scrub and leaves the region
    // alone.
    void arm_region_drag_at(int64_t anchor_frame, int x, int y);

    // THE REGION DRAG'S ONE MOTION PATH, hoisted for its two drivers
    // (2026-08-12, the touch half): on_motion's region branch (mouse motion
    // under the held button, past its own button-lost arm) and
    // update_touch_region (the platform's per-frame region hook, which never
    // has a button to lose). The whole live body: the shared Chebyshev gate
    // from the press/down point (moved latches once), the moved-drag
    // double-click-candidate clear, the far endpoint at the pointer column
    // through the click->frame basis, the span install, and the playhead
    // riding the moving end. Caller guards active.
    void apply_region_drag_motion(int mouse_x, int mouse_y);

    // THE PLACEMENT'S PLAYHEAD HALF, and the whole of what the live routes
    // and the `h` history mode's own have in common: drop the
    // playhead at the clicked column, reseek a live scanner to it (keeping the
    // session alive) and override follow for that session. NO selection, NO
    // region, NO drag arm — each caller owns those, which is what lets the mode
    // reuse this recipe without inheriting a region former it must not have.
    // FOUR ROUTES REACH IT since 2026-08-12 (re-derived 2026-08-13 at the
    // two-halves ruling, which added no route — the lower half's release runs
    // the SCRUB arm, which returns above this body): the DEFERRED CLICK ACT
    // (run_nav_click_act, its two placement arms), the two SHIFT formers'
    // presses, and
    // the touch region begin's MODE arm (its live arm rides the shift
    // former's own body).
    // `click_rel_x` is x - waveform_area.x; the gutter (click_rel_x outside
    // [0, area.w)) seats nothing and returns -1, a value no seated frame can
    // take (the clamp's floor is 0). `was_playing` / `playhead_at_entry` are
    // the caller's readings from AHEAD of its own acts — the formers
    // capture at their press/begin entry, the deferred click act reads at the
    // release
    // (equivalent, its press having touched nothing, and honest about a
    // session that ended under the hold).
    // Neither is a pre-stop reading: the playback stops are claim-keyed and sit
    // at the branches that claim a gesture, and every route reaching this body
    // is a stop-free one, so no stop stands between the capture and either
    // reader. What each parameter really predates is a write of its own —
    // playhead_at_entry predates move_playhead_to's cursor write, and
    // was_playing predates the stop that reseek_keeping_alive may run internally
    // on an out-of-range position — and together they fire the reseek only on a
    // real move of a live session.
    int64_t place_playhead_at_click_column(int click_rel_x, bool was_playing,
                                           int64_t playhead_at_entry);

    // THE REGION FORMER'S LIVE PRESS HALF — TWO call sites since the touch
    // half (2026-08-12): the shift-exact press on the navigation surface
    // (on_button_press) and the touch region begin's live arm
    // (begin_touch_region — the hold's expiry at the down point): clear
    // the marker selection, run the body above, and arm the region drag (which
    // dissolves any resting highlight at mouse-down). The clear runs ahead of
    // the body's gutter return, so an inert-gutter click still deselects but
    // seats no playhead and arms no drag. (The plain presses that shared this
    // body left it 2026-08-12 for the pending pan — the eighth glass ruling.)
    void place_playhead_and_arm_region(int click_rel_x, int x, int y,
                                       bool was_playing,
                                       int64_t playhead_at_entry);

    // ARM THE NAVIGATION SURFACE'S PLAIN PRESS — the pending click that
    // becomes the grab-pan at the 8px crossing (contract at ScrollDragState,
    // app_state.h; the deferred act below). Records the press and its three
    // surface facts and does nothing else. `scrub_release` is the waveform's
    // LOWER half (2026-08-13): the motionless release runs the audition scrub
    // instead of the placement, which is the halves' one remaining difference.
    void arm_nav_press(int x, int y, bool history, bool seed_empty_lane,
                       bool scrub_release);

    // THE CTRL ENTRY TO THE SAME ONE DRAG (2026-08-14, the live-ctrl model —
    // contract at ScrollDragState, app_state.h): arms the ordinary nav press
    // with no click act, opens it in the ZOOM phase with the pivot seated at
    // the press column, and paints the anchor stem from the press (the
    // stem-at-press ruling kept; the arm owes that first frame's damage).
    // The capture still begins at the 8px crossing — a ctrl click never
    // blinks the cursor, superseding the retired zoom drag's
    // capture-at-press.
    void arm_nav_zoom_press(int x, int y);

    // THE POINTER'S NOTIONAL COLUMN — its clamped position in waveform
    // columns, and the zoom pivot SEAT's one source (the seat converts it to
    // the song frame under it; the pivot itself is that frame). A PURE
    // PROJECTION of the platform's notional pointer position, held nowhere and
    // computed at each seat; the contract, and why exactly one position exists
    // and it is not this layer's, are at the definition (input_pointer.cpp)
    // and at GuiPlatform::notional_pointer_x_.
    double nav_notional_col() const;

    // TELL THE CAPTURED POINTER ITS WRAP SPAN — the waveform's inclusive
    // bounds, between which the hidden cursor folds edge to edge. It stays a
    // BODY with the nav drag's crossing its one caller (there were two capture
    // sites until 2026-08-15): the wrap is a property of the captured pointer
    // rather than of a gesture, so a second capture would take it verbatim (the
    // derivation and why the
    // bounds are the waveform's are at the definition, input_pointer.cpp).
    void tell_capture_wrap_span() const;

    // THE ZOOM STEM'S COLUMN X — the anchor's live column in the waveform's
    // bounds, as that column's ORIGIN in surface coordinates and NOT a pixel
    // centre. ONE OWNER for the zoom body's per-event restore stamp and the
    // ctrl-up handover that gives the pointer's notional position that same
    // column, so the column the cursor is sent to cannot drift from the one
    // the stem was stamped at. What is shared is the COLUMN; the PIXEL
    // CONVENTION is each consumer's own (the restore adds the +0.5 a cursor
    // wants, the handover takes the bare coordinate a pointer position wants)
    // — that split, and why this reads the LIVE viewport where the painter
    // reads the displayed basis, are at the definition.
    double nav_stem_column_x() const;

    // THE NAV DRAG'S ZOOM PHASE, one event: dx off the live level
    // (kNavZoomPxPerLevel, RIGHT zooming in — the pinch's own sign, the
    // superseded pan-derived one at the contract) through
    // Viewport::apply_strip_drag_zoom about the seated pivot, dy discarded —
    // the same axis the pan phase reads, the modifier deciding what horizontal
    // travel means — while the POINTER'S OWN x is frozen for the phase's whole
    // life by set_strip_capture_notional_x_frozen above, so the level spends
    // that travel and the position does not spend it twice: two statements,
    // not one. The capture's restore x is driven to the stem each event.
    // Defined in input_pointer.cpp.
    void apply_nav_zoom_at(int x, int y, bool final_event);

    // THE DEFERRED CLICK ACT — the motionless navigation-surface release's
    // whole body, running THE PRESSED HALF'S OWN ACT: the audition scrub
    // (lower half), or deselect (live) / mode-focus clear (`h` view), region
    // dissolve, then the placement above at the press column, playback state
    // read at the act. The full contract is at the definition
    // (input_pointer.cpp).
    void run_nav_click_act(int press_x, bool history, bool scrub_release);

    // THE MARKER CLICK ACT, AT THE PRESS (2026-08-17) — stop, the three-way
    // selection fork, the land, the region clear, the PLAIN shape's
    // double-click consume-open (against the press-time candidate snapshot,
    // its three gates read live), and the plain arm of PendingMarkerPress for
    // the drag the press may become and the seed its motionless release owes
    // (a consumed open arms nothing). TWO call sites, both in
    // on_button_press's marker claims: the ctrl toggle branch and the
    // plain / shift branch. Contract at PendingMarkerPress (app_state.h),
    // reasoning at the definition (input_pointer.cpp).
    void run_marker_click_act(int hit, int x, int y, bool shift, bool ctrl,
                              const DoubleClickCandidate& dc_at_press);

    // THE ONE SURVIVING DEFERRED CLICK — the trim bar's ctrl (begin) /
    // ctrl+shift (end) bound set (2026-08-17: its press IS the endcap drag's
    // arm, the one genuinely ambiguous click; the record's other four kinds
    // went back to the press with that ruling). The arm writes PendingClickAct
    // and NOTHING ELSE; the act is taken BY VALUE so the caller can disarm
    // before running it, the release bodies' standing shape. Every gate the
    // set meets is re-asked inside it, live at the lift.
    // Contract at PendingClickAct (app_state.h), reasoning at the
    // definitions (input_pointer.cpp).
    void arm_pending_click_act(int x, int y, bool is_begin);
    void run_pending_click_act(PendingClickAct press);

    // The empty marker-lane double-click marker CREATE, run at the SECOND
    // PRESS (2026-08-17) from the lane's consume in on_button_press: the
    // bare-`s`
    // drop equivalent at the clicked column — the AUGMENTED drop in both
    // columns, exactly as bare `s` performs it, the drop's own single-select
    // and playhead land included (create + select + land, the eighth glass
    // ruling's words) — home-view and read-only gated silently.
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
    // editor, the load editor, the commit-title editor, or the top-strip flag
    // editor in EITHER kind (unlike modal_dialog_editor_active, which names the
    // four DIALOG-hosted surfaces — those first three plus the flag editor's
    // BpmBracket kind — and omits the FlagPayload kind). The platform's
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
    // probe lambda calls it. ONE THING REPEATS ON A MODAL SURFACE, the focus
    // ring's Tab walk (architect 2026-08-13) — on a prompt and under a dialog
    // editor alike, both surfaces having a ring; every OTHER key a prompt
    // could answer with stays one-shot, the definition's ring arm saying why.
    bool repeat_eligible(GuiKey key, GuiInputState mods) const;

    // Per-iteration promotion check for the archival status message, wired from
    // main.cpp's on_tick beside the preview label's own tick (the reason the
    // tick is the observer is stated at both sites). The message is composed and
    // PARKED at dispatch and written to the status slot only once the worker
    // reports that synthesis actually began, so a render served by one of
    // do_render's reuse rungs says nothing at all. Cheap: one empty-string test
    // per tick when nothing is parked.
    void tick_promote_render_status();

    // Per-iteration transition writer for the Render button's mid-render
    // CANCEL face (AppState::render_cancel_face — the contract is at the bit),
    // wired from main.cpp's on_tick beside tick_promote_render_status. One
    // bool compare per tick at rest.
    void tick_render_cancel_face();

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
    // gate; it is now fired by Enter in the BPM editor (after
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

    // Shared wheel handler for source and target view; on_wheel is its only
    // caller. Exact-match modifiers, TWO arms (2026-08-12): PLAIN = the stepped
    // pan (a tenth of the visible span per detent, through the scroll_viewport
    // funnel) and CTRL = the zoom step (one whole level per detent, up = in,
    // through Viewport::zoom_steps — the `=`/`-` commands' own coalesced body).
    // Every other combination — Alt+wheel, Shift+wheel and every mixed pair —
    // no-ops. `inside_waveform` is true over the waveform area or the overview
    // lane, `inside_top` anywhere over the top strip; both mean "a wheel-live
    // navigation surface" and neither forks the vocabulary.
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
    // write in run_marker_click_act — same two-step placement basis, but NO
    // viewport move). Both leave the playhead coincident with the focus, and a later
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

    // P / I / M / K / L letter-key handlers: Ctrl+P-family phase-reset
    // clipboard ops, `p` view toggle, `i` iteration mode, `m` bpm mode, `k`
    // ADD TO SELECTION (the sticky ctrl, 2026-08-18) and `l` listen-to-
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
    // `autocomplete` is the ONLY OPTIONAL hook — the FORWARD-Tab one (the
    // reverse walk never completes), passed by
    // the SETTINGS and LOAD editors and returning whether it ADVANCED the
    // buffer, which is what decides between consuming the key and letting it
    // walk the focus ring (THE ONE AUTOCOMPLETE MODEL, architect 2026-08-13,
    // stated in full at the definition). The commit-title, bpm and flag editors
    // have nothing to complete and pass an empty hook; for the flag editor bare
    // Tab never arrives at all, the on_key gate swallowing it before this route
    // sees it. Every OTHER hook is REQUIRED and called unmodified: commit /
    // cancel / Ctrl+Q teardown are the per-editor bodies, and `repaint` is the
    // editor's own damage for a text change — the four dialog surfaces
    // pass the modal's own owner (the bottom row's lane,
    // viewport.cpp), the flag editor the top strip. `repaint` is
    // invoked UNCONDITIONALLY on every consumed key, so an empty std::function
    // there would throw; the route carries no null check for it deliberately (a
    // caller that forgets it is a program bug, not a runtime condition to guard).
    bool route_modal_editor_key(text_editor::State& ed, GuiKey key,
                                GuiInputState mods,
                                const std::function<bool()>& autocomplete,
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
    // route_modal_editor_key, passing GuiSettingsEditor::autocomplete_value as
    // the bare-Tab hook: the value completion is Tab's under the one
    // autocomplete model (architect 2026-08-13 — completion first, the focus
    // ring when it did not advance; the typed-`=` trigger it wore for part of
    // that day is reverted with the ruling).
    bool handle_settings_editor_key(GuiKey key, GuiInputState mods);

    // Load prompt (bare `'`). A dialog modal editor, structural
    // sibling of the settings editor: it takes a render entry's identifier
    // relative to renders/ and, on Enter, loads that render's frozen sidecar
    // recipe in place as the new authoring baseline through
    // load_render_entry_in_place.
    //
    // THE `h` HISTORY MODE GIVES THE SAME EDITOR TWO MORE SUBJECTS, one per walk
    // (2026-08-04 for the commit, 2026-08-08 for the local member): on the REMOTE
    // tab it takes a COMMIT SPELLING, opens prefilled with the viewed commit's
    // SHA and loads it through load_history_commit_in_place; on the LOCAL tab it
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
    // bare-Tab longest-common-prefix completion over the entry identifiers,
    // returning whether it advanced the buffer (the one autocomplete model, at
    // route_modal_editor_key); in the mode it speaks neither vocabulary, so it
    // answers false and the Tab walks the dialog's focus ring instead.
    // load_editor_commit: resolve the pending — to exactly one render entry,
    // or in the mode to a commit — and load it in place.
    // load_editor_exit_no_commit: Esc / Ctrl+Q teardown. handle_load_editor_key:
    // the key router, through route_modal_editor_key like the settings editor.
    void open_load_editor();
    bool load_editor_autocomplete();
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
    // commit title being free text — so bare Tab walks the dialog's focus ring
    // from the first press, as it does in the bpm editor.
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
    // (that arm moved to Shift+X) and it never SHOWS a region either — the
    // no-region arm SEEDED one for a day (2026-08-15..16) and the architect
    // split that act off onto Ctrl+Shift+X because one key had come to mean two
    // unrelated things, show and commit; the seed is
    // handle_show_region's now, so "NO region" is a refusal again exactly
    // as it was before that day.
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

    // Ctrl+Shift+X SHOWS THE REGION (architect 2026-08-16) — the icon row's
    // IconShowRegion button and its keyboard twin. ONE ACT: make sure a region
    // exists (seeding one at the current trim window's two bounds if none
    // stands) and BRING IT INTO VIEW through bring_span_into_view. It CLEARS
    // NOTHING and it is deliberately NOT a toggle — a lamp over the region's
    // existence would strand a scrolled-away span behind a lit button whose
    // only press cleared it, which is the architect's own reason and is
    // recorded at the roster entry (app_state.h) and at the definition. It
    // writes NO TRIM and no authored content, which is why it is
    // read-only-LEGAL like `x` itself (its entry is on read_only_key_blocked's
    // allowlist) and why the `h` view simply consumes it (not on that mode's
    // allowlist — the derived partition greys the button with nothing
    // hand-listed). The seed half is `x`'s own 2026-08-15 arm moved WHOLE,
    // ruling and all; the full record is at the definition, input_trim.cpp.
    void handle_show_region();

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
    // (auto_clear_crossed_trim, the waveform + status-chain repaints, the
    // target-render trigger, then the playhead park above), and this member is
    // their one spelling. FOUR
    // CALLERS — `x`'s set-from-region (handle_trim_x), the drag release
    // (commit_trim_drag) and the bound-set click (set_trim_bound_at_click), all
    // input_trim.cpp, plus the settings editor's `:trim_*=` active-tab arm
    // (settings_editor.cpp, reaching it through the friendship above; its
    // status-chain repaint rides applied() as well, harmlessly twice).
    // Callers own everything around it: their refusals, the playback stop and
    // the setter's deselect, which differ per route by
    // ruling. ONE DELIBERATE NON-CALLER, a different tail by design:
    // handle_trim_clear_both (the Shift+X maximizer resets rather than
    // auto-clears — a non-setter), which calls the park directly instead.
    void commit_trim_mutation();

    // Plain trim-bar press routing — the PLAIN press's route into a trim
    // drag, and ONE OF TWO since the bound-set clicks came back 2026-08-01 (the
    // other is set_trim_bound_at_click_then_arm_drag, run at the bound set's
    // threshold CROSSING since 2026-08-15, which arms the same
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
    // bridge span). The band's one caller CONSUMES the press either way — a
    // false return is the band's consumed-nothing click (nothing below the
    // band ever sees the press), never a fall-through.
    // Read-only no longer refuses anywhere on this route
    // (2026-08-07). Trim drags are SETTERS, so they DESELECT
    // and STOP a live audition at their first ACCEPTED bound change (the press
    // carries neither since 2026-07-30 — a trim-bar press that never becomes a
    // drag is a consumed nothing); the PLAYHEAD is what they never touch.
    // (THE TOUCH HOOK BODIES ARE NOT HERE: apply_touch_nav_update,
    // end_touch_nav, the pan-zone query and the region trio
    // (begin/update/end_touch_region) are PUBLIC entry points —
    // main.cpp's hook wiring calls
    // them, like on_key and on_motion — declared beside those siblings above;
    // the nav implementation lives beside the strip drag's in
    // input_pointer.cpp, whose application chokepoint it shares, and the
    // region bodies beside the former's own press half there.)

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
    // READ-ONLY DOES NOT REFUSE (architect 2026-08-07 — trim is band, not
    // authored content; the gate this line used to name was deleted with that
    // reclassification, and the stale claim is corrected here 2026-08-15). OWNS
    // the click's playback
    // stop, placed past those refusals and just ahead of the bound write, so the
    // ctrl / ctrl+shift press carries none of its own and a refused click leaves
    // a live audition playing. RETURNS whether a bound was written (the wrapper's
    // arm gate). is_begin picks the bound: ctrl sets begin, ctrl+shift sets end.
    // IT RUNS AT THE LIFT (2026-08-15, the act-at-lift sweep): the press arms
    // PendingClickAct, the motionless lift calls this at the PRESS column, and a
    // crossing calls it and hands over to the drag. The body is unchanged.
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

    // The ctrl / ctrl+shift trim-bar bound set's THRESHOLD CROSSING (a PRESS
    // until 2026-08-15, when the set moved to the lift): sets the bound at the
    // PRESS column (set_trim_bound_at_click, above) AND arms the EXISTING
    // single-bound trim drag on it through arm_pending_trim_drag — the same
    // pending an ENDCAP press arms, so the gesture continues under the drag's own
    // unchanged rules and its outcome is byte-for-byte the press-time model's.
    // NOTHING is stashed: the click-set is committed when made
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
    // the one-shot scrub body (scrub_press_at).
    void scrub_act_at(int64_t frame);

    // The scanner scrub body. ONE CALLER since 2026-08-13: the DEFERRED CLICK
    // ACT's scrub arm (run_nav_click_act), reached by a MOTIONLESS RELEASE of a
    // plain lower-half waveform press. IT USED TO RUN AT THE PRESS and the
    // architect moved it to the lift — "the playhead scrub is an outlier. We do
    // everything on lift the finger or on mouse up, but the playhead scrub, we
    // do right on mouse down. We should remove that" — which is what let the
    // lower half take the grab-pan and the region former. (The BARE RIGHT
    // full-height entry of 2026-08-01 died 2026-08-12 with the right button's
    // unbinding; the marker-text lane's empty-spot scrub was DELETED
    // 2026-07-27.) Given the click's waveform-relative column, run ONE scrub
    // act (scrub_act_at — stop a live session, else launch) at that column's
    // frame — the scrub is ONE-SHOT per click (architect 2026-07-23, the
    // Ableton model): the press arms only the pending click, a held press does
    // nothing further, and a drag past the threshold replaces the act with the
    // pan, so each click pays AT MOST one stop quiescence fence and a stopped
    // session's launch pays none. A gutter/invalid column
    // (outside [0, area.w)) is a silent no-op (no launch position). Touches
    // NOTHING else — no selection, region, cursor, follow, or double-click seed.
    // THAT is what makes it the REGION'S PREVIEW GESTURE (architect 2026-07-30,
    // Q2): clicking inside a resting scratch span auditions from the clicked
    // frame and leaves the span standing, which is why Space no longer carries a
    // region launch of its own. It is also THE HALVES' ONE DIFFERENCE — read
    // honestly, two: the upper half's act deselects, dissolves the region and
    // overrides follow, and this one does none of the three.
    // Playback stays alive from the press to the act (the press claims nothing
    // and stops nothing, and the drag-modal gate swallows every chord while the
    // pending stands), so the act sees the LIVE session — load-bearing for the
    // stop-then-start ruling: a route that let the session die first would turn
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
    // THE ZONES, each with the press branch it is taken from (re-derived
    // 2026-08-13 under THE TWO-HALVES RULING — the NAVIGATION SURFACE is the
    // WHOLE waveform + the ruler + the marker lane's empty stretches, in every
    // view, read from its one geometry owner point_on_nav_surface):
    // - Pan: the NAVIGATION SURFACE, plain — the cue promises the drag, which
    //   is the grab-pan there now; the motionless click needs no cue, no click
    //   anywhere carrying one, and BOTH halves' click acts (the placement and
    //   the scrub) are clicks. "The hand shows up in both the top and the
    //   bottom half" (architect 2026-08-13).
    // - (Scrub: DELETED with this ruling, the kind and its `crosshair` name
    //   with it — "we need to just get rid of the crosshairs but retain the
    //   scrub action". The lower half's audition is still there; it is a click
    //   act now, and the drag the cue must promise is the pan.)
    // - Zoom: the NAVIGATION SURFACE, CTRL-exact — since 2026-08-14 the ONE
    //   NAV DRAG'S ZOOM MODIFIER (the same drag the Pan row promises, ctrl
    //   live mid-gesture; the surface always covered both halves, the lanes
    //   joined it with the eighth glass ruling, so plain and ctrl name the
    //   same rect and the hover pair reads as the drag's two phases; a ctrl
    //   press on a FLAG is the membership toggle, no cue).
    //   Live in the `h` view too — the zoom is its admitted navigation.
    //   IT IS THE KIND'S ONE SURFACE: the OVERVIEW STRIP carried a second
    //   ctrl-exact Zoom arm from the lane rework (2026-08-12, "require ctrl on
    //   zoom strip also") until the redesign of 2026-08-15 DELETED the lane's
    //   dual-axis strip drag whole, so ctrl there binds nothing and answers the
    //   Arrow with every other modifier (the ctrl+WHEEL zoom step is still
    //   live on the lane and is deliberately uncued — this map answers what a
    //   PRESS would do, and no wheel is cued anywhere).
    // - TrimResize: the trim bar's inter-cap BRIDGE, plain — the pair drag,
    //   which moves BOTH bounds together — AND THE OVERVIEW BOX'S INTERIOR,
    //   plain (the lane rework): the drag there is the
    //   box-follows-pointer PAN, a move-the-whole-span gesture, "left/right
    //   arrows like on plain trim hover" (the architect's words) — AND THE
    //   LANE'S WHOLE PLAIN SURFACE with it, since an OUTSIDE press teleports
    //   and then arms that same pan (2026-08-18), so the cue is the same
    //   promise everywhere off the endcaps. It names the DRAG, not the
    //   teleport the press also runs, the marker flag box's own rule. (It was
    //   the ARROW outside the box from codex round 19 until then, under the
    //   standing rule that a point arming nothing shows the Arrow.) A LIVE
    //   box pan KEEPS the cue since 2026-08-13, the edge drags' own rule
    //   (below) grown by one member, and the band-wide hover answer is what
    //   keeps an outside press from changing cue at its own crossing.
    //   AND EVERY MARKER FLAG BOX, plain (architect 2026-08-13): markers move
    //   SIDE TO SIDE, the bridge's own promise, and the flag box is the
    //   marker's one pointer surface in every view since stems went
    //   pointer-inert. It names the SURFACE rather than one press branch — the
    //   plain drag is the flag drag in a live view at home, while an off-home
    //   flag, a locked tab and the `h` view's click-only diff flags wear it
    //   too — through the press path's own hit_test_flag, so the live lane and
    //   the diff lane answer on one term.
    // - TrimBoundBegin / TrimBoundEnd: EXTENDING ONE BOUNDARY, in the routes
    //   that do it — the trim bar's BEGIN / END endcap on a plain hover (the
    //   single-bound drags), the bound-set clicks that write the same two
    //   bounds (ctrl for begin, ctrl+shift for end), AND THE OVERVIEW BOX'S
    //   OWN ENDCAPS on a plain hover (hit_test_overview_endcap — the box
    //   outline's left/right edges, which extend ONE viewport bound; the
    //   endcap claim outranks the lane's pan, and a LIVE edge drag keeps its
    //   cue for its whole life, the trim exception's rule). Every one of
    //   those arms a single-bound drag, so the cue is one shape for one act.
    // - Text: EDITABLE TEXT UNDER THE POINTER, which in this product is two
    //   published rects and nothing else (2026-08-13) — the open top-strip
    //   FLAG EDITOR's unrolled box (which wore the nav surface's Pan before
    //   the kind existed) and the MODAL DIALOG's inset FIELD. Both are the
    //   click-to-caret / text-drag claim regions the press path reads, so the
    //   cue is that claim's own shape. The field's arm is THE VEIL'S ONE
    //   EXCEPTION, stated at the gate below; the flag editor's sits ABOVE the
    //   modifier arms because its press claim does.
    // - Arrow: everything else — the button rows, the gap band, and every
    //   modified press with no claim (SHIFT deliberately unnamed: it is the
    //   region former, which carries no cue — the deferred placement's own
    //   model; ALT unnamed because its pointer vocabulary is empty). The FLAG
    //   BOXES left this list 2026-08-13 for the TrimResize arm above.
    // THE TRIM BAR'S THREE ZONES READ THE ROUTER'S OWN OWNERS and re-derive
    // nothing: hit_test_trim_endcap and point_in_trim_bridge_span for the plain
    // hover (exactly what route_trim_bar_press calls, in its order), and
    // trim_bound_click_frame for the two ctrl clicks (exactly what
    // set_trim_bound_at_click decides on). So a point on the band that would arm
    // NOTHING — the bar's outside on a trimmed-in window, or a ctrl click the
    // STRICTLY-INSIDE guard would consume — shows the Arrow, and the cue cannot
    // drift from the gesture because there is no second copy to drift.
    // THE TWO CTRL CLICKS ASK THAT SAME DECIDER UNDER A HELD PRESS TOO
    // (2026-08-15, when the bound set moved to the lift): the press arms
    // PendingClickAct unconditionally, so the live-gesture arm reads the decider
    // at the pending's PRESS column before keeping the bound's cue — the same
    // question, asked once more where the arm can no longer answer it, so a
    // press the guard will refuse keeps the Arrow for its whole hold.
    // ALL THREE ARE MODE-SCOPED, and per zone rather than per band (2026-08-05):
    // the `h` history view consumes the endcap/bridge drags and both ctrl clicks,
    // so those three cues go while it stands and the whole band answers Arrow
    // there. (That answer was the LOCKED TAB'S too until 2026-08-07, when trim
    // became read-only-legal; the view is the only zone consumer now.) The one gesture the view DOES give
    // that band is a DOUBLE-click (its diff-span framing), and a double-click
    // carries no cue anywhere in the product, the live band's own span framing
    // included. (The mode-scoped SCRUB ZONE this paragraph used to carry died
    // with the kind itself, 2026-08-13: there is no crosshair anywhere to
    // scope, in the view or out of it.)
    // THE MODIFIER ARMS OUTRANK THE PLAIN ZONES ON THE WAVEFORM, exactly as the
    // press path ranks them: ctrl held means the zoom drag, not the pan.
    // SHIFT IS NOT IN THE MAP over any surface — it is
    // the REGION FORMER (the one mouse region gesture since 2026-08-12, whose
    // surface grew to the lower half 2026-08-13), which
    // carries no cue, so shift
    // takes the Arrow like everything unnamed; the one place a shift
    // combination IS named is ctrl+shift on
    // the trim bar, which is a real bound-set claim rather than an unbound stray.
    // ALT IS NOT IN THE MAP EITHER (2026-08-12): its pointer vocabulary is
    // empty, so an alt hover answers Arrow everywhere.
    //
    // READ-ONLY IS NOT IN THIS MAP AT ALL SINCE 2026-08-07, and the change is a
    // deletion rather than a move: read-only protects the authored musical
    // content, trim is BAND, and so every zone this map answers — the strip
    // drag, the pan, the endcap and bridge drags, the two ctrl
    // bound-set clicks — runs unrefused in a locked tab. The per-zone read-only
    // record that stood here (navigation live, TRIM refusing through the band
    // gate, the two ctrl cues through trim_bound_click_frame's first gate) is
    // RETIRED with those two gates; the `h` history view is the sole per-zone
    // consumer left, above. Neither this map nor its callers test the bit.
    //
    // WHAT IT IS BLIND TO, deliberately and by ruling:
    // - The FLAG editor does not refuse — it is pointer-transparent by ruling, so
    //   a scrub still acts under an open one and the cursor must not lie about
    //   that. Its own BOX is the exception and not a refusal: that rect takes
    //   the caret press, so it answers Text (above) while everything around it
    //   answers whatever the surface under the editor would. The four DIALOG
    //   modal editors DO refuse, because their veil really
    //   does swallow the press (modal_dialog_editor_active) — with the FIELD
    //   the one rect inside that veil which takes an act, and so the one thing
    //   the veil's blanket names rather than blanks.
    //
    // THE CUES ARE HOVER-ONLY WITH NAMED EXCEPTIONS, all of them the same
    // exception (architect 2026-08-03, grown twice since): a LIVE GESTURE WHOSE
    // DRAGGED THING IS THE THING THE CURSOR NAMES owns the cursor for as long
    // as it lasts. THE FIRST AND THE MODEL FOR THE OTHERS IS TRIM —
    // pending or past the threshold; an endcap drag, the
    // bridge drag, or a ctrl bound-set's armed drag — OWNS the cursor for as
    // long as it lasts, wherever the pointer is: a begin-bound drag keeps
    // TrimBoundBegin, an end-bound drag TrimBoundEnd, the bridge TrimResize.
    // The kind is read from the drag's own record of what it grabbed, never
    // re-derived from the pointer's position, so the cue neither flickers as
    // the pointer leaves the band nor reverts to the Arrow mid-drag. Trim can
    // be the one exception because on this gesture alone the thing being
    // dragged is the thing the cursor names, so the cue stays true throughout.
    // THE OVERVIEW BOX'S THREE DRAGS ARE THE SAME EXCEPTION, on the same
    // reasoning: the two EDGE drags joined at the lane rework (2026-08-12) and
    // THE BOX PAN 2026-08-13 (architect), so all three keep the kind their own
    // record names — TrimBoundBegin / TrimBoundEnd / TrimResize — for the
    // gesture's life. They can, for the same reason trim can: the box is the
    // thing being dragged and the cue names it.
    // THE MARKER REPOSITION DRAG JOINED THEM 2026-08-14 (architect: "it should
    // remain left/right arrows during the drag, like trim and overview drag
    // currently do"), same shape, same reasoning — the flag box wears
    // TrimResize at rest because a marker slides side to side, and the drag is
    // that slide, so the cue stays true from the press to the release. It has
    // exactly ONE shape, so the record is that it is live (drag or pending
    // marker drag) and there is no kind to read.
    // EVERY OTHER gesture keeps the uniform refusal — no cursor changes during
    // the region, strip or grab-pan drags (the captured two hide the cursor
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
    // is the waveform's one-shot CLICK ACT (scrub_act_at / scrub_press_at — the
    // lower half's motionless release, its one entry),
    // a pointer act that starts or stops a scanner and
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
    // EVERY editor does — the three single-State dialog ones (settings, load,
    // commit title), the bpm bracket, and the
    // top-strip FlagPayload flag editor, which this ruling brought in, reversing
    // the old "commands punch through" design and deleting the tail that
    // discarded an edit on the way to a command.
    // THREE READERS, re-derived 2026-08-12 (the touch half): the on_key gate
    // (input_handler.cpp), paired with modal_editor_key_blocked, the
    // roster hover walk's no-dwell term (recompute_redesign_button_hover,
    // input_pointer.cpp — no shift-tooltip dwell runs under a surface that
    // owns the keyboard), and the touch region begin's gate
    // (begin_touch_region, input_pointer.cpp — the gesture bypasses the
    // press path that closes the flag editor, so it refuses under every
    // editor; the same reading the dead trim-move begin briefly held,
    // 2026-08-11, whose gesture died 2026-08-12 — the region hold revived
    // the pattern, not the trim move).
    // Modality here is CHORDS only, which is why the flag editor's OTHER
    // transparencies do not consult this predicate — see
    // modal_dialog_editor_active below for what does and does not.
    bool keyboard_modal_editor_active() const;

    // Modal-editor predicate + key gate (bodies in input_key_dispatch.cpp).
    // THIS DECLARATION IS THE AUTHORITATIVE STATEMENT of what
    // modal_dialog_editor_active is for; other sites carry a pointer here.
    // It names the DIALOG-HOSTED modal editors — the settings editor, the
    // load editor, the commit-title editor and the bpm bracket editor (plus
    // the prompts, gated separately), the four surfaces that paint in the
    // MODAL since 2026-08-12 — on the BOTTOM ROW since 2026-08-13, which is
    // where the name came from in the first place (it was
    // modal_bottom_strip_editor_active while they wrote onto the status lane;
    // the MEANING — this exact four-editor set — has never moved, and the
    // surface is a modal that yields the whole row now rather than a tenant
    // of its status span). THE FOUR-EDITOR MEMBERSHIP ITSELF LIVES AT
    // AppState::dialog_editor_session (app_state.h), which names them once and
    // hands back the live one's session id; this predicate is that id being
    // non-zero, so the set cannot drift between the two. EIGHT CALLING
    // FUNCTIONS, RE-DERIVED BY GREP 2026-08-14 (two left that day with the
    // round-15 session fix — the dialog BUTTON claim in on_button_press and
    // dispatch_modal_dialog_button, both of which ask
    // modal_dialog_stash_current instead, a strictly narrower question that
    // implies this one): SIX ask about a POINTER fact and TWO about a KEY.
    //   wheel_context's swallow (input_handler.cpp), because the wheel's
    //     stepped pan is NAVIGATION, not a chord, so it still punches through
    //     an open top-strip flag editor;
    //   pointer_cursor_kind (2026-08-03), because these four editors are
    //     exactly the ones whose veil SWALLOWS a pointer press, so they are
    //     exactly the ones over which no cursor may promise a gesture;
    //   the dialog button claim's RELEASE MIRROR in on_button_release
    //     (2026-08-13 — the buttons act at the lift) and on_motion's
    //     dialog-hover branch (2026-08-12) — the modal's own two pointer
    //     surfaces;
    //   the CHROME release's veil re-ask (finish_chrome_press_release,
    //     2026-08-13), which since the reach-through's retirement refuses
    //     every armed kind outright — one term above the kind switch — and
    //     exists for the editor OPENED MID-HOLD;
    //   the roster hover walk's veil term (recompute_redesign_button_hover —
    //     under an editor dialog nothing hovers, the prompt's own answer);
    //   and on the KEYBOARD side, modal_editor_key_blocked's bare-Tab
    //     admission (2026-08-13: the focus ring's Tab is admitted for exactly
    //     these four, the flag editor publishing no dialog and so having no
    //     ring to walk) and repeat_eligible's two arms (the ring walk repeats,
    //     and nothing repeats once the focus is on a button).
    // paint_modal_dialog's editor fork is a reader BY PROXY and not a caller
    // (it reads the same four is_active tests in the same order).
    // (THE MODAL-TRAP block at on_button_press's top was an eleventh from
    // 2026-08-11 and is deleted; its record is at the retired predicate's site,
    // input_pointer.cpp.)
    // The flag editor's exemption is the same fact in all of them: it is
    // pointer-transparent, so the wheel reaches the viewport under it, a
    // waveform press reaches the audio under it, and its roster presses were
    // never blocked to begin with.
    // IT IS NOT A PLAYBACK-STOP PREDICATE and never was one in code. The stop is
    // not decided here — but it is no longer scattered either: since 2026-07-28
    // it has ONE owner, GuiPlaybackLifecycle::stop_playback_for_modal_open, which
    // every open site calls and which records the whole decision table (the four
    // dialog editors and the prompts stop; the top-strip flag editor is
    // explicitly EXEMPT and keeps a live audition playing). So a new modal
    // surface inherits the wheel swallow from this predicate and its playback
    // answer from that owner — it grows neither by hand.
    // The gate is the sibling of read_only_key_blocked's allowlist shape: true
    // when key+mods should be dropped while a keyboard-modal editor is open
    // (admits only the keys the active editor consumes, bare Esc, Ctrl+S, and
    // Ctrl+Q). It serves all five editors, top strip included.
    bool modal_dialog_editor_active() const;
    bool modal_editor_key_blocked(GuiKey key, GuiInputState mods);

    // THE MODAL DIALOG'S POINTER HALF (2026-08-12; bodies in
    // input_pointer.cpp — the painter's stash is AppState::modal_dialog and
    // the veil contract lives at on_button_press's two dialog gates):
    // the button hit test over the stash, the hover-face writer the motion
    // branches call, and the editor dialog's OK/Cancel dispatch — the
    // session's own Enter/Esc through the per-editor key routes,
    // button-is-its-chord.
    int  modal_dialog_button_hit(int x, int y) const;
    void update_modal_dialog_hover(int x, int y);
    void dispatch_modal_dialog_editor_act(bool ok);

    // THE DIALOG BUTTONS ACT AT THE RELEASE (architect 2026-08-13,
    // "everything else acts on lift"), which is what these three own — the
    // arm, its hard end, and the lift's verdict. The arm itself is
    // AppState::modal_dialog_pressed, whose declaration carries the whole edge
    // list and the reason it is not the roster's own arm (AppState::ChromePress).
    //   arm_modal_dialog_press    — press: arm the hit button and paint it,
    //                               dispatching nothing. True iff one was hit.
    //   take_modal_dialog_release — release: consume the arm and return the
    //                               button the lift LANDED on if it is the one
    //                               armed, else -1.
    //   clear_modal_dialog_press  — the pointer-leave / capability-loss edge
    //                               (main.cpp's hook, beside the roster's own
    //                               clear; PUBLIC for that one caller, like
    //                               the roster's own clear beside it).
    //   dispatch_modal_dialog_button — THE ACT, shared by both pointer release
    //                               arms and the KEYBOARD's own release: it
    //                               owns the gate pair (the painted bit and
    //                               the stash's identity) and the live-
    //                               response-set validation, so the three
    //                               lifts cannot drift. True iff it
    //                               dispatched.
    bool arm_modal_dialog_press(int x, int y);
    int  take_modal_dialog_release(int x, int y);
    bool dispatch_modal_dialog_button(int index);

    // IS THE PUBLISHED STASH THE LIVE SURFACE'S — the ONE comparison behind
    // "published geometry may only SELECT; live state DECIDES" (the doctrine,
    // the two identity fields and what each answers are at
    // AppState::ModalDialogGeometry). Every site that reads the stash to ACT
    // asks this and no site spells it twice: the two press claims in
    // on_button_press, route_modal_dialog_focus_key, and the shared act above.
    // Its companion returns the focus ring's index only while it holds, which
    // is what stops a stale index swallowing a freshly opened editor's keys in
    // the one dispatch batch before that editor paints.
    bool modal_dialog_stash_current() const;
    int  modal_dialog_focus_live() const;

    // THE MODAL'S KEYBOARD FOCUS RING, one route for both surfaces (2026-08-13;
    // the state and the two meanings of its -1 are at
    // AppState::modal_dialog_focus, the navigation rules at the definition,
    // input_key_dispatch.cpp). Called by the prompt gate (input_handler.cpp)
    // and by route_modal_editor_key, and returns true when it consumed the
    // key. It takes no statement about the field's own Tab: an editor with an
    // autocomplete gets FIRST REFUSAL on the FORWARD key upstream, and by the
    // time a Tab reaches this route the completion has already declined it (the
    // one autocomplete model, at route_modal_editor_key). The REVERSE walk —
    // Shift+Tab and IsoLeftTab, the live marker cycle's spellings through the
    // one predicate modal_ring_tab_shape — is never offered to a completion at
    // all and is the ring's own one exception to bare-exactness, on the
    // strict-modifier rule's own untightened-families precedent (cited at the
    // definition). BARE ENTER AND BARE SPACE ARE THE RING'S TWO ACT KEYS since
    // 2026-08-13: with the focus on a BUTTON they press it DOWN and the act
    // runs at the key's release (on_key_release, public above); with the focus
    // in the FIELD they never reach this route and keep their old meanings.
    bool route_modal_dialog_focus_key(GuiKey key, GuiInputState mods);

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
    //     stands. The redesigned buttons and the File menu's one item reach it
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
    //     press clears the shared field before this is reached. IT ACTS AT THE
    //     PRESS for the framing and for all three diff-flag clicks (2026-08-17,
    //     reverting the one-day lift deferral: the mode has no drag for any of
    //     them to become): the two bodies below are called from this router's
    //     own flag claims.
    //   * focus_history_diff_flag is the PLAIN focus click's body — the flag
    //     box in the lane, the flag's one pointer surface (its waveform STEM
    //     surface died with the stems-inert ruling, 2026-08-12). It
    //     clears the mode's multi-selection: a plain click replaces it.
    //   * select_history_diff_flags_modified is the SHIFT and CTRL clicks' body,
    //     over the MARKER LANE ALONE — the range extend and the membership
    //     toggle, both then focusing the clicked flag and landing on it (over
    //     the waveform a modifier names a gesture, not a selection — the
    //     2026-08-06 symmetry ruling — and since 2026-08-12 a plain waveform
    //     press resolves no flag either: the placement press at every column).
    //   * close_history_mode is the ONE exit owner; every closer calls it. It
    //     PUT THE EDITOR'S PARKED NAVIGATION BAND BACK from 2026-08-05 to
    //     2026-08-18 and does not any more: the view owns no navigation state
    //     at all, so it touches no viewport, no zoom and no playhead (the
    //     record is at AppState::HistoryMode). What it still owns is the
    //     whole-struct reset behind a surviving generation counter, the
    //     view-local REGION clear, the lane-stash drop and the LIVE lane's
    //     republication, the full-window damage, and the deferred prefetch
    //     kick's flush.
    //   * frame_viewed_commit_diff_span frames the viewed checkpoint's whole
    //     delta. Since 2026-08-05 it is an ON-DEMAND ACT with ONE caller — the
    //     trim bar's plain DOUBLE-CLICK, the regular views' span-framing gesture
    //     with this act as its command — rather than an edge effect. Its own
    //     comment carries the recipe and the span rule.
    //   * frame_history_view_whole_song is the whole song in the window, and
    //     since 2026-08-18 it is ONE CALLER's alone — the framing act above,
    //     whose empty-delta arm falls through to it. The MODE WRITES NO
    //     VIEWPORT ANYWHERE now: the `,` / `.` step and the walk-or-reading
    //     switch stopped calling it on 2026-08-08 and the ENTRY on 2026-08-18,
    //     the window being the USER'S from before `h` until after it.
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
    //     edges (entry, step, reading switch, exit — the last one after the
    //     whole-struct reset, where the mode is down and the lane it publishes
    //     is the LIVE one). It is the view switch's own synchronous route, and it
    //     is what makes an edge swap the lane's content atomically instead of
    //     blanking it for a frame (architect 2026-08-07). The drop's comment
    //     carries both arguments.
    //   * set_history_reading is the ONE switch owner for WHAT THE LANE SHOWS
    //     (2026-08-05 as the two compare readings' owner, generalized
    //     2026-08-07 to the (walk source, reading) PAIR): bare `g` STEPS THE
    //     WALK through it (2026-08-18, the icon row's two radio buttons' chord
    //     — row 3's repurposed tabs selected it directly until then) and bare
    //     `u` FLIPS THE READING through it (2026-08-08,
    //     when the reading left the row for row 4's own toggle). A switch is a MODE EDGE with the `,` / `.` step's own
    //     shape, and the owner is idempotent, which is what makes a step onto
    //     the walk already shown a consumed nothing at its call site.
    // THE COMMIT ACT'S GUI HALF is the last pair, and the act itself lives in
    // the diff module (commit_history_checkpoint, history_diff.h):
    //   * the COMMIT-TITLE EDITOR asks for the message (its cluster is declared
    //     above, beside the load editor whose pattern it takes).
    //   * run_history_commit is that editor's Enter: save, rebuild the bytes,
    //     close the view, and hand the captured job to the background worker.
    //     Its body owns the close partition (THE VIEW CLOSES IFF THE SAVE
    //     LANDED, architect 2026-08-07) and the capture list.
    //   * on_history_checkpoint_complete is the worker's completion, back on
    //     the main thread: it clears the in-flight bit and writes the CRITICAL
    //     SLOT — the four failing verdicts set it, the two established ones
    //     clear it
    //     (AppState::critical_error_message owns the contract). It raised an
    //     acknowledge modal until 2026-08-09; the slot is paint-only, so the
    //     completion now needs nothing from the input layer at all.
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
    // and again at every prefetch drain that appended a member or ended the
    // run; it measures exactly once — when member 0 first exists, or when the
    // run finishes having delivered none — and is a no-op forever after.
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
};
