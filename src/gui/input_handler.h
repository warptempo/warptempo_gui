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

// X.7.8b-1: keyboard input handler. Owns the on_key callback body extracted
// verbatim from main.cpp's lambda at the original main.cpp:1588. Lifetime is
// the same scope as the other operation structs.
//
// X.7.8b-2 adds on_button_press / on_button_release as public methods plus
// the shared wheel handler as a private helper.
//
// X.7.8b-3 adds on_motion the same way.

// -- Brief X.3 BPM-sweep math primitive ---------------------------------
//
// X.7.8b-1: promoted out of main.cpp's anonymous namespace so
// input_handler.cpp can reach it. on_key (Ctrl+Alt+M) is the sole caller
// after this brief; if a future TU needs it the home is open for relocation.
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
// callers, all inside the on_key body. After this brief it has zero
// remaining callers in main.cpp, so it moves onto this struct as a private
// helper. RenderBatchResult was a struct local to main() (no other
// callers); it becomes a nested type here for the same reason.
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
                    GuiTargetRender&      target_render_)
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
          target_render(target_render_) {}

    void on_key(GuiKey key, GuiInputState mods);
    void on_button_press(GuiMouseButton button, int x, int y, GuiInputState mods);
    void on_button_release(GuiMouseButton button, int x, int y,
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

    // X.7.8b-2: shared wheel handler covering source-view and render-view.
    // Promoted from a lambda in main.cpp:1444 because on_button_press is
    // its only caller. Ctrl+Alt = fine-pan (2% of viewport), Alt = coarse-
    // pan (10%), plain = zoom; Ctrl+wheel moves the playhead by one pixel
    // (and stops playback), matching the bare Left/Right keyboard binding.
    void handle_wheel(GuiMouseButton button, bool ctrl, bool alt,
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

    // Shift+b / Shift+e clear the active tab's trim_begin / trim_end
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
    void handle_trim_unset(TrimSide side);

    // Bare `t` toggle: flip app.active_audio_view between Source and Target.
    // Translates app.viewport_start_sample / playhead_sample / zoom_level
    // through the current timemap in place (forward on S→T, inverse on
    // T→S) so the visible viewport stays the same screen-pixel extent
    // across the toggle. Stops playback (target view has no playback in
    // brief 1) and invalidates the whole window. Silent no-op while
    // render-view is active — the render-view gate above this dispatcher
    // already drops bare `t`.
    void handle_active_audio_view_toggle();
};
