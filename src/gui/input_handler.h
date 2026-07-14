#pragma once

#include "app_state.h"
#include "async_renderer.h"
#include "audio.h"
#include "flag_editor.h"
#include "playback.h"
#include "playback_lifecycle.h"
#include "prompt.h"
#include "render_pipeline.h"
#include "render_view.h"
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
    double  ratio;
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

    return BaseTempoScale{base_tempo_cents, scale, ratio};
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
// The walk: first the raw-store walk — enumerate_marker_store_defects over
// BOTH marker columns (`markers` + `phase_resets`, under a sr>0 &&
// total>0 guard), first defect message wins — so target view is reachable
// only from a raw-valid store, mirroring warp_render_preflight's raw walk
// and render view's load walk (the enumerator's coincidence window is
// wider than the parser owners' sub-frame refusals, so a raw-invalid but
// owner-clean store — e.g. two close phase resets — is refused here too).
// Then resolve_warp_markers_for_render over `markers`, then
// build_warp_frame_map with `scale` (the whole-song map — trim is
// render-time, not view-time), then — only when a trim bound is set —
// validate_trim_frames against the full map just built. Failure of any
// stage blocks entry. On success the built map is returned so the
// toggle can reuse it for its viewport/playhead translation (no second
// build); on failure the first owner's error string is returned verbatim.
std::expected<std::vector<WarpFrameMapSegment>, std::string>
validate_target_view_entry(const std::vector<GuiWarpMarker>& markers,
                           const std::vector<GuiPhaseResetMarker>& phase_resets,
                           double scale, int sample_rate, long total_frames,
                           bool has_trim_begin, int64_t trim_begin_frame,
                           bool has_trim_end,   int64_t trim_end_frame);

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
    GuiRenderView&           render_view;
    GuiActiveViews&          active_views;
    PhaseResetPropagate&     phase_reset_propagate;
    GuiAsyncRenderer&        async_renderer;
    GuiPlaybackLifecycle&    playback_lifecycle;
    GuiSaveOps&              save_ops;
    GuiPrompt&               prompt;
    GuiSettingsEditor&       settings_editor;
    GuiTargetRender&         target_render;
    GuiPaintHandler&         paint_handler;

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
                    GuiRenderView&           render_view_,
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
          render_view(render_view_),
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
    // 2 inside the top strip, 0 outside both. on_wheel's completed-detent
    // gate and the platform's per-frame sub-detent accumulator probe both
    // consult it so the two surfaces can never drift.
    int wheel_context(int x, int y) const;
    void on_motion(int mouse_x, int mouse_y, GuiInputState mods);

    // Stop all four pointer drag gestures. A marker or trim drag is stopped
    // before its deferred commit, so live state returns to pre-drag (including
    // the tracked playhead for a marker drag); scroll and playhead drags just
    // end where they are. No-op when no drag is active. Callers: the
    // drag-modal escape hatches in on_key, and the WM-close callback in
    // main.cpp (both cancel before raising the close prompt).
    void cancel_active_drags();

    // Target-view validity gate, kick-back half. Called from on_tick every
    // event-loop iteration: while target view is displayed (and render-view
    // is off), consult the memoized target-view map cache; if the last
    // rebuild failed (invalidating edit — first-marker grammar violation,
    // dangling label ref, tie, ...) or a set trim fails validate_trim_frames,
    // toggle back to source view through the same T→S path bare `t` uses,
    // then open the defect-resolution series on the modeled defect (the
    // error-notice popup remains only as the backstop for the non-modeled
    // class). The cache rebuild is keyed on the marker-store generation, so
    // this fires on the first tick after the invalidating edit — i.e.
    // before/with its first paint — not lazily later. Deferred (silently,
    // one tick at a time) while another prompt is up; edits are impossible
    // mid-prompt, so the pending kick survives until dismissal —
    // run_commit_validation runs first on the same tick, so a commit's own
    // modal wins the beat. No-op in source view, render-view, blank/loading
    // state.
    void enforce_target_view_validity();

    // Defect-resolution modal series (bodies in input_defect_series.cpp).
    //
    // run_commit_validation: the once-per-tick funnel check, run from
    // main.cpp's on_tick immediately before enforce_target_view_validity.
    // When the pending-validation flag is set — Commit origin from the
    // history push helpers / do_redo in undo.cpp and trim's history-less
    // commit sites, Load origin from the end of a successful source load —
    // and no prompt is up and nothing is loading, clears the flag and
    // opens the series (commit context iff the origin is Commit) — so the
    // modal opens on the same beat as the commit's or the load's own
    // repaint, never mid-gesture. While a prompt is already up the flag is
    // kept for the next tick (the same deferral shape
    // enforce_target_view_validity uses).
    void run_commit_validation();

    // open_defect_series: slice the live stores, enumerate marker defects
    // (both columns; chronological), then — when the marker list is clean —
    // walk the trim column: the frame-level crossed-or-equal check, the
    // map-format-with-trim conflict (any set bound under a non-wav
    // output_format), then the full validate_trim_frames against the
    // memoized target-view map when that map builds. Fills app.prompt
    // (DialogTrigger::DEFECT_RESOLUTION) with the current defect and returns
    // true; when everything is clean, closes any open defect prompt and
    // returns false.
    bool open_defect_series(bool commit_context);

    // present_defect_pre_step: the pre-remedy funnel gate, called by
    // open_defect_series with the first defect's own message once a defect is
    // known but before the [U]ndo/[R]eset/[Delete] remedy is built. If render
    // view is enabled or the active tab is read-only, sets
    // defect_series.pre_step and presents a forced single-[Y]es prompt to leave
    // that context ("<summary>. Leave render view to resolve?" /
    // "<summary>. Disable read-only?"), returning true so the caller returns
    // without building the remedy. Otherwise clears pre_step to None and
    // returns false, so the caller builds the real remedy in the now-editable
    // context.
    bool present_defect_pre_step(const std::string& defect_summary);

    // handle_defect_response: a pre-step [Y]es (pre_step != None) leaves render
    // view / disables read-only with NO history and re-enters the funnel;
    // otherwise apply the chosen resolution ([U]ndo / [R]eset / [Delete] per
    // defect kind), then re-open the series with the same commit-context flag —
    // re-validate from scratch; next defect or done. Keys not offered by the
    // current defect are swallowed.
    void handle_defect_response(char k);

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
    // parked archival command. Called by handle_escape_cancels and by the
    // render-view Esc-cancel paths (input_render_view.cpp). Returns true
    // when there was a session to cancel.
    bool cancel_archival_session();

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
        // Snapshotted at start_render_batch time from reqs.front()
        // because each entry's RenderRequest::batch_folder is moved
        // out during dispatch — by the time the terminal success
        // branch needs the folder for auto-open, reqs[0].batch_folder
        // is empty. All entries in a single batch share one folder,
        // so reading from reqs.front() is canonical.
        std::filesystem::path      batch_folder;
        // Snapshotted the same way, from reqs.front()'s output_format:
        // render view is a WAV audio player, so the terminal auto-open
        // runs only for a wav-output batch. Every entry in a batch shares
        // one output_format (both sweep builders copy the live
        // app.engine_settings.output_format), and reqs is moved onto batch_
        // before the terminal branch reads this — so the front request is
        // the canonical read and is captured up front, like batch_folder.
        bool                       wav_browseable = false;
    };
    ActiveBatch batch_;

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

    // Attach the process-wide render resources to an assembled request:
    // the single RenderCache (constructed in main, reached through
    // target_render's reference), the GUI's shared source buffer, and the
    // source's load identity. These are required request fields — do_render
    // reads no source-sample cache and dereferences the
    // buffer and cache without fallbacks. Every archival dispatch site must
    // call this after build_render_request.
    void attach_shared_render_resources(RenderRequest& req);

    // Sweep every BPM in the BPM owner's [bpm_lo, bpm_hi] range,
    // computing (base_tempo, scale) per cell and rendering one .wav per
    // cell into `<source_parent>/renders/<N>_render_bpm_iterations/`. The
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

    // Render dispatch pre-flight (GUI thread, marker-count-sized, cheap).
    // Always validates the live state (`markers` / `phase_resets` / trim at
    // dispatch time). First the raw-store walk —
    // enumerate_marker_store_defects over both given marker lists plus the
    // trim checks (crossed-or-equal, the map-format-with-trim conflict, and
    // validate_trim_frames), mirroring open_defect_series' predicate; on a
    // modeled defect the dispatch opens the defect-resolution series and is
    // refused. Then the backstop chain:
    // resolve_warp_markers_for_render on the sliced `markers`,
    // build_warp_frame_map with `scale` and the live sample_rate /
    // total_frames, then — when a trim bound is set — the map-format
    // refusal (trim is wav-only, ruled; the raw walk routes this into the
    // series, so this popup stays the backstop) and validate_trim_frames
    // against the built map, all surfacing through the error-notice popup
    // with the owner's error string, unmodified. Returns false on any
    // refusal — the caller must then refuse to enqueue. Called by every
    // archival render dispatch site (Ctrl+Alt+R single render, Ctrl+Alt+I
    // iteration sweep, the BPM sweep); the async pipeline's stderr failure
    // paths stay as the backstop for per-cell mutations the pre-flight does
    // not model (no strings are plumbed through the async callback).
    bool warp_render_preflight(const std::vector<GuiWarpMarker>& markers,
                               const std::vector<GuiPhaseResetMarker>& phase_resets,
                               double scale,
                               const std::string& output_format,
                               bool has_trim_begin, int64_t trim_begin_frame,
                               bool has_trim_end, int64_t trim_end_frame);

    // F2.1: end an in-flight editor-text drag (motion-with-lost-button and
    // button release both route here). Collapses a never-moved anchor back
    // to a plain caret (no selection), repaints the active editor's strip,
    // and clears app.editor_text_drag. No-op on the strip repaint if no
    // editor is active (the editor closed out from under the drag); the
    // flag is cleared regardless.
    void finalize_editor_text_drag();

    // F2.1: after a mouse press opens (or switches) a flag editor, arm a
    // selection drag with a collapsed anchor at the caret enter_top_flag_edit
    // just placed from the click x, so the opening gesture itself can
    // drag-select (web-address-bar behavior) instead of requiring a second
    // press. No-op when the open was refused (editor not active, e.g.
    // read-only), so a refused open changes nothing.
    void arm_editor_text_drag_on_open();

    // Clipboard: handle a Copy/Cut/Paste editor action against editor `s`
    // using the internal session clipboard (AppState::text_clipboard; there is
    // no Wayland clipboard), and report whether it handled one. Copy and cut
    // push the selection to the session clipboard (cut then deletes it); paste
    // pulls the clipboard text into the selection. Returns false for any other
    // action so the caller can fall through to its remaining branches.
    bool apply_editor_clipboard(text_editor::KeyAction action,
                                text_editor::State& s);

    // Shared wheel handler covering source-view and render-view; on_wheel is
    // its only caller. Exact-match modifiers: plain = zoom, Alt = pan (10% of
    // the visible span per detent), Ctrl = nudge the focused warp marker's base
    // tempo by 0.01 per detent. Any other combination (Shift+wheel,
    // Ctrl+Alt+wheel, ...) no-ops.
    void handle_wheel(GuiMouseButton button, int count, bool ctrl, bool shift,
                      bool alt, bool inside_waveform, bool inside_top);

    // Tab / Shift+Tab / IsoLeftTab dispatch: cycle marker focus, then stop
    // playback, move the playhead onto the newly focused marker, and recenter
    // the viewport at the current zoom. Mode-aware: reads from
    // phaseresetmarkers in 'P' mode, warpmarkers otherwise.
    void cycle_marker_focus_with_recenter(bool forward);

    // Esc-cancel handlers: while a render or queued batch is in flight, Esc
    // cancels it. Returns true if it consumed the key (on_key then returns).
    // Routed after the editor modal (which cancels an active edit on Esc
    // first) and before the rest of the key handlers.
    bool handle_escape_cancels(GuiKey key);

    // Render-trigger chords: Ctrl+Alt+R (single render), Ctrl+Alt+I
    // (render iteration sweep), Ctrl+Alt+C (commit displayed render).
    // Returns true if key+mods matched
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
    // Home-End / trim begin-end. Caller gates on no modifiers held.
    void handle_plain_bare_keys(GuiKey key);

    // Routes a key to the active top-flag editor. Returns true if the editor
    // consumed it (on_key then returns); false if the key is a command that
    // must run. The two kinds differ at the command tail: the top-strip
    // flag editor (non-modal) cancels the edit and lets every command fall
    // through, while the bpm editor (a modal bottom-strip surface) reaches
    // the tail only for the three chords the on_key modal gate admits
    // beyond its own keys — Ctrl+S runs the save with the editor left
    // open, Ctrl+Q tears the editor and bpm mode down together and falls
    // through to the close routing, and anything else swallows.
    bool handle_top_flag_editor_key(GuiKey key, GuiInputState mods);

    // Routes a key to the active settings-prompt editor. Same
    // consumed/command contract as handle_top_flag_editor_key's modal
    // (bpm) tail: Ctrl+S saves with the editor left open, Ctrl+Q discards
    // the edit and falls through, everything else the editor does not
    // consume swallows.
    bool handle_settings_editor_key(GuiKey key, GuiInputState mods);

    // Side-parameterized helpers shared by the trim entry points below.
    enum class TrimSide { Begin, End };

    // Plain x: set the begin bound at the playhead (exact int64 frame)
    // and autoset the end bound half of the visible span later. Only the
    // autoset PARTNER is placement-clamped to [0, live EOF] in the active
    // domain — a choice of where to put the bound the user did not position,
    // not a wall (see handle_trim_set_autoset).
    void handle_trim_set_begin_autoset();

    // Shift+x: clear both trim bounds unconditionally. Silent no-op when
    // neither bound is set.
    void handle_trim_clear_both();

    void handle_trim_set_autoset(TrimSide side);
    void handle_trim_unset(TrimSide side);

    // Mouse gestures on the trim boundary stems. on_press routes a
    // waveform-area press that misses every marker but lands on a trim
    // boundary here. Ctrl begins a single-bound drag; Ctrl+Shift begins a
    // move-both-bounds drag; plain/Shift selects within the trim group. All
    // update app.last_sel_group = Trim.
    void handle_trim_boundary_press(TrimHit which, bool ctrl, bool shift,
                                    int mouse_x);
    void select_trim_boundary(TrimHit which, bool additive);
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
    // Delete on the trim group: unset each selected bound (reuses
    // handle_trim_unset) and clears the trim-selected flags.
    void delete_selected_trim();

    // Ctrl+Left / Ctrl+Right on the trim group: nudge the focused bound by
    // one whole-frame pixel-anchored column at the current zoom, exactly
    // like nudge_selected_markers (painted column steps by one, the new
    // column's time commits through snap_authored_frame). direction: -1
    // earlier, +1 later. Differs from the marker nudge only in its walls:
    // each bound clamps to its own absolute per-bound range — begin to
    // [0, EOF-1], end to [0, EOF] (EOF is the exclusive upper bound) — and
    // there is no partner wall, so the two bounds may cross or equal
    // mid-gesture; the render boundary owns cross/equal validity at commit.
    void nudge_selected_trim(int direction);

    // Bare `t` toggle: flip app.active_audio_view between Source and Target.
    // Stops any current playback before switching domains. Source → Target
    // translates app.viewport_start_sample / playhead_cursor_sample /
    // zoom_level through the current warp_frame_map in place and enters target
    // view only when target view is available. target-view playback is
    // allowed once the target buffer is ready; target render
    // update-in-progress gates playback elsewhere. Silent no-op while
    // render-view is active — the render-view gate above this dispatcher
    // already drops bare `t`.
    void handle_active_audio_view_toggle();

    // Render-view input helpers. Extracted verbatim from the mega event
    // handlers (on_key / on_button_press / on_motion); each is a cohesive,
    // behavior-preserving lift of one render-view-only block. They live on
    // the class so their definitions can move to input_render_view.cpp.
    //
    // render_view_key_blocked: the render-view key gate as a predicate — true
    // when `key`+`mods` is NOT permitted while render-view is active (so the
    // caller drops it with an early return). Expressed as read_only_key_blocked
    // plus a small named delta (render-view-specific admits and extra blocks)
    // so the two gates cannot drift; the delta is documented at the definition.
    bool render_view_key_blocked(GuiKey key, GuiInputState mods);

    // Source-view read-only allowlist, and the base gate render_view_key_blocked
    // defers to. Returns true if key+mods is NOT on the allowlist of navigation
    // / playback / zoom / view-switch / close-prompt / save keys honored in a
    // read-only source tab — i.e. should be dropped. Authoring-mutation chords
    // (trim gestures, Delete, undo/redo, the propagate commands) are blocked
    // here at the gate.
    bool read_only_key_blocked(GuiKey key, GuiInputState mods);

    // Bottom-strip modal-editor predicate + key gate (bodies in
    // input_key_dispatch.cpp). Modal surfaces are bottom-strip surfaces —
    // the two bottom-strip editors (settings editor, bpm bracket editor)
    // and the prompts; the top-strip flag editor is deliberately non-modal.
    // The gate is the sibling of read_only_key_blocked's allowlist shape:
    // true when key+mods should be dropped while a bottom-strip editor is
    // open (admits only the keys the active editor consumes, Esc, Ctrl+S,
    // and Ctrl+Q).
    bool modal_bottom_strip_editor_active() const;
    bool modal_editor_key_blocked(GuiKey key, GuiInputState mods);

    // handle_render_view_toggle: the bare-R enter/exit handler. Returns false
    // if the chord is not bare R (caller falls through); otherwise performs
    // the enter (enumerate, migrate persisted selection, load) or exit (stash
    // entry view state + selection, restore source, clear render-view state)
    // and returns true.
    bool handle_render_view_toggle(GuiKey key, GuiInputState mods);

    // handle_render_view_nav: render-view list navigation. Handles both
    // Shift+Left/Right (wraparound) and Shift+Home / Shift+End (clamp, no wrap).
    // Returns true when it handled the chord (so the caller returns); false
    // when neither chord matches (or render-view is off), so the caller falls
    // through to the source-view handlers.
    bool handle_render_view_nav(GuiKey key, GuiInputState mods);

    // handle_render_view_press: the on_button_press render-view block. Fully
    // terminating (the caller returns after it). Handles Left-only gating, the
    // phase-reset top-strip no-op, marker hit-test + selection bookkeeping,
    // playhead move, and waveform-press playhead-drag arming. Recomputes its
    // own cheap geometry; derives `shift` from `mods`.
    void handle_render_view_press(GuiMouseButton button, int x, int y,
                                  bool inside_top, bool inside_waveform,
                                  GuiInputState mods);

    // handle_render_view_motion: the on_motion render-view block. Fully
    // terminating (caller returns after it). During a playhead drag, snaps the
    // playhead to the visible sub-view's markers (3px epsilon) with Shift
    // sweep-select; otherwise runs hover-popup detection over the render-view
    // markers. Recomputes its own cheap geometry.
    void handle_render_view_motion(int mouse_x, int mouse_y, GuiInputState mods);
};
