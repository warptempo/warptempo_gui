#pragma once

#include "app_state.h"
#include "async_renderer.h"
#include "audio.h"
#include "flag_editor.h"
#include "playback.h"
#include "render_pipeline.h"
#include "render_view.h"
#include "selection.h"
#include "tab_mode.h"
#include "phase_reset_propagate.h"
#include "phase_reset_markers_ops.h"
#include "undo.h"
#include "viewport.h"
#include "warpmarkers_ops.h"
#include "gui_input.h"
#include "platform_wayland.h"

#include <cmath>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// X.7.8b-1: keyboard input handler. Owns the on_key callback body extracted
// verbatim from main.cpp's lambda at the original main.cpp:1588. Construction
// site: main.cpp, after every operation struct (Viewport, Selection, Undo,
// GuiPhaseResetMarkersOps, GuiWarpMarkersOps, GuiFlagEditor, GuiRenderView,
// GuiTabMode, GuiPaintHandler) and after every std::function the body
// references is forward-declared. Lifetime is the same scope as the other
// operation structs.
//
// X.7.8b-2 adds on_button_press / on_button_release as public methods plus
// the shared wheel handler as a private helper. Bodies are byte-identical
// to the lambdas they replaced in main.cpp (set_on_button_press at the
// original main.cpp:1483; set_on_button_release at main.cpp:1835; the
// handle_wheel lambda at main.cpp:1444). The reference list this struct
// already carries covers every dependency the new bodies need; nothing
// new is captured.
//
// X.7.8b-3 adds on_motion the same way (body byte-identical to the
// lambda at the original main.cpp:1319). The same reference list
// covers it; popup_eligible_marker and compute_hover_popup_text are
// reached as free functions (promoted out of main.cpp's anonymous
// namespace into app_state.{h,cpp} and render.{h,cpp} respectively).
//
// Residual-cluster lambdas the brief listed (revert_to_blank,
// restore_playhead_to_lsp, load_then_drain) are intentionally absent: a
// scan of the on_key body confirmed the keyboard handler does not call
// them. They will be added when the corresponding mouse handler / file
// handler that does call them moves out in a later brief.

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
    AppState&                  app;
    const GuiAudio&            audio;
    GuiPlatform&                    gui;
    GuiPlayback&               playback;
    Viewport&                  viewport;
    Selection&                 selection;
    Undo&                      undo;
    GuiWarpMarkersOps&         warpops;
    GuiPhaseResetMarkersOps&    phase_resets;
    GuiFlagEditor&             flag_editor;
    GuiRenderView&             render_view;
    GuiTabMode&                tab_mode;
    PhaseResetPropagate&        phase_reset_propagate;
    GuiAsyncRenderer&          async_renderer;
    std::function<void()>&     clear_hover_popup;
    std::function<void()>&     stop_playback_if_playing;
    std::function<bool()>&     save_markers;
    std::function<void(DialogTrigger)>& request_close_or_revert;
    std::function<void(char)>& prompt_activate_response;
    std::function<void()>&     toggle_playback;
    std::function<void(float)>& set_playback_speed;

    GuiInputHandler(AppState&                  app_,
                    const GuiAudio&            audio_,
                    GuiPlatform&                    gui_,
                    GuiPlayback&               playback_,
                    Viewport&                  viewport_,
                    Selection&                 selection_,
                    Undo&                      undo_,
                    GuiWarpMarkersOps&         warpops_,
                    GuiPhaseResetMarkersOps&    phase_resets_,
                    GuiFlagEditor&             flag_editor_,
                    GuiRenderView&             render_view_,
                    GuiTabMode&                tab_mode_,
                    PhaseResetPropagate&        phase_reset_propagate_,
                    GuiAsyncRenderer&          async_renderer_,
                    std::function<void()>&     clear_hover_popup_,
                    std::function<void()>&     stop_playback_if_playing_,
                    std::function<bool()>&     save_markers_,
                    std::function<void(DialogTrigger)>& request_close_or_revert_,
                    std::function<void(char)>& prompt_activate_response_,
                    std::function<void()>&     toggle_playback_,
                    std::function<void(float)>& set_playback_speed_)
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
          tab_mode(tab_mode_),
          phase_reset_propagate(phase_reset_propagate_),
          async_renderer(async_renderer_),
          clear_hover_popup(clear_hover_popup_),
          stop_playback_if_playing(stop_playback_if_playing_),
          save_markers(save_markers_),
          request_close_or_revert(request_close_or_revert_),
          prompt_activate_response(prompt_activate_response_),
          toggle_playback(toggle_playback_),
          set_playback_speed(set_playback_speed_) {}

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

    // b / e key handlers — set the settings-side trim_begin / trim_end to
    // the playhead's current position. Mode-agnostic. Re-press at the
    // same sample frame toggles off; equal-frame collision with the
    // opposite trim is refused; out-of-order candidate auto-swaps with
    // the opposite trim. Trim is settings-class — no undo participation;
    // sets app.settings_dirty and invalidates.
    void handle_trim_set_begin_at_playhead();
    void handle_trim_set_end_at_playhead();
};
