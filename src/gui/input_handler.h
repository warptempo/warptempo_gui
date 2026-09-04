#pragma once

#include "app_state.h"
#include "async_renderer.h"
#include "audio.h"
#include "external_sync.h"
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
#include "ab_audition.h"
#include "render_player.h"
#include "notifications.h"
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
#include "platform.h"

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
// reach it. Two callers: render_bpm_sweep() per cell, and commit_bpm_edit()
// at the bracket's two ends (the derivation is monotone in bpm). The cell's
// marker rewrite below shares the same two callers.
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

// -- The BPM cell's marker rewrite ---------------------------------------
//
// THE ONE OWNER of a sweep cell's marker vector (architect 2026-08-26: "the
// selected markers determine the BPM, but the shape of the tempo map should
// remain"). render_bpm_sweep calls it once per cell; commit_bpm_edit calls it
// at the bracket's two ends — the derivation is monotone in bpm, so the ends
// bound every cell's rescaled values exactly as they bound the derived base
// tempo, and the editor red-flashes what the sweep would otherwise have to
// reject cell by cell.
//
// A DISABLED MARKER IS INVISIBLE TO THIS ACT, IN THE SPAN AND OUT OF IT
// (architect 2026-08-29: "`m` should disregard disabled markers — it should
// calculate the span by ignoring disabled, and it should not update the base
// tempos of disabled markers"). THE SPAN HALF IS ALREADY STRUCTURAL and needs
// nothing here: the span's boundary is section_end_index (warpmarkers.h), the
// next marker that PARTICIPATES IN THE RENDER, so a disabled marker never
// bounds the span and the duration compute_base_tempo_scale is handed — the
// boundary marker's frame, or the song end — never stops at one. THE REWRITE
// HALF is the two loops below: an effectively-disabled marker is SKIPPED by
// both, every field untouched, and `effective_disabled` (warpmarkers.h, the
// store's one predicate, never re-spelled) is the verdict at each. It reads
// `base`, which the rewrite cannot move — the cell touches tempo fields only,
// and disabledness is `disabled`, `label_ref` and the ref's def.
//   THIS SUPERSEDES THE RE-ENABLE ARGUMENT of 2026-08-26 ("so a marker
// re-enabled later still sits in proportion"). By ruling a marker re-enabled
// later carries the tempo it had when it was disabled; the act simply does not
// see it.
//
// THE SPAN [owner_idx, endpoint_idx) WORKS AS IT ALWAYS HAS FOR THE MARKERS
// THAT PARTICIPATE: the owner takes the derived base tempo (typed scale reset
// — the residual rides the cell's .settings scale), and every effectively
// ENABLED span-internal marker becomes a pass — the three tempo fields and
// nothing else, so label_def, label_ref, disabled and every other field are
// untouched (the `m` gate refuses an effectively-enabled in-span ref up front,
// so a ref reaching this loop is disabled and skipped above). The disabled
// ones the extended boundary sweeps in past the selection are left exactly as
// they are. The boundary
// marker, when one exists, is simply the first marker OUTSIDE the span, and it
// is effectively enabled by the boundary walk's own rule.
//
// EVERYTHING OUTSIDE THE SPAN KEEPS ITS SHAPE: every EFFECTIVELY ENABLED
// marker that OWNS its tempo (tempo_inherits false, no label_ref) has its
// tempo cents rescaled by THE OWNER'S OWN CHANGE, the ratio of the derived
// base tempo to the owner's old effective tempo (its base times its typed
// scale, which the cell resets):
//
//     cents_i' = nearbyint(cents_i * new_cents / (old_cents * owner_scale))
//
// The global settings scale cancels out of that ratio — it multiplies every
// section alike, so the cell's residual scale reaches the rescaled markers and
// the owner in one proportion. Passes stay passes and refs stay refs: each
// follows its ancestor or its definition, which is either rescaled here or is
// the owner itself, so the map's STRUCTURE is untouched and only its owned
// VALUES move. The `m` gate's one-tempo rule (input_key_dispatch.cpp) is what
// makes the owner's change THE span's change: the run's owning members carry
// the owner's tempo, so a boundary-side pass that walks back into the span
// inherits exactly the rescaled value too.
//
// BANKER'S ROUNDING ONTO THE CENTS GRID — the project's one rule for points on
// a grid. The product of two integer cents counts is exact in double and the
// single division is correctly rounded, so a true tie reaches std::nearbyint
// as an exact .5; the cents-domain form is used rather than two
// tempo_from_cents conversions for that reason (the /100s cancel and would
// only add rounding). A rescaled value outside [kTempoMinCents, kTempoMaxCents]
// REFUSES the cell (nullopt), never clamps — a clamped section would deform
// the very shape this exists to preserve. A DISABLED MARKER'S VALUE CAN NO
// LONGER TRIGGER THAT REFUSAL, since it is never rescaled: only the tempos
// that reach the render can refuse a cell.
//
// THE RESOLVER'S OWN 1.00s STAND OUTSIDE THE RESCALE (recorded asymmetry,
// codex 2026-08-26): the rewrite moves AUTHORED values, and what the render
// resolver normalizes it normalizes afterwards — the frame-0 seed ahead of the
// first marker, a leading pass's fallback, a pass inheriting through an
// enabled ref, and a ref whose implied tempo leaves the resolver's envelope
// (ExtremeRatio, the lane's red flag) all render at the resolver's 1.00 in
// the cell as they do in the source. A def's rescale can push an outside ref
// across that envelope exactly as a cent step on the def can today; under
// the load-lenient, render-normalizing doctrine that is a red flag on the
// loaded cell, never a refusal here. The coincident-collapse run is the one
// such state the `m` gate refuses up front (input_key_dispatch.cpp) — an
// owner inside it can take no tempo at all.
inline std::optional<std::vector<GuiWarpMarker>> bpm_cell_warp_markers(
    const std::vector<GuiWarpMarker>& base,
    int owner_idx, int endpoint_idx,
    int64_t derived_base_tempo_cents) {
    const int n = static_cast<int>(base.size());
    if (owner_idx < 0 || owner_idx >= n)           return std::nullopt;
    if (endpoint_idx <= owner_idx || endpoint_idx > n) return std::nullopt;
    const GuiWarpMarker& owner = base[owner_idx];
    // In-bracket at every input surface: cents >= kTempoMinCents, a typed
    // scale in [kScaleMin, kScaleMax] — the divisor is positive by
    // construction; the guard is the breach backstop.
    const double old_effective_cents =
        static_cast<double>(owner.tempo_cents) *
        owner.tempo_scale.value_or(1.0);
    if (!(old_effective_cents > 0.0)) return std::nullopt;
    const double new_cents = static_cast<double>(derived_base_tempo_cents);

    std::vector<GuiWarpMarker> cell = base;
    cell[owner_idx].tempo_inherits = false;
    cell[owner_idx].tempo_cents    = derived_base_tempo_cents;
    cell[owner_idx].tempo_scale.reset();
    for (int i = owner_idx + 1; i < endpoint_idx; ++i) {
        if (effective_disabled(base, i)) continue;   // invisible to the act
        cell[i].tempo_inherits = true;
        cell[i].tempo_cents    = 100;   // inert default
        cell[i].tempo_scale.reset();    // inert: no typed scale
    }
    for (int i = 0; i < n; ++i) {
        if (i >= owner_idx && i < endpoint_idx) continue;   // the span
        if (effective_disabled(base, i)) continue;   // invisible to the act
        GuiWarpMarker& m = cell[i];
        if (m.tempo_inherits || !m.label_ref.empty()) continue;
        const double rescaled = std::nearbyint(
            static_cast<double>(m.tempo_cents) * new_cents /
            old_effective_cents);
        if (!std::isfinite(rescaled) ||
            rescaled < static_cast<double>(kTempoMinCents) ||
            rescaled > static_cast<double>(kTempoMaxCents)) {
            return std::nullopt;
        }
        m.tempo_cents = static_cast<int64_t>(rescaled);
    }
    return cell;
}

// -- Target-view entry validity predicate --------------------------------
//
// The single definition of "may this state enter target view", shared by
// the two entry surfaces: the keyboard S → T toggle
// (GuiInputHandler::switch_active_audio_view_to) and the load-time
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
//
// A REFUSAL IS SILENT ON SCREEN, AND BOTH CALLERS REFUSE ALIKE (architect
// 2026-08-30). The gate STAYS — it is a silent-wrong guard: the preview must
// never receive a map the builder refuses. Its MESSAGE goes. The `t` road
// (and the icon row's audio-view lamp, and every caller composing the set-to form)
// stays in source view and prints ONE stderr line with the builder's own
// words, `warptempo_gui: Target view entry refused: <error>`, which is
// exactly what the load restore has always done bar the line itself — one
// predicate, one answer, one sentence.
//
// WHY NO ON-SCREEN SURFACE EXISTS FOR IT, by ruling rather than by omission:
// the refusals here are UNREACHABLE FROM PROGRAM-WRITTEN INPUT. Loads are
// strict (an adversarial store hard-fails the load long before this walk),
// the resolver normalizes every ambiguous marker arrangement to tempo 1.00
// ahead of the build, and authored tempo is integer cents in [25, 400] — so
// the engine-metadata / non-positive-tempo-product class the builder refuses
// on cannot be authored. What would catch a builder/resolver disagreement is
// not a message here but the PREVIEW's own `Target render failed` card: the
// same map is rebuilt on the render road, where a failure is an event the
// user was not watching. This wore the dismiss-only ERROR_NOTICE modal until
// 2026-08-30; that prompt kind retired whole with the move (its other caller,
// the iteration sweep's cell cap, became a normal card).
std::expected<std::vector<WarpFrameMapSegment>, std::string>
validate_target_view_entry(const std::vector<GuiWarpMarker>& markers,
                           double scale, int sample_rate, long total_frames);

// HIDE THE TRIM REGION OVERLAY, damaging the waveform so the recolored ground
// repaints away. A no-op when it is not shown. It was a CLEAR until 2026-08-18
// and is a HIDE since, and that is the whole of the change: the region IS the
// trim now, so there is nothing left to discard.
//
// WHAT THE OVERLAY IS, so the rule below reads as one thing: THE TRIM, painted a
// second time (architect 2026-08-18). The 9 px bar is right for a mouse and
// unusable with a fingertip, so trim gained a large waveform surface — one
// state, two painted surfaces, the span DERIVED from the trim every frame and
// stored nowhere (the model is at RegionState, app_state.h). It is not a
// playhead form, not a selection visual and not free scratch: the cursor
// playhead paints straight across it and the singleton stem is never
// suppressed.
//
// HIDING IS SAFE BY CONSTRUCTION: it DISCARDS NOTHING, because the trim persists
// and bare `[` re-shows an identical overlay. That is what lets the rule below
// be a rule rather than a negotiation — no site has to weigh what a hide costs.
//
// ============ THE RULE (architect 2026-08-19, amended 2026-08-20) ============
// THE OVERLAY HIDES WHEN THE PLAYHEAD'S POSITION IN THE MUSIC CHANGES, WHEN A
// MARKER IS TOUCHED, AND WHEN THE SWEEP ENDS. Nothing else hides it, and this
// comment is the ONE statement of that — there is no call-site inventory any
// more, because the calls are FIVE, in THREE CLASSES, and the first class is a
// pair of OWNERS every future caller inherits from (re-derived by grep
// 2026-08-21). The three clauses are now EXACTLY the surviving hides: the bare
// Esc hide that used to sit outside them retired 2026-08-21, so the rule needs
// no fourth clause and no exception.
// The model is the DAW instinct the architect named: moving the
// playhead, or touching an event, collapses the region — and the third clause
// is the same instinct read forward, a stroke that has just said "this exact
// region" having nothing left to look at.
//
// SPATIALLY, which is how he arrived at it: the TIMELINE is the ruler, the
// marker lane and the waveform — the part of the GUI a drag pans left and
// right. Anything in the timeline hides the overlay, EXCEPT touching the trim
// itself. The trim bar and the overview strip are lanes 5 and 4, ABOVE the
// timeline, so the carve-out is a different BAND rather than an exception, and
// the layout needs no special case for it.
//
// THREE THINGS THE RULE SAYS THAT A LIST COULD NOT:
//   * A TRANSLATION IS NOT A MOVEMENT. The `t` flip maps the same musical
//     instant into the other domain — "the closest thing it can do to not moving
//     it" — and every MAP-CHANGE RE-LAND does the same for a marker whose IMAGE
//     moved under a resting cursor: the tempo cent step's target-view tail, and
//     since 2026-08-25 the warp status/value family's tails beside it (Ctrl+D,
//     Ctrl+N, Delete and the flag editor's payload commit, admitted in W+target
//     as the home-view binding's fifth ruled exception). The delete's subject
//     is the cursor's own musical instant rather than a focus's image, the
//     delete leaving no focus, but the reading is the same. All take the RESEAT
//     entry points (reseat_playhead_on_marker, Viewport::reseat_playhead_to),
//     whose caller inventory is at the latter's definition (viewport.cpp).
//   * A RESTORE IS NOT A MOVEMENT. The A/B tabs are two virtual playheads over
//     one piece and a switch brings the other one forward; it writes the entering
//     tab's stored cursor direct and never reaches an owner.
//   * THE CAMERA IS NEVER A MOVEMENT. The grab-pan, the wheel pan, every zoom,
//     the overview strip's whole vocabulary and the `h` view's edges move the
//     WINDOW, not the cursor. None of them writes a playhead at all.
// And THE TRIM'S OWN SURFACES ARE EXEMPT BY CONSTRUCTION: touching the thing the
// overlay depicts cannot be a reason to stop depicting it. That exemption needs
// no suppression anywhere, because the trim family writes the cursor DIRECT —
// park_playhead_at_trim_start and the sweep's per-motion carry — and so passes
// through neither owner. THE SWEEP'S END IS THE ONE HIDE INSIDE THAT FAMILY and
// it is no leak but an explicit CALL, the third clause of the rule: the exempt
// thing is touching the trim, and a stroke that has ENDED is no longer touching
// it.
//
// THE THREE CLASSES, five calls:
//   * THE MOVEMENT OWNERS — two owners in three entry points, which is where the
//     rule's first half lives and
//     where every future caller inherits it: Viewport::move_playhead_to (the
//     cursor's live chokepoint — the Left/Right step, Home/End, the drops, the
//     nudges, the typed `playhead_cursor=`, the navigation click's placement,
//     the `h` mode's Home/End) and land_playhead_on_marker /
//     land_playhead_on_source_frame (the land owner — the whole Tab family, `c`,
//     the marker click, the editor opens, the undo/redo restore, the propagate
//     paste, the Ctrl+N collapse, the `h` mode's Tab cycle and diff-flag
//     clicks — the marker and frame forms each carrying the call, since they
//     share only the write below them). ALL HIDE UNCONDITIONALLY, never gated on whether the write moved
//     anything: a Home pressed on the frame the cursor already holds still
//     hides, which is what the bottom row's ungreyed skip buttons promise
//     (architect 2026-08-15).
//   * THE MARKER TOUCH, one site — run_marker_click_act (input_pointer.cpp),
//     which hides on all three arms even where the arm lands nothing (a
//     ctrl-toggle that empties the selection). This is the rule's SECOND CLAUSE
//     and the reason it is not simply "the playhead moved": touching a flag is
//     an act on the timeline whether or not the cursor was already on it.
//   * THE SWEEP'S COMMIT, one site — commit_region_sweep (input_pointer.cpp),
//     the sweep's ONE end owner, which hides UNCONDITIONALLY at every end path
//     it serves (the clean release, the button-lost arm, the touch hook's end
//     and the force-end finalizer) and whatever the stroke wrote. This is the
//     rule's THIRD CLAUSE (architect 2026-08-20): "shift+drag or longpress+drag
//     on the waveform should collapse the region as soon as it is set — this
//     method usually indicates 'this exact region'." The sweep's raise is
//     therefore bracketed by the stroke — up at its first accepted trim write,
//     down here — and the definition carries the reading
//     of each end case — the `h` view's carved-out playhead sweep included,
//     which collapses an overlay carried into the view precisely because that
//     former moves the playhead in the music and only ever escaped the rule by
//     writing the cursor direct.
// A FOURTH CLASS STOOD HERE AND IS RETIRED: BARE ESC, the one route that hid
// and did nothing else (joined 2026-07-30 as a clear, a hide from 2026-08-18,
// retired 2026-08-21). The durable show's twin is the durable hide, so a second
// key aimed at the same surface was a second road; bare `[` is that road now,
// both ways. Esc's own enumeration lives at its dispatch point in on_key
// (input_handler.cpp).
//
// THE TWO NON-CALLERS WORTH NAMING, because both write the visibility bit
// themselves rather than through this helper:
//   * BARE `[` (handle_toggle_trim_region, input_trim.cpp) — the toggle, and
//     since 2026-08-21 the ONE MANUAL ROAD onto and off the overlay, its hide
//     half being the user asking for it outright;
//   * THE FILE LOAD (file_loader.cpp) — an in-place reset that pairs the hide
//     with a whole new piece, a stranger's window already lit on the waveform
//     being the wrong greeting.
//
// WHAT THE RULE DELIBERATELY LEAVES STANDING, since a reader will look for
// each: the grab-pan and the wheel pan; every zoom, bare `0` included; the
// overview strip's teleport, box pan and bound drags; the A/B tab switch, the
// S/T flip and the `p` W/P swap; entering, walking and leaving the `h` history
// view; the scrub, which auditions without moving the cursor; the playback
// scanner, auditioning being not a cursor move; every trim and region gesture
// including the ones that PARK the playhead at the new trim start — EXCEPT THE
// SWEEP'S END, which hides by its own class above; the settings
// engine commit and the settings-only ('S') undo/redo entry, both of which
// rebuild the map the overlay re-derives against and neither of which lands a
// playhead; the propagate paste's no-created arm; and any
// chrome that dispatches no playhead command. None of these is an exception
// written anywhere — each simply never reaches an owner.
// ==========================================================================
void clear_region_highlight(AppState& app, Viewport& viewport);

// SHOW THE TRIM REGION OVERLAY — the hide's counterpart and the ONE raise every
// gesture uses (architect 2026-08-19). THE SURFACE YOU ARE DRAWING ON SHOULD BE
// VISIBLE WHILE YOU DRAW IT: the sweep has just written the trim under it, and
// since the overlay is DERIVED from the trim every frame it then tracks the
// rest of the stroke live. A no-op when it already stands, which is what lets
// its one caller sit on a per-motion path.
//
// IT DOES NOT FRAME, which is the one thing that separates it from bare `[`'s
// show half (handle_toggle_trim_region, input_trim.cpp — the toggle's raise
// runs bring_span_into_view because the user asked to LOOK at the window). The
// caller here is a LIVE POINTER GESTURE, already under the pointer or the
// finger, so moving the viewport out from under it would be the wrong answer —
// the argument is about the gesture, not about the event's phase, which is why
// it survived the raise moving off the press.
//
// CALL SITES, RE-DERIVED BY GREP 2026-08-21 — ONE, and it is a MOTION BODY:
//   * THE SWEEP'S FIRST ACCEPTED TRIM WRITE (apply_region_drag_motion,
//     input_pointer.cpp), the one motion path all three of its arms share, so
//     the shift former and the touch region hold raise it alike.
//
// IT MOVED THERE FROM THE ARM ON 2026-08-21 (architect, on his first drive):
// "on shift+click on empty waveform, current region is highlighted ... the
// first part is incorrect." The overlay is DERIVED from the RESTING trim, so a
// press-time raise necessarily showed the OLD window until the stroke wrote its
// own. Raising inside the accepted-write branch means the surface that comes up
// is always a span the stroke itself authored — and a motionless shift click,
// which writes nothing, now shows nothing at all.
//
// THE TRIM BAR'S THREE PRESSES LEFT THIS INVENTORY ON 2026-08-20 (architect,
// partly reversing his own 2026-08-19 "touching the trim shows the trim"):
// "touching the tiny lane means I'm on the laptop, and the region exists mostly
// for the touchscreen." The 9 px band under a POINTER is already its own
// display of the trim window; the big surface exists for GLASS, where that lane
// is unusable. So the plain endcap/bridge/bare-band press and the two
// Ctrl / Ctrl+Shift bound-set presses raise nothing at all now, and a laptop
// press that does want the big surface asks for it with bare `[`.
//
// AND THE ONE RAISE LEFT IS STROKE-SCOPED, which is this owner's counterpart
// (architect 2026-08-20): commit_region_sweep collapses the overlay
// unconditionally at every end path, so the overlay stands for exactly as long
// as the stroke is drawing a region of its own.
// Bare `[` is the recall, and hiding discards nothing.
// The waveform overlay's own three presses need no call — they are reachable
// only through region_manipulation_hit, which answers None while it is hidden.
//
// THE `h` VIEW IS CARVED OUT HERE, once and inside the owner rather than at the
// caller: trim is FROZEN in that view and its sweep writes no trim to derive an
// overlay from, so a raise in there could only put up a surface no gesture in
// the view can move.
// Bare `[` is consumed in the mode besides, so nothing can take it down again.
// The carve-out is BELT AND BRACES since the raise moved into the write branch
// — that view's former is excluded from the write itself and so never reaches
// this owner at all — and it stays, being the owner's own promise rather than
// the caller's.
void show_trim_region_overlay(AppState& app, Viewport& viewport);

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
// Undo's own inline W/P swap (deleted 2026-08-28 — the restore reaches the
// writer itself now); and apply_settings_engine_and_prefs, which
// replaces S/T, W/P and A/B wholesale. So the rule now
// sits at every site that assigns app.active_audio_view / active_markers_view /
// active_tab_view — grep those three names and this list is what comes back:
//   * GuiInputHandler::switch_active_audio_view_to — the S/T writer (bare
//     `t`, the settings `active_audio_view=` key, the propagate paste's audio
//     half), below its own refusals.
//   * GuiActiveViews::switch_active_markers_view_to — the W/P writer, below its
//     same-mode early return (`p` through toggle_active_markers_view, the
//     settings key, the propagate paste, which reaches this helper direct, and
//     Undo's column restore, which reached it 2026-08-28 when the hand-kept copy
//     of this body in restore_history_entry was deleted for it).
//   * GuiActiveViews::switch_active_tab_view_to — the A/B writer (Ctrl+Tab, the
//     settings `active_tab_view=` key, Undo's cross-tab restore).
//   * apply_settings_engine_and_prefs (file_loader.cpp) — all three fields at
//     once, the source load's alone since 2026-08-24 (a load in place writes no
//     view state at all now). It takes a
//     Viewport for this and for nothing else; its VALUES-ONLY contract is
//     otherwise intact, the clear being a lifecycle end rather than a side
//     effect the caller could time differently.
// load_file's own two direct writes (the pre-parse 'W' reset and the forced 'S'
// of a failed target-view restore) need no call of their own: it runs the
// routine above in the same body, and it is invoked once from the startup tick,
// before any input exists.
// THE COMMAND WRAPPERS DO NOT SPELL IT — Ctrl+Tab keeps its call because it IS
// the writer, while toggle_active_markers_view and
// handle_active_audio_view_toggle each lost (or never had) a call of their own
// to the writer they delegate to, so there is ONE spelling of the rule per
// write.
// The bare 1/2/3 selectors, the view bar's buttons, the two icon-row VIEW LAMPS
// and the settings keys all compose those writers and inherit it.
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
// seated pinch survives a whole modal editor session and resumes when the modal
// closes — which is exactly how a settings editor's `active_audio_view=T` commit
// reaches a seat taken in SOURCE.
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
// clamp_playhead_to_live_domain, a direct cursor write). It touches no
// selection. IT HIDES THE TRIM REGION OVERLAY (2026-08-19): this is one of the
// rule's two movement owners, the other being Viewport::move_playhead_to, and
// the hide is no longer any caller's to spell — the rule and the whole exemption
// set live at clear_region_highlight above. Read-only allowed. Definition in
// input_pointer.cpp, whose comment is the AUTHORITATIVE statement of the
// marker-lane-owns-the-playhead rule and the one
// enumeration of the landing sites — do not restate either here.
void land_playhead_on_marker(AppState& app, const GuiAudio& audio,
                             Viewport& viewport, int hit);

// THE SAME LAND WITHOUT THE HIDE — the non-hiding entry point for the two
// callers whose write is a RESEAT rather than a movement: the S/T flip's
// re-express of a surviving focus (a translation) and the coincidence
// auto-select's provable no-op. Both are argued at the definition
// (input_pointer.cpp); a third caller needs an argument of its own, and the
// answer is never a flag on the land.
void reseat_playhead_on_marker(AppState& app, const GuiAudio& audio,
                               Viewport& viewport, int hit);

// The same land with the store lookup taken off the front: place the playhead on
// an authored SOURCE frame directly, through the identical two-step basis, the
// identical damage and the identical hide. It exists for the callers holding a
// frame that belongs to no store entry — the `h` history mode's focus click,
// whose removed diff flags name frames the session no longer has — and
// land_playhead_on_marker shares its write, so the marker route and the frame
// route cannot drift. Same contract otherwise: playhead write plus the movement
// owner's hide, no selection, read-only allowed.
void land_playhead_on_source_frame(AppState& app, const GuiAudio& audio,
                                   Viewport& viewport, int64_t src_frame);

// COINCIDENCE AUTO-SELECT — the entry counterpart of the never-park rule
// (architect 2026-07-29): the selection is never stashed, so an ENTRY re-acquires
// it from the playhead instead of from memory. Scans the ACTIVE column's store in
// order and SINGLE-SELECTS the first marker whose land value is EXACTLY the
// resting playhead. Definition in input_pointer.cpp, beside the land whose
// formula it reuses; that comment states the rule, the exactness, and the
// first-in-store tie-break. FOUR CALL SITES (re-greped 2026-08-24, the count
// having fallen from six as the load-in-place family collapsed onto one body),
// each stating only its own class and pointing there: the source load's tail
// (file_loader.cpp), the `p` column entry (toggle_active_markers_view) and the
// Ctrl+Tab tab entry (switch_active_tab_view_to), both in active_views.cpp, and
// THE LOAD-IN-PLACE FAMILY'S ONE SHARED TAIL (apply_recipe_in_place,
// input_key_dispatch.cpp), which all three acts reach — the `'` render-entry
// load (load_render_entry_in_place, joined 2026-07-30), the `h` view's commit
// load (load_history_commit_in_place) and that view's LOCAL-tab load
// (load_history_local_entry_in_place, 2026-08-08) — because a load-in-place
// replaces the store under a resting playhead, which is an ENTRY into a new set
// of markers exactly as a load is. No match leaves the selection exactly as the caller
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
// run_span_framing_command (both arms) and the GROUP undo/redo
// restore's offscreen framing. Definition in input_handler.cpp.
void frame_span_into_view(AppState& app, const GuiAudio& audio,
                          Viewport& viewport, int64_t lo, int64_t hi,
                          bool margin);

// PREFER A SCROLL, ZOOM ONLY WHEN THE SPAN CANNOT FIT — the three-arm framing
// the GROUP undo/redo restore has taken since 2026-07-25, HOISTED into its own
// owner on 2026-08-16 when the Show trim region button needed the identical
// behaviour ("like undo in terms of zoom/viewport", architect). It is the
// framer above's caller, not its sibling: arm three IS
// frame_span_into_view(margin=true).
//
// [lo, hi] are ACTIVE-DOMAIN frames, the same domain frame_span_into_view
// takes and the same one BOTH callers already hold — the restore derives its
// extent through clamp_playhead_to_live_domain(source_frame_to_active_domain
// (...)) and the region's endpoints ARE active-domain frames by definition. A
// caller holding SOURCE frames (the trim bounds, say) converts before it calls,
// which is what the Show trim region act does.
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
//     walk), bare Tab / Shift+Tab / IsoLeftTab (the diff-flag cycle),
//     Ctrl+Shift+Tab (the PAIRED MARCH over that cycle, 2026-08-18 — the ONE
//     ctrl-carrying claim), bare `g`
//     (the WALK toggle over the two sources, the icon row's WALK LAMP's chord
//     since 2026-08-18 — a radio pair's until the 2026-09-04 collapse, and
//     Ctrl+Tab / Ctrl+Shift+Tab over row 3's repurposed tabs before that), bare
//     Home / End and
//     bare `c`. The definition carries the
//     derivation. handle_history_mode_key consumes exactly these,
//     one line ABOVE the allowlist, which is why a face derivation has to ask
//     this first.
//   * history_mode_key_blocked — the allowlist gate, read_only_key_blocked's
//     shape: true when the press is not admitted while the mode stands. Its
//     admitted membership is enumerated at the definition.
//
// THE SECOND TAKES THE WHOLE AppState, THE FIRST TAKES NOTHING BUT THE PRESS,
// and the asymmetry is the membership's own: the mode's keys are a fixed keymap,
// while TWO allowlist admissions are conditional on state they are asked about
// (re-derived 2026-09-01) — the revert act's, on a subject standing on a
// writable tab (history_revert_actionable — a selected diff flag, else the
// focused one, and the lock since planner decision 58, 2026-08-30), and the
// load-in-place's, on the active walk carrying a member. THE COMMIT ACT'S TWO
// LEFT THIS LIST ON 2026-09-01 (architect: a gate's membership test is the
// CHORD'S ALONE): Ctrl+S — head_delta_empty and history_checkpoint_in_flight —
// was the instance he hit, a committed-and-current session being told the
// view's own save chord was "not available in the history view", so the chord
// is admitted as vocabulary and open_history_commit_editor answers both states
// (the publishing card, then the one-dimensional silence), with the Save
// button's grey reading history_checkpoint_actionable at its own arm. Both
// readers hand it the
// same `app` and neither restates a term of it, which is what
// keeps the key and the face one decision — FOR THE REVERT ACT TOO SINCE
// 2026-08-30: its subject term decided the KEY alone from 2026-08-15, when
// redesign_button_enabled lifted the four history companions over the derived
// partition so Revert stayed lit on an empty subject (the architect's
// reversal of a grey that tracked the diff-flag selection and blinked at
// interaction cadence), and the truthful-buttons ruling deleted that lift, so
// the same admission greys the button again (the record is at the
// companions' arm in app_state.h).
// It took the HistoryMode struct alone
// until the in-flight bit joined, that bit living on AppState because the act
// outlives the view it was launched from; the bit left again with the commit
// act's terms, and the whole AppState stays for the revert admission's lock.
bool history_mode_owns_key(GuiKey key, GuiInputState mods);
bool history_mode_key_blocked(GuiKey key, GuiInputState mods,
                              const AppState& app);

// THE CLIPBOARD REFUSAL'S ONE SENTENCE, composed for whichever write met it —
// the verb is the act's own word ("copy", "cut"). The body and the reasoning
// are at its definition (input_key_dispatch.cpp); it is declared here because
// its raisers now live in two translation units, the editor's cut having
// joined the two carded copies on 2026-09-03.
void card_clipboard_refusal(GuiNotifications& notifications, const char* verb);

// THE TRIM SETTER-DESELECT RULE, stated here where the retired trim-highlight
// sync used to declare it. THE SYNC ITSELF IS DELETED (architect 2026-07-30, Q3)
// and did not come back on 2026-08-18 when the region became the trim: a sync is
// a CONTINUOUS INVARIANT binding two states, and there is only one state now —
// the overlay is DERIVED from the trim every frame and nothing is published into
// anything (its contract is at clear_region_highlight above and at RegionState,
// app_state.h). What survives is
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
// (app.trim.* / the settings arms' active branch), RE-DERIVED AGAIN 2026-08-18
// when the SET-FROM-REGION act retired and the sweep replaced it — SIX
// call sites, FIVE in input_trim.cpp and ONE in settings_editor.cpp:
//   * THE SWEEP (write_trim_from_sweep), which deselects at its first accepted
//     write, idempotently across the gesture's later events — the drag arms'
//     own arrangement below. (This list's first member was the SET-FROM-REGION
//     act, which retired with the free scratch span it committed; the key it
//     used is the trim region TOGGLE's now and writes no bound at all, at
//     handle_toggle_trim_region.);
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
//     playhead_cursor= key having already cleared the selection and hidden the
//     overlay under the no-exemptions rule. Their INACTIVE-tab arm is not a member and never was:
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
// THE NON-SETTER IS EXACTLY ONE ROUTE: Shift+[, the dedicated trim MAXIMIZER,
// which widens the window to the whole song rather than claiming one, so there is
// no window for a deselect to hand the user (the architect's 2026-07-29
// exemption, kept verbatim through the 2026-07-30 re-pose). Everything else
// that touches a trim bound is not a command in this sense: auto_clear_crossed_trim
// is a shared commit tail every setter already runs inside its own body, and the
// ENTRY / RESTORE routes (file load, the Ctrl+Tab band pull, the settings-file
// tab-band pull) install a trim wholesale. The `'` load-in-place left that list
// 2026-08-24: it installs no trim at all, trim having no undo and the act
// writing only what its undo entry restores.
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
    // THE A/B AUDITION (2026-08-26). ONE reader here: on_key's Shift+Space
    // arm, which calls its start; the advance is the tick's (main.cpp).
    GuiAbAudition&           ab_audition;
    // THE RENDER PLAYER (2026-08-28). Its readers here: the two KEY openers'
    // arms (bare `l` in handle_mode_keys, bare `'` in on_key — one icon-row
    // BUTTON behind them since 2026-09-01, Play renders), the player's
    // own key router (route_render_player_key), the overlay's and the
    // scrub's press routers, and the load road; the tick is main.cpp's.
    GuiRenderPlayer&         render_player;
    // THE NOTIFICATION CARDS (2026-08-29). Its readers here: every producer
    // that answers a user's act with a sentence (the load-in-place refusals,
    // the picker's, Synchronize's, the checkpoint verdicts, the measure
    // paste's stop, the revert's wall) calls notify; the X's press claim
    // calls dismiss; the motion handler and the pointer-left hook drive the
    // hover; the tick is main.cpp's.
    GuiNotifications&        notifications;
    PhaseResetPropagate&     phase_reset_propagate;
    GuiAsyncRenderer&        async_renderer;
    // The checkpoint act's background worker (2026-08-07). ONE user:
    // run_history_commit, which dispatches the captured job onto it; the
    // completion comes back through main.cpp's eventfd wiring into
    // on_history_checkpoint_complete.
    GuiHistoryCommitWorker&  history_commit_worker;
    // THE HISTORY WALK'S PREFETCH STORE (2026-08-07). The `h` visit BINDS to
    // it (GuiHistoryDiff::init) instead of running git itself, and this handler
    // owns the three kick sites' one funnel — kick_history_prefetch, whose
    // definition carries the proof that none of the three can fire with a
    // visit standing.
    GuiHistoryPrefetch&      history_prefetch;
    // The Synchronize to external storage act's background worker
    // (2026-08-27). ONE user: synchronize_to_external_storage, which
    // dispatches the captured job onto it; the completion comes back through
    // main.cpp's eventfd wiring into on_external_sync_complete.
    GuiExternalSyncWorker&   external_sync_worker;
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
                    GuiAbAudition&           ab_audition_,
                    GuiRenderPlayer&         render_player_,
                    GuiNotifications&        notifications_,
                    PhaseResetPropagate&     phase_reset_propagate_,
                    GuiAsyncRenderer&        async_renderer_,
                    GuiHistoryCommitWorker&  history_commit_worker_,
                    GuiHistoryPrefetch&      history_prefetch_,
                    GuiExternalSyncWorker&   external_sync_worker_,
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
          ab_audition(ab_audition_),
          render_player(render_player_),
          notifications(notifications_),
          phase_reset_propagate(phase_reset_propagate_),
          async_renderer(async_renderer_),
          history_commit_worker(history_commit_worker_),
          history_prefetch(history_prefetch_),
          external_sync_worker(external_sync_worker_),
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
    // The propagate's paste tail lands in target view through the same S/T
    // chokepoint in its set-to spelling (switch_active_audio_view_to); the
    // friendship lets it reach that private method through its back-pointer.
    friend struct PhaseResetPropagate;
    // AND THE UNDO RESTORE, for the same private method (2026-08-28): an undo
    // entry records the S/T view beside the tab and the column, and the restore
    // hands each axis to its own owner — the other two live on GuiActiveViews,
    // which Undo holds outright, and this one lives here.
    friend struct Undo;
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
    // live at their own homes: the editors' modality
    // (route_modal_editor_key), strict modifier validation (an unbound
    // modifier combination is a consumed no-op; conventions.md), the Super
    // press drop (deliver_key, input_core.cpp), the modal Enter/Space
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
    // down column, live-session reseek, the overlay hide at the arm, the drag
    // arm); the
    // MODE = the view-local former's recipe (clear the mode focus +
    // selection through the pair clearer, the shared placement body, the
    // same arm) — EVERY REGION FORMER DROPS THE SELECTION ITS SURFACE OWNS,
    // the family rule at RegionState (app_state.h).
    // THE REFUSALS LIVE IN THE BEGIN, mirroring the press path the gesture
    // bypasses (the dead begin_touch_trim_move's own list MINUS the `h`
    // view, which ADMITS this former as its own view-local vocabulary):
    // prompt, the editors via keyboard_modal_editor_active (the flag
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
    // path's own body (a moved drag has ALREADY written the trim, per motion
    // event and under no width rule at all, so its end runs the sweep's
    // commit tail — where a coincident stroke resets to the whole song; a
    // MOTIONLESS end wrote nothing and leaves the playhead where
    // the begin seated it — the former's motionless-release rule, which is what
    // makes a long-press-then-lift a PLACEMENT. The release-time sliver
    // dissolve retired with the free span it protected, 2026-08-18, and the
    // minimum width floor with the architect's 2026-08-19 ruling). Any end
    // commits — finger up,
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
    // resting pointer, the zoom and navigation keys, `[`/`Shift+[`, an undo
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
    // writer serving the WHOLE roster — row 1's five menu anchors and
    // the view bar's three, row 3's two tabs, row 4's twenty-five (the
    // toolbar four included since the 2026-08-12 relayout, the history group's
    // seven closing it — the opener, the walk lamp and the four companions
    // since 2026-08-18, Load in place at the tail since 2026-09-01) and the
    // bottom
    // row's eighteen — the transport three, then the right block's four marker
    // verbs with the COPY VALUE button (2026-08-29), the EDIT FLAG button
    // (2026-08-27), the MARKER MEASURE
    // (2026-08-19) and ADD TO SELECTION (2026-08-18) behind them, three walk
    // steps and four cardinal arrows. EVERY ONE OF THEM
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
    // action of EVERY non-chord button — the five of kDropdownMenus, File,
    // Edit, Iterations, Settings and Help (re-greped 2026-09-03): it closes the
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
    //     so do the prompt, the dialog editors, the folder overlay's three
    //     contents and every live gesture —
    //     plus ONE condition the call site restates because nothing above
    //     returns on it: a HELD PRIMARY BUTTON refuses the open (codex round 2;
    //     the two held-motion producers are recorded at the call). The four
    //     anchors the `h` view and the folder overlay kill are refused inside
    //     toggle_dropdown (menu_anchor_dead_in_mode), not here. SO THE ARMED
    //     HOVER OPEN IS UNREACHABLE UNDER THE BAND — the three overlay
    //     branches return above this tail — while the hover SWITCH, which
    //     lives in the open-dropdown branch above them, is live there: File
    //     can be opened by a press over the band and the pointer can cross to
    //     the dead anchors, where that guard answers;
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
    // GEOMETRY, and since 2026-08-15 the whole answer: no item on any menu
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
    // being a consumed nothing). The five buttons
    // outside it are row 1's anchors — File, Edit, Iterations, Settings and
    // Help (re-greped 2026-09-03 against kDropdownMenus) — whose action is a
    // dropdown
    // toggle — the recorded press-time exception, stated at their claim (row 1
    // had a chord button, Quit, inside it until 2026-08-13, when its act became
    // the File menu's one item, and one of the anchors was Navigation until its
    // menu was deleted 2026-08-15).
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

    // THE TRIM REGION OVERLAY'S HIT VERDICT (the whole model, and why it
    // exists, are at RegionState, app_state.h).
    //
    // region_manipulation_hit: the ONE hit owner, and the ONE spelling of
    // "inside the shown overlay" the whole product reads — THREE consumers, so
    // it cannot drift: the plain waveform press claim (which asks it BEFORE
    // arming the nav drag, and arms the TRIM drags on a non-None answer), the
    // pointer cursor map's region arm, and THE TOUCH PAN ZONE, which must
    // answer false wherever this answers non-None or the finger would become
    // the phone-model pan and never reach the drag at all.
    // It is Y-GATED TO THE WAVEFORM RECT ALONE — the ruler and the marker lane
    // answer None even where the span covers their columns, the architect's
    // ruling and the reason a pan stays reachable while a full-width overlay
    // stands — and it answers None while the `h` history view stands, where the
    // press claim never reaches this arm and trim is frozen (one gate, so the
    // cue and the touch zone cannot promise a gesture the router does not run).
    //
    // THE SPAN IT MEASURES AGAINST IS DERIVED (trim_overlay_span) and projected
    // to columns on the DISPLAYED (PLATE) BASIS through the painter's own
    // region_columns owner — the very call paint_region_ground makes — so a
    // grabbed bound is exactly a painted one by construction rather than by two
    // derivations agreeing.
    //
    // (The gesture's own motion body and column->frame conversion lived here
    // until 2026-08-18. They are DELETED: the move and the two bound drags are
    // the TRIM bridge and endcap drags now, armed from this surface and running
    // trim's own machinery, so there is no second editor to keep in step.)
    RegionHit region_manipulation_hit(int x, int y) const;

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
    // THE RENDER PLAYER'S TWO ARMS' HARD ENDS (2026-08-28): the folder
    // overlay's row press and the play-scrub's marker drag, dropped
    // uncommitted on the pointer-leave edge (main.cpp's hook), the
    // button-lost edge (clear_release_time_press_arms) and the FORCE-END
    // FINALIZER (finalize_active_drags, which owes them because both arms are
    // members of any_pointer_gesture_active and its callers promise a
    // gesture-free state) — the contracts are with their press routers, in
    // the player's block below.
    void clear_folder_overlay_press();
    // THE BAND'S HOVER FACE, dropped on the pointer-leave edge alone (the
    // roster's own clear_redesign_button_hover beside it): every other end of
    // a hover is a MOTION, which the hover walk answers by itself. A leave
    // delivers none, so the lit row would stay lit with the pointer gone.
    // THIS EDGE IS ALSO A FINGER'S LIFT, which is why the panel needs no
    // touch term of its own for it (architect 2026-08-29: "hover clears at a
    // finger's lift", GTK's and Kirigami's habit — no prelight left behind
    // after a touch): the touch translation's end delivers the button release
    // and then, with no mouse resting in the window, this very leave
    // (GuiInputCore::deliver_touch_translation_end), so the row the tap lit is
    // dark again in the same frame. With a mouse focused the end delivers a
    // restore MOTION at the mouse's own position instead, and the hover walk
    // re-answers from there — the mouse's hover, unchanged, which is the other
    // half of the ruling.
    void clear_folder_overlay_hover();
    void clear_player_scrub_drag();
    // THE NOTIFICATION CARDS' THREE POINTER HALVES (2026-08-29; the rule is
    // at notifications.h). The CLAIM ranks above every veil in
    // on_button_press: a press on a published card is consumed whole, and
    // the LEFT press on its X box dismisses it — the X and only the X, on
    // both backends (architect 2026-08-29). The HOVER is re-derived on every
    // motion and cleared on the pointer-left edge, exactly as the folder
    // overlay's is.
    bool claim_notification_press(GuiMouseButton button, int x, int y);
    void update_notification_hover(int x, int y);
    void clear_notification_hover();
    // THE LOAD CONFIRMATION'S TWO ANSWERS (2026-08-28), called by GuiPrompt
    // through its back-pointer when the LOAD_IN_PLACE_CONFIRM prompt answers
    // OK or Cancel. ONE PROMPT BODY, TWO SUBJECTS since 2026-08-29 (the
    // player's highlighted batch entry, and the `h` view's viewed walk
    // member): the OK forks on which subject the raise parked and the Cancel
    // drops both. The two roads' contracts are at render_player_load_in_place
    // and history_load_in_place.
    void confirm_load_in_place();
    void cancel_load_in_place();

    // WHAT CLOSES BEFORE THE QUIT QUESTION, the editors' half (2026-08-28):
    // every standing keyboard-modal editor abandoned without committing, each
    // through its own exit-no-commit body. ONE CALLER, GuiPrompt::request_close
    // — the one close road, which the keyboard's Ctrl+Q, the compositor's WM
    // close and File → Open project's commit all reach — so no road restates
    // the step and none can forget it. At most one editor can stand at a time
    // (each opener refuses under the others), so the chain runs exactly one
    // body; every one of them is is_active-guarded, which makes the whole call
    // a no-op on the ordinary close and idempotent after a road that already
    // closed its own editor (the reopen commit's).
    void close_modal_editors_no_commit();
    // AND THE PICKER'S HALF, the same road one line up in request_close: the
    // one close body of the field-less picker (the contract is with the
    // picker cluster below, private beside its openers); public for that one
    // caller, exactly as the editors' body above is.
    void close_picker();
    // AND THE AV SYNC STATS PANEL'S, the same road one line further
    // (2026-09-03): the one close body of the panel — it disarms the display
    // measurement, so the close road cannot leave the instrument running
    // behind a torn-down window. Public for that one caller, exactly as the
    // two above are; its contract is with the panel's cluster below.
    void close_stats_panel();
    // The quit refuses outright while a synchronization to external storage
    // runs (architect 2026-09-04), which is why this one is a question rather
    // than a fourth closing step. GuiExternalSyncWorker::shutdown has no
    // cancel by design, so a quit taken mid-mirror joins the worker with the
    // window already gone and the act finishes blind: a failing mirror's
    // verdict reaches no screen at all, and a working one costs the user
    // however long the stick takes with nothing painted. Refusing keeps that
    // join off a live-window road entirely; the wait is seconds and the bit
    // falls by itself.
    //
    // A running render at the same press does not refuse, and the difference
    // belongs to the acts rather than to the quit. A render is disposable and
    // killable: the quit kills it exactly as any dispatch kills it, nothing
    // authored is lost and the deliverable standing in render/ stays as it was.
    // The mirror is the one act the product cannot kill cleanly, by its own
    // design, and its half-done state is a stick the user is about to pull — so
    // what can be killed is killed and what cannot be killed refuses, which is
    // the level at which the rule is symmetric. Row 8's Synchronizing... line
    // covers the time before the press and says nothing about what a quit would
    // do to the act, which is why the refusal carries a card of its own.
    //
    // Making the mirror cancellable at the quit instead — finish the file in
    // hand, skip the deletions, join and go — was considered and ruled out as
    // more code than the ruling asked for, on an act whose whole wait is
    // seconds.
    //
    // One caller, GuiPrompt::request_close, asked at its head and ahead of
    // every closing step, so this one gate covers every road into the quit
    // through the body those roads already share: the keyboard's Ctrl+Q
    // (which the render player's, the picker's, the stats panel's and the
    // modal editors' routers all fall through to, as do the drag-modal and
    // paste-confirm hatches), the File menu's Quit row (which dispatches that
    // same chord), the compositor's title-bar X and the tablet's BACK press
    // (both the seam's close callback, main.cpp's set_on_close), and the Open
    // project picker's reopen completion. True means the close does not
    // proceed: no prompt raised, nothing torn down, and the answer said on a
    // normal card.
    //
    // It reads GuiExternalSyncWorker::is_busy — the act's own
    // single-in-flight bit — and says the act's own sentence, so the running
    // mirror refuses in one wording everywhere. The picker's open act keeps
    // its own arm on that same bit, which is not a second predicate: it
    // refuses earlier, above close_picker and above the reopen name it would
    // otherwise seat, so the picker stays open with its answer instead of
    // closing over a quit that will not happen.
    bool close_refused_by_external_sync();
    // AND THE PANEL'S PER-FRAME REFRESH, public for the run loop's tick, which
    // is its ONE caller (main.cpp). It returns on its first line with the mode
    // bit down, which is what makes the measurements gated: no reading is
    // taken and no frame is driven while the panel is closed. Contract with
    // the panel's cluster below.
    void refresh_stats_panel_rows();

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
    // THE ON-SCREEN KEYBOARD'S HELD KEY JOINED 2026-08-27 as the family's
    // fourth member and its first DELIVERING one — the argument is at the
    // body, and its producer is the second-finger upgrade, which delivers this
    // motion and no button release.
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

    // Arm THE SWEEP at a press — THE ONE ARM, serving its entries (shift+drag
    // on the desk, the region hold on glass — the deliberate act's two device
    // forms since 2026-08-12, the eighth glass ruling; the plain placement
    // presses that used to arm it are the pending pan now, and the one-day
    // RULER arm with its deferred dissolve is deleted). `anchor_col` is the
    // press column (waveform-relative, in range — every caller has just seated
    // the playhead at it and refused the gutter), from which THE ARM ITSELF
    // authors the fixed end of every trim pair the sweep writes:
    // RegionDragState::anchor_source_frame through sweep_trim_frame_at_column,
    // the sweep's one column->trim route, beside the active-domain frame the
    // caller's placement seated the playhead at (the two-values-per-column rule
    // at the field). (x, y) is the press position for
    // the press-becomes-drag threshold. IT RAISES NO OVERLAY (2026-08-21): the
    // raise stood here from 2026-08-19, but the overlay derives from the
    // RESTING trim, so at the press it could only show the window the stroke is
    // about to replace — it now happens at the sweep's FIRST ACCEPTED TRIM
    // WRITE (apply_region_drag_motion), where the span on screen is the
    // stroke's own. THE PRESS WRITES NO TRIM either: a motionless release is
    // the placement and nothing else, and it leaves no surface behind it.
    // THREE CALLERS (re-derived 2026-08-19): the LIVE former's press half
    // (place_playhead_and_arm_region — the shift press and the touch begin's
    // live arm both route through it), the `h` history view's own shift
    // former (handle_history_mode_press), and the touch begin's MODE arm
    // (begin_touch_region — the view-local recipe re-expressed at the down
    // point). The LOWER half is on the sweep's surface too since 2026-08-13
    // ("shift plus drag to map out a region should also be allowed in the
    // lower half, for consistency"); its PLAIN press is the pending click,
    // whose motionless release runs the one-shot scrub and leaves the overlay
    // alone.
    void arm_region_drag_at(int anchor_col, int x, int y);

    // THE SWEEP'S ONE MOTION PATH, hoisted for its two drivers (2026-08-12, the
    // touch half): on_motion's region branch (mouse motion under the held
    // button, past its own button-lost arm) and update_touch_region (the
    // platform's per-frame region hook, which never has a button to lose). The
    // whole live body: the shared Chebyshev gate from the press/down point
    // (moved latches once), the moved-drag double-click-candidate clear, the
    // moving endpoint at the pointer column through the click->frame basis, THE
    // TRIM WRITE from the anchor to that endpoint (write_trim_from_sweep), and
    // the playhead riding the moving end. Caller guards active.
    //
    // AND IT IS THE OVERLAY'S ONE RAISE since 2026-08-21, inside the
    // accepted-write branch (show_trim_region_overlay, above): the surface comes
    // up only once the stroke has authored a region of its own, so what it
    // shows is never the resting window the stroke is replacing.
    //
    // THE `h` VIEW IS CARVED OUT OF THE TRIM WRITE HERE, at the one site the
    // three arms share: that view promises the trim window is untouched
    // throughout, so its sweep carries the playhead and writes nothing — and
    // raises nothing, the raise sitting inside that same branch.
    void apply_region_drag_motion(int mouse_x, int mouse_y);

    // THE SWEEP'S ONE END OWNER — every end path calls it (clean release,
    // button lost in on_motion, the touch hook's end, and the force-end
    // finalizer), so the disarm and the commit cannot fork. A sweep that WROTE
    // a bound runs the shared trim commit tail (commit_trim_mutation: the
    // crossed/coincident reset, the repaint, the target-render trigger, and the
    // playhead parked at the committed trim start) — AT THE END ONLY, a
    // per-frame cursor chase being a cursor fighting the gesture that is moving
    // the bounds. A sweep that wrote nothing — motionless, refused by geometry,
    // or run inside the `h` view — runs none of that tail.
    // IT COLLAPSES THE TRIM REGION OVERLAY UNCONDITIONALLY though, on every one
    // of those paths (architect 2026-08-20): the sweep's raise is scoped to the
    // stroke — up at its first accepted trim write — and this is its other
    // bracket, a guarded no-op where the stroke raised nothing. The reasoning
    // and the per-case reading are at the definition; bare `[` is the recall.
    void commit_region_sweep();

    // THE PLACEMENT'S PLAYHEAD HALF, and the whole of what the live routes
    // and the `h` history mode's own have in common: drop the
    // playhead at the clicked column, reseek a live scanner to it (keeping the
    // session alive) and override follow for that session. NO selection, NO
    // overlay hide, NO drag arm — each caller owns those, which is what lets the
    // mode reuse this recipe without inheriting a sweep it must not have.
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

    // THE SWEEP'S LIVE PRESS HALF — TWO call sites since the touch
    // half (2026-08-12): the shift-exact press on the navigation surface
    // (on_button_press) and the touch region begin's live arm
    // (begin_touch_region — the hold's expiry at the down point): clear
    // the marker selection, run the body above, and arm the sweep. THE ARM
    // RAISES NOTHING since 2026-08-21: the overlay's one raise sits inside
    // apply_region_drag_motion's accepted-write branch, so a motionless shift
    // press shows nothing at all and the press's own hide (through the movement
    // owner in the body above) simply stands. The clear runs ahead of
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
    // and at GuiInputCore::notional_pointer_x_.
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
    // (nav_zoom_px_per_level(), the gui_scale-resolved rate whose scaling
    // argument is at that accessor; RIGHT zooming in — the pinch's own sign,
    // the superseded pan-derived one at the contract) through
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
    // (lower half), or deselect (live) / mode-focus clear (`h` view), the
    // overlay hide, then the placement above at the press column, playback state
    // read at the act. The full contract is at the definition
    // (input_pointer.cpp).
    void run_nav_click_act(int press_x, bool history, bool scrub_release);

    // THE MARKER CLICK ACT, AT THE PRESS (2026-08-17) — stop, the three-way
    // selection fork, the land, the region hide, the PLAIN shape's
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
    // editor, the commit-title editor, the measure paste-offset editor, or
    // the top-strip flag editor in ANY of its three kinds (unlike
    // modal_dialog_editor_active, which names the four DIALOG-hosted surfaces
    // — those first three plus the flag editor's BpmBracket kind — and omits
    // the FlagPayload and MeasureText kinds, both of which paint in the marker
    // lane). The platform's
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

    // THE BARE `c` COMMAND, and the ONE owner of both its recipes: the working
    // zoom centered on the playhead, with a focused stop re-landed under it
    // first. THE MODE FORK IS INSIDE — the live recipe walks the live stores
    // (repair_last_selected + jump_playhead_to_focused_marker), the history
    // mode's re-expression walks its own diff-flag list and its own focus — so
    // the FIVE call sites (re-greped 2026-09-02: the live `c` arm, the mode's
    // `c` claim, run_overview_command's already-full-out arm, the A/B
    // audition's GuiAbAudition::apply_working_zoom, and Shift+`j`'s jump,
    // which calls it TWICE — once on the tab it leaves and once on the tab it
    // lands) share one decision instead of
    // spelling it each. Rationale at the definition.
    // PUBLIC because the audition's caller is another cluster: the audition opens
    // each of its halves with this command on the tab that half plays, run
    // inside the switch's own frame (ab_audition.h carries the rule and the
    // ordering it owes the sequence).
    // THE ZOOM IS A PARAMETER since 2026-08-18 and defaults to kWorkingZoomLevel,
    // which is what `c` itself means — the two key arms pass nothing. `0`'s
    // already-full-out arm passes the level IT stamped on the way out
    // (ViewState::zoom_recall_level) when one stands, so the return trip is this
    // one command with ONE substitution rather than a second recipe: same mode
    // fork, same land, same centering, and the same clamp (apply_zoom_change
    // pre-clamps every request, so no caller can drive the level outside the
    // per-file window). That pre-clamp is a bound, not a rescue: a stamp read
    // back under a fallen ceiling would clamp onto the level the key is already
    // standing on, so THAT arm resolves the stamp itself and passes the working
    // zoom instead when it cannot move — its own comment carries the case.
    void run_center_command(double target_zoom_level = kWorkingZoomLevel);

private:
    // ActiveBatch holds the batch render state machine (start_render_batch
    // and its lifecycle). Each entry is dispatched onto GuiAsyncRenderer and
    // the next entry fires from the worker-completion callback. The GUI
    // remains interactive between (and during) entries.
    struct ActiveBatch {
        std::vector<RenderRequest> reqs;
        std::string                label;
        // The one folder every cell of this batch publishes into, read off the
        // first request at the start (the sweeps compose one folder per batch
        // and stamp it on every cell). It outlives the requests, which are
        // moved onto the worker one at a time, so the batch's TAIL can still
        // ask whether the folder ended up empty — see dispatch_next_batch_entry.
        std::string                folder;
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
    // render, "Rendering N of M <label>..." for a sweep entry) waiting for
    // permission to appear. Parked by park_render_status at the two archival
    // dispatch sites instead of being written to app.queue_progress_text, copied
    // into the slot by tick_promote_render_status, and cleared there and at
    // finalize_render_run — so a render served by a reuse rung, which never
    // fires the signal, simply drops its message at the completion with nothing
    // ever painted.
    //
    // status_promoted_ says the text currently in the SHARED slot is ours, and
    // exists so a park and the finalize can retract it (a sweep cell's "3 of 8"
    // must not linger over the reuse cells that follow, and a finished session
    // leaves no line behind) without ever erasing another owner's message — the
    // preview's "Updating..." and the mirror's "Synchronizing..." live in the
    // same slot, each cleared by its own owner.
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

    // Result of one walk over the tmp/ batch root: the highest
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
    // observes cancellation, removes a FAILED cell's own partial output, and
    // either dispatches the next entry or finalizes the batch (clear progress
    // text, log summary). The cell's folder and basename are the dispatch's,
    // captured before the request was moved onto the worker: they name the
    // files this cell was to write, which is what a failure removes.
    void on_batch_entry_complete(RenderOutcome outcome,
                                 const std::string& cell_folder,
                                 const std::string& cell_basename);

    // Dispatch reqs[batch_.next_index] (or finalize when out of range / on
    // cancel). Caller must have already mutated batch_ so next_index points
    // to the entry to run.
    void dispatch_next_batch_entry();

    // Finalize the current single-render-or-batch run on the GUI thread:
    // clear queue_running, take down the state-cell text if this session
    // promoted one (status_promoted_ is that test, so a rung-served run and
    // another owner's line are both left alone), invalidate the bottom strip,
    // and drop the deferred status message with its signal. The summary log is
    // the caller's concern.
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
    // worker: deliverable naming (empty batch_folder/basename inside
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

    // Allocate the Ctrl+Alt+Shift+R miscellaneous output cell: derive tmp/
    // from the (process-immutable) source path, decide append-into-the-most-
    // recent `_miscellaneous` folder vs. a new `<max+1>_miscellaneous`,
    // create the folder, and scan it for the next `<N>.wav` cell. Writes the
    // folder path into `out_folder` and `<max_cell+1>` into `out_basename`;
    // returns false after printing the one stderr line on directory-creation
    // failure. MUST only run when the render worker is idle: idle means
    // GuiAsyncRenderer::is_busy() is false, which spans the whole
    // CompletionPending interval, so the prior worker has finished do_render
    // and ALL publication into tmp/ before the scan runs; every OTHER
    // tmp/ mutation (sweep batch-folder creation, the load-in-place
    // wipe) runs on
    // this same GUI event thread, so none can interleave with the scan
    // either; the only other writer thread, the master-floats cache writer,
    // writes solely under the cache tree, never tmp/ — so an
    // idle-moment scan cannot race a publication. The
    // command-time scan it replaces could, because a cancelled render may
    // still publish into tmp/ during its cancellation drain, after the
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

    // THE TRIM FALLBACK'S CARD ON THE ARCHIVAL ROAD (architect 2026-09-02,
    // deep dive item L): raise kTrimFallbackCard when the live trim pair is a
    // proper sub-window plan_trim will refuse, so the render this press
    // produces is the FULL, untrimmed piece under a hairline trim bar. Called
    // by the two archival CHORDS, whose requests carry the live stores and the
    // live trim verbatim — the full rationale, and the recorded asymmetry that
    // keeps the two SWEEPS out of it, are at the definition
    // (input_key_dispatch.cpp).
    void card_trim_fallback_if_any();

    // Sweep every BPM in the BPM owner's [bpm_lo, bpm_hi] range,
    // computing (base_tempo, scale) per cell and rendering one .wav per
    // cell into `<source parent>/tmp/<N>_bpm/`. The
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
    // nothing here (THE CUT DELETES ONLY ON A TRUE VERDICT, and cards its
    // refusal otherwise — the reasoning is at the case); paste takes whatever
    // the system clipboard holds — our own payload while we still own the selection,
    // another application's otherwise — and inserts it, doing NOTHING at all
    // when there is nothing to paste. Returns false for any other action so the
    // caller can fall through to its remaining branches.
    //
    // This and bare `j`'s value copy (copy_focused_marker_value,
    // input_key_dispatch.cpp) are the whole of the GUI's clipboard reach; no
    // other site copies or pastes. (`j` took that reach from the retired
    // readout's global Ctrl+C on 2026-08-29.)
    bool apply_editor_clipboard(text_editor::KeyAction action,
                                text_editor::State& s);

    // Shared wheel handler for source and target view; on_wheel is its only
    // caller. Exact-match modifiers, THREE arms (2026-08-27): PLAIN = the
    // waveform magnification step (one ladder rung per detent, up = taller,
    // through apply_waveform_magnification_level, the frame's whole count
    // clamped and applied in one call), ALT = the stepped pan (a tenth of the
    // visible span per detent, through the scroll_viewport funnel) and CTRL =
    // the zoom step (one whole level per detent, up = in, through
    // Viewport::zoom_steps — the Ctrl+`=`/Ctrl+`-` commands' own coalesced
    // body). Every other combination — Shift+wheel and every mixed pair —
    // no-ops. `inside_waveform` is true over the waveform area or the overview
    // lane, `inside_top` anywhere over the top strip; both mean "a wheel-live
    // navigation surface" and neither forks the vocabulary.
    void handle_wheel(GuiMouseButton button, int count, bool ctrl, bool shift,
                      bool alt, bool inside_waveform, bool inside_top);

    // Tab / Shift+Tab / IsoLeftTab dispatch: cycle marker focus, then stop
    // playback and move the playhead onto the newly focused marker. The zoom is
    // untouched — AT THE CURRENT ZOOM LEVEL, which the cycle never changes
    // (architect 2026-08-05, "no zoom on Tab", reverting the same-day
    // working-zoom landing this carried for one commit; `c` remains the direct
    // route to kWorkingZoomLevel, and `0`'s second arm reaches it through `c`
    // when its tab has stamped no return level). A step that focuses nothing
    // does nothing at all.
    //
    // FRAMING IS THE CALLER'S AND `frame` IS REQUIRED (architect 2026-09-04).
    // This body moves the focus and lands the playhead; it decides nothing
    // about the camera and reads no preference of its own — follow mode never
    // gated it, and the Center on next marker lamp does not gate it either.
    // The lamp governs the BARE Tab walk alone, so the three bare arms pass
    // marker_walk_frame(app) (app_state.h, the lamp's one reader) while the
    // Ctrl+Shift+Tab paired march passes MarkerLandingFrame::Center outright.
    // The parameter carries no default precisely so a future third caller
    // cannot inherit either answer by saying nothing.
    // The WHOLE Tab family comes through here: the three bare chords and the
    // Ctrl+Shift+Tab lockstep march, which calls this once per tab.
    // Mode-aware: reads from phaseresetmarkers in 'P' mode, warpmarkers
    // otherwise. The history mode's diff-flag cycle is the mode-local mirror of
    // this rule, over its own list (handle_history_mode_key).
    void cycle_marker_focus(bool forward, MarkerLandingFrame frame);

    // Jump the playhead directly onto the currently focused marker
    // (app.last_selected_marker), stopping playback and then treating the
    // camera as `frame` says — MarkerLandingFrame::Center recentering the
    // viewport on the landing AT THE LEVEL IT IS CALLED AT,
    // MarkerLandingFrame::FollowPage leaving the camera where it stands and
    // merely paging an OFFSCREEN landing into view through follow's own body.
    // The zoom belongs to the caller too, and the callers differ in it: `c`
    // sets the working zoom right after this returns, the Tab family sets
    // nothing (2026-08-05).
    // Returns true when a marker was
    // focused and the jump happened, false (leaving the playhead alone) when
    // there is none. This is the shared jump tail of cycle_marker_focus (the
    // Tab family) and the `c` gesture; a plain marker click is the other
    // land-onto-marker route (its own direct write in run_marker_click_act —
    // same two-step placement basis, but NO viewport move). Both leave the
    // playhead coincident with the focus, and a later nudge/drag re-lands it on
    // the focused marker as that marker moves.
    // `frame` IS REQUIRED and this body resolves nothing (architect
    // 2026-09-04): it asks no lamp, no follow bit and no mode, so every route
    // that lands a focus states its own camera. `c` states Center; the walk
    // states what ITS caller handed it.
    bool jump_playhead_to_focused_marker(MarkerLandingFrame frame);

    // The bare `0` key: FULL ZOOM OUT FIRST, THE `c` COMMAND WHEN ALREADY THERE
    // (architect 2026-08-05, replacing the working-zoom toggle; the second arm
    // was a bare center for one day). Below the per-file effective ceiling →
    // STAMP the level being left into the active tab's zoom_recall_level, then
    // jump to the ceiling (whole song visible); already at it → run_center_command
    // AT THE STAMPED LEVEL, falling back to the working zoom when nothing has
    // been stamped OR when the stamp can no longer move the zoom (a ceiling that
    // fell under it) — so `0` twice is overview then back to the magnification it
    // was pressed at (architect 2026-08-18), and overview then the working zoom
    // on a tab that never stamped. THIS FUNCTION IS THE STAMP'S ONE WRITER (the
    // field, app_state.h) AND, SINCE 2026-09-02, THE ONE SETTER OF THE TAB'S
    // WHOLE-SONG STATE (ViewState::whole_song_visible, R-17g): the zoom-out arm
    // raises it beside the stamp, the clamp chokepoint then keeps the level on
    // whatever the live ceiling becomes across a resize or an S/T flip, and the
    // already-full-out fork reads that state rather than a number that a moved
    // ceiling had made false. The FIRST arm is a
    // PURE VIEWPORT MOVE (architect 2026-07-30): it writes neither the selection
    // nor the region nor the playhead; the second carries `c`'s regime, stated
    // at that command. The rationale is at the definition.
    void run_overview_command();

    // THE SPAN-FRAMING command, run by the TRIM BAR LANE's DOUBLE-CLICK:
    // ZOOM TO A SPAN, never the working zoom. TWO ARMS since 2026-08-18 (the
    // region arm above them died with the separate region state) — a proper
    // trim SUB-WINDOW (expressed in the active domain) → the whole
    // song (full zoom-out, which the FULL trim window also takes). The trim span is framed with a 2.5%-per-side
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

    // P / I / M / K / L letter-key handlers plus the MEASURE PROPAGATE'S TWO
    // SLASH CHORDS: Ctrl+P-family phase-reset clipboard ops, `p` view toggle,
    // `i` iteration mode, `m` bpm mode, `k` ADD TO SELECTION (the sticky ctrl,
    // 2026-08-18), `l` listen-to-renders launcher, and Ctrl+/ / Ctrl+Alt+/
    // (the measure copy and paste, 2026-08-20 — they live here beside their
    // phase-reset twins rather than with bare `/`, which on_key claims well
    // above this dispatch). Returns true if key+mods matched one (on_key then
    // returns), false otherwise.
    bool handle_mode_keys(GuiKey key, GuiInputState mods);

    // Tab-key family: Ctrl+Tab / Ctrl+Shift+Tab switch A/B tabs; Tab /
    // Shift+Tab / IsoLeftTab cycle marker focus. Returns true if key+mods
    // matched one (on_key then returns), false otherwise.
    bool handle_tab_switch_keys(GuiKey key, GuiInputState mods);

    // THE WAVEFORM-LANE PLAYHEAD STEP'S ONE ACT BODY (2026-08-31, R12): the
    // stop, the stale-focus clear and the signed COLUMN step, shared by the
    // bare form (handle_plain_bare_keys' Left / Right arms below) and the two
    // modified ones (on_key's own arm, which must claim them above the bare
    // dispatch). `step_columns` is the press's signed painted-column count —
    // ±1 bare, ±3 shifted, ±10 with ctrl, through the ladder's owner
    // arrow_step_magnitude (gui_input.h). Reached only with an empty
    // selection: the marker-lane branch claims the press first in every
    // magnitude. The full contract is at the definition.
    void run_waveform_lane_playhead_step(int step_columns);

    // Bare-key (no-modifier) dispatch: playhead move / zoom / follow / center /
    // Home-End / trim begin-end. Caller gates on no modifiers held. Its
    // Left/Right arms are the WAVEFORM-LANE half of the horizontal arrows' BARE
    // form: the marker-lane half returns before reaching the tail, so these run
    // only with an empty selection (playhead_in_marker_lane false), and the
    // modified forms never arrive here at all.
    void handle_plain_bare_keys(GuiKey key);

    // THE Home / End JUMP, one body for every route that spells it. A form
    // that would change NOTHING — no live transport to stop, no selection to
    // clear, no shown overlay to hide, a landing the cursor already rests on
    // (playhead_end_jump_actionable, the acts' one owner) — refuses at the
    // head with a card naming its own rest; past that, its three acts are
    // UNCONDITIONAL and deliberately not gated on the jump moving anything:
    // stop a live audition, clear the marker selection (the marker
    // lane's exit — the rule is at land_playhead_on_marker, input_pointer.cpp)
    // and land through Viewport::move_playhead_to, which owns the trim region
    // overlay's hide. `forward` picks End over Home; `whole_piece` asks the
    // landing owner for the piece's own ends instead of the trim bounds, which
    // is what the CTRL forms and the skip buttons' modified arm pass
    // (playhead_skip_landing_frame, app_state.h, states the two arms).
    // FOUR CALLERS: the bare Home and End arms and their two CTRL arms. The
    // `h` history view's own pair is NOT one of them — it clears the MODE's
    // diff-flag focus where these clear the live selection, so it spells its
    // own body and shares only the landing owner, which its mode bit already
    // sends down the whole-piece arm.
    void run_playhead_end_jump(bool forward, bool whole_piece);

    // Shared key route for EVERY keyboard-modal editor — the settings prompt,
    // the commit-title editor, the measure paste-offset editor, the bpm
    // bracket editor, and (architect 2026-07-28) the top-strip flag editor. The modal contract is stated once
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
    // sees it. Every OTHER hook is REQUIRED and called unmodified: commit and
    // cancel are the per-editor bodies, and `repaint` is the
    // editor's own damage for a text change — the four dialog surfaces
    // pass the modal's own owner (the bottom row's lane,
    // viewport.cpp), the flag editor the top strip. `repaint` is
    // invoked UNCONDITIONALLY on every consumed key, so an empty std::function
    // there would throw; the route carries no null check for it deliberately (a
    // caller that forgets it is a program bug, not a runtime condition to guard).
    // CTRL+Q PASSES A HOOK NO LONGER: the route simply hands the close routing
    // on, and the CLOSE ROAD tears the editor down (GuiPrompt::request_close,
    // through close_modal_editors_no_commit) — one statement of what closes
    // before the quit question, shared with the compositor's own close.
    bool route_modal_editor_key(text_editor::State& ed, GuiKey key,
                                GuiInputState mods,
                                const std::function<bool()>& autocomplete,
                                const std::function<void()>& commit,
                                const std::function<void()>& cancel,
                                const std::function<void()>& repaint);

    // Routes a key to the active top-flag editor. Returns true if the editor
    // consumed it (on_key then returns); false on Ctrl+Q so on_key runs the
    // close routing. ALL THREE kinds now take route_modal_editor_key: the bpm
    // bracket editor as ever, the FlagPayload flag editor since it became
    // keyboard-modal, and the MeasureText measure editor since 2026-08-19 —
    // the three differ only in their commit/cancel bodies and in which area
    // they repaint. There is no longer a tail that cancels an edit to let an
    // unmatched key through: the gate means no unmatched key arrives.
    bool handle_top_flag_editor_key(GuiKey key, GuiInputState mods);

    // Routes a key to the active settings-prompt editor through
    // route_modal_editor_key, passing GuiSettingsEditor::autocomplete_value as
    // the bare-Tab hook: the value completion is Tab's under the one
    // autocomplete model (architect 2026-08-13 — completion first, the focus
    // ring when it did not advance; the typed-`=` trigger it wore for part of
    // that day is reverted with the ruling).
    bool handle_settings_editor_key(GuiKey key, GuiInputState mods);

    // -- THE PICKER (architect 2026-08-28, R22/R23; bodies in
    //    input_key_dispatch.cpp) ---------------------------------------------
    //
    // THE FIELD-LESS MODAL OVER THE FOLDER OVERLAY'S LIST — "we're not
    // allowing free-form typing there; the preview doesn't use a text field
    // and it works fine". The state is AppState::Picker (one bit and a
    // session id; the overlay's owner tag says WHICH content stands), the
    // fourth ModalDialogOwner and never an editor: the band is the whole
    // state, the row is **Cancel** alone since 2026-08-29 (a click on a row is
    // the open act), and route_picker_key below is its whole
    // plastic vocabulary. ONE CONTENT:
    //
    //   THE OPEN PROJECT PICKER — File → Open project and Ctrl+O, one chord
    //   (is_open_project_key, gui_input.h; on_key's arm dispatches it at the
    //   press, the read-only allowlist admits it because the picker authors
    //   nothing, and the File menu's row is that same chord dispatched
    //   through on_key at the lift like Quit's). It REOPENS IN PLACE:
    //   gui_main's loop rebuilds the object set around the chosen source (the
    //   contract is at main.cpp).
    //     open_project_picker: the opener. Refuses without touching
    //       playback while a prompt, any editor or a picker stands, and
    //       during a load — SAYING SO on the ONE arm where the user has a
    //       surface to leave (any editor: "Close the editor first") and
    //       SILENTLY on the other three, a prompt veiling everything and
    //       being its own answer, a second picker being what is already on
    //       screen (the reasoning is at the arms, 2026-08-30). THE RENDER
    //       PLAYER HAS NO ARM AT ALL since 2026-09-03: its own router consumes
    //       Ctrl+O in silence, and the File menu's Open Project row IS that
    //       chord (the row dispatches it through on_key), so no road reaches
    //       this body from in there even with the anchor live above the band
    //       since that evening — and the picker's and the stats panel's
    //       routers answer the same way. ("Close the render player first"
    //       stood from 2026-08-30 and a close-then-open body replaced it for
    //       the one day the File anchor stayed lit under the panel; both are
    //       retired.) It
    //       stops playback through the
    //       shared modal stop only once the picker is definitely opening,
    //       mints the session and builds the list. IT IS ADMITTED IN THE `h`
    //       HISTORY VIEW since 2026-08-29 (architect, "admit both"), on both
    //       roads: history_mode_key_blocked admits Ctrl+O beside Ctrl+Q, and
    //       this body's own refusal is deleted, so the File menu has no dead
    //       row in the view. The picker stands OVER the mode (its router runs
    //       ahead of the mode's gate in on_key and its veil consumes the
    //       view's presses); a Cancel leaves the view untouched, this view
    //       owning no navigation state; an open tears it down as Ctrl+Q's
    //       exit does.
    //     build_project_picker_rows: THE SESSION'S ONE LIST, built AT THE
    //       OPEN — the folder names under projects_path (the model's own
    //       order and its own membership, project_model.h: the enumeration
    //       already drops every name the device config cannot carry) filtered
    //       here by the model (resolve_project), so a row that would refuse at
    //       Enter is not a row: an invalid folder simply does not show
    //       (architect R8). Never
    //       kept fresh (a project that appears or vanishes while the picker
    //       stands shows at the next open), no `..` row and no folder inside
    //       a project — a project is a leaf here. The band opens on the
    //       CURRENT project's row.
    //     open_project_commit: THE OPEN ACT on row `index` — a click on the
    //       row and Enter on the highlight both reach it, one body. The
    //       row's name goes through the project model (resolve_project); the
    //       project already open is a consumed no-op that CLOSES the picker;
    //       then the STRICT SIDECAR DRY-RUN (source_load_dry_run,
    //       file_loader.h) — the load's own failure arms run before anything
    //       is torn down. Either refusal says its reason on a notification
    //       card and the picker STAYS OPEN (there is no field to red-flash).
    //       Only a
    //       project that passes both reaches the reopen: the picker closes,
    //       the name is seated in app.reopen_project and the close request
    //       runs with the REOPEN target — the unsaved-tab prompt EXACTLY as
    //       Ctrl+Q's, its answer completing a reopen instead of an exit
    //       (GuiCloseTarget, prompt.h). A RUNNING RENDER IS KILLED BY THE
    //       SESSION'S TEARDOWN and not by this act, because this act only
    //       ASKS: the prompt's Cancel keeps the session, and a kill spelled
    //       here would already have taken a sweep down for a reopen that did
    //       not happen. The teardown's worker join is the one kill, shared by
    //       both completions (main.cpp's run_project).
    //
    //   (THE `h` VIEW HAD A SECOND PICKER for one day — bare `'` over the
    //   viewed walk's members — and it was retired 2026-08-29 as
    //   "overengineered; I don't really need it": that key raises the load
    //   CONFIRMATION on the viewed member directly now, with no list
    //   (history_load_in_place, input_key_dispatch.cpp).)
    //
    //   THE PICKER'S SHARED MACHINERY:
    //     picker_open_highlight: Enter on the list — the open act on the
    //       highlighted row, forked on the overlay's owner through
    //       folder_overlay_open_row; a -1 highlight is a consumed no-op.
    //     picker_set_highlight / picker_move_highlight: the widget's two
    //       highlight mechanics (folder_overlay.h) with the picker's damage —
    //       the row click's first half (folder_overlay_highlight_row's picker
    //       arm) and bare Up / Down's walk.
    //     close_picker: THE ONE CLOSE BODY — Esc, the Cancel button,
    //       Ctrl+Q's and the
    //       compositor's close road (GuiPrompt::request_close, at its head
    //       beside the player's close), the same-project no-op and the
    //       reopen's own tail all pass through it; the overlay leaves with
    //       it (the reset restores Owner::None, which IS the band's standing
    //       predicate answering false). A no-op when no picker stands.
    //     route_picker_key: THE WHOLE PLASTIC VOCABULARY while a picker
    //       stands, in the render player's router's shape and at its rank in
    //       on_key (under the prompt gate and the dropdown gate, above every
    //       ordinary dispatch): Tab / Shift+Tab walk the ring [list, Cancel]
    //       through the one modal ring route (opening NOWHERE, the
    //       first Tab landing on the list); Enter presses a ring-focused
    //       button (press-at-press, commit-at-release, the modal's own) or,
    //       with none focused, OPENS the highlight; Space presses a focused
    //       button only; Up / Down walk the highlight (repeat-eligible,
    //       scrolling to keep it visible); Esc closes; Ctrl+S saves with the
    //       picker standing (GuiSaveOps::save — the typed prompt's own
    //       contract); Ctrl+Q falls through to the close
    //       road. EVERY OTHER CHORD IS CONSUMED (strict modifier validation's
    //       no-op). Returns true when consumed here, false for the one
    //       fall-through.
    //
    // No undo (a reopen discards the stack by construction, and the project
    // picker authors nothing); LEGAL ON A READ-ONLY TAB (Ctrl+O is a named
    // entry on
    // read_only_key_blocked's allowlist; bare `'` stays blocked there, its
    // name being the load's). Ctrl+O admits no shift and no alt, so
    // Ctrl+Shift+O stays the strict rule's consumed no-op, and no editor
    // admits the chord: an open editor swallows it whole through
    // route_modal_editor_key like every chord that is not one of its own.
    //
    // THE PICKER IS NOT A SECOND ROAD ONTO A FIELD ANY MORE, and the
    // no-second-road doctrine's stated exception for the Open prompt
    // (2026-08-28, one afternoon: GLASS NEEDS A ROAD THAT IS NOT TYPING) is
    // SPENT rather than kept — the list is now the only road on both
    // backends, and a row's click and Enter are one act on it.
    void open_project_picker();
    void build_project_picker_rows();
    void open_project_commit(int index);
    void picker_open_highlight();
    void picker_set_highlight(int index);
    void picker_move_highlight(int delta);
    // (close_picker is declared public above, beside
    // close_modal_editors_no_commit, for GuiPrompt::request_close.)
    bool route_picker_key(GuiKey key, GuiInputState mods);
    // The picker's standing predicate, the mode bit and nothing else — named
    // so its readers (the veil, the cursor, the hover walk, the chrome
    // release's re-ask, the touch region begin, the stash's live owner) say
    // what they mean, exactly as render_player_active below does.
    bool picker_active() const { return app.picker.active; }

    // -- THE AV SYNC STATS PANEL (architect 2026-09-03) --------------------
    //
    // HELP → AV SYNC STATS: the folder overlay's THIRD content, a listing of
    // TEXT rows over a bottom row that is **Copy to Clipboard · Close**. What
    // it shows and why the two figures mean what they do is at
    // av_sync_stats.h; what is here is the mode.
    //
    // ONE OPENER, REACHED THREE WAYS since 2026-09-03: the Help menu's row,
    // which is its chord like every other command row and dispatches Shift+L
    // through on_key; that chord pressed on the keyboard (is_av_sync_stats_key,
    // gui_input.h — bare `l`'s shifted twin, both toggles of one overlay); and
    // the Play renders button's shift-click or long press, which synthesizes
    // the same chord. The row carried no chord at all from the panel's landing
    // that morning until that evening, the opener's own body holding the gates
    // a chord would have met; the gates stayed where they are and the road
    // count changed. The opener refuses — silently, touching no playback —
    // under a prompt, under any keyboard-modal editor, under the render
    // player, under a picker, under a standing panel, in the `h` history view
    // and during a load; past every one of those it takes the shared modal
    // stop (stop_playback_for_modal_open, whose decision table names every
    // dialog modal surface), raises the band and arms the display measurement.
    //
    // THE MEASUREMENTS RUN ONLY WHILE IT STANDS, which is the feature and not
    // an optimization (the architect's ruling). Three things are gated and
    // each is gated at its own owner: the DISPLAY instrument, armed and
    // disarmed by this pair through GuiPlatform::set_display_measurement, so
    // no presentation feedback object is ever created with the panel down; the
    // AUDIO read, which is a plain query refresh_stats_panel_rows makes and
    // nothing else in the product calls; and the PER-FRAME PAINT, which is
    // that same refresh, called from the run loop's tick under the mode bit
    // (main.cpp) and from nowhere else.
    //
    //   open_av_sync_stats:       the opener (above), with
    //     toggle_av_sync_stats the keyboard's toggle over it and the one close
    //     body.
    //   close_stats_panel:        THE ONE CLOSE BODY — Esc, the Close button
    //     and Ctrl+Q's / the compositor's close road (GuiPrompt::request_close,
    //     beside the player's and the picker's) all pass through it. It
    //     DISARMS the measurement, drops the overlay (the reset restores
    //     Owner::None, which IS the band's standing predicate answering false)
    //     and damages the whole window. A no-op when no panel stands.
    //   refresh_stats_panel_rows: recompose the rows from the two live
    //     readings and damage the band when a line changed. Called once per
    //     tick while the panel stands; a no-op otherwise.
    //   route_stats_panel_key:    THE WHOLE PLASTIC VOCABULARY while it
    //     stands, in route_picker_key's shape and at its rank in on_key:
    //     Ctrl+S saves (with the checkpoint-in-flight card the picker raises);
    //     Ctrl+C copies the whole report (copy_stats_panel_report, the button's
    //     key twin); Ctrl+Q is THE ONE FALL-THROUGH; Tab / Shift+Tab walk the
    //     ring [band, Copy to Clipboard, Close]; Esc closes; Up / Down SCROLL
    //     THE BAND (there is no
    //     highlight to walk — which is exactly why this panel could not reuse
    //     route_picker_key and needed a modal owner of its own, the record
    //     being at AppState::ModalDialogOwner); Shift+L closes, the opener's
    //     own chord answering in here as bare `l` answers inside the player;
    //     every other bare key and every other modified chord is consumed,
    //     silently.
    //
    // No undo, nothing authored, LEGAL ON A READ-ONLY TAB — it reads hardware
    // and touches no piece.
    void open_av_sync_stats();
    // The panel's opener toggle for Shift+L (2026-09-03), bare `l`'s shifted
    // twin and toggle_render_player's own shape: a standing panel closes
    // through the one close body, and anything else opens through the one
    // opener above, whose gates decide whether the open happens at all. Two
    // callers, both keyboard: the dispatch arm in handle_mode_keys beside bare
    // `l`, and the Play renders button's shift-click or long press, which
    // synthesizes that same chord.
    void toggle_av_sync_stats();
    // Compose the rows from the two live readings and seat them in the
    // overlay's table (the TABLE alone, so a refresh keeps the scroll offset
    // and the band's press arm). A frame in which no line changed leaves the
    // stored strings untouched and costs one composition — it does NOT decide
    // whether the refresh damages, which is unconditional while the panel
    // stands (the heartbeat's record is at refresh_stats_panel_rows). Two
    // callers: the opener's first listing and the tick's refresh.
    void build_stats_panel_rows();
    // THE WHOLE REPORT ONTO THE SYSTEM CLIPBOARD, and its card. TWO CALLERS,
    // one body: the modal row's Copy to Clipboard button at its lift
    // (dispatch_modal_dialog_button's Stats arm) and Ctrl+C in the router
    // below. It reads the overlay's own rows — no second copy of the text
    // exists — and it CANNOT REFUSE, which is why its button carries no face
    // arm (the reasoning is at the definition, input_key_dispatch.cpp).
    void copy_stats_panel_report();
    bool route_stats_panel_key(GuiKey key, GuiInputState mods);
    // (close_stats_panel and refresh_stats_panel_rows are declared public
    // above, beside close_picker, for GuiPrompt::request_close and for the run
    // loop's tick.)
    // The panel's standing predicate, the mode bit and nothing else — named so
    // its readers (the veil, the cursor, the hover walk, the chrome release's
    // re-ask, the touch region begin, the stash's live owner) say what they
    // mean, exactly as picker_active above and render_player_active below do.
    bool stats_panel_active() const { return app.stats_panel.active; }

    // SYNCHRONIZE TO EXTERNAL STORAGE (architect 2026-08-27) — reached from TWO
    // places, both landing in this one body: the File menu's own row
    // (finish_dropdown_release, the GuiPopupAct::SyncExternal item, which calls
    // this directly rather than dispatching a chord) and BARE BACKSLASH since
    // 2026-08-31 (architect; is_sync_external_key, gui_input.h — on_key's arm
    // sits beside Ctrl+O's, and both allowlists admit it). It was the File
    // menu's ONE CHORD-LESS act from 2026-08-28, when Open took Ctrl+O, until
    // that date: the binding had been REFUSED rather than deferred
    // (Ctrl+Alt+Shift+R keeping its current meaning), and `\` is the spelling
    // that costs the render family nothing. WHAT it mirrors onto the stick, and the mirror's own scope and
    // order, are stated whole at external_sync.h; WHERE it goes is the DEVICE
    // CONFIG's `sync_path` since 2026-08-30 (device_config.h — it was the
    // seam's own `GuiPlatform::removable_volume` answer until then, a
    // discovery that could never work on the tablet). What is here is the
    // act's GUI half.
    //
    // synchronize_to_external_storage: the opener. It refuses silently, without
    // touching playback, while a prompt or any editor stands and during a
    // load — the Open project row's own three gates, mirrored
    // because a menu row's refusals are the menu's, not the act's, and
    // silent here where the Open row's editor arm speaks, this row being
    // unreachable under a pointer-transparent editor's own menu press — and
    // it refuses with no source loaded, having nothing to mirror. ITS OWN
    // refusals all card (the already-running answer and the unset key
    // below). IT IS ADMITTED
    // IN THE `h` HISTORY VIEW since 2026-08-29 (architect, "admit both", with
    // the Open row): a viewer running it is a viewer copying files, and THE
    // VIEW COSTS IT NOTHING — its sentences are notification cards, which the
    // mode cannot hide (they were the status chain's transient tier, invisible
    // under the mode's own line, for the one day before the cards landed).
    // It is LEGAL ON A
    // READ-ONLY TAB (it authors nothing: it reads two output folders and writes
    // outside the project entirely) and STOPS NO PLAYBACK (the act is silent
    // and changes no audio; nothing about the sound is different while it
    // runs). A SECOND ACT WHILE ONE RUNS is a consumed no-op that says so on
    // a notification card — the checkpoint act's single-in-flight shape,
    // answered in words rather than with a grey, since a menu item never
    // greys. Then the DESTINATION: an EMPTY `sync_path` is the device saying
    // it has none, answered `sync_path is not set` on a card — the key named
    // by its own spelling — and the act ends there. Otherwise the job is
    // captured whole by value — the sync root, the
    // project name, the project folder, and the project's TWO OUTPUT FOLDERS
    // themselves, `render/` and `tmp/` (no title: the act lists them, so the
    // set it mirrors is the set the disk holds; external_sync.h rule 1)
    // — and the worker takes it; nothing says the act has
    // started (a process line is state, and the verdict follows within
    // seconds).
    // on_external_sync_complete: the verdict, back on the main thread through
    // main.cpp's eventfd wiring. IT RAISES NOTHING ON SUCCESS (architect
    // 2026-08-30, the render's precedent: a render served silently publishes
    // silently), and a FAILURE takes the worker's own sentence onto a NORMAL
    // notification card and nothing else — never the critical class: that
    // class is the checkpoint act's, whose
    // failure leaves the repository in a state only the terminal can fix, while
    // a failed synchronization is retried by pressing the row again.
    void synchronize_to_external_storage();
    void on_external_sync_complete(GuiExternalSyncOutcome outcome);

    // THE COMMIT-TITLE EDITOR (architect 2026-08-07) — the settings editor's
    // dialog pattern for the history view's OTHER act. Ctrl+S while the view
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
    // architect moved the act onto the SAVE button's chord). IT OWNS THE ACT'S
    // TWO SESSION REFUSALS since 2026-09-01 (a checkpoint in flight, an empty
    // head delta), which the mode's allowlist carried as admission terms until
    // then; its body states both.
    // commit_title_editor_commit: Enter — validate non-blank, close the editor,
    // run the act (run_history_commit, which owns the save, the close and the
    // dispatch).
    // commit_title_editor_exit_no_commit: the abandon body — Esc's cancel, and
    // Ctrl+Q's through the close road's own step (close_modal_editors_no_commit).
    // handle_commit_title_editor_key: the key router, through
    // route_modal_editor_key like the three editors before it. It passes NO
    // autocomplete hook — there is no vocabulary here to complete against, a
    // commit title being free text — so bare Tab walks the dialog's focus ring
    // from the first press, as it does in the bpm editor.
    void open_history_commit_editor();
    void commit_title_editor_commit();
    void commit_title_editor_exit_no_commit();
    bool handle_commit_title_editor_key(GuiKey key, GuiInputState mods);

    // THE MEASURE PROPAGATE (architect 2026-08-20) — the phase reset
    // propagate's shape for the marker MEASURE field, and the second member of
    // that family. It lives HERE rather than in a module of its own, unlike
    // PhaseResetPropagate: that one needs a target render, an active-views
    // handle and an end-of-paste view switch, while this one writes a string
    // field on markers already on screen, and its whole modal surface is a
    // dialog editor whose four bodies are handler methods anyway.
    //
    // copy_measures_from_selection: Ctrl+/. Caller has verified W-mode + a
    // CONTIGUOUS run of warp markers selected — the SAME contiguity gate
    // Ctrl+P takes, and for the same reason: the paste matches label sequences
    // in strict lockstep, so a gap would misalign them. Replaces the clipboard
    // with one entry per selected marker that PROPAGATES (labeled and
    // effectively enabled, `warp_marker_propagates`). Non-mutating — no undo
    // entry, no dirty bit, no marker changes.
    //
    // open_measure_paste_editor: Ctrl+Alt+/. Caller has verified W-mode + a
    // non-empty measure clipboard + exactly one selected warp marker. Stops
    // playback, seats that marker as the editor's subject and raises the
    // paste-offset dialog seeded with `0`, open-selected.
    //
    // measure_offset_editor_commit: Enter — parse the buffer as one canonical
    // signed integer, run the paste, close on success; red-flash and STAY OPEN
    // on either refusal (a malformed offset, or an offset that would carry a
    // pasted measure out of the [1, 99999] bracket).
    // measure_offset_editor_exit_no_commit: the abandon body — Esc's cancel, and
    // Ctrl+Q's through the close road's own step (close_modal_editors_no_commit);
    // the anchor
    // dies with the session.
    // handle_measure_offset_editor_key: the key router, through
    // route_modal_editor_key like the four editors before it. NO autocomplete
    // hook — an integer has no vocabulary to complete against — so bare Tab
    // walks the dialog's focus ring from the first press.
    //
    // apply_measure_paste: the act, called ONLY from the commit above. Returns
    // A REFUSAL SENTENCE having written NOTHING when the paste cannot be
    // honored whole (GuiOpRefusal, warpmarkers_ops.h — std::nullopt is every
    // path that completed, the ones that wrote nothing included); the commit
    // red-flashes and cards it. The contract is stated in full at the
    // definition.
    void copy_measures_from_selection();
    void open_measure_paste_editor();
    void measure_offset_editor_commit();
    void measure_offset_editor_exit_no_commit();
    bool handle_measure_offset_editor_key(GuiKey key, GuiInputState mods);
    GuiOpRefusal apply_measure_paste(int64_t offset_measures);

    // -- THE RECIPE APPLY, and the rule it exists to state once ----------
    //
    // A LOAD IN PLACE WRITES EXACTLY WHAT ITS ONE UNDO ENTRY RESTORES
    // (architect 2026-08-24: "load in place should overwrite elements which
    // have undo — so trim is excluded also"). That is the warp store, the phase
    // reset store and the engine settings — what push_undo_both captures — and
    // NOTHING ELSE. Both tab bands stay live, TRIM INCLUDED (trim has no undo;
    // Shift+[ is its recovery), and so do the S/T bit, the W/P bit, the A/B
    // tab, the camera, follow, the waveform magnification level and
    // projects_repo — and gui_scale is outside the question
    // entirely since 2026-08-27, an entry's sidecar not carrying it at all. A recipe is a set of markers and an engine block; where
    // the user is standing when he loads one is his own. Undo/redo and the `h`
    // view are how he then inspects what the load changed. (It SUPERSEDES the
    // whole-file apply that stood until this date, which was 1:1 with a source
    // load of the same sidecars — that one yanked a source-view session into
    // target view on the entry's dispatch tab and restored the
    // dispatch-moment camera, three things the user never asked for.)
    //
    // THE WHOLE LOAD-IN-PLACE FAMILY SHARES THIS ONE BODY — THREE CALLERS,
    // re-greped 2026-08-28: load_render_entry_in_place (a tmp/ entry),
    // load_history_commit_in_place (a commit's sidecars) and
    // load_history_local_entry_in_place (a state of this session's own
    // undo/redo timeline, which joined on 2026-08-24, having hand-rolled the
    // same sequence minus the live-playhead clamp and the basis reset). So the
    // rule lives here rather than in three places, and the three acts differ
    // only in where their three pieces come from and in the tail each keeps.
    //
    // ITS PRECONDITIONS, both the caller's: every input is read, validated and
    // past its last refusal (nothing here can fail, and nothing may mutate
    // before it), and the `h` mode is already closed where each caller's own
    // reasoning puts that close. What it does, in order: snapshot the outgoing
    // stores, replace both, clear the selection, push ONE cross-file undo entry
    // (the live W/P as its op_mode and NO tab override — the entry belongs to
    // the tab the user is standing in, no tab switch happening any more), run
    // the bpm scratch wipe (a statement over a set that already carries
    // defaults — NO bracket is touched and NEITHER sweep-mode bit moves since
    // 2026-09-02: iteration mode is where you stand, the record at the body),
    // assign the engine block, take the store-change basis reset, clamp
    // the live playhead and viewport into the possibly-changed domain, and run
    // the coincidence auto-select and the sync/invalidate/trigger tail. Each
    // caller keeps its own tail after it (the tmp/ trash-then-wipe, the
    // stderr line, the full-window invalidate).
    void apply_recipe_in_place(std::vector<GuiWarpMarker> warp,
                               std::vector<GuiPhaseResetMarker> phase_resets,
                               const EngineSettings& engine);

    // THE PROMOTE ROADS' PAST-EOF WALL GUARD (2026-08-29), the ONE owner for
    // the three roads that install a marker set the LIVE stores did not
    // author. It asks the loader's own shared guard
    // (first_past_eof_wall_defect, marker_store_validate.h) of a candidate
    // marker pair against THIS session's audio and hands back the guard's own
    // first-offender sentence, or nullopt when every position is inside its
    // wall.
    //
    // WHY IT EXISTS: the startup loader and the picker's dry run both call
    // that guard, and a marker past `total - 1` is the ADVERSARIAL class they
    // hard-fail on — "the audio was swapped outside the GUI". But a `tmp/`
    // cell, a checkpoint's sidecars and a diff flag's then side are all
    // sidecar-shaped state authored against WHATEVER audio stood at the time,
    // and none of them passed through those two doors. Without this the
    // program could install a marker past the wall, Ctrl+S would write it, and
    // the next launch would refuse the file this program wrote — the same
    // class as a program-written file the program then refuses. It also keeps
    // warp_frame_map_build.cpp's "unreachable from a live store" wall true.
    //
    // THE TRIM PAIR PASSED IN IS THE LIVE ONE and can only pass: a load in
    // place writes no trim, every trim gesture walls at total-1, and the load
    // boundary proved this pair at startup. It is passed because the guard's
    // six checks are one call; the marker arms are the only ones that can
    // fire here.
    //
    // THREE CALLERS: load_render_entry_in_place and
    // load_history_commit_in_place (each of the parsed pair, before
    // apply_recipe_in_place) and run_history_revert (of the markers the
    // subject's REMOVED flags would restore, refusing the whole revert). The
    // LOCAL-tab load is deliberately NOT a caller: its state is a snapshot of
    // THIS session's own stores, which were proved at the load boundary and
    // walled by every gesture since, so there is no foreign authoring in it.
    std::optional<std::string> in_place_load_wall_defect(
        const std::vector<GuiWarpMarker>& warp,
        const std::vector<GuiPhaseResetMarker>& phase_resets) const;

    // load_render_entry_in_place: apply render entry `e`'s frozen sidecar recipe
    // (.settings + the marker pair) as the new authoring baseline, view-
    // agnostic (source OR target authoring view). Reads and validates the wav's
    // existence and all three sidecars BEFORE mutating any store, and returns
    // false leaving authoring untouched on any missing/malformed input — each
    // such genuine-failure arm naming its cause and path on stderr since
    // 2026-08-02, while the caller's unknown-id refusal (a typo) stays silent
    // behind its red flash; otherwise applies the recipe through
    // apply_recipe_in_place above — the marker pair and the engine block, the
    // file's view keys and tab bands ignored — wipes tmp/, and returns true.
    // ONE CALLER, re-greped: the RENDER PLAYER's Load in place button through
    // its confirmation (confirm_load_in_place's player arm, the highlighted
    // batch cell), which copies the entry before calling — the tail wipes
    // tmp/. THE
    // TWO-ROAD SANCTION RETIRED WITH THE TYPED ROAD (2026-08-28): the act had
    // a second caller for part of that day, the `'` load editor's Enter
    // matching a typed entry id, and the design (§4) sanctioned the pair as the
    // no-second-road doctrine's stated exception because glass needs a road
    // that is not typing. Then bare `'` outside the `h` view became the
    // player's opener, leaving the typed road unreachable, and it was deleted
    // — so the exception is spent and the doctrine holds here plainly again.
    bool load_render_entry_in_place(const AppState::RenderEntry& e);

    // load_history_commit_in_place: the same act with the COMMITTED HISTORY as its
    // source — apply the three sidecars commit `sha` carried (the full SHA
    // the prefetch store holds for the VIEWED member — the one caller,
    // confirm_load_in_place, hands it a store member; the typed spelling
    // retired with the load prompt's field) as the new
    // authoring baseline, in memory, with the disk untouched.
    // Validate-before-mutate like its sibling: the resolve, the three-sidecar
    // presence and all three STRICT whole-file parses run before any store is
    // written, each failure returning false with one stderr line naming the
    // cause, the committed path and the SHA. No wav is compared (the corpus
    // stores no audio — the loaded source is the source), no tmp/ wipe and so
    // no running-render guard, and the mode itself closes as part of the act.
    // Gated on the mode standing: the sidecar base name is the session's.
    // Full behaviour paragraph at the definition.
    bool load_history_commit_in_place(const std::string& sha);

    // load_history_local_entry_in_place: the same act with A STATE OF THIS
    // SESSION'S OWN UNDO/REDO TIMELINE as its source (architect 2026-08-08) —
    // apply member `number`, its displayed NUMBER in [1, N] (the VIEWED
    // member, handed over by confirm_load_in_place), as the new authoring
    // baseline. Validate-before-mutate like both siblings: a zero,
    // out-of-range or unreadable member is one stderr line and a false return
    // with nothing touched. It restores exactly what an undo
    // entry carries — the two marker columns and the engine block — which since
    // 2026-08-24 is exactly what its two sidecar-sourced siblings write as well,
    // and since 2026-08-24 it writes them THROUGH the same body
    // (apply_recipe_in_place above), so the three differ only in where the three
    // pieces come from. It applies them ON TOP as ONE new undo entry
    // rather than as a rollback, writes no disk and
    // closes the mode as part of the act. Full behaviour paragraph at the
    // definition.
    bool load_history_local_entry_in_place(std::size_t number);

    // THE SWEEP'S TRIM WRITE (architect 2026-08-18 — the region IS the trim):
    // one ANCHOR and one MOVING end, both whole SOURCE frames already inside
    // the song walls (sweep_trim_frame_at_column below is their one producer),
    // written straight into the trim store as an ordered pair — no domain hop
    // in here. Its two entries are the
    // shift+drag former and the touch region hold, and both call it PER MOTION
    // EVENT, so a stroke sets the trim in one gesture with no need to show the
    // overlay first. It takes the setter's regime at the first accepted change
    // — the trim-mutation playback stop and the deselect — and leaves the
    // PLAYHEAD PARK and the shared commit tail to the release
    // (commit_region_sweep), the endcap drag's own timing. IT ENFORCES NO
    // WIDTH: the ordered pair is written as the two ends describe it, walls
    // apart, and a COINCIDENT stroke is cleared to the whole song by the
    // release's auto_clear_crossed_trim — the endcap drag's escape, shared
    // rather than copied.
    // Returns whether a bound was actually written, which is the release's
    // commit gate.
    //
    // (SET THE TRIM FROM A FREE SCRATCH SPAN is RETIRED with the two-step model
    // it completed: setting the region IS setting the trim now. The key it used
    // was repointed onto the overlay's show/hide toggle,
    // handle_toggle_trim_region, which writes no bound and is not a member of
    // anything on this page. The MAXIMIZER survives unchanged as the recovery
    // route.)
    bool write_trim_from_sweep(int64_t anchor_source, int64_t moving_source);

    // THE SWEEP'S ONE COLUMN->TRIM ROUTE: the whole SOURCE frame a
    // waveform-relative column authors, through authored_frame_at_column over
    // the displayed-or-live target map with the song walls applied after —
    // the lattice every other trim former commits on. Both of the sweep's
    // trim ends (the arm's anchor, the motion path's moving end) come from
    // here and nowhere else; the PLAYHEAD those same columns seat and carry
    // takes the active-domain frame from playhead_frame_at_click_column
    // instead, the two domains kept apart (the rule and its reasons are at
    // RegionDragState::anchor_source_frame and at the definition).
    int64_t sweep_trim_frame_at_column(int col) const;

    // (THE MINIMUM TRIM WIDTH FLOOR — kMinTrimSpanFrames, a flat count of
    // source frames derived from the engine window and the authored value
    // brackets — lived one day, 2026-08-18..19, and is DELETED: the architect
    // found the enforced minimum distracting and too short to be worth its
    // machinery, and ruled the degenerate cases get the no-backstop treatment.
    // The retirement record, including what now answers each degenerate shape,
    // is at write_trim_from_sweep in input_trim.cpp. Do not reinstate a span
    // floor at a gesture: the render boundary owns whether a window is
    // honorable, and it answers with an untrimmed render, not a refusal.)

    // Shift+[ MAXIMIZES the trim to the full window [0, total-1] (architect
    // 2026-07-25 for the act, re-posed 2026-07-30 under always-set: the full
    // window IS the old unset state — it renders untrimmed and plays to the
    // natural end). Read-only admits it (trim is band, the ruling at
    // read_only_key_blocked); the body then delegates WHOLE to
    // handle_trim_clear_both. A trim MAXIMIZER, not a setter: it does NOT
    // deselect. It is also THE RECOVERY ROUTE — trim has no undo, and since the
    // region became the trim (2026-08-18) this is the keyboard's way back to the
    // whole song, the pointer's and the finger's being a bound dragged onto its
    // partner.
    void handle_trim_maximize();

    // BARE `[` SHOWS AND HIDES THE TRIM REGION OVERLAY (architect 2026-08-16
    // for the act, made a TOGGLE on 2026-08-18 when the region became the trim
    // and the set-from-region act stopped existing; Ctrl+Shift+X carried the
    // show act for two days before that and is UNBOUND, a no-op everywhere
    // under strict modifier validation) —
    // the icon row's
    // IconShowRegion button and its keyboard twin. ONE ACT, TWO HALVES over the
    // one visibility bit that is the whole region state: SHOW and BRING THE
    // SPAN INTO VIEW through bring_span_into_view, or HIDE. It writes NO TRIM,
    // no selection and no playhead, and HIDING DISCARDS NOTHING — the trim
    // persists and a later show restores an identical overlay, which is what
    // makes a toggle safe here where the 2026-08-16 existence-lamp design was
    // not (that record, and why its hole cannot recur, are at the definition
    // and at the roster entry, app_state.h). Read-only-LEGAL (its entry is on
    // read_only_key_blocked's allowlist), and the `h` view simply consumes it —
    // not on that mode's allowlist, so the derived partition greys the button
    // with nothing hand-listed, which is where trim's freeze in that view is
    // expressed for this act.
    void handle_toggle_trim_region();

    // Write the FULL window [0, total-1]. Silent no-op when the window is
    // ALREADY full (the identity guard that replaced the old has-a-bound refusal
    // gate — an already-maximized Shift+[ stops nothing, repaints nothing and
    // triggers nothing). The caller is handle_trim_maximize.
    void handle_trim_clear_both();

    // Field-reset core shared by handle_trim_clear_both (the Shift+[ maximizer)
    // and the crossed-commit reset below: write the canonical full pair for the
    // loaded source through the one seeding owner, full_trim_window
    // (app_state.h). No invalidation and no trigger — callers own their repaint
    // tail. One implementation so the two can never drift.
    void reset_trim_to_full_window();

    // Crossed/equal trim cannot REST (ruling comment at the definition): when
    // end_frame <= begin_frame — exact integer compare — at a trim COMMIT, RESET
    // both bounds to the song edges (the endcaps jump there, and since
    // 2026-08-30 the reset also says so on one card raised here, this being
    // the one owner every former reaches). Recognizes the already-full window first, so the one-frame
    // canonical pair [0, 0] is left alone rather than reset every commit. Called
    // by every trim commit site after its mutation and before its
    // invalidations, so the repaint shows the reset state.
    void auto_clear_crossed_trim();

    // EVERY TRIM WRITE PARKS THE PLAYHEAD AT THE NEW TRIM START (architect
    // 2026-08-05, generalizing the trim region toggle's own 2026-07-30 land).
    // Reads the COMMITTED
    // begin out of the store — never a caller's local — so a crossed pair that
    // the commit tail has just RESET to the full window parks the playhead at
    // frame 0, which is that window's start: a reset is a trim write like any
    // other and needs no arm of its own. Lands through
    // land_playhead_on_marker's placement basis (source frame → active domain →
    // live-domain clamp) with NO viewport move, so a trim start offscreen
    // leaves the view where the user left it. IT DOES NOT HIDE THE TRIM REGION
    // OVERLAY: the region IS the trim, so hiding here would hide the overlay the
    // instant its own bound was dragged — and since 2026-08-19 that exemption is
    // structural, this function writing the cursor DIRECT and so reaching
    // neither of the rule's two movement owners (the rule at
    // clear_region_highlight; the argument at this function's definition).
    // Callers own the refusals above it: a route that
    // writes no bound must not call this. The full per-route inventory is at
    // the head of input_trim.cpp.
    void park_playhead_at_trim_start();

    // THE SHARED TRIM COMMIT TAIL, in code rather than in prose: every
    // trim-SETTING commit runs the same acts in the same order
    // (auto_clear_crossed_trim, the waveform + status-chain repaints, the
    // target-render trigger, then the playhead park above), and this member is
    // their one spelling. FOUR
    // CALLERS (re-derived 2026-08-18, when the SET-FROM-REGION act retired and
    // the sweep took its place; the trim region toggle calls none of this —
    // handle_toggle_trim_region writes no trim)
    // — the SWEEP's release (commit_region_sweep, input_pointer.cpp),
    // the drag release (commit_trim_drag) and the bound-set click
    // (set_trim_bound_at_click), the last two in
    // input_trim.cpp, plus the settings editor's `trim_*=` active-tab arm
    // (settings_editor.cpp, reaching it through the friendship above; its
    // status-chain repaint rides applied() as well, harmlessly twice).
    // Callers own everything around it: their refusals, the playback stop and
    // the setter's deselect, which differ per route by
    // ruling. ONE DELIBERATE NON-CALLER, a different tail by design:
    // handle_trim_clear_both (the Shift+[ maximizer resets rather than
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
    // only once the pointer crosses drag_moved_threshold_px(). A full ordered pair
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
    // only once on_motion crosses drag_moved_threshold_px() from the press. TWO
    // SURFACES ARM IT since 2026-08-18 (the region IS the trim): the 9 px bar's
    // endcaps and bridge, and the waveform OVERLAY's bounds and interior, which
    // pass `waveform_click_act` so that a motionless lift there falls to the
    // waveform's ordinary click act instead of the bar's consumed nothing (the
    // field's contract is at PendingTrimDrag, app_state.h). Same drag, same
    // rules, armed from two places.
    void arm_pending_trim_drag(bool is_begin, bool both, int press_x,
                               int press_y, bool waveform_click_act = false);
    void begin_trim_drag(TrimHit which, int mouse_x, bool both = false);
    void update_trim_drag(int mouse_x);   // motion: writes the live store
    // mouse_x → source-domain frame double, the single conversion both the
    // drag anchor (begin) and the live cursor (update) read so they can never
    // diverge. Returns false (out untouched) when audio/zoom state is unusable.
    bool trim_mouse_x_to_source_frame(int mouse_x, double& out_frame);
    // mouse_x → active-domain frame: the cursor-column half of
    // trim_mouse_x_to_source_frame, shared with the move-both-bounds drag
    // so the gap is preserved in the domain the user actually sees (active),
    // not source. Lands on the one displayed-grid owner like every other
    // column→frame landing (the basis and the domain rule are at the
    // definition, input_trim.cpp). Returns false (out untouched) when
    // audio/zoom state is unusable.
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
    // Q2): clicking inside a SHOWN trim region overlay auditions from the
    // clicked frame and leaves the overlay standing, which is why Space no
    // longer carries a region launch of its own. It is also THE HALVES' ONE
    // DIFFERENCE — read honestly, two: the upper half's act deselects, HIDES
    // the overlay and overrides follow, and this one does none of the three.
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
    //   model; ALT unnamed because its one pointer binding is the WHEEL's
    //   stepped pan, which no press can wear a cue for). The FLAG
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

    // THE S/T CHOKEPOINT, in its two spellings. The BODY is the set-to form:
    // put app.active_audio_view at 'S' or 'T', a no-op when it is already
    // there. Stops any current playback before switching domains. Source →
    // Target translates app.viewport_start_sample / playhead_cursor_sample /
    // zoom_level through the current warp_frame_map in place and enters target
    // view only when target view is available (a refusal leaves the view where
    // it was and says one stderr line, nothing on screen — so callers read the
    // verdict off app.active_audio_view rather than a return value; the ruling
    // is at validate_target_view_entry above). Target-view playback
    // is allowed once the target buffer is ready; target render
    // update-in-progress gates playback elsewhere.
    //
    // `handle_active_audio_view_toggle` is the FLIP — bare `t` and the settings
    // editor's `active_audio_view=` commit — and it is one line over the set-to
    // form, so both spellings own the same translation, the same target-view
    // entry gate, the same flag-editor teardown and the same history-focus
    // clear. The SET-TO form exists for the callers that name a view rather than
    // an axis: the bare 1/2/3 absolute selectors, the phase-reset propagate's
    // land-in-target tail, and — both since 2026-08-28 —
    // Undo::restore_history_entry, which restores the entry's own S/T tag
    // (UndoEntry::audio_view) exactly as it restores the tab and the column,
    // each through that axis's owner, and drop_phase_reset_in_target_view,
    // Shift+S's trip to target view ahead of its lead-in drop.
    void switch_active_audio_view_to(char target_view);
    void handle_active_audio_view_toggle();

    // SHIFT+S — DROP A PHASE RESET FROM ANY VIEW (architect 2026-08-28):
    // "Shift+S should set a phase reset marker in T+P from S+W at the current
    // playhead frame, switch to T+P, and highlight the marker that was just
    // created — which it would automatically do because it's the one under the
    // playhead." The act lands the session in T+P through the two view
    // chokepoints and then runs the ONE lead-in drop bare `s` runs there, so
    // it is bare `s` with a view trip in front of it and never a second drop
    // body. In T+P the two chords are the same one call, the trip having
    // nothing to do.
    //
    // WHAT IT LEAVES BEHIND: the new reset SELECTED and the playhead ON it —
    // both the drop's own doing, which is why the column switch (whose
    // selection clear would undo them) has to run first. ONE undo entry, the
    // drop's, tagged T / P / the standing tab.
    //
    // THE REFUSALS ARE EVERY GATE THE CHORD ALREADY PASSES, none of them new:
    // the read-only lock (read_only_key_blocked drops Shift+S beside bare `s`
    // — a phase reset is authored content), the `h` view (its allowlist admits
    // neither), a standing modal (on_key's ranked dispatch above the arm), a
    // blank session, and a target-view ENTRY that refuses its validity gate,
    // which stops the whole act rather than dropping into a half-applied view.
    // The drop's own two refusals stay its.
    //
    // THE HOME-VIEW BINDING IS NOT INVOLVED and this is not a sixth exception
    // to it: the act does not author off home, it GOES home first (the note
    // is at active_column_authoring_allowed, app_state.h).
    //
    // TWO CALLERS: on_key's Shift+S arm, and the bottom row's Drop marker
    // button through the ordinary chord dispatch — its shift-click and its
    // long press synthesize this very chord (redesign_button_shift_admits),
    // so glass reaches the act with no keyboard and there is no second road.
    void drop_phase_reset_in_target_view();

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

    // THE WAVEFORM MAGNIFICATION'S ONE WRITER — the gesture chokepoint every
    // route to app.waveform_magnification_level goes through, and the only site
    // in the product that assigns that field. TWO HOTKEYS reach it (bare `=`
    // steps up, bare `-` steps down, since 2026-08-27), the two icon-row
    // buttons reach it through those same chords, THE PLAIN WHEEL reaches it
    // per frame (handle_wheel's own arm), and the settings editor's
    // `waveform_magnification_level=` commit calls it directly — which is also
    // the reset road, level 0 having no chord of its own since Ctrl+0 was
    // retired with its button. `level` must
    // be in the schema's range (is_waveform_magnification_level,
    // settings_file.h); an out-of-range level is REFUSED WHOLE and never
    // clamped, which is also what makes a step at either end a consumed no-op —
    // the caller asks for cur ± 1 and the bracket answers. THE WHEEL'S BURST IS
    // THE ONE CALLER THAT CLAMPS BEFORE ASKING, a frame's several detents
    // having to land on the ladder's end rather than refuse whole. A typed
    // out-of-range
    // value never gets this far: the editor's own red flash comes from the
    // shared grammar owner validate_gui_setting one step earlier.
    //
    // WHAT IT DOES: assign, then kick_waveform_sync — the synchronous full
    // rebuild every user-driven pan/zoom frame takes, whose tail damages the
    // window top through the waveform's bottom and so carries the OVERVIEW
    // LANE with it (the lane's bar cache keys on the level, so it rebuilds in
    // that same frame). A magnification change is a DISCRETE COMMAND, so the
    // damage is the whole waveform area rather than a narrowed scanner rect.
    //
    // HISTORY-LESS, like every GUI-kind key's gesture: no undo entry, no dirty
    // bit; the value persists on the next ordinary Ctrl+S. And NO AUDIO MOVES —
    // the level scales the picture the painter draws and reaches no sample, no
    // playback path and no render input.
    void apply_waveform_magnification_level(int level);

    // The read-only bit's one setter (2026-09-04, converting a codex finding
    // that the two roads had drifted apart on damage). Two roads write the
    // bit: bare `o` on the active tab, which the icon row's Lock button
    // reaches by synthesizing that press, and the settings editor's
    // `tab_a_read_only=` / `tab_b_read_only=` commit, which may name either
    // tab. Both come here, so the damage is decided once. `tab_view` is 'A' or
    // 'B' and resolves the band exactly as every other per-tab route does;
    // read_only lives in the band for both tabs and is never mirrored to a
    // live field.
    //
    // What it does: writes the named band's bit, and invalidates the top strip
    // when that band is the active one. The damage is for the icon row's Lock
    // button, whose glyph swaps closed-for-open on this flag with no stashed
    // bit behind it, so the tick comparator cannot catch it; the button's lamp
    // and the toolbar faces that also move with the flag do ride the
    // comparator and merely arrive after this. A write to the parked band
    // changes no painted pixel and so takes no damage at all.
    //
    // The tab row's own lane took a second damage call until 2026-08-29, on
    // the reading that the flag moved that row's face; the top-strip damage is
    // a superset of that lane, and the status chain that lived there is gone.
    //
    // History-less, like every band key: no undo entry, no dirty bit; the
    // value persists on the next ordinary Ctrl+S, which a locked tab may run
    // itself. A same-value write is a no-op here as well as at the editor's
    // own unchanged() gate one step earlier.
    void set_tab_read_only(char tab_view, bool value);

    // THE CENTER-ON-NEXT-MARKER LAMP'S ONE SETTER (architect 2026-09-04) —
    // set_tab_read_only's shape, and the same two-road membership: bare `n`,
    // which the icon row's own button reaches by synthesizing that press, and
    // the settings editor's `center_on_next_marker=` commit.
    //
    // THIS TOGGLE GOVERNS THE BARE TAB WALK ALONE — bare Tab, Shift+Tab and
    // IsoLeftTab, whose three arms are the only callers of marker_walk_frame
    // (app_state.h), the bit's one reader. Nothing else in the product asks
    // it: the Ctrl+Shift+Tab paired march states MarkerLandingFrame::Center at
    // both of its steps, `c`, Shift+`j` and the A/B audition frame through
    // run_center_command, and a marker click has never moved the camera. The
    // walk itself takes framing as a required argument and reads no preference
    // of its own, so this bit cannot leak into any act that composes it.
    //
    // What it does: writes the field. No damage call — the lamp's face rides
    // the per-tick comparator like every other stateful button face, and no
    // glyph swaps on this bit. History-less, like every GUI-kind key's
    // gesture: no undo entry, no dirty bit, and the value persists on the next
    // ordinary Ctrl+S. The camera does not move at the toggle either way: the
    // bit is read at the NEXT walk, so lighting it re-frames nothing that is
    // already on screen.
    void set_center_on_next_marker(bool desired);

    // THE RESTRICT-UNDO-TO-VIEWPORT LAMP'S ONE SETTER (architect 2026-09-04) —
    // set_center_on_next_marker's shape with ONE road instead of two: bare `z`,
    // which the icon row's own button reaches by synthesizing that press. There
    // is no settings-editor road, because there is no key: the bit is
    // SESSION-ONLY (AppState::restrict_undo_to_viewport), off at every launch,
    // in no sidecar and in no vocabulary.
    //
    // WHAT THE BIT GOVERNS IS UNDO AND REDO AND NOTHING ELSE. Lit, a step whose
    // restore would move the viewport refuses, cards and leaves both stacks
    // untouched; the verdict has one owner,
    // undo_step_permitted_by_viewport_lamp (app_state.h), which the Ctrl+Z arm,
    // the two buttons' faces and their tooltips' reasons all read. The bit
    // reaches no camera, no walk and no other act.
    //
    // What it does: writes the field. No damage call — the lamp's face and the
    // two faces it can grey all ride the per-tick comparator like every other
    // stateful button face, and no glyph swaps on this bit. History-less: no
    // undo entry, no dirty bit, and nothing to persist. Nothing moves at the
    // toggle either way — the bit is read at the NEXT Ctrl+Z, so lighting it
    // changes nothing already on screen.
    void set_restrict_undo_to_viewport(bool desired);

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
    // ONE READER since 2026-08-30, the on_key dispatch (which picks the
    // lane); read_only_key_blocked's is_playhead_step entry — which admits the
    // horizontal arrows, in all three of the step ladder's magnitudes, only in
    // the waveform lane, this gate being their
    // sole read-only defense — reads the lane through the lock's own owner
    // horizontal_arrow_step_lock_admits (app_state.h) instead, so the Left /
    // Right buttons' face can read the same answer. THE BODY
    // DELEGATES TO marker_selection_standing (app_state.h) since 2026-08-30:
    // the lane IS a standing selection, and that fact gained face readers on
    // the bottom row (the arrows' horizontal_arrow_step_actionable among
    // them) which cannot call a handler member — so the expression lives
    // once, out there, and this member keeps the lane's name and contract.
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
    // renders and the `[` / Shift+[ trim gestures are ADMITTED — a save writes
    // the state the tab already holds, a render reads it, and trim is band.
    // One entry is
    // STATE-DEPENDENT: the horizontal arrows are admitted as navigation
    // only while playhead_in_marker_lane is false, since in the marker lane the
    // same press authors — the lane decides, and the step ladder's modifier
    // (bare / shift / ctrl, 2026-08-31) does not enter the decision.
    bool read_only_key_blocked(GuiKey key, GuiInputState mods);

    // KEYBOARD MODALITY (architect 2026-07-28): true when an open editor owns
    // the keyboard, so every chord outside the admitted set is a silent no-op.
    // EVERY editor does — the three single-State dialog ones (settings,
    // commit title, measure paste-offset), the bpm bracket, the marker MEASURE
    // editor, and the
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
    // commit-title editor, the measure paste-offset editor and the bpm
    // bracket editor (plus the prompts, gated separately), the surfaces that
    // paint in the MODAL since 2026-08-12 — on the BOTTOM ROW since
    // 2026-08-13, which is where the name came from in the first place (it
    // was modal_bottom_strip_editor_active while they wrote onto the status
    // lane; the surface is a modal that yields the whole row now rather than
    // a tenant of its status span). THE FOUR-EDITOR MEMBERSHIP ITSELF LIVES AT
    // AppState::dialog_editor_session (app_state.h), which names them once and
    // hands back the live one's session id; this predicate is that id being
    // non-zero, so the set cannot drift between the two. (The `h` view's load
    // editor and the Open project editor were members until 2026-08-28, when
    // both became the field-less PICKER — a modal owner with its own
    // predicate, picker_active, and NOT in this answer, exactly as the render
    // player is not.) NINE CALLING
    // FUNCTIONS, RE-DERIVED BY GREP 2026-08-29 (the count was eight from
    // 2026-08-14, when two left with the round-15 session fix — the dialog
    // BUTTON claim in on_button_press and dispatch_modal_dialog_button, both
    // of which ask modal_dialog_stash_current instead, a strictly narrower
    // question that implies this one; the ninth arrived 2026-08-29, and it is
    // not a new reader but a re-spelling: on_button_press's own veil had the
    // four is_active tests written out and now asks here):
    // SEVEN ask about a POINTER fact and TWO about a KEY.
    //   wheel_context's swallow (input_handler.cpp), because the wheel is
    //     NAVIGATION and display, not a chord, so it still punches through
    //     an open top-strip flag editor;
    //   pointer_cursor_kind (2026-08-03), because these editors are
    //     exactly the ones whose veil SWALLOWS a pointer press, so they are
    //     exactly the ones over which no cursor may promise a gesture;
    //   the dialog button claim's RELEASE MIRROR in on_button_release
    //     (2026-08-13 — the buttons act at the lift) and on_motion's
    //     dialog-hover branch (2026-08-12) — the modal's own two pointer
    //     surfaces;
    //   THE VEIL ITSELF, at the tail of on_button_press's modal block: a
    //     press that reached neither the field nor the buttons is swallowed
    //     while any of the four stands (it spelled the four is_active tests
    //     inline until 2026-08-29);
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
    // THE RENDER PLAYER, THE PICKER AND THE AV SYNC STATS PANEL ARE
    // DELIBERATELY NOT IN IT (the first two 2026-08-28, the panel 2026-09-03):
    // each is a MODE and not a text editor, so each has its own
    // predicate (render_player_active, picker_active and stats_panel_active,
    // above) and every
    // reader here was audited for whether the mode belongs in its answer —
    // the veil, the cursor, the hover walk's veil term and the chrome
    // release's re-ask took a term of their own for all three (the wheel reads
    // the overlay's standing predicate, which covers all three); the field's
    // I-beam,
    // the editors' Tab admission and the repeat arms did not (the ring's Tab
    // and the list walk repeat through each router's own arm).
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
    // Ctrl+Q). It serves all six editor kinds, top strip included (the list is
    // text_editor::Kind).
    bool modal_dialog_editor_active() const;
    bool modal_editor_key_blocked(GuiKey key, GuiInputState mods);

    // -- THE RENDER PLAYER'S INPUT HALF (2026-08-28) ------------------------
    //
    // THE PLAYER'S ONE POINTER RULE IS THE VEIL: while the player stands every
    // press outside the folder overlay's band and the modal row is CONSUMED —
    // the tab row's tabs, the marker lane's flags, the waveform, the four dead
    // menu anchors, the view bar, all of it — and the roster's buttons are
    // dead through redesign_button_enabled's first arm (their faces grey,
    // their press claims refuse). THE ONE EXEMPTION IS THE LIVE FILE ANCHOR
    // above the band (architect 2026-09-03 evening — the `h` view's partition
    // extended to all three overlay contents): its press reaches the menu-row
    // claim through press_on_live_menu_anchor, and its OPEN MENU then owns
    // every press outright, the dropdown's claim ranking above this veil and
    // above the band (so a press on a row while the menu stands dismisses the
    // menu rather than opening the row, and arms no modal button). The wheel scrolls the overlay one row per detent over
    // the band and is consumed everywhere else; the cursor is the Arrow; the
    // tooltip dwell is the modal buttons' own. A MODIFIED PRESS IS CONSUMED
    // WITH ONE ADMISSION (R37): a SHIFT press on the modal row's two skips
    // arms their shifted twin, the roster's shift-click over this surface, and
    // ctrl, alt and every other shift press stay the consumed nothing they
    // were. THE CHORD MODALITY is the key
    // router below, which runs in on_key ahead of every ordinary dispatch (the
    // prompt gate and the dropdown gate above it, a prompt outranking the
    // player). This predicate is what every one of those readers asks; it is
    // the mode bit and nothing else, named so the readers say what they mean.
    // (modal_dialog_editor_active stays the four-editor question and is NOT
    // widened: the player is a mode, not a text editor, and the readers that
    // ask about text — the field's I-beam, the editors' Tab admission, the
    // repeat arms — must not see it. The PICKER is the same shape one mode
    // over — picker_active, with its own router and its own veil arm.)
    bool render_player_active() const { return app.render_player.active; }

    // THE PLAYER'S KEY ROUTER — the whole plastic vocabulary while the mode
    // stands (architect 2026-08-28, revised the same day): Tab / Shift+Tab
    // walk the ring [list, buttons…]; Enter presses a ring-focused button
    // (press-at-press, commit-at-release, the modal's own) or, with none
    // focused, OPENS the highlight (a folder enters, `..` goes up, a wav
    // plays); Space is the Play button's act; Up / Down move the highlight;
    // Left / Right seek ∓5 s; bare Home the item's own start, or THE PREVIOUS
    // ENTRY when the press lands inside the item's first three seconds
    // (kPlayerPreviousThresholdMs, the previous-track window, 2026-08-31);
    // bare End the item's own END (2026-08-30, the scrub's own right edge —
    // not a walk, the natural end being what advances); Backspace up one
    // folder (a consumed no-op at the root); SHIFT+Home / Shift+End the
    // folder's FIRST / LAST wav (R37, re-keyed off Page Up / Page Down
    // 2026-08-30 and off comma/period 2026-08-31 — bare `,` / `.` are unbound
    // here and fall to the silent catch-all below, and bare `v` joined them
    // 2026-09-01 when the player's Stop retired with its
    // button); `r` the Repeat one lamp; `'` the Load in
    // place button's chord; `l` and Esc close; Ctrl+S falls through to the save (legal, no stop); Ctrl+Q
    // falls through to the quit road, which takes the player down at its
    // head (GuiPrompt::request_close, the compositor's close road too, so
    // neither gesture restates the step). EVERY OTHER
    // CHORD IS CONSUMED (strict modifier validation's no-op). Returns true
    // when the key is consumed here, false for the two fall-throughs.
    bool route_render_player_key(GuiKey key, GuiInputState mods);

    // THE PLAYER'S OPENER TOGGLE for bare `l` and bare `'` outside the `h`
    // view: closes a standing player on `l`, opens it otherwise through the
    // one opener (GuiRenderPlayer::open). Its callers have already refused
    // the modal states (a prompt, an editor, the `h` view, loading — all
    // above it in on_key).
    void toggle_render_player();

    // THE OVERLAY'S POINTER HALF (bodies in input_pointer.cpp), ONE ROUTER
    // FOR EVERY CONTENT: the row press claim (arm at the press — a modified or
    // non-left press is consumed, the shift the player's MODAL ROW admits
    // since R37 being the row's, never a row of the list's), the release (A
    // MOTIONLESS LIFT HIGHLIGHTS THE ROW AND OPENS IT — a click activates,
    // architect 2026-08-29; a scroll drag ends), the motion
    // (past the drag gate — the product's one Chebyshev crossing, on EITHER
    // axis — the arm is the band's scroll drag; inside
    // it the feint's inside bit), the hover walk, and the hard end (the
    // pointer-leave hook, the button-lost edge and the force-end finalizer —
    // the arm dropped, nothing committed). The claim is RANKED under the
    // prompt gate and under the OPEN DROPDOWN'S claim (a standing menu owns
    // every press before any band does — 2026-09-03, on_button_press), and
    // ABOVE the THREE mode veils (the player's, the picker's
    // and the AV Sync Stats panel's), each of which admits exactly the band
    // and its own modal row;
    // it owns its own button gate so the rank costs the veils nothing.
    bool claim_folder_overlay_press(int x, int y, GuiMouseButton button,
                                    GuiInputState mods);
    bool finish_folder_overlay_release(int x, int y);
    void update_folder_overlay_press_motion(int x, int y);
    void update_folder_overlay_hover(int x, int y);
    // THE TWO HALVES OF THE ROW CLICK, FORKED ON THE OWNER and nowhere else:
    // the highlight (the band moves, under every owner — the picker has
    // no field beside it) and the open act (the player enters a folder, goes
    // up or plays a wav; the project picker reopens the row's project).
    void folder_overlay_highlight_row(int index);
    void folder_overlay_open_row(int index);
    // (clear_folder_overlay_press is public, beside clear_modal_dialog_press:
    // main.cpp's pointer-leave hook and the force-end finalizer are its other
    // callers.)

    // THE PLAY-SCRUB'S POINTER HALF (bodies in input_pointer.cpp), over the
    // painter's published slider item (AppState::ModalDialogGeometry::scrub):
    // a press ON THE HANDLE'S OWN BOX (its 20 px, through the one test
    // render_player_scrub_handle_hit — the trim endcaps' 10 px band until
    // 2026-08-28, when the scrub became a Breeze slider and the handle got a
    // size) arms the handle drag — its painted x follows the pointer while
    // playback continues where it was, and the RELEASE commits the seek (the
    // product's deferred-click shape); a press on the track elsewhere SEEKS AT
    // THE PRESS (identity certain) and arms nothing. The hard end — the
    // pointer-leave hook, the button-lost edge and the force-end finalizer —
    // drops the arm and commits nothing.
    bool claim_player_scrub_press(int x, int y, GuiInputState mods);
    bool finish_player_scrub_release(int x, int y);
    void update_player_scrub_motion(int x);
    // The drag's carried column clamped onto the handle's TRAVEL, the two
    // writers' one expression (the body says why).
    int  clamp_player_scrub_marker_x(int x) const;
    // (clear_player_scrub_drag is public, beside clear_modal_dialog_press,
    // for the same hook and the same finalizer.)

    // THE VALUE PAIR (architect 2026-08-29), the two acts that took the
    // retired resolved readout's place: bare `j` COPIES the focused marker's
    // resolved value to the system clipboard, Shift+`j` JUMPS to the marker
    // that value came from — the pass's owner or the ref's definition — ON THE
    // OTHER A/B TAB, so a reference and its definition stand one Ctrl+Tab
    // apart. One composer answers both (resolved_marker_payload, the value and
    // its source index), one gate refuses both — carding since 2026-08-30, in
    // each chord's own words (payload_eligible_marker) — and the jump's five
    // acts are each somebody
    // else's chokepoint IN A RULED ORDER — `c` ON THE TAB IT LEAVES FIRST
    // (architect 2026-08-29, the A/B audition's own shape: the origin tab is
    // framed on the reference before the act moves on, and `c`'s focused land
    // rests its playhead there), then the tab switch, then the plain click's
    // single-select on the source's own index, then the marker land, then `c`
    // again on the tab that landed — so BOTH tabs end framed, each on its own
    // half of the pair (the order and what each step is for are at the body).
    // Both are
    // read-only-legal, both live in S and T, both refused in the `h` view at
    // its allowlist; the bottom row's Copy resolved value button is `j` at its
    // lift and Shift+`j` at its shift-click or long press. Definitions and the whole
    // reasoning are in input_key_dispatch.cpp.
    void copy_focused_marker_value();
    void jump_to_value_source();

    // THE LOAD ROAD (design R10 / R15, revised 2026-08-28 into a button): the
    // Load in place button's act and bare `'`'s inside the player — says
    // "Cannot load in place while a render is running" on a card over a
    // running or parked render (silent for the one day a status bar stood at
    // the window's foot to explain it) and
    // says "This tab is read-only" on a read-only tab (kTabReadOnlyCard, the
    // lock's own literal and this one of its three readers — the arm was
    // silent until 2026-08-30), says "A folder carries no recipe to load in
    // place" on any highlight that is not a load-capable
    // wav — the sentence NAMES ONLY WHAT IS REACHABLE since 2026-09-01, a
    // folder row being the whole of that set once the `..` row and the
    // deliverable left the listings (the reasoning is at the raise) — and
    // otherwise
    // pauses the transport and raises the LOAD_IN_PLACE_CONFIRM prompt on
    // the highlighted entry — the one prompt raised with its FIRST button
    // focused, so a bare Enter answers OK (the reason is at the raise). The
    // prompt's OK runs confirm_load_in_place's player arm (the shared act
    // load_render_entry_in_place, WHICH SAYS ITS OWN REFUSAL on a card and on
    // stderr since 2026-08-30 — the caller's vague "Load refused" is gone —
    // and whose success closes the player); Cancel runs
    // cancel_load_in_place.
    // THE TWO-ROAD SANCTION RETIRED with the typed renders road the same day;
    // the record is at load_render_entry_in_place, which has this one caller.
    void render_player_load_in_place();
    // THE `h` VIEW'S OWN LOAD ROAD (architect 2026-08-29, retiring the one-day
    // history picker): bare `'` there raises THE SAME PROMPT on the VIEWED
    // walk member — "Load '<member label>' in place?", Enter answering OK —
    // and the OK runs that walk's own act (the fork is at
    // confirm_load_in_place, the one site of it). Its guards are the picker's
    // former ones: the mode standing, no prompt or editor, no player and no
    // picker, a source loaded, and a walk that carries a member (the
    // allowlist's own term, restated as a guard).
    void history_load_in_place();
    // (confirm_load_in_place / cancel_load_in_place — the prompt's
    // two answers — are public, beside the hard-end clearers above: GuiPrompt
    // reaches them through its back-pointer.)

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

    // -- THE ON-SCREEN KEYBOARD'S POINTER HALF (2026-08-27) ----------------
    //
    // The surface itself — the layout table, the geometry and the two lamps —
    // is onscreen_keyboard.h's; these are its two edges, and they sit ABOVE
    // EVERY OTHER GATE in on_button_press / on_button_release for one reason:
    // while the surface stands, its own rect belongs to no other surface, so
    // there is nothing below to arbitrate with. In particular it must outrank
    // the dialog editors' VEIL, which would otherwise swallow the press that
    // types into the very editor raising the veil.
    //
    // THE PRESS CLAIMS THE WHOLE RECT and answers true for every press inside
    // it, key or not: the gaps between keys, the outer margin and a blank slot
    // all CONSUME: a finger that misses a key must not fall through to the
    // waveform's pan underneath. A key's
    // act runs AT THE PRESS through GuiPlatform::synthesize_key, so the whole
    // ordinary key path — the keyboard-modal gate, route_modal_editor_key, the
    // editor's own vocabulary, the undo coalescing, the core's repeat arming —
    // runs unchanged from there. Ahead of the hit test it runs the SESSION-
    // CHANGE OWNER (onscreen_keyboard::reconcile_session, whose declaration
    // names its other caller), so a press can never be routed against lamps the
    // previous edit armed.
    //
    // THE RELEASE OWES THE KEY-UP AND NOTHING ELSE. It is guarded on the held
    // index alone, which only this surface's own press ever sets, so it can sit
    // above every gate without claiming a release that is not its. It fires
    // even when the press's own act closed the editor under it (Enter, Esc):
    // the key-down was delivered, so its pair is owed whatever became of the
    // surface — and the core's repeat cancel is that pair's other job.
    bool claim_onscreen_keyboard_press(GuiMouseButton button, int x, int y);
    bool finish_onscreen_keyboard_release();

    // THE DIALOG BUTTONS ACT AT THE RELEASE (architect 2026-08-13,
    // "everything else acts on lift"), which is what these three own — the
    // arm, its hard end, and the lift's verdict. The arm itself is
    // AppState::modal_dialog_pressed, whose declaration carries the whole edge
    // list and the reason it is not the roster's own arm (AppState::ChromePress).
    //   arm_modal_dialog_press    — press: arm the hit button and paint it,
    //                               dispatching nothing. True iff one was hit
    //                               (or a shift press was consumed on a button
    //                               with no shifted twin). `shift` is the
    //                               PRESS-TIME modifier, carried on the arm;
    //                               only the player's claim ever passes true.
    //   take_modal_dialog_release — release: consume the arm and return the
    //                               button the lift LANDED on if it is the one
    //                               armed, else -1.
    //   modal_dialog_press_shifted — the SHIFTED-TWIN verdict for the arm that
    //                               is still standing (R37): the press-time
    //                               shift ORed with a hold past
    //                               kChromeShiftHoldMs, the roster lift's own
    //                               term. Read BEFORE the take, which clears
    //                               the arm.
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
    bool arm_modal_dialog_press(int x, int y, bool shift = false);
    int  take_modal_dialog_release(int x, int y);
    bool modal_dialog_press_shifted() const;
    // `shifted` reaches only the buttons player_button_shift_admits names
    // (app_state.h); every other act ignores it, so a held Play is a plain
    // Play exactly as a held roster button with no twin is its plain act. The
    // KEYBOARD's own release passes the default: a ring-focused button's Enter
    // or Space is bare by the strict-modifier rule.
    bool dispatch_modal_dialog_button(int index, bool shifted = false);

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
    //     the toggle, the walk, the diff-flag cycle, the march that composes
    //     that cycle with the A/B switch, the absolute Home/End and
    //     `c` — and returns true when it consumed the press. The membership is
    //     re-derived at history_mode_owns_key; its position in on_key IS its
    //     entry-gate list. Its cycle is a member of its own
    //     (cycle_history_diff_flag_focus) because the march composes it twice.
    //   * history_mode_key_blocked is the allowlist gate, read_only_key_-
    //     blocked's shape: true when the press is not admitted while the mode
    //     stands. The redesigned buttons and the File menu's one item reach it
    //     through their synthesized chords, so it covers them too — and since
    //     2026-08-04 it also DECIDES THEIR FACES (history_mode_disables_button,
    //     app_state.h). It is a FREE function beside this class, with
    //     history_mode_owns_key (that vocabulary's shape), for that second reader:
    //     both are pure, the face derivation having no press and no handler in
    //     hand. Declarations above the class, where the two conditional
    //     admissions — and why only those take the session — are stated.
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
    //   (THE MODE'S TWO FRAMING OWNERS ARE DELETED — 2026-08-18.
    //     frame_viewed_commit_diff_span framed the viewed checkpoint's whole
    //     delta, an ON-DEMAND ACT from 2026-08-05 with the trim bar's plain
    //     DOUBLE-CLICK as its one caller; that gesture runs the ORDINARY span
    //     framing now (run_span_framing_command), the bar having stopped
    //     displaying the diff span, so the act went producer-less.
    //     frame_history_view_whole_song was the full zoom out it fell through
    //     to on an empty delta, and that empty arm was its last caller — the
    //     `,` / `.` step and the walk-or-reading switch stopped calling it on
    //     2026-08-08 and the ENTRY on 2026-08-18. THE MODE WRITES NO VIEWPORT
    //     ANYWHERE and now has no framing route of its own at all: the window
    //     is the USER'S from before `h` until after it.)
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
    //   * set_history_delta is the ONE switch owner for WHAT THE LANE SHOWS
    //     (2026-08-05 as the two compare readings' owner, generalized
    //     2026-08-07 to the (walk source, reading) PAIR): bare `g` STEPS THE
    //     WALK through it (2026-08-18, the icon row's WALK LAMP's chord — a
    //     radio pair's until 2026-09-04, and row 3's repurposed tabs selected
    //     it directly before 2026-08-18) and bare
    //     `u` FLIPS THE READING through it (2026-08-08,
    //     when the reading left the row for row 4's own toggle). A switch is a MODE EDGE with the `,` / `.` step's own
    //     shape, and the owner is idempotent, which is what makes a step onto
    //     the walk already shown a consumed nothing at its call site.
    // THE COMMIT ACT'S GUI HALF is the last pair, and the act itself lives in
    // the diff module (commit_history_checkpoint, history_diff.h):
    //   * the COMMIT-TITLE EDITOR asks for the message (its cluster is declared
    //     above, beside the picker).
    //   * run_history_commit is that editor's Enter: save, rebuild the bytes,
    //     close the view, and hand the captured job to the background worker.
    //     Its body owns the close partition (THE VIEW CLOSES IFF THE SAVE
    //     LANDED, architect 2026-08-07) and the capture list.
    //   * on_history_checkpoint_complete is the worker's completion, back on
    //     the main thread: it clears the in-flight bit and RAISES A CRITICAL
    //     NOTIFICATION CARD for each of the four failing verdicts (the two
    //     established ones raise nothing; GuiNotifications owns the card).
    //     It raised an acknowledge modal until 2026-08-09 and wrote the tab
    //     row's permanent critical chip until 2026-08-29; a card takes
    //     nothing from the keyboard either, so the completion still needs
    //     nothing from the input layer.
    // THE REVERT ACT is the odd one out and deliberately so:
    //   * run_history_revert applies the SELECTED diff flags backwards into the
    //     live store of the active column and then closes the view. Its key,
    //     bare `v` (Ctrl+H until 2026-09-01), is NOT part of the mode's own
    //     vocabulary — it is admitted by
    //     the allowlist (conditionally, on a subject standing) and dispatched
    //     from on_key's ordinary body BELOW the read-only gate, so a locked tab
    //     refuses it exactly as it refuses `'` — but no longer as it refuses the
    //     checkpoint act, which authors nothing and runs from a locked tab
    //     (2026-08-07's band ruling; the act's chord is Ctrl+S since
    //     2026-08-08, admitted by that same gate). Its body owns the
    //     per-class inverse, the always-force rule and the one undo entry.
    bool handle_history_mode_key(GuiKey key, GuiInputState mods);
    // The mode's Tab act, one step over the viewed checkpoint's diff flags in
    // the given direction. Two callers, both in handle_history_mode_key: its
    // Tab arm and its Ctrl+Shift+Tab march, which composes this with the A/B
    // switch. Every walk rule it obeys is stated at those arms.
    void cycle_history_diff_flag_focus(bool forward);
    bool open_history_mode_fresh();
    void drop_lane_stash_across_history_edge();
    void republish_history_lane_now();
    void set_history_delta(GuiHistoryWalkSource source,
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
public:
    // -- THE HISTORY PREFETCH'S THREE PUBLIC EDGES (2026-08-07) -------------
    //
    // START A FRESH SCAN of the loaded source's committed history — the ONE
    // funnel for all three kickers (main.cpp's startup load tail, the
    // checkpoint completion's re-warm, and the `h` entry's staleness kick),
    // none of which can fire with a visit standing (the definition carries the
    // proof, and the deferral bit that stood for the case none of them
    // produces was deleted producer-less 2026-08-29).
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
