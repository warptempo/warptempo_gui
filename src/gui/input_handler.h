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
#include "phase_reset_markers_ops.h"
#include "undo.h"
#include "viewport.h"
#include "warpmarkers_ops.h"
#include "gui_input.h"
#include "platform_wayland.h"

#include <cmath>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct GuiPaintHandler;

// Keyboard input handler. Owns the on_key callback body extracted
// verbatim from main.cpp's lambda at the original main.cpp:1588. Lifetime is
// the same scope as the other operation structs.
//
// Also provides on_button_press / on_button_release as public methods plus
// the shared wheel handler as a private helper, and on_motion the same way.

// -- BPM-sweep math primitive -------------------------------------------
//
// Promoted out of main.cpp's anonymous namespace so
// input_handler.cpp can reach it. render_bpm_sweep() is the sole caller;
// if a future TU needs it the home is open for relocation.
//
// Given a span's measured duration (seconds), the user-asserted beat count
// for that span, and a target BPM, return the (base_tempo, scale) pair the
// engine needs so that one cell of the BPM sweep renders at exactly the
// target tempo. base_tempo rounds to 2 decimals via banker's rounding
// (std::nearbyint with FE_TONEAREST); scale rounds to 6 decimals the same
// way. The bash-script port uses an epsilon nudge before rounding to work
// around shell-level numerics — that nudge does not apply in C++ and is
// intentionally omitted here. The C++ port may diverge from the bash script
// on tie cases; this is documented behavior.
struct BaseTempoScale {
    double base_tempo;
    double scale;
    double ratio;
};

inline std::optional<BaseTempoScale> compute_base_tempo_scale(
    double duration_seconds, int beats, int target_bpm) {
    if (!(duration_seconds > 0.0)) return std::nullopt;
    if (beats      <= 0) return std::nullopt;
    if (target_bpm <= 0) return std::nullopt;

    const double desired_duration =
        static_cast<double>(beats) * 60.0 /
        static_cast<double>(target_bpm);
    if (!std::isfinite(desired_duration) ||
        desired_duration == 0.0) return std::nullopt;

    const double ratio = duration_seconds / desired_duration;
    if (!std::isfinite(ratio)) return std::nullopt;

    const double base_tempo =
        std::nearbyint(ratio * 100.0) / 100.0;
    if (!std::isfinite(base_tempo) ||
        base_tempo == 0.0) return std::nullopt;

    const double scale =
        std::nearbyint((ratio / base_tempo) * 1e6) / 1e6;
    if (!std::isfinite(scale)) return std::nullopt;

    return BaseTempoScale{base_tempo, scale, ratio};
}

// -- GuiInputHandler ----------------------------------------------------
//
// run_render_batch was a non-trivial lambda local to main() with three
// callers, all inside the on_key body. It now has zero
// remaining callers in main.cpp, so it lives on this struct as a private
// helper. RenderBatchResult was a struct local to main() (no other
// callers); it is a nested type here for the same reason.
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
    GuiFlagEditor&           flag_editor;
    GuiRenderView&           render_view;
    GuiActiveViews&          active_views;
    PhaseResetPropagate&     phase_reset_propagate;
    GuiAsyncRenderer&        async_renderer;
    GuiPlaybackLifecycle&    playback_lifecycle;
    GuiSaveOps&              save_ops;
    GuiPrompt&               prompt;
    GuiSettingsEditor&       settings_editor;
    GuiTargetRender&      target_render;
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
                    GuiFlagEditor&           flag_editor_,
                    GuiRenderView&           render_view_,
                    GuiActiveViews&          active_views_,
                    PhaseResetPropagate&     phase_reset_propagate_,
                    GuiAsyncRenderer&        async_renderer_,
                    GuiPlaybackLifecycle&    playback_lifecycle_,
                    GuiSaveOps&              save_ops_,
                    GuiPrompt&               prompt_,
                    GuiSettingsEditor&       settings_editor_,
                    GuiTargetRender&      target_render_,
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
          paint_handler(paint_handler_) {}

    void on_key(GuiKey key, GuiInputState mods);
    void on_button_press(GuiMouseButton button, int x, int y, GuiInputState mods);
    void on_button_release(GuiMouseButton button, int x, int y,
                           GuiInputState mods);
    // Coalesced scroll-wheel entry. `count` is the net detent count for one
    // pointer frame (>= 1); the platform's set_on_wheel routes here.
    void on_wheel(GuiMouseButton dir, int count, int x, int y,
                  GuiInputState mods);
    void on_motion(int mouse_x, int mouse_y, GuiInputState mods);

private:
    // ActiveBatch holds the run_render_batch state machine. The batch loop
    // used to be synchronous (blocking inside do_render); now each entry is
    // dispatched onto GuiAsyncRenderer and the next entry fires from the
    // worker-completion callback. The GUI remains interactive between (and
    // during) entries.
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

    // Sweep every BPM in the BPM owner's [bpm_lo, bpm_hi] range,
    // computing (base_tempo, scale) per cell and rendering one .wav per
    // cell into `<source_parent>/renders/<N>_render_bpm_iterations/`. The
    // body is the former Ctrl+Alt+M block verbatim, minus the keystroke
    // gate; it is now fired by Enter in the bottom-strip BPM editor (after
    // a successful commit). Returns true if a render batch was dispatched;
    // false on any guard bail (wrong view / mode off / no owner / blank
    // values / zero-duration span / no valid cells / renderer busy).
    bool render_bpm_sweep();

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

    // Clipboard: perform the platform I/O for a Copy/Cut/Paste editor action
    // against editor `s`, and report whether it handled one. Copy and cut
    // push the selection to the clipboard (cut then deletes it); paste pulls
    // the clipboard text into the selection. Returns false for any other
    // action so the caller can fall through to its remaining branches.
    bool apply_editor_clipboard(text_editor::KeyAction action,
                                text_editor::State& s);

    // Shared wheel handler covering source-view and render-view.
    // Promoted from a lambda in main.cpp:1444 because on_button_press is
    // its only caller. Ctrl+Alt = fine-pan (2% of viewport), Alt = coarse-
    // pan (10%), plain = zoom; Ctrl+wheel moves the playhead by one pixel
    // (and stops playback), matching the bare Left/Right keyboard binding.
    void handle_wheel(GuiMouseButton button, int count, bool ctrl, bool alt,
                      bool inside_waveform, bool inside_top);

    // Tab / Shift+Tab / IsoLeftTab dispatch: cycle marker focus, then stop
    // playback, move the playhead onto the newly focused marker, and
    // recenter the viewport at maximum zoom. Mirrors the GuiKeys::C zoom +
    // center sequence with the playhead-move folded in. Mode-aware: reads
    // from phase_reset_markers in 'P' mode, warpmarkers otherwise.
    void cycle_marker_focus_with_recenter(bool forward);

    // b / e key handlers — set the settings-side trim_begin / trim_end to
    // the playhead's current position. Mode-agnostic. Re-press at the
    // same sample frame toggles off; equal-frame collision with the
    // opposite trim is refused; out-of-order candidate auto-swaps with
    // the opposite trim. Trim is settings-class — no undo participation;
    // no dirty signal (silently persisted on Ctrl+S, silently discarded
    // on Ctrl+W without save); invalidates waveform + timestamp areas.
    void handle_trim_set_begin_at_playhead();
    void handle_trim_set_end_at_playhead();

    // Shift+b / Shift+e clear the project trim_begin / trim_end
    // unconditionally (independent of playhead position). Silent no-op
    // when the relevant trim is already unset.
    void handle_trim_unset_begin();
    void handle_trim_unset_end();

    // Side-parameterized helpers shared by the four trim entry points
    // above. The entry points are kept as named per-side wrappers
    // because the on_key dispatch reads more cleanly as four named
    // methods than as four side-parameterized invocations.
    enum class TrimSide { Begin, End };
    void handle_trim_set_at_playhead(TrimSide side);
    void stop_playback_if_scanner_out_of_trim();
    void handle_trim_unset(TrimSide side);

    // Mouse gestures on the trim boundary stems. on_press routes a
    // waveform-area press that misses every marker but lands on a trim
    // boundary here. Ctrl begins a drag; plain/Shift selects within the
    // trim group. All update app.last_sel_group = Trim.
    void handle_trim_boundary_press(TrimHit which, bool ctrl, bool shift,
                                    int mouse_x);
    void select_trim_boundary(TrimHit which, bool additive);
    void begin_trim_drag(TrimHit which, int mouse_x);
    void update_trim_drag(int mouse_x);   // motion: writes the live store
    // mouse_x → source-domain seconds, the single conversion both the drag
    // anchor (begin) and the live cursor (update) read so they can never
    // diverge. Returns false (out untouched) when audio/zoom state is unusable.
    bool trim_mouse_x_to_source_seconds(int mouse_x, double& out_seconds);
    void commit_trim_drag();               // release: undo push + render
    // Delete on the trim group: unset each selected bound (reuses
    // handle_trim_unset) and clears the trim-selected flags.
    void delete_selected_trim();

    // Bare `t` toggle: flip app.active_audio_view between Source and Target.
    // Translates app.viewport_start_sample / playhead_cursor_sample / zoom_level
    // through the current frame_map in place (forward on S→T, inverse on
    // T→S) so the visible viewport stays the same screen-pixel extent
    // across the toggle. Stops playback (target view has no playback)
    // and invalidates the whole window. Silent no-op while
    // render-view is active — the render-view gate above this dispatcher
    // already drops bare `t`.
    void handle_active_audio_view_toggle();

    // Render-view input helpers. Extracted verbatim from the mega event
    // handlers (on_key / on_button_press / on_motion); each is a cohesive,
    // behavior-preserving lift of one render-view-only block. They live on
    // the class so their definitions can move to input_render_view.cpp.
    //
    // render_view_key_blocked: the on_key read-only allowlist as a predicate
    // — true when `key`+`mods` is NOT one of the chords permitted while
    // render-view is active (so the caller drops it with an early return).
    bool render_view_key_blocked(GuiKey key, GuiInputState mods);

    // handle_render_view_toggle: the bare-R enter/exit handler. Returns false
    // if the chord is not bare R (caller falls through); otherwise performs
    // the enter (enumerate, migrate persisted selection, load) or exit (stash
    // rendersettings + selection, restore source, clear render-view state)
    // and returns true.
    bool handle_render_view_toggle(GuiKey key, GuiInputState mods);

    // handle_render_view_nav: render-view list navigation. Handles both
    // Shift+Left/Right (wraparound) and Shift+Home/End (clamp, no wrap).
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
