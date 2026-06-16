#include "input_handler.h"

#include "paint_handler.h"
#include "render.h"
#include "render_pipeline.h"
#include "settings_io.h"
#include "text_editor.h"
#include "time_format.h"
#include "timemap.h"
#include "warpmarkers.h"
#include "frame_map.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <map>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

// X.7.8b-1: keyboard input handler. Method bodies are byte-identical to the
// lambdas they replaced in main.cpp (set_on_key at the original main.cpp:1588;
// run_render_batch at the original main.cpp:1539). The only changes are:
//
//   - Capture-by-reference of `app`, `audio`, `gui`, `playback`, `viewport`,
//     `selection`, `undo`, `warpops`, `phase resets`, `flag_editor`,
//     `render_view`, `active_views`, `playback_lifecycle`, `save_ops`, `prompt`
//     is now reference-member access on `this`. The std::function forwarder
//     pattern (clear_hover_popup, stop_playback_if_playing, save_markers,
//     request_close_or_revert, prompt_activate_response, toggle_playback,
//     set_playback_speed) was retired in favor of direct struct method
//     calls (viewport.clear_hover_popup,
//     playback_lifecycle.stop_playback_if_playing /
//     toggle_playback / set_playback_speed, save_ops.save,
//     prompt.request_close_or_revert / activate_response).
//   - Forwarder lambdas in main.cpp (do_undo, do_redo, recompute_dirty,
//     push_undo_both, select_*, clear_selection, set_single_selection,
//     toggle_selection_membership, move_playhead_*, zoom_*, scroll_viewport,
//     center_viewport_on_playhead, invalidate_*, trim_*_sample, drop_*,
//     delete_*, force_delete_*, toggle_inherits/disabled/begin_time/end_time,
//     adjust_tempo, clear_trim, nudge_*, jump_*, drop_phase_reset_at_playhead,
//     toggle_phase_reset_*, detect_phase_resets, clear_all_phase_resets,
//     enter_*_edit, commit_*_edit, exit_top_flag_edit_no_commit,
//     bulk_clear_*_values, enter_bpm_mode, exit_bpm_mode,
//     toggle_active_markers_view, load_render_view_at, restore_source_audio,
//     stash_render_view_selection_to_active_entry,
//     enumerate_render_view_list, write_rendersettings_for) were rewritten
//     to direct method calls on the appropriate operation struct ref.
//   - Free function calls (do_render, iter_popup_eligible_marker,
//     effective_disabled, compute_base_tempo_scale, text_editor::*,
//     std::filesystem::*, std::printf, etc.) keep their original spelling.
//     compute_base_tempo_scale + BaseTempoScale moved out of main.cpp's
//     anonymous namespace into input_handler.h so this TU can reach them;
//     render_bpm_sweep() is the sole caller.

// F2.1: mouse drag-to-select for the three text editors. The selection
// highlight is already painted from the editor State's selection_anchor /
// cursor_pos, so the whole gesture is input-side: a press sets the anchor
// and arms the drag, motion moves cursor_pos (extending the highlight),
// release finalizes. The only per-editor geometry the mouse path needs is
// each editor's char-0 text origin; advance is the shared monospace cell.
namespace {

// Brief 4c: sweep-select every marker in the time-ordered `markers` list
// whose time_seconds falls in [lo_t, hi_t], iterating in travel order
// (ascending indices when `forward`, else descending) so the final
// last_selected_marker lands on the most recently passed marker. Skips
// press_marker_idx (preserves the Shift-press toggle non-re-add guarantee)
// and already-selected indices. Mutates app's selection set / focus /
// last_sel_group; returns true if anything was added. Shared by the
// source/target and render-view playhead-drag Shift sweeps; templated on
// the vector element type because the two stores hold different marker
// types that both expose time_seconds. O(log n + added) per call.
template <typename MarkerVec>
bool sweep_select_interval(AppState& app, const MarkerVec& markers,
                           double lo_t, double hi_t, bool forward,
                           int press_marker_idx) {
    if (lo_t > hi_t) return false;
    // First index with time_seconds >= lo_t through the last with
    // time_seconds <= hi_t (half-open [first, last)).
    const int first = static_cast<int>(
        std::lower_bound(markers.begin(), markers.end(), lo_t,
                         [](const auto& m, double t) {
                             return m.time_seconds < t;
                         }) - markers.begin());
    const int last = static_cast<int>(
        std::upper_bound(markers.begin(), markers.end(), hi_t,
                         [](double t, const auto& m) {
                             return t < m.time_seconds;
                         }) - markers.begin());
    bool changed = false;
    auto add = [&](int idx) {
        if (idx == press_marker_idx) return;
        if (app.selected_markers.count(idx)) return;
        app.selected_markers.insert(idx);
        app.last_selected_marker = idx;
        app.last_sel_group = LastSelGroup::Markers;
        changed = true;
    };
    if (forward) {
        for (int i = first; i < last; ++i) add(i);
    } else {
        for (int i = last - 1; i >= first; --i) add(i);
    }
    return changed;
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
        g.text_left = static_cast<double>(kTimestampPadX) +
            std::strlen(kSettingsEditorPrefix) * adv;
        g.bottom_strip = true;
    } else if (text_editor::is_active(app.top_flag_editor) &&
               app.top_flag_editor.kind == text_editor::Kind::BpmBracket) {
        g.ed = &app.top_flag_editor;
        g.text_left = static_cast<double>(kTimestampPadX) +
            std::strlen(kBpmEditorPrefix) * adv;
        g.bottom_strip = true;
    } else if (text_editor::is_active(app.top_flag_editor)) {
        // FlagPayload / IterationBracket — top strip.
        const double tl = flag_pending_text_left_x(
            app, audio, app.top_flag_editor.target);
        if (tl < 0.0) return g;   // flag off-view: leave invalid
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

} // namespace

static bool is_play_pause_key(GuiKey key) {
    return key == GuiKeys::Space
        || key == GuiKeys::Return
        || key == GuiKeys::KpEnter;
}

void GuiInputHandler::finalize_render_run() {
    app.queue_running          = false;
    app.queue_cancel_requested = false;
    // Invalidate the wide bottom-strip rect before clearing the
    // progress text. bottom_strip_wide() reads queue_progress_text;
    // clearing first would shrink the invalidated rect to the narrow
    // timestamp width, leaving the trailing pixels of the final
    // "rendering N of N..." string undamaged.
    viewport.invalidate_timestamp_area();
    app.queue_progress_text.clear();
    // A target-view edit during this archival render may have queued a
    // pending target render. Worker is now idle — fire it.
    target_render.maybe_dispatch_pending();
}

void GuiInputHandler::start_render_batch(std::vector<RenderRequest> reqs,
                                         std::string batch_label) {
    if (reqs.empty()) return;

    // Snapshot the batch's destination folder before moving reqs onto
    // batch_. Each per-entry dispatch moves a RenderRequest out of
    // batch_.reqs, so reqs.front().batch_folder is only readable here
    // — by the time the terminal success branch needs it for auto-
    // open, it's been moved away. All three batch call sites construct
    // every RenderRequest in the batch with the same batch_folder
    // string, so reading the front entry is canonical.
    batch_.batch_folder = std::filesystem::path(reqs.front().batch_folder);

    batch_.reqs       = std::move(reqs);
    batch_.label      = std::move(batch_label);
    batch_.next_index = 0;
    batch_.rendered   = 0;
    batch_.active     = true;

    app.queue_cancel_requested = false;
    app.queue_running          = true;
    viewport.clear_hover_popup();

    dispatch_next_batch_entry();
}

void GuiInputHandler::dispatch_next_batch_entry() {
    if (!batch_.active) return;

    const int total = static_cast<int>(batch_.reqs.size());

    // Batch terminates if Esc was pressed since the last dispatch OR if
    // we ran out of entries. Either way: log a summary and clean up.
    const bool out_of_entries = (batch_.next_index >= total);
    const bool cancelled      = app.queue_cancel_requested;
    if (out_of_entries || cancelled) {
        if (cancelled) {
            std::fprintf(stderr,
                "warptempo_gui: %s: rendered %d of %d entries (cancelled)\n",
                batch_.label.c_str(), batch_.rendered, total);
        } else {
            std::fprintf(stderr,
                "warptempo_gui: %s: rendered %d of %d entries\n",
                batch_.label.c_str(), batch_.rendered, total);
        }
        // Auto-open render-view at the just-rendered batch's first
        // file. Gated on a non-cancelled terminal branch that
        // actually produced at least one .wav on disk; a batch
        // where every entry returned Failed leaves rendered == 0
        // and there is nothing to view. Per the render-view
        // gatekeeper invariant, render-view must be off here:
        // S / E / Ctrl+Alt+R / Ctrl+Alt+E / Ctrl+Alt+I are all dropped
        // while render-view is active, and the BPM sweep fires only from
        // the BPM editor's Enter (which cannot be open in render-view), so
        // no batch dispatch can reach this terminal branch from inside
        // render-view. The call runs before finalize_render_run +
        // reqs.clear so render-view sees the same surrounding
        // state ordering it would on a manual `r` toggle.
        const bool success = !cancelled && batch_.rendered > 0;
        if (success) {
            render_view.auto_open_batch_at_first_file(batch_.batch_folder);
        }
        batch_.active = false;
        batch_.reqs.clear();
        batch_.reqs.shrink_to_fit();
        finalize_render_run();
        return;
    }

    char buf[128];
    std::snprintf(buf, sizeof(buf),
                  "%s: rendering %d of %d...",
                  batch_.label.c_str(),
                  batch_.next_index + 1, total);
    app.queue_progress_text = buf;
    viewport.invalidate_timestamp_area();

    RenderRequest req = std::move(batch_.reqs[batch_.next_index]);
    async_renderer.dispatch(std::move(req),
        [this](RenderOutcome o) { on_batch_entry_complete(o); });
}

void GuiInputHandler::on_batch_entry_complete(RenderOutcome outcome) {
    if (!batch_.active) return;

    if (outcome == RenderOutcome::Success)   ++batch_.rendered;
    if (outcome == RenderOutcome::Cancelled) app.queue_cancel_requested = true;

    ++batch_.next_index;
    dispatch_next_batch_entry();
    // dispatch_next_batch_entry either dispatched the next batch
    // entry (worker is busy → pending target render stays queued) or
    // finalized via finalize_render_run (which already pumps the
    // pending target render). Either way we don't need to call
    // maybe_dispatch_pending here.
}

// Brief E: human-readable provenance descriptor for a committed BPM cell,
// e.g. "36 beats @ 220 bpm from 00:32.008 to 00:46.562". Beats and bpm are
// integers; the two timestamps are the span's owner and endpoint marker
// times, formatted via the shared mm:ss.mmm formatter. Stored verbatim in
// the cell's .rendersettings bpm= field and promoted into .settings on
// commit.
static std::string format_bpm_descriptor(int beats, int bpm,
                                         double start_seconds,
                                         double end_seconds) {
    return std::to_string(beats) + " beats @ " +
           std::to_string(bpm) + " bpm from " +
           format_timestamp(start_seconds) + " to " +
           format_timestamp(end_seconds);
}

// Brief E (formerly Brief X.3): sweep every BPM in the BPM owner's
// [bpm_lo, bpm_hi] range, computing (base_tempo, scale) per cell and
// rendering one .wav per cell into
// `<source_parent>/renders/<N>_render_bpm_iterations/`. The per-cell
// engine values land in the `.rendersettings` sidecar's engine block
// (written by do_render); Ctrl+Alt+C reads only the scale field back when
// committing a BPM cell. The substantive difference from the iter render
// handler is per-cell mutation of cell_settings.scale, in addition to
// per-cell marker mutation. Returns true iff a batch was dispatched; every
// guard bail returns false. Body is the former Ctrl+Alt+M block verbatim,
// minus the keystroke gate.
bool GuiInputHandler::render_bpm_sweep() {
    if (app.active_markers_view != 'W') return false;
    if (!app.bpm_mode_enabled) return false;
    if (app.source_audio_path.empty()) return false;
    if (audio.sample_rate() <= 0) return false;
    if (audio.total_frames() <= 0) return false;

    const std::vector<GuiWarpMarker> base_markers =
        app.warpmarkers.markers();
    int owner_idx = -1;
    for (int i = 0; i < static_cast<int>(base_markers.size()); ++i) {
        if (base_markers[i].bpm_owner) {
            owner_idx = i;
            break;
        }
    }
    if (owner_idx < 0) return false;
    const GuiWarpMarker& owner = base_markers[owner_idx];
    if (owner.bpm_beats <= 0) return false;
    if (owner.bpm_lo    <= 0) return false;
    if (owner.bpm_hi    <= 0) return false;

    // Span endpoint is explicit (set on the `m` two-marker gate, part 1).
    const int endpoint_idx = owner.bpm_endpoint;
    if (endpoint_idx <= owner_idx ||
        endpoint_idx >= static_cast<int>(base_markers.size())) {
        return false;   // missing or malformed span: no sweep
    }
    const double duration_seconds =
        base_markers[endpoint_idx].time_seconds - owner.time_seconds;
    if (!(duration_seconds > 0.0)) return false;

    std::vector<int> bpm_values;
    for (int b = owner.bpm_lo; b <= owner.bpm_hi; ++b) {
        bpm_values.push_back(b);
    }
    if (bpm_values.empty()) return false;

    std::filesystem::path src(app.source_audio_path);
    std::filesystem::path src_parent = src.parent_path();
    if (src_parent.empty()) src_parent = std::filesystem::path(".");
    const std::filesystem::path queue_root = src_parent / "renders";

    int next_index = 1;
    std::error_code ec;
    if (std::filesystem::is_directory(queue_root, ec)) {
        int max_idx = 0;
        for (const auto& de :
             std::filesystem::directory_iterator(queue_root, ec)) {
            if (!de.is_directory()) continue;
            const std::string name = de.path().filename().string();
            int v = 0;
            size_t i = 0;
            while (i < name.size() &&
                   name[i] >= '0' && name[i] <= '9') {
                v = v * 10 + (name[i] - '0');
                ++i;
            }
            if (i == 0 || i >= name.size() || name[i] != '_') continue;
            if (v > max_idx) max_idx = v;
        }
        next_index = max_idx + 1;
    }

    const std::string command_tag = "render_bpm_iterations";
    const std::filesystem::path batch_folder =
        queue_root /
        (std::to_string(next_index) + "_" + command_tag);

    int pad_width = 1;
    for (int n = static_cast<int>(bpm_values.size());
         n >= 10; n /= 10) ++pad_width;
    if (pad_width > 9) pad_width = 9;

    const std::vector<GuiPhaseResetMarker> base_phase_resets =
        app.phase_reset_markers.markers();
    const std::vector<int64_t> base_phase_reset_frames =
        phase_reset_source_frames(
            slice_to_phase_reset_markers(base_phase_resets),
            audio.sample_rate());

    std::vector<RenderRequest> reqs;
    reqs.reserve(bpm_values.size());
    int seq = 1;
    for (int bpm : bpm_values) {
        const auto computed = compute_base_tempo_scale(
            duration_seconds, owner.bpm_beats, bpm);
        if (!computed) {
            std::fprintf(stderr,
                "warptempo_gui: render-bpm: rejected cell "
                "bpm=%d (duration=%.6f, beats=%d)\n",
                bpm, duration_seconds, owner.bpm_beats);
            continue;
        }

        std::vector<GuiWarpMarker> cell_markers = base_markers;
        // Owner: concrete computed base tempo, scale carried in settings.
        cell_markers[owner_idx].tempo_inherits = false;
        cell_markers[owner_idx].tempo_base     = computed->base_tempo;
        cell_markers[owner_idx].tempo_scale.clear();
        // Span-internal markers pass: their own tempo is subsumed by the
        // owner's span tempo. Disabled span-internal markers stay disabled
        // but also pass (the disabled flag is independent of tempo_inherits).
        for (int i = owner_idx + 1; i < endpoint_idx; ++i) {
            cell_markers[i].tempo_inherits = true;
            cell_markers[i].tempo_base     = 1.0;       // inert default
            cell_markers[i].tempo_scale    = "1.0000";  // model's inert scale
            // label_def on a span-internal marker is preserved (refs are
            // excluded from spans by the part-1 gate, but a def may exist);
            // only the tempo fields are rewritten. Do not touch label_def,
            // disabled, or any non-tempo field.
        }
        // endpoint marker: untouched — its section lies outside the span.

        EngineSettings cell_settings = app.engine_settings;
        cell_settings.scale = computed->scale;
        // Provenance descriptor for this cell's .rendersettings; promoted
        // verbatim into .settings on Ctrl+Alt+C commit (part 3).
        cell_settings.bpm =
            format_bpm_descriptor(owner.bpm_beats, bpm,
                                  owner.time_seconds,
                                  base_markers[endpoint_idx].time_seconds);

        char num_buf[16];
        std::snprintf(num_buf, sizeof(num_buf),
                      "%0*d", pad_width, seq);
        char rest_buf[64];
        std::snprintf(rest_buf, sizeof(rest_buf),
                      "_%d,%.2f,%.6f",
                      bpm, computed->base_tempo, computed->scale);
        std::string basename = num_buf;
        basename += rest_buf;

        RenderRequest req;
        req.source_audio_path    = app.source_audio_path;
        req.markers              = std::move(cell_markers);
        req.phase_resets           = base_phase_resets;
        req.phase_reset_frames     = base_phase_reset_frames;
        req.engine_settings      = std::move(cell_settings);
        {
            const ViewState& vs = active_view_state(app);
            req.has_trim_begin       = vs.has_trim_begin;
            req.trim_begin_sec       = vs.trim_begin_seconds;
            req.has_trim_end         = vs.has_trim_end;
            req.trim_end_sec         = vs.trim_end_seconds;
        }
        req.batch_folder         = batch_folder.string();
        req.batch_basename       = std::move(basename);
        reqs.push_back(std::move(req));
        ++seq;
    }

    if (reqs.empty()) {
        std::fprintf(stderr,
            "warptempo_gui: render-bpm: no valid cells; "
            "nothing to render\n");
        return false;
    }

    std::filesystem::create_directories(batch_folder, ec);
    if (ec) {
        std::fprintf(stderr,
            "warptempo_gui: render-bpm: could not create "
            "'%s': %s\n",
            batch_folder.string().c_str(), ec.message().c_str());
        return false;
    }

    if (async_renderer.is_busy()) return false;
    start_render_batch(std::move(reqs), "bpm");
    return true;
}

void GuiInputHandler::on_key(GuiKey key, GuiInputState mods) {
    if constexpr (kDebugPerf) {
        app.last_input_event_time = std::chrono::steady_clock::now();
    }
    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;
    const bool alt   = mods.alt;

    // Transient bottom-strip status message clears on every real
    // keypress, including the press that may set a new message later
    // in this same on_key call (the handler that sets it does so at
    // the very end of its branch, after this clear). Guarded so the
    // bottom-strip invalidate fires only when there was a message to
    // erase. See AppState::transient_status_message.
    if (!app.transient_status_message.empty()) {
        app.transient_status_message.clear();
        viewport.invalidate_timestamp_area();
    }

    // Bottom-strip prompt owns input while active. Only the prompt's
    // own response keys do anything; everything else is swallowed so
    // marker edits / playback / viewport keys cannot sneak in while
    // the prompt is up. Letter keys are case-insensitive; Delete and
    // Escape map to sentinel chars '\x7f' and '\x1b' so they participate
    // in the same vector<char> match as letter responses.
    if (app.prompt.active) {
        // PASTE_CONFIRM only: Ctrl+Q / Ctrl+W abandon the pending paste
        // (the real cancel, not a synthesized Esc) and then run the
        // normal close/revert path. The unsaved-work dialogs
        // (CLOSE_WINDOW / REVERT_TO_BLANK) deliberately fall through to
        // the modal swallow below and keep blocking these chords.
        if (app.prompt.trigger == DialogTrigger::PASTE_CONFIRM &&
            ctrl && !shift && !alt &&
            (key == GuiKeys::Q || key == GuiKeys::W)) {
            prompt.cancel_paste_confirmation();
            prompt.request_close_or_revert(
                key == GuiKeys::Q ? DialogTrigger::CLOSE_WINDOW
                                  : DialogTrigger::REVERT_TO_BLANK);
            return;
        }
        char k = 0;
        if (key >= GuiKeys::A && key <= GuiKeys::Z) {
            k = static_cast<char>('a' + (key - GuiKeys::A));
        } else if (key == GuiKeys::Delete) {
            k = '\x7f';
        } else if (key == GuiKeys::Escape) {
            k = '\x1b';
        }
        if (k != 0) {
            for (char rk : app.prompt.response_keys) {
                if (k == rk) {
                    prompt.activate_response(rk);
                    return;
                }
            }
        }
        return;
    }

    // Blank / loading state: only the quit / close-gesture bindings run;
    // everything else no-ops. Dialog can't fire here because dirty is
    // always false in blank state (history is reset on revert).
    if (app.loading || audio.total_frames() <= 0) {
        if (ctrl && !shift && !alt && key == GuiKeys::Q) {
            prompt.request_close_or_revert(DialogTrigger::CLOSE_WINDOW);
        }
        return;
    }

    // V.A1 top-flag editor owns the keyboard while active. Routes here
    // BEFORE queue/drag/playhead Esc handlers so Esc cancels the edit
    // first; Esc with no active edit falls through to the rest.
    if (text_editor::is_active(app.top_flag_editor)) {
        const auto action = text_editor::handle_key(
            app.top_flag_editor, key, mods);
        if (action == text_editor::KeyAction::CommitRequested) {
            // Brief D: iteration editing is a widened-grammar FlagPayload
            // commit (commit_top_flag_edit), not a separate bracket editor.
            if (app.top_flag_editor.kind ==
                    text_editor::Kind::BpmBracket) {
                // Brief E: Enter commits + renders + closes in one action.
                // A successful commit stores the values on the owner and
                // closes the editor; only then does the BPM sweep fire. A
                // parse failure leaves the editor open (red) and renders
                // nothing.
                if (flag_editor.commit_bpm_edit()) {
                    // Ordering is load-bearing: render_bpm_sweep early-bails
                    // on !bpm_mode_enabled, so the sweep MUST fire before
                    // exit_bpm_mode clears the flag. Then close out the mode
                    // and repaint the bottom strip without the editor.
                    render_bpm_sweep();
                    flag_editor.exit_bpm_mode();
                    // Behavior 1: the fired sweep consumes its values. The
                    // render already snapshotted the owner into its
                    // RenderRequest, so clearing here doesn't disturb it.
                    // Next M on this marker seeds [].
                    auto& mv = app.warpmarkers.markers_mut();
                    for (auto& m : mv) {
                        if (m.bpm_owner) {
                            m.bpm_owner = false;
                            m.bpm_beats = 0;
                            m.bpm_lo    = 0;
                            m.bpm_hi    = 0;
                        }
                    }
                    viewport.invalidate_timestamp_area();
                }
            } else {
                flag_editor.commit_top_flag_edit();
            }
            return;
        }
        if (action == text_editor::KeyAction::CancelRequested) {
            if (app.top_flag_editor.kind == text_editor::Kind::BpmBracket) {
                flag_editor.exit_top_flag_edit_no_commit();
                flag_editor.exit_bpm_mode();
                viewport.invalidate_timestamp_area();
            } else {
                flag_editor.exit_top_flag_edit_no_commit();
            }
            return;
        }
        if (apply_editor_clipboard(action, app.top_flag_editor)) {
            // Same repaint as the Consumed branch — text may have changed
            // (cut / paste); copy repaints harmlessly.
            if (app.top_flag_editor.kind == text_editor::Kind::BpmBracket)
                viewport.invalidate_timestamp_area();
            else
                viewport.invalidate_top_strip();
            return;
        }
        if (action == text_editor::KeyAction::Consumed) {
            // The BpmBracket editor draws in the bottom strip (like the
            // settings editor); FlagPayload / IterationBracket draw in the
            // top strip. Invalidate whichever strip the editor lives in.
            if (app.top_flag_editor.kind == text_editor::Kind::BpmBracket)
                viewport.invalidate_timestamp_area();
            else
                viewport.invalidate_top_strip();
            return;
        }
        // NotConsumed: the editor does not own this key, so it is a command.
        // Cancel the edit (Esc-discard: no commit, no validation), using the
        // same teardown Esc uses, then fall through (no return) so the key
        // reaches the global command dispatch below and runs. This is how every
        // command (Ctrl+Q/W/S, Ctrl+Z, Ctrl+Tab, Ctrl+P, Ctrl+E, ...) works
        // mid-edit: exit first, then the command. No command list — the editor
        // owns only its editing keymap and everything else punches through.
        if (app.top_flag_editor.kind == text_editor::Kind::BpmBracket) {
            flag_editor.exit_top_flag_edit_no_commit();
            flag_editor.exit_bpm_mode();
            viewport.invalidate_timestamp_area();
        } else {
            flag_editor.exit_top_flag_edit_no_commit();
        }
        // No return — fall through to the global dispatch.
    }

    // Settings-prompt editor (`;` opener). Same shape as the flag-editor
    // block above. The two editors are mutually exclusive in practice
    // because the flag editor's block returns early while it owns the
    // keyboard, so a stray `;` can't open settings over a live flag
    // edit. Routed before queue/drag/playhead Esc handlers so Esc
    // cancels the edit first.
    if (text_editor::is_active(app.settings_editor)) {
        // Bare Tab autocompletes the value side of `key=` with the key's
        // current stored value (canonical engine keys only), for recall and
        // editing. Only an unmodified Tab is intercepted; Shift / Ctrl /
        // Alt + Tab fall through to handle_key unchanged.
        if (key == GuiKeys::Tab && !ctrl && !shift && !alt) {
            settings_editor.autocomplete_value();
            return;
        }
        const auto action = text_editor::handle_key(
            app.settings_editor, key, mods);
        if (action == text_editor::KeyAction::CommitRequested) {
            settings_editor.commit();
            return;
        }
        if (action == text_editor::KeyAction::CancelRequested) {
            settings_editor.exit_no_commit();
            return;
        }
        if (apply_editor_clipboard(action, app.settings_editor)) {
            viewport.invalidate_timestamp_area();
            return;
        }
        if (action == text_editor::KeyAction::Consumed) {
            viewport.invalidate_timestamp_area();
            return;
        }
        // NotConsumed: a command. Cancel the settings edit (Esc-discard) and
        // fall through so the global dispatch runs the command.
        settings_editor.exit_no_commit();
        // No return — fall through.
    }

    // Chunk W: render-view input gate. While render-view is active
    // only keys driving navigation / playback / exit / commit are
    // honored; every authoring key is silently dropped so a stray
    // press can't mutate state through a swapped-out view.
    // Allowlist:
    //   - r (no mods)            → toggle render-view off
    //   - Shift+Left/Right       → previous/next render
    //   - Ctrl+Alt+C             → commit displayed render's markers
    //   - Space                  → playback toggle
    //   - Left/Right (no mods)   → playhead-by-pixel scrub
    //   - Home/End (no mods)     → playhead to absolute file bounds
    //                              (render-view has no trim — see
    //                              viewport.cpp trim_range)
    //   - Esc                    → top-level no-op (chunk Q)
    //   - p (no mods)            → toggle warp/phase reset sub-view (Brief F)
    //   - Tab / Shift+Tab /      → cycle marker focus (no A/B tabs in
    //     IsoLeftTab               render-view, so Ctrl+Tab / Ctrl+Shift+Tab
    //                              stay no-ops; cycles the render-domain
    //                              collection per the active p-state)
    //   - Ctrl+Q / Ctrl+W        → close-prompt routing (Brief F)
    //   - Up/Down (no mods)      → zoom in/out (Brief S.2)
    //   - =/- (no mods)          → zoom in/out symbol-key alias (Brief S.2)
    //   - 0 (no mods)            → fit ↔ max-zoom-in toggle
    //   - f (no mods)          → follow mode toggle
    //   - c (no mods)            → center+max-zoom-in on playhead
    //
    // Note on the absent disk-save-shape keys: Ctrl+S (save),
    // Ctrl+E (queue-add), Ctrl+Alt+R (single render to source dir),
    // Ctrl+Alt+E (render queue), and Ctrl+Alt+I (render iterations) are
    // intentionally NOT on the allowlist. The BPM sweep render is likewise
    // unreachable here — it fires only from the BPM editor's Enter, which
    // cannot be open in render-view. Rendering and queue-add are disk-write
    // operations against the source folder (the same shape as
    // Ctrl+S), and render-view is read-only with respect to
    // authoring state and source-dir writes — admitting any of
    // them would mutate state through a swapped-out view. The
    // batch-render auto-open path in dispatch_next_batch_entry
    // relies on this invariant: because none of the render-
    // shaped keys can fire while render-view is on, the auto-
    // open call cannot be reached with render-view already
    // active, and so the new render_view.auto_open_batch_at_
    // first_file method handles only the "render-view is off"
    // case.
    if (app.render_view_enabled) {
        const bool is_r =
            (key == GuiKeys::R && !ctrl && !shift && !alt);
        const bool is_nav =
            ((key == GuiKeys::Left || key == GuiKeys::Right) &&
             shift && !ctrl && !alt);
        const bool is_render_view_nav_jump =
            ((key == GuiKeys::Home || key == GuiKeys::End) &&
             shift && !ctrl && !alt);
        const bool is_commit =
            (ctrl && alt && !shift &&
             key == GuiKeys::C);
        const bool is_playback = is_play_pause_key(key);
        const bool is_scrub =
            ((key == GuiKeys::Left || key == GuiKeys::Right) &&
             !ctrl && !shift && !alt);
        const bool is_jump =
            ((key == GuiKeys::Home || key == GuiKeys::End) &&
             !ctrl && !shift && !alt);
        const bool is_esc = (key == GuiKeys::Escape);
        const bool is_sub_view_toggle =
            (key == GuiKeys::P && !ctrl && !shift && !alt);
        const bool is_tab_cycle =
            (!ctrl && !alt &&
             (key == GuiKeys::Tab || key == GuiKeys::IsoLeftTab));
        const bool is_ctrl_q =
            (ctrl && !shift && !alt && key == GuiKeys::Q);
        const bool is_ctrl_w =
            (ctrl && !shift && !alt && key == GuiKeys::W);
        const bool is_zoom =
            ((key == GuiKeys::Up || key == GuiKeys::Down) &&
             !ctrl && !shift && !alt);
        const bool is_zoom_symbol =
            ((key == GuiKeys::Equal || key == GuiKeys::Minus) &&
             !ctrl && !shift && !alt);
        const bool is_zero =
            (key == GuiKeys::Digit0) && !ctrl && !shift && !alt;
        const bool is_follow =
            (key == GuiKeys::F && !ctrl && !shift && !alt);
        const bool is_center =
            (key == GuiKeys::C && !ctrl && !shift && !alt);
        if (!(is_r || is_nav || is_render_view_nav_jump ||
              is_commit || is_playback ||
              is_scrub || is_jump || is_esc ||
              is_sub_view_toggle || is_tab_cycle ||
              is_ctrl_q || is_ctrl_w ||
              is_zoom || is_zoom_symbol || is_zero || is_follow ||
              is_center)) {
            return;
        }
    }

    // Per-tab read-only keyboard gate. Mirrors the render-view gate
    // above structurally: a permitted-keys allowlist that filters out
    // every authoring chord while admitting navigation, playback,
    // view-switching, the close-prompt routing, and the bare-o
    // toggle-off escape chord. Only runs when render-view is off and
    // the active tab's ViewState carries read_only = true. Render-view
    // is its own read-only modality that supersedes everything else,
    // so this gate sits second.
    //   - Bare o                 → toggle read-only off (escape chord)
    //   - Space                  → playback toggle
    //   - Left/Right (no mods)   → playhead-by-pixel scrub
    //   - Shift+Left/Right       → playhead-by-samples scrub
    //   - Home/End (no mods)     → playhead to trim region bounds
    //   - Up/Down (no mods)      → zoom in/out
    //   - =/- (no mods)          → zoom symbol-key alias
    //   - 0 (no mods)            → fit ↔ max-zoom-in toggle
    //   - f (no mods)            → follow mode toggle
    //   - c (no mods)            → center+max-zoom on playhead
    //   - t (no mods)            → S/T sub-view toggle
    //   - p (no mods)            → W/P sub-view toggle
    //   - Tab/Shift+Tab/IsoLeftTab → cycle marker focus
    //   - Ctrl+Tab               → switch A/B tab (the other escape)
    //   - Ctrl+Shift+Tab         → march paired tabs in lockstep
    //   - Esc                    → top-level no-op
    //   - Ctrl+Q / Ctrl+W        → close-prompt routing
    //   - Ctrl+P                 → copy phase reset placements
    //                              (read-only: writes only to the
    //                              session-only phase_reset_clipboard)
    // Every authoring chord (drop, drag, delete, label/tempo edit,
    // trim set/unset, disabled-flag toggle, paste, iteration / BPM
    // mode, render dispatch, queue add, undo/redo) is silently
    // dropped at this gate.
    if (!app.render_view_enabled && active_view_state(app).read_only) {
        const bool is_o =
            (key == GuiKeys::O && !ctrl && !shift && !alt);
        const bool is_play_pause = is_play_pause_key(key);
        const bool is_scrub =
            ((key == GuiKeys::Left || key == GuiKeys::Right) &&
             !ctrl && !shift && !alt);
        const bool is_scrub_samples =
            ((key == GuiKeys::Left || key == GuiKeys::Right) &&
             !ctrl && shift && !alt);
        const bool is_home_end =
            ((key == GuiKeys::Home || key == GuiKeys::End) &&
             !ctrl && !shift && !alt);
        const bool is_page_updown =
            ((key == GuiKeys::PageUp || key == GuiKeys::PageDown) &&
             !ctrl && !shift && !alt);
        const bool is_zoom =
            ((key == GuiKeys::Up || key == GuiKeys::Down) &&
             !ctrl && !shift && !alt);
        const bool is_zoom_symbol =
            ((key == GuiKeys::Equal || key == GuiKeys::Minus) &&
             !ctrl && !shift && !alt);
        const bool is_zero =
            (key == GuiKeys::Digit0 && !ctrl && !shift && !alt);
        const bool is_follow =
            (key == GuiKeys::F && !ctrl && !shift && !alt);
        const bool is_center =
            (key == GuiKeys::C && !ctrl && !shift && !alt);
        const bool is_sub_t =
            (key == GuiKeys::T && !ctrl && !shift && !alt);
        const bool is_sub_p =
            (key == GuiKeys::P && !ctrl && !shift && !alt);
        const bool is_tab_cycle =
            (!ctrl && key == GuiKeys::Tab) ||
            (!ctrl && key == GuiKeys::IsoLeftTab);
        const bool is_ctrl_tab =
            (ctrl && !shift && key == GuiKeys::Tab);
        const bool is_ctrl_shift_tab =
            (ctrl && shift && key == GuiKeys::Tab);
        const bool is_esc = (key == GuiKeys::Escape);
        const bool is_ctrl_q =
            (ctrl && !shift && !alt && key == GuiKeys::Q);
        const bool is_ctrl_w =
            (ctrl && !shift && !alt && key == GuiKeys::W);
        const bool is_save =
            (ctrl && !shift && !alt && key == GuiKeys::S);
        const bool is_copy_phase_resets =
            (ctrl && !shift && !alt && key == GuiKeys::P);
        // Ctrl+Z (undo) and Ctrl+Shift+Z (redo) are admitted through this
        // active-tab gate unconditionally. The honored-ness rule is not the
        // active tab's read-only state but the target entry's tab, which may
        // be the other tab; only do_undo / do_redo have that entry in hand,
        // so the real read-only decision lives there, not here.
        const bool is_undo_redo = (ctrl && !alt && key == GuiKeys::Z);
        if (!(is_o || is_play_pause || is_scrub || is_scrub_samples ||
              is_home_end || is_page_updown ||
              is_zoom || is_zoom_symbol || is_zero ||
              is_follow || is_center || is_sub_t || is_sub_p ||
              is_tab_cycle || is_ctrl_tab || is_ctrl_shift_tab ||
              is_esc || is_ctrl_q || is_ctrl_w || is_save ||
              is_copy_phase_resets || is_undo_redo)) {
            return;
        }
    }

    // Brief 3b: target-view keyboard authoring is fully unblocked.
    // Every binding source view honors runs in target view too; the
    // input-to-source-frame boundary translation lives at the individual
    // handlers (drop_marker_at_playhead, handle_trim_*, nudge_*, etc.).

    // Bare `t` toggles view-domain (S ↔ T). Placed before the marker /
    // phase reset edit handlers so the toggle wins over any future
    // bare-t binding; placed after the prompt / editor / render-view
    // / queue gates so those still own the keyboard when active.
    // Render-view drops bare `t` via the gate above (target view is
    // unreachable from render-view; render-view exits via `r`).
    if (key == GuiKeys::T && !ctrl && !shift && !alt) {
        handle_active_audio_view_toggle();
        return;
    }

    // Bare `o` toggles the active tab's read-only flag. Always admitted
    // by the read-only allowlist above (the locked-out user must be
    // able to unlock) and dropped by the render-view allowlist (the
    // flag is irrelevant while render-view is its own read-only
    // modality). Pure view-state mutation: not undoable, not dirty;
    // silently persisted on Ctrl+S. The bottom-strip dim update lands
    // through invalidate_timestamp_area, which covers the A/B tab
    // letter glyph.
    if (key == GuiKeys::O && !ctrl && !shift && !alt) {
        ViewState& vs = active_view_state(app);
        vs.read_only = !vs.read_only;
        viewport.invalidate_timestamp_area();
        return;
    }

    // Esc during a render-in-flight requests cancellation. Two effects:
    //   1. async_renderer.request_cancel() sets the worker's cancel flag.
    //      The engine observes it at the next frame boundary (warptempo)
    //      or at the next 10 ms waitpid tick (subprocess engines) and
    //      returns Cancelled.
    //   2. app.queue_cancel_requested = true so that on_batch_entry_complete
    //      finalizes the batch instead of dispatching the next entry.
    // Both are needed: (1) interrupts the current render mid-stream;
    // (2) stops the batch state machine from advancing after the
    // cancelled render's on_done fires.
    if (key == GuiKeys::Escape && async_renderer.is_busy()) {
        async_renderer.request_cancel();
        app.queue_cancel_requested = true;
        return;
    }
    if (key == GuiKeys::Escape && app.queue_running) {
        // Render-state housekeeping flag survives a frame past the
        // worker's actual completion (worker_state_ transitions
        // Running -> CompletionPending while is_busy() still returns
        // true; once on_completion_event fires it goes Idle). The
        // is_busy() branch above covers that window. This branch is
        // the rare case where queue_running is set but the worker has
        // already cleared — defensive, mirrors the prior behavior.
        app.queue_cancel_requested = true;
        return;
    }

    // Escape during a playhead drag ends the gesture at its current
    // position (no restore — the drag already committed its visible
    // progress per motion event, so there's nothing to revert).
    if (key == GuiKeys::Escape && app.playhead_drag.active) {
        app.playhead_drag = PlayheadDragState{};
        return;
    }

    // Escape during a trim-boundary drag reverts the dragged bound to its
    // pre-drag value (the drag mutated the live store per motion event)
    // and ends the gesture without an undo entry.
    if (key == GuiKeys::Escape && app.trim_drag.active) {
        ViewState& vs = active_view_state(app);
        double& field = app.trim_drag.is_begin ? vs.trim_begin_seconds
                                                : vs.trim_end_seconds;
        double& other = app.trim_drag.is_begin ? vs.trim_end_seconds
                                                : vs.trim_begin_seconds;
        bool changed = false;
        if (field != app.trim_drag.orig_seconds) {
            field = app.trim_drag.orig_seconds;
            changed = true;
        }
        // Group drag moved both bounds; restore the other one too.
        if (app.trim_drag.group && other != app.trim_drag.orig_other_seconds) {
            other = app.trim_drag.orig_other_seconds;
            changed = true;
        }
        if (changed) {
            viewport.invalidate_waveform_area();
            viewport.invalidate_timestamp_area();
            target_render.trigger();
        }
        app.trim_drag = TrimDragState{};
        return;
    }

    // Ctrl+Q: quit (via unsaved-work dialog when dirty).
    if (ctrl && !shift && !alt && key == GuiKeys::Q) {
        prompt.request_close_or_revert(DialogTrigger::CLOSE_WINDOW);
        return;
    }

    // Ctrl+W: revert to blank state (via unsaved-work dialog when dirty).
    if (ctrl && !shift && !alt && key == GuiKeys::W) {
        prompt.request_close_or_revert(DialogTrigger::REVERT_TO_BLANK);
        return;
    }

    // Ctrl+E: snapshot current authoring state into the in-memory
    // render queue. No disk writes; on-disk authoring files are untouched.
    // Settings are not snapshotted per-entry — the queue walker uses
    // the live engine_settings at execution time, mirroring the
    // chunk-U convention. (Chunk W: snapshots moved from disk to memory.)
    if (ctrl && !alt && !shift &&
        key == GuiKeys::E) {
        if (app.source_audio_path.empty()) return;
        AppState::QueuedRender q;
        q.source_audio_path = app.source_audio_path;
        q.markers           = app.warpmarkers.markers();
        q.phase_resets        = app.phase_reset_markers.markers();
        app.queued_renders.push_back(std::move(q));
        std::fprintf(stderr,
            "warptempo_gui: queued render (%zu in queue)\n",
            app.queued_renders.size());
        return;
    }

    // Ctrl+Alt+R: single render into the source directory using `title`
    // from settings. Mirrors the pre-Chunk-W non-batch path inside
    // do_render: empty batch_folder/batch_basename triggers the
    // engine/limiter-prefix naming. Title-not-set is a hard error
    // surfaced from do_render. Does not consult the in-memory queue and
    // does not write any sidecars beyond the .peaks pyramid that
    // do_render already deposits.
    if (ctrl && alt && !shift &&
        key == GuiKeys::R) {
        if (app.source_audio_path.empty()) return;
        // Reject overlapping submissions: the dispatcher is single-job.
        // The GUI's existing serialization (queue_running gate) prevents
        // this in practice, but a defensive early-return keeps the
        // contract local.
        if (async_renderer.is_busy()) return;

        RenderRequest req;
        req.source_audio_path    = app.source_audio_path;
        req.markers              = app.warpmarkers.markers();
        req.phase_resets           = app.phase_reset_markers.markers();
        req.engine_settings      = app.engine_settings;
        {
            const ViewState& vs = active_view_state(app);
            req.has_trim_begin       = vs.has_trim_begin;
            req.trim_begin_sec       = vs.trim_begin_seconds;
            req.has_trim_end         = vs.has_trim_end;
            req.trim_end_sec         = vs.trim_end_seconds;
        }
        for (const auto& m : app.phase_reset_markers.markers()) {
            if (m.disabled) continue;
            req.phase_reset_frames.push_back(static_cast<int64_t>(
                std::nearbyint(m.time_seconds *
                               static_cast<double>(audio.sample_rate()))));
        }
        // Empty batch_folder/basename selects the source-dir naming
        // convention inside do_render. The dispatch hands the request to
        // the worker thread; on_done fires on the GUI thread when the
        // render finishes (success, failure, or cancel).
        app.queue_cancel_requested = false;
        app.queue_running          = true;
        app.queue_progress_text    = "rendering...";
        viewport.clear_hover_popup();
        viewport.invalidate_timestamp_area();
        async_renderer.dispatch(std::move(req),
            [this](RenderOutcome o) {
                if (o == RenderOutcome::Cancelled) {
                    std::fprintf(stderr, "warptempo_gui: render cancelled\n");
                }
                finalize_render_run();
            });
        return;
    }

    // Ctrl+Alt+E: render the in-memory queue as one batch. Each
    // queued entry produces a sibling .wav (+ .warpmarkers /
    // .phaseresetmarkers when non-empty / .peaks sidecars) inside a fresh
    // batch folder `<source_parent>/renders/<index>_render_all_in_queue/`.
    // The index is one greater than the highest pre-existing batch index
    // in that renders folder (regardless of command tag). Filenames
    // inside the batch are the entry position zero-padded to fit the
    // queue size: 01..10 for 10 entries, 1..7 for 7, 001..100 for 100.
    //
    // Empty queue auto-enqueues the current authoring state (same shape
    // as Ctrl+E) and dispatches a queue-of-one batch. Use Ctrl+Alt+R for
    // single-shot rendering into the source directory; Ctrl+Alt+E always
    // produces a batch folder under renders/.
    //
    // Esc between entries drops the remainder. The current render
    // cannot be interrupted (no mid-engine cancellation); its sidecars
    // are written if it succeeds, then the loop exits and the rest of
    // the queue is discarded. The batch folder is left as-is on disk —
    // partial batches just contain fewer files than the queue had.
    // The in-memory queue is cleared after execution whether all
    // entries ran or Esc cut it short.
    if (ctrl && alt && !shift &&
        key == GuiKeys::E) {
        if (app.source_audio_path.empty()) return;
        if (app.queued_renders.empty()) {
            AppState::QueuedRender q;
            q.source_audio_path = app.source_audio_path;
            q.markers           = app.warpmarkers.markers();
            q.phase_resets      = app.phase_reset_markers.markers();
            app.queued_renders.push_back(std::move(q));
            std::fprintf(stderr,
                "warptempo_gui: queue empty; enqueueing current state "
                "and rendering\n");
        }

        std::vector<AppState::QueuedRender> entries =
            std::move(app.queued_renders);
        app.queued_renders.clear();

        std::filesystem::path src(app.source_audio_path);
        std::filesystem::path src_parent = src.parent_path();
        if (src_parent.empty()) src_parent = std::filesystem::path(".");
        const std::filesystem::path queue_root = src_parent / "renders";

        // Resolve the next batch index: max+1 over directory entries
        // matching `<digits>_<anything>`. Empty / missing renders/
        // folder seeds index 1.
        int next_index = 1;
        std::error_code ec;
        if (std::filesystem::is_directory(queue_root, ec)) {
            int max_idx = 0;
            for (const auto& de :
                 std::filesystem::directory_iterator(queue_root, ec)) {
                if (!de.is_directory()) continue;
                const std::string name = de.path().filename().string();
                int v = 0;
                size_t i = 0;
                while (i < name.size() &&
                       name[i] >= '0' && name[i] <= '9') {
                    v = v * 10 + (name[i] - '0');
                    ++i;
                }
                if (i == 0 || i >= name.size() || name[i] != '_') continue;
                if (v > max_idx) max_idx = v;
            }
            next_index = max_idx + 1;
        }

        const std::string command_tag = "render_all_in_queue";
        const std::filesystem::path batch_folder =
            queue_root /
            (std::to_string(next_index) + "_" + command_tag);
        std::filesystem::create_directories(batch_folder, ec);
        if (ec) {
            std::fprintf(stderr,
                "warptempo_gui: render-all: could not create '%s': %s\n",
                batch_folder.string().c_str(), ec.message().c_str());
            return;
        }

        // Width-to-fit zero-padding for filename indices. pad_width is
        // computed from the queue size and clamped to a sane upper
        // bound so the snprintf below has a known-bounded output.
        const int total = static_cast<int>(entries.size());
        int pad_width = 1;
        for (int n = total; n >= 10; n /= 10) ++pad_width;
        if (pad_width > 9) pad_width = 9;

        std::vector<RenderRequest> reqs;
        reqs.reserve(total);
        for (int i = 0; i < total; ++i) {
            const auto& q = entries[i];
            char num_buf[16];
            std::snprintf(num_buf, sizeof(num_buf),
                          "%0*d", pad_width, i + 1);
            std::fprintf(stderr,
                "warptempo_gui: rendering entry %d of %d: %s/%s.wav\n",
                i + 1, total,
                batch_folder.filename().string().c_str(), num_buf);

            RenderRequest req;
            req.source_audio_path    = q.source_audio_path;
            req.markers              = q.markers;
            req.phase_resets           = q.phase_resets;
            req.engine_settings      = app.engine_settings;
            {
                const ViewState& vs = active_view_state(app);
                req.has_trim_begin       = vs.has_trim_begin;
                req.trim_begin_sec       = vs.trim_begin_seconds;
                req.has_trim_end         = vs.has_trim_end;
                req.trim_end_sec         = vs.trim_end_seconds;
            }
            for (const auto& m : q.phase_resets) {
                if (m.disabled) continue;
                req.phase_reset_frames.push_back(static_cast<int64_t>(
                    std::nearbyint(m.time_seconds *
                                   static_cast<double>(audio.sample_rate()))));
            }
            req.batch_folder   = batch_folder.string();
            req.batch_basename = num_buf;
            reqs.push_back(std::move(req));
        }

        if (async_renderer.is_busy()) return;
        start_render_batch(std::move(reqs), "render queue");
        return;
    }

    // Brief T: Ctrl+Alt+I renders the Cartesian product of the
    // per-marker iter ranges authored in iteration mode. Output lands
    // in `<source_parent>/renders/<N>_render_iterations/`, with one
    // .wav per cell named `<seq>_<delta_csv>.wav`. The CSV holds the
    // swept markers' deltas in timeline order, formatted `%+0.2f`;
    // markers with no iter range authored are excluded from the CSV
    // and contribute one fixed value (their authored tempo_base) to
    // the product. Per-cell progress and Esc cancellation are handled
    // by run_render_batch. Silent no-op outside iteration mode.
    if (ctrl && alt && !shift &&
        key == GuiKeys::I) {
        if (app.source_audio_path.empty()) return;
        if (!app.iteration_mode_enabled) return;

        // Snapshot markers in timeline order (GuiWarpMarkers guarantees
        // strict-monotonic by time_seconds). For each owning marker
        // build its per-cell delta list: a single 0.0 when no iter
        // range is authored, otherwise integer-cents enumeration from
        // iter_start to iter_end inclusive. Integer-cents avoids the
        // float-accumulation drift a naive `for (d=start; d<=end;
        // d+=0.01)` would suffer across many steps.
        const std::vector<GuiWarpMarker> base_markers =
            app.warpmarkers.markers();
        std::vector<int>                 eligible_indices;
        std::vector<std::vector<double>> per_marker_deltas;
        std::vector<bool>                is_swept;
        for (int i = 0; i < static_cast<int>(base_markers.size()); ++i) {
            const GuiWarpMarker& m = base_markers[i];
            if (!iter_popup_eligible_marker(m)) continue;
            eligible_indices.push_back(i);
            const bool swept =
                !std::isnan(m.iter_start) && !std::isnan(m.iter_end);
            is_swept.push_back(swept);
            std::vector<double> deltas;
            if (swept) {
                const int start_cents = static_cast<int>(
                    std::lround(m.iter_start * 100.0));
                const int end_cents = static_cast<int>(
                    std::lround(m.iter_end * 100.0));
                // The bracket commit enforces start <= end, but a stray
                // hand-edit of memory could violate it. Treat that as
                // no sweep rather than producing zero cells.
                if (start_cents > end_cents) {
                    deltas.push_back(0.0);
                    is_swept.back() = false;
                } else {
                    for (int c = start_cents; c <= end_cents; ++c) {
                        deltas.push_back(static_cast<double>(c) / 100.0);
                    }
                }
            } else {
                deltas.push_back(0.0);
            }
            per_marker_deltas.push_back(std::move(deltas));
        }

        bool any_swept = false;
        for (bool s : is_swept) {
            if (s) { any_swept = true; break; }
        }
        if (!any_swept) {
            std::fprintf(stderr,
                "warptempo_gui: render-iterations: no iter ranges "
                "authored; nothing to render\n");
            return;
        }

        size_t total_cells = 1;
        for (const auto& d : per_marker_deltas) total_cells *= d.size();
        if (total_cells == 0) return;

        std::filesystem::path src(app.source_audio_path);
        std::filesystem::path src_parent = src.parent_path();
        if (src_parent.empty()) src_parent = std::filesystem::path(".");
        const std::filesystem::path queue_root = src_parent / "renders";

        // Resolve the next batch index: max+1 over `<digits>_<anything>`
        // entries. Mirrors render_all_in_queue's scanner.
        int next_index = 1;
        std::error_code ec;
        if (std::filesystem::is_directory(queue_root, ec)) {
            int max_idx = 0;
            for (const auto& de :
                 std::filesystem::directory_iterator(queue_root, ec)) {
                if (!de.is_directory()) continue;
                const std::string name = de.path().filename().string();
                int v = 0;
                size_t i = 0;
                while (i < name.size() &&
                       name[i] >= '0' && name[i] <= '9') {
                    v = v * 10 + (name[i] - '0');
                    ++i;
                }
                if (i == 0 || i >= name.size() || name[i] != '_') continue;
                if (v > max_idx) max_idx = v;
            }
            next_index = max_idx + 1;
        }

        const std::string command_tag = "render_iterations";
        const std::filesystem::path batch_folder =
            queue_root /
            (std::to_string(next_index) + "_" + command_tag);
        std::filesystem::create_directories(batch_folder, ec);
        if (ec) {
            std::fprintf(stderr,
                "warptempo_gui: render-iterations: could not create "
                "'%s': %s\n",
                batch_folder.string().c_str(), ec.message().c_str());
            return;
        }

        const int total = static_cast<int>(total_cells);
        int pad_width = 1;
        for (int n = total; n >= 10; n /= 10) ++pad_width;
        if (pad_width > 9) pad_width = 9;

        // Snapshot phase resets once — every cell shares the same
        // phase reset configuration, only marker tempo_base values
        // differ across cells.
        const std::vector<GuiPhaseResetMarker> base_phase_resets =
            app.phase_reset_markers.markers();
        const std::vector<int64_t> base_phase_reset_frames =
            phase_reset_source_frames(
                slice_to_phase_reset_markers(base_phase_resets),
                audio.sample_rate());

        // Cartesian product enumeration. `indices[k]` holds the
        // current cell coordinate along the k-th eligible marker
        // (timeline order). Rightmost dimension increments fastest:
        // consecutive cells differ in the last marker's delta first.
        const size_t num_dims = per_marker_deltas.size();
        std::vector<size_t> indices(num_dims, 0);

        std::vector<RenderRequest> reqs;
        reqs.reserve(total);
        for (int cell = 0; cell < total; ++cell) {
            std::string delta_csv;
            for (size_t k = 0; k < num_dims; ++k) {
                if (!is_swept[k]) continue;
                const double d = per_marker_deltas[k][indices[k]];
                char dbuf[16];
                std::snprintf(dbuf, sizeof(dbuf), "%+0.2f", d);
                if (!delta_csv.empty()) delta_csv += ',';
                delta_csv += dbuf;
            }

            char num_buf[16];
            std::snprintf(num_buf, sizeof(num_buf),
                          "%0*d", pad_width, cell + 1);
            std::string basename = num_buf;
            basename += '_';
            basename += delta_csv;

            std::vector<GuiWarpMarker> cell_markers = base_markers;
            for (size_t k = 0; k < num_dims; ++k) {
                const int mi = eligible_indices[k];
                cell_markers[mi].tempo_base =
                    base_markers[mi].tempo_base +
                    per_marker_deltas[k][indices[k]];
                // The engine doesn't consume iter values; clear them
                // so the request is quiet.
                cell_markers[mi].iter_start =
                    std::numeric_limits<double>::quiet_NaN();
                cell_markers[mi].iter_end =
                    std::numeric_limits<double>::quiet_NaN();
            }

            RenderRequest req;
            req.source_audio_path    = app.source_audio_path;
            req.markers              = std::move(cell_markers);
            req.phase_resets           = base_phase_resets;
            req.phase_reset_frames     = base_phase_reset_frames;
            req.engine_settings      = app.engine_settings;
            {
                const ViewState& vs = active_view_state(app);
                req.has_trim_begin       = vs.has_trim_begin;
                req.trim_begin_sec       = vs.trim_begin_seconds;
                req.has_trim_end         = vs.has_trim_end;
                req.trim_end_sec         = vs.trim_end_seconds;
            }
            req.batch_folder         = batch_folder.string();
            req.batch_basename       = std::move(basename);
            reqs.push_back(std::move(req));

            // Increment rightmost dimension; carry left on overflow.
            // The last cell leaves indices in an overflowed state but
            // the loop exits before that's read.
            for (int k = static_cast<int>(num_dims) - 1; k >= 0; --k) {
                ++indices[k];
                if (indices[k] < per_marker_deltas[k].size()) break;
                indices[k] = 0;
            }
        }

        if (async_renderer.is_busy()) return;
        start_render_batch(std::move(reqs), "render iterations");
        app.iteration_mode_enabled = false;   // behavior 2: mode off after fire
        viewport.invalidate_top_strip();
        return;
    }

    // Brief E: the BPM sweep render formerly bound to Ctrl+Alt+M now lives
    // in render_bpm_sweep(), fired by Enter in the bottom-strip BPM editor
    // after a successful commit. The keystroke is retired here.

    // Chunk W: Ctrl+Alt+C commits the displayed render's markers
    // and phase resets into authoring memory. Single cross-file undo
    // entry; both warp_dirty and phase_reset_dirty are recomputed.
    // After the commit succeeds: render-view exits, the parked
    // source audio is restored, and <source_parent>/renders/ is
    // recursively wiped — by definition the user has chosen one
    // render's parameters as the new baseline, so the prior batch
    // outputs are stale and shouldn't accumulate. Silent no-op
    // outside render-view.
    if (ctrl && alt && !shift &&
        key == GuiKeys::C) {
        if (!app.render_view_enabled) return;
        if (app.render_view_index < 0) return;

        // Addendum 3: app.render_view_markers / _phase_resets are now
        // render-domain (loaded from .renderwarpmarkers /
        // .renderphaseresetmarkers for display). The commit promotes
        // the render's *source-domain*
        // markers into authoring memory, so reload them from the
        // adjacent .warpmarkers / .phaseresetmarkers sidecars at commit
        // time. Failure to read the source-domain warpmarkers aborts —
        // committing render-domain values into authoring would corrupt
        // the source coordinate system.
        const auto& cur_e =
            app.render_view_list[app.render_view_index];
        std::vector<GuiWarpMarker>    src_warp;
        std::vector<GuiPhaseResetMarker> src_trans;
        {
            const std::filesystem::path wm =
                cur_e.batch_folder / (cur_e.basename + ".warpmarkers");
            GuiWarpMarkers m;
            if (!m.load(wm.string())) {
                std::fprintf(stderr,
                    "warptempo_gui: render-view: commit aborted, failed "
                    "to load %s\n", wm.string().c_str());
                return;
            }
            src_warp = m.markers();
        }
        {
            const std::filesystem::path tm = cur_e.batch_folder /
                (cur_e.basename + ".phaseresetmarkers");
            std::error_code ec;
            if (std::filesystem::exists(tm, ec)) {
                GuiPhaseResetMarkers t;
                if (t.load(tm.string())) {
                    src_trans = t.markers();
                }
                // Load failure: treat as empty phase resets (the
                // load() call already logged its own diagnostics).
            }
        }

        std::vector<GuiWarpMarker>    warp_pre  = app.warpmarkers.markers();
        std::vector<GuiPhaseResetMarker> trans_pre = app.phase_reset_markers.markers();
        const int                 hint_last = app.last_selected_marker;

        app.warpmarkers.markers_mut()    = std::move(src_warp);
        app.phase_reset_markers.markers_mut() = std::move(src_trans);
        app.selected_markers.clear();
        app.last_selected_marker = -1;
        // Brief J.2 Section 4: the active tab's per-mode slots
        // referenced the OLD app.warpmarkers/phase resets we just
        // replaced. Clear them so restore_source_audio loads
        // empty into the live pair (and so a later mode flip
        // doesn't surface stale indices).
        {
            ViewState& t = (app.active_tab_view == 'B') ? app.tab_b
                                                   : app.tab_a;
            t.warp_selected.clear();
            t.warp_last_selected      = -1;
            t.phase_reset_selected.clear();
            t.phase_reset_last_selected = -1;
        }

        undo.push_undo_both(std::move(warp_pre), std::move(trans_pre),
                       'W', OpKind::Other, hint_last);
        undo.recompute_dirty();

        // Folder-gated engine-settings commit. BPM batch folders are
        // named `<N>_render_bpm_iterations/` — the `_bpm_` substring
        // appears in BPM folder names and in no other batch folder
        // name (queue is `_render_all_in_queue`, iter is
        // `_render_iterations`). On a BPM cell, read the per-cell
        // `.rendersettings` sidecar's engine block and assign the
        // per-cell `scale` and its originating `bpm` into
        // app.engine_settings — the BPM sweep varies these two
        // together per cell; every other engine setting in the
        // sidecar matches the user's dispatch-time engine state,
        // and the user may have changed engine settings mid-batch.
        // On any other batch type, no engine commit happens — same
        // as today's iter / queue behavior. Settings has no undo by
        // convention; this mutation is permanent until the next
        // Ctrl+S overwrites or the user manually edits the .settings
        // file.
        {
            const std::string folder_name =
                cur_e.batch_folder.filename().string();
            const bool is_bpm_cell =
                folder_name.find("_bpm_") != std::string::npos;
            if (is_bpm_cell) {
                const std::filesystem::path sidecar =
                    cur_e.batch_folder /
                    (cur_e.basename + ".rendersettings");
                auto es = read_rendersettings_engine_block(sidecar);
                if (!es) {
                    std::fprintf(stderr,
                        "warptempo_gui: render-view: commit: "
                        "rendersettings engine block invalid or absent "
                        "at '%s'; engine settings unchanged\n",
                        sidecar.string().c_str());
                } else {
                    app.engine_settings.scale = es->scale;
                    app.engine_settings.bpm   = es->bpm;
                }
            }
        }

        const std::filesystem::path src(app.source_audio_path);
        std::filesystem::path src_parent = src.parent_path();
        if (src_parent.empty()) src_parent = std::filesystem::path(".");
        const std::filesystem::path renders_root =
            src_parent / "renders";

        // Behavior 4: committing a render is a wholesale authoring reset.
        // Clear every marker's session-only iteration values and turn off
        // both sweep modes' visibility. (BPM values were already consumed
        // when the sweep that produced this render fired — behavior 1.)
        // No separate undo entry: the commit pushes its own cross-file undo
        // and wipes renders/; iter values are session-only, never serialized.
        {
            auto& mv = app.warpmarkers.markers_mut();
            for (auto& m : mv) {
                m.iter_start = std::numeric_limits<double>::quiet_NaN();
                m.iter_end   = std::numeric_limits<double>::quiet_NaN();
            }
        }
        app.iteration_mode_enabled = false;
        app.bpm_mode_enabled       = false;

        render_view.restore_source_audio();
        app.render_view_list.clear();
        app.render_view_markers.clear();
        app.render_view_phase_resets.clear();
        app.render_view_index             = -1;
        app.render_view_src_F_begin       = 0;
        app.render_view_src_F_end         = 0;
        app.last_render_view_path.clear();

        std::error_code ec;
        if (std::filesystem::is_directory(renders_root, ec)) {
            std::filesystem::remove_all(renders_root, ec);
            if (ec) {
                std::fprintf(stderr,
                    "warptempo_gui: render-view: failed to wipe "
                    "%s: %s\n",
                    renders_root.string().c_str(),
                    ec.message().c_str());
            }
        }

        std::fprintf(stderr,
            "warptempo_gui: render-view: committed render and wiped "
            "renders/\n");
        gui.invalidate_region(0, 0, app.width, app.height);
        return;
    }

    // Space / Return / KpEnter is modifier-independent.
    if (is_play_pause_key(key)) {
        // Target-view playback gating: refuse Space-to-play while a
        // target render is in flight (current is stale by
        // definition). Space-to-stop is still honored — if playback
        // happened to be running before an edit, the trigger() helper
        // already froze it, so playback.is_playing() is false in
        // practice. The empty-target-buffer case (no successful target
        // render yet in this session) is also refused so the
        // user can't play stale source-domain samples through a
        // target-view binding. Source view AND render view fall through
        // unchanged: render view plays its own loaded render buffer, not
        // target_buffer, so this in-flight / empty-buffer target gating must
        // not apply to it. render_view_enabled overrides active_audio_view
        // here, matching the "actually in target view" idiom in
        // playback_lifecycle (active_audio_view == 'T' && !render_view_enabled).
        if (app.active_audio_view == 'T' && !app.render_view_enabled &&
            !playback.is_playing()) {
            if (target_render.is_updating()) return;
            if (app.target_buffer_frames <= 0) return;
        }
        playback_lifecycle.toggle_playback();
        return;
    }

    // Shift+<digit> selects a playback speed. Shift+0 is 1.00, Shift+1
    // is 0.10, Shift+9 is 0.90. Applies immediately whether or not
    // playback is active — the audio callback picks up the new atomic
    // on the next buffer.
    if (shift && !ctrl) {
        switch (key) {
        case GuiKeys::Digit0: playback_lifecycle.set_playback_speed(1.0f); return;
        case GuiKeys::Digit1: playback_lifecycle.set_playback_speed(0.1f); return;
        case GuiKeys::Digit2: playback_lifecycle.set_playback_speed(0.2f); return;
        case GuiKeys::Digit3: playback_lifecycle.set_playback_speed(0.3f); return;
        case GuiKeys::Digit4: playback_lifecycle.set_playback_speed(0.4f); return;
        case GuiKeys::Digit5: playback_lifecycle.set_playback_speed(0.5f); return;
        case GuiKeys::Digit6: playback_lifecycle.set_playback_speed(0.6f); return;
        case GuiKeys::Digit7: playback_lifecycle.set_playback_speed(0.7f); return;
        case GuiKeys::Digit8: playback_lifecycle.set_playback_speed(0.8f); return;
        case GuiKeys::Digit9: playback_lifecycle.set_playback_speed(0.9f); return;
        default: break;
        }
    }

    // Bare 0 toggles between fit-file and max-zoom-in (kMinNumericLevel).
    // From fit-file → kMinNumericLevel (centered on playhead via
    // apply_zoom_change's numeric branch). From any numeric level →
    // fit-file. Two presses from any state always reach max-zoom-in;
    // C remains the always-direct max-in gesture. Digits 1..9 are
    // intentionally unbound.
    if (!ctrl && !alt && !shift && key == GuiKeys::Digit0) {
        if (app.zoom_level == kFitFileLevel) {
            viewport.apply_zoom_change(kMinNumericLevel);
        } else {
            viewport.apply_zoom_change(kFitFileLevel);
        }
        return;
    }

    // Ctrl+Z undo / Ctrl+Shift+Z redo. Placed before the GuiKeys::S save
    // handling so modifier dispatch reads left-to-right in the source.
    // Both are silent no-ops when their respective stack is empty.
    if (ctrl && key == GuiKeys::Z) {
        if (shift) undo.do_redo();
        else       undo.do_undo();
        return;
    }

    // Ctrl+P: copy phase reset placements from a two-warp-marker
    // selection into the session clipboard. W-mode only; phase reset
    // mode is a silent no-op. Off-count selection in W-mode emits a
    // one-line stderr nudge.
    if (key == GuiKeys::P && ctrl && !shift && !alt) {
        if (app.active_markers_view != 'W') return;
        if (app.selected_markers.size() != 2) {
            std::fprintf(stderr,
                "warptempo_gui: phase_reset copy: select exactly two warp "
                "markers\n");
            return;
        }
        phase_reset_propagate.copy_from_selection();
        return;
    }

    // Ctrl+Alt+P: paste clipboard phase resets onto the destination
    // anchored at the single selected warp marker. W-mode only; phase
    // reset mode is a silent no-op. Empty clipboard is a silent no-op.
    // Opens a confirmation prompt before any mutation.
    if (key == GuiKeys::P && ctrl && !shift && alt) {
        if (app.active_markers_view != 'W') return;
        if (app.phase_reset_clipboard.empty()) return;
        if (app.selected_markers.size() != 1) {
            std::fprintf(stderr,
                "warptempo_gui: phase_reset paste: select exactly one warp "
                "marker\n");
            return;
        }
        phase_reset_propagate.open_paste_confirmation();
        return;
    }

    // Ctrl+Alt+Shift+P: propagate the enabled/disabled *state* of
    // clipboard placements onto the matching destination region's
    // phase resets, in order. Positions are not modified. W-mode only;
    // phase reset mode is a silent no-op. Empty clipboard is a silent
    // no-op. Unlike Ctrl+Alt+P, no confirmation prompt — applies
    // directly. Divergence/mismatch is reported via the bottom-strip
    // transient status message rather than a modal dialog.
    if (key == GuiKeys::P && ctrl && shift && alt) {
        if (app.active_markers_view != 'W') return;
        if (app.phase_reset_clipboard.empty()) return;
        if (app.selected_markers.size() != 1) {
            std::fprintf(stderr,
                "warptempo_gui: phase_reset state-paste: select exactly one "
                "warp marker\n");
            return;
        }
        phase_reset_propagate.paste_state_apply();
        return;
    }

    // `p` (no modifiers) toggles phase reset view globally. Brief
    // J.2: render-view shares the global active_markers_view flag, so a
    // single handler serves both views. Render-view inherits the
    // engine precondition check from toggle_active_markers_view.
    if (key == GuiKeys::P && !ctrl && !shift && !alt) {
        active_views.toggle_active_markers_view();
        return;
    }

    // V.B `i` (no modifiers) toggles iteration mode in warp. Silent
    // no-op in phase reset view (phase reset flags carry no tempo to
    // iterate). The editor-active branch above already swallows any
    // keystroke while a popup edit is in flight, so this code only
    // runs with no active editor. Toggling repaints the top strip
    // so iteration popups appear or vanish in one frame.
    if (key == GuiKeys::I && !ctrl && !shift && !alt) {
        if (app.active_markers_view == 'W') {
            // Brief X.2: mutual exclusion. Toggling iter ON forces
            // BPM mode off; toggling iter OFF leaves BPM untouched.
            const bool turning_on = !app.iteration_mode_enabled;
            if (turning_on && app.bpm_mode_enabled) {
                app.bpm_mode_enabled = false;
            }
            app.iteration_mode_enabled = !app.iteration_mode_enabled;
            viewport.clear_hover_popup();
            viewport.invalidate_top_strip();
        }
        return;
    }
    // V.B Shift+I: bulk-clear every marker's iter values AND exit
    // iteration mode in one keystroke ("stop authoring this mode").
    // Only fires while iteration mode is on; otherwise silent no-op.
    if (key == GuiKeys::I && !ctrl && shift && !alt) {
        if (app.active_markers_view == 'W' && app.iteration_mode_enabled) {
            flag_editor.bulk_clear_iter_values();
            app.iteration_mode_enabled = false;
            viewport.invalidate_top_strip();
        }
        return;
    }

    // Brief E `m` (no modifiers): open the BPM editor on the earlier of two
    // selected markers that define an explicit span, or — if BPM mode is
    // already on — toggle it (and the editor) off. Warp view only; silent
    // no-op in phase reset view. Mutual exclusion with iter mode is handled
    // inside enter_bpm_mode. The gate requires exactly two selected markers
    // with no label_ref anywhere in the span; any other selection is a
    // silent no-op.
    if (key == GuiKeys::M && !ctrl && !shift && !alt) {
        if (app.active_markers_view != 'W') return;
        if (app.bpm_mode_enabled) {
            // Re-press: editor and mode go down together.
            if (text_editor::is_active(app.top_flag_editor) &&
                app.top_flag_editor.kind ==
                    text_editor::Kind::BpmBracket) {
                flag_editor.exit_top_flag_edit_no_commit();
            }
            flag_editor.exit_bpm_mode();
            viewport.invalidate_timestamp_area();
            return;
        }
        // Two-marker span gate. Exactly two markers must be selected; the
        // earlier owns, the later closes the span. Neither endpoint nor any
        // span-internal marker may be a label_ref — commit rewrites every
        // in-span tempo and a ref cannot take a manual tempo.
        if (app.selected_markers.size() != 2) return;
        const auto& mv = app.warpmarkers.markers();
        auto it = app.selected_markers.begin();
        const int owner    = *it++;       // std::set: ascending, so owner is
        const int endpoint = *it;         // the earlier index, endpoint later
        if (owner < 0 || endpoint >= static_cast<int>(mv.size())) return;
        // No label_ref anywhere in [owner, endpoint] inclusive (endpoint
        // included in the eligibility scan even though its section is not in
        // the rendered region — a ref endpoint still cannot bound the span
        // cleanly). Disabled markers ARE allowed and remain in-span.
        for (int i = owner; i <= endpoint; ++i) {
            if (!mv[i].label_ref.empty()) return;   // silent no-op
        }
        // Owner must still satisfy the BPM-eligibility predicate (e.g. not
        // itself a label_ref — already covered — and any other standing
        // condition bpm_popup_eligible_marker encodes).
        if (!bpm_popup_eligible_marker(mv[owner])) return;
        // enter_bpm_mode tags the owner and flips the mode flag. It no
        // longer auto-selects a next-marker cue; the span endpoint is
        // explicit, so record it on the owner and keep both selected
        // markers highlighted as the span cue.
        flag_editor.enter_bpm_mode();
        if (!app.bpm_mode_enabled) return;   // gate inside bailed
        {
            auto& mvw = app.warpmarkers.markers_mut();
            mvw[owner].bpm_endpoint = endpoint;
        }
        const std::set<int> span_selection = app.selected_markers;
        flag_editor.enter_bpm_edit(owner);
        bool restored = false;
        for (int s : span_selection) {
            if (app.selected_markers.insert(s).second) restored = true;
        }
        if (restored) viewport.invalidate_top_strip();
        return;
    }

    // Chunk W: plain `r` toggles render analysis mode. Source audio
    // must be loaded; otherwise silent no-op (nothing to base the
    // renders folder lookup on). Toggle-on enumerates the renders
    // folder and loads either the last-displayed render (if its
    // path is still in the list) or the first entry; an empty
    // enumeration aborts the toggle. Iteration mode is forcibly
    // disabled on entry per the chunk W brief; the prior value is
    // not restored on toggle-off — the user re-enables it
    // explicitly if desired.
    if (key == GuiKeys::R && !ctrl && !shift && !alt) {
        if (app.source_audio_path.empty()) return;
        if (app.loading) return;
        if (!app.render_view_enabled) {
            std::vector<AppState::RenderViewEntry> list =
                render_view.enumerate_render_view_list();
            if (list.empty()) {
                std::fprintf(stderr,
                    "warptempo_gui: render-view: no renders found "
                    "under %s/renders/\n",
                    std::filesystem::path(app.source_audio_path)
                        .parent_path().string().c_str());
                return;
            }
            // Brief F Section 4: migrate persisted selection from
            // the prior render-view session (still on the old
            // app.render_view_list) into the freshly enumerated
            // list, keyed by wav_path. Entries that disappeared
            // since last session simply lose their persisted state;
            // newly added entries start with default-empty
            // persistence (no match → load_render_view_at clears).
            if (!app.render_view_list.empty()) {
                std::map<std::string,
                    AppState::RenderViewEntry*> prior;
                for (auto& pe : app.render_view_list) {
                    prior[pe.wav_path.string()] = &pe;
                }
                for (auto& ne : list) {
                    auto it = prior.find(ne.wav_path.string());
                    if (it == prior.end()) continue;
                    const auto& src = *it->second;
                    ne.state           = src.state;
                    ne.persisted_size  = src.persisted_size;
                    ne.persisted_mtime = src.persisted_mtime;
                }
            }
            int target = 0;
            if (!app.last_render_view_path.empty()) {
                for (size_t i = 0; i < list.size(); ++i) {
                    if (list[i].wav_path.string() ==
                        app.last_render_view_path) {
                        target = static_cast<int>(i);
                        break;
                    }
                }
            }
            app.render_view_src_sr    = audio.sample_rate();
            app.render_view_src_total = audio.total_frames();
            app.render_view_list      = std::move(list);
            // E.4 behavior 3: iter/BPM modes persist across render-view
            // enter/leave. The flags are inert inside render view (input
            // gate drops i/M; paint gates on !render_view_enabled) and are
            // restored on exit. Ctrl+Alt+C is now the only forced reset.
            app.render_view_enabled    = true;
            // Brief J.2: render-view shares the global active_markers_view
            // flag, so the user's chosen mode carries across the
            // view-domain transition without per-entry restore.
            if (!render_view.load_render_view_at(target)) {
                app.render_view_enabled = false;
                app.render_view_list.clear();
            }
        } else {
            // Capture the just-viewed render's zoom/viewport/playhead
            // before restoring source-audio state. Not done on the
            // Ctrl+Alt+C commit path — the renders folder is wiped
            // immediately after commit, so the write would be lost.
            if (app.render_view_index >= 0 &&
                app.render_view_index <
                    static_cast<int>(app.render_view_list.size())) {
                render_view.write_rendersettings_for(
                    app.render_view_list[app.render_view_index]);
            }
            // Brief F Section 4: stash the live selection onto
            // the active entry so the next toggle-on can restore
            // it (gated by the wav's stat tuple still matching).
            // render_view_list is intentionally NOT cleared here
            // — re-entry migrates its persisted_* fields into the
            // freshly enumerated list.
            render_view.stash_render_view_selection_to_active_entry();
            render_view.restore_source_audio();
            app.render_view_markers.clear();
            app.render_view_phase_resets.clear();
            app.render_view_index             = -1;
            app.render_view_src_F_begin       = 0;
            app.render_view_src_F_end         = 0;
        }
        return;
    }

    // The platform boundary case-folds letters and delivers the
    // unshifted GuiKey, so a Shift+letter press arrives as the lowercase
    // GuiKeys::* with mods.shift set — disambiguate via the `shift` bool.
    if (key == GuiKeys::S) {
        if (ctrl)                          save_ops.save();
        else if (app.active_markers_view == 'P')   phase_resets.drop_phase_reset_at_playhead();
        else if (shift)                    warpops.drop_inherit_marker_at_playhead();
        else                               warpops.drop_marker_at_playhead();
        return;
    }
    // Shift+P: toggle inherit (warp only).
    if (key == GuiKeys::P && !ctrl && !alt && shift) {
        if (app.active_markers_view == 'P') return;
        warpops.toggle_inherits();
        return;
    }
    // Ctrl+D: toggle disabled (warp + phase reset). Plain `d` and Shift+D are unbound.
    if (key == GuiKeys::D && ctrl && !alt && !shift) {
        if (app.active_markers_view == 'P') phase_resets.toggle_phase_reset_disabled();
        else                        warpops.toggle_disabled();
        return;
    }
    if (key == GuiKeys::Delete && !ctrl) {
        // Brief C: Delete acts on the group named by last_sel_group. With
        // a trim boundary last-selected, clear the selected bound(s) and
        // leave markers untouched; otherwise the marker-delete runs.
        if (app.last_sel_group == LastSelGroup::Trim) {
            delete_selected_trim();
            return;
        }
        if (app.active_markers_view == 'P') {
            phase_resets.delete_selected_phase_reset();
            return;
        }
        if (shift) warpops.force_delete_selected_marker();
        else       warpops.delete_selected_marker();
        return;
    }

    // Ctrl+Tab toggles A/B navigational tabs. Stops playback, saves
    // current viewport/zoom/playhead to the leaving tab, restores the
    // target tab. Does not mark the document dirty.
    if (ctrl && !shift && key == GuiKeys::Tab) {
        active_views.switch_active_tab_view_to(app.active_tab_view == 'A' ? 'B' : 'A');
        return;
    }

    // Ctrl+Shift+Tab: advance both tabs' marker focus and end on the
    // opposite tab. Composes bare Tab and Ctrl+Tab so the user can
    // march paired tabs forward in lockstep with one chord.
    if (ctrl && shift && key == GuiKeys::Tab) {
        cycle_marker_focus_with_recenter(true);
        active_views.switch_active_tab_view_to(app.active_tab_view == 'A' ? 'B' : 'A');
        cycle_marker_focus_with_recenter(true);
        return;
    }

    // Bare Tab / Shift+Tab / IsoLeftTab: cycle focus and recenter at max
    // zoom on the focused marker. The Ctrl+Tab branch above runs first and
    // returns, so Ctrl+Tab is consumed before reaching here; the explicit
    // !ctrl guards below ensure Ctrl+Shift+Tab does not slip into the
    // cycle path either.
    if (!ctrl && key == GuiKeys::Tab && !shift) {
        cycle_marker_focus_with_recenter(true);  return;
    }
    if (!ctrl && key == GuiKeys::Tab && shift)  {
        cycle_marker_focus_with_recenter(false); return;
    }
    if (!ctrl && key == GuiKeys::IsoLeftTab)    {
        cycle_marker_focus_with_recenter(false); return;
    }

    // Tempo nudge. Ctrl+Up / Ctrl+Down only. Bare `=` / `-` were the
    // previous binding; they now zoom (see below) so the keyboard has
    // a symbol-key alias for the bare Up/Down zoom chord.
    if (ctrl && !shift && !alt && key == GuiKeys::Up) {
        warpops.adjust_tempo(+0.01); return;
    }
    if (ctrl && !shift && !alt && key == GuiKeys::Down) {
        warpops.adjust_tempo(-0.01); return;
    }
    if (key == GuiKeys::Equal && !shift && !ctrl && !alt) {
        viewport.zoom_in(); return;
    }
    if (key == GuiKeys::Minus && !shift && !ctrl && !alt) {
        viewport.zoom_out(); return;
    }

    // `u` (no modifier) unsets the active tab's trim bounds. `Shift+U`
    // clears the selection set (UI-only — no dirty, no playhead move).
    // Trim un-set is undoable as a settings entry: snapshot pre-state,
    // mutate, push.
    if (key == GuiKeys::U && !ctrl) {
        if (shift) {
            selection.clear_selection();
        } else {
            ViewState& vs = active_view_state(app);
            if (vs.has_trim_begin || vs.has_trim_end) {
                SettingsSnapshot pre = capture_current_settings(app);
                vs.has_trim_begin      = false;
                vs.has_trim_end        = false;
                vs.trim_begin_seconds  = 0.0;
                vs.trim_end_seconds    = 0.0;
                vs.trim_begin_selected = false;
                vs.trim_end_selected   = false;
                undo.push_settings_undo(std::move(pre));
                viewport.invalidate_waveform_area();
                viewport.invalidate_timestamp_area();
                target_render.trigger();
            }
        }
        return;
    }

    // Shift+b / Shift+e clear the active tab's trim_begin / trim_end
    // unconditionally. Plain b / e (no shift) set the same fields at
    // the playhead and toggle off only on equal-frame re-press; the
    // shift form makes the unset gesture independent of playhead
    // position. Both are silent no-ops when the relevant trim is
    // already unset.
    if (shift && !ctrl && !alt && key == GuiKeys::B) {
        handle_trim_unset_begin();
        return;
    }
    if (shift && !ctrl && !alt && key == GuiKeys::E) {
        handle_trim_unset_end();
        return;
    }

    // `:` opens the settings prompt in the bottom strip. Keyboard-only
    // (no click analogue). The active-editor block at the top of on_key
    // routes subsequent keystrokes; opening here just primes the State.
    if (key == GuiKeys::Semicolon && shift && !ctrl && !alt) {
        settings_editor.open();
        return;
    }

    // `j` jumps the selected set to the playhead, anchored on
    // last_selected_marker. All-or-nothing clamp check.
    if (key == GuiKeys::J && !shift && !ctrl) {
        if (app.active_markers_view == 'P') phase_resets.jump_phase_reset_selection_to_playhead();
        else                        warpops.jump_selection_to_playhead();
        return;
    }

    // Chunk W: Shift+Left / Shift+Right navigates the render-view
    // list with wraparound. Active only when render_view_enabled is
    // true; in source-view these chords fall through to the normal
    // playhead-by-pixel handler in the switch below. Wraparound
    // mirrors the brief: Shift+Right past the end loops to index 0,
    // Shift+Left before index 0 loops to the last entry.
    //
    // Pre-nav refresh: re-enumerate the renders/ folder and merge
    // per-entry persisted state into the refreshed list before
    // computing the target index. New batch folders or wavs that
    // appeared since render-view was entered become visible; deleted
    // entries vanish. Empty-after-refresh exits render-view.
    if (app.render_view_enabled && shift && !ctrl && !alt &&
        (key == GuiKeys::Left || key == GuiKeys::Right)) {
        // Capture outgoing state and stash selection before refresh —
        // refresh_render_view_list may reorder/drop entries.
        if (app.render_view_index >= 0 &&
            app.render_view_index <
                static_cast<int>(app.render_view_list.size())) {
            render_view.write_rendersettings_for(
                app.render_view_list[app.render_view_index]);
        }
        // Brief F Section 4: stash the outgoing entry's
        // selection so re-navigating back later (in the same
        // session) restores it. load_render_view_at then loads
        // the destination's own persisted state if its stat tuple
        // still matches; otherwise leaves selection empty.
        render_view.stash_render_view_selection_to_active_entry();

        if (!render_view.refresh_render_view_list()) {
            // Renders folder is empty (e.g. user deleted it externally).
            // Exit render-view gracefully.
            render_view.exit_render_view_and_clear();
            return;
        }

        const int n = static_cast<int>(app.render_view_list.size());
        int next = app.render_view_index;
        if (key == GuiKeys::Left)  next = (next - 1 + n) % n;
        else                       next = (next + 1) % n;
        render_view.load_render_view_at(next);
        return;
    }

    // Shift+Home / Shift+End: jump render-view to first / last entry,
    // clamped (no wraparound — Shift+Home at index 0 stays at 0,
    // Shift+End at the last entry stays). Gated on
    // app.render_view_enabled; outside render-view the chord is a
    // silent no-op (the bare-key switch at the bottom of this
    // function is modifier-strict). Same pre-nav refresh of the
    // renders/ folder.
    if (app.render_view_enabled && shift && !ctrl && !alt &&
        (key == GuiKeys::Home || key == GuiKeys::End)) {
        if (app.render_view_index >= 0 &&
            app.render_view_index <
                static_cast<int>(app.render_view_list.size())) {
            render_view.write_rendersettings_for(
                app.render_view_list[app.render_view_index]);
        }
        render_view.stash_render_view_selection_to_active_entry();

        if (!render_view.refresh_render_view_list()) {
            render_view.exit_render_view_and_clear();
            return;
        }

        const int n = static_cast<int>(app.render_view_list.size());
        const int target = (key == GuiKeys::Home) ? 0 : (n - 1);
        if (target == app.render_view_index) return;
        render_view.load_render_view_at(target);
        return;
    }

    // Ctrl+Left / Ctrl+Right: nudge selected markers by one pixel.
    if (ctrl && !shift && key == GuiKeys::Left) {
        if (app.active_markers_view == 'P') phase_resets.nudge_selected_phase_resets(-1);
        else                        warpops.nudge_selected_markers(-1);
        return;
    }
    if (ctrl && !shift && key == GuiKeys::Right) {
        if (app.active_markers_view == 'P') phase_resets.nudge_selected_phase_resets(+1);
        else                        warpops.nudge_selected_markers(+1);
        return;
    }

    // PageUp / PageDown: step the viewport back / forward by exactly the
    // Alt-wheel step (samples_visible / 10). PageUp goes back, PageDown
    // forward. Source-view only — the render-view allowlist above excludes
    // them.
    if (!ctrl && !alt && !shift &&
        (key == GuiKeys::PageDown || key == GuiKeys::PageUp)) {
        const int64_t step = std::max<int64_t>(
            1, samples_visible(app, audio) / 10);
        viewport.scroll_viewport(key == GuiKeys::PageUp ? -step : +step);
        return;
    }

    // Bare-key dispatch. Every modifier-gated handler above this point
    // returns on match, so by the time we reach here, any modifier being
    // held means the chord had no binding and should be a silent no-op
    // — never fall through into a bare binding (e.g. Ctrl+Shift+Alt+E
    // must not toggle end-time via GuiKeys::E).
    if (!ctrl && !shift && !alt) {
        switch (key) {
        case GuiKeys::Escape: /* top-level Escape is a no-op (chunk Q) */ break;
        case GuiKeys::Left:   playback_lifecycle.stop_playback_if_playing();
                        viewport.move_playhead_pixels(-1);         break;
        case GuiKeys::Right:  playback_lifecycle.stop_playback_if_playing();
                        viewport.move_playhead_pixels(+1);         break;
        case GuiKeys::Up:     viewport.zoom_in();                        break;
        case GuiKeys::Down:   viewport.zoom_out();                       break;
        case GuiKeys::F: {
            const bool was_off = !app.follow_mode;
            app.follow_mode = !app.follow_mode;
            if (was_off && app.follow_mode &&
                playback.is_playing()) {
                // Explicit enable overrides a prior manual-pan suppression so
                // follow resumes paging, not just the one initial jump.
                app.follow_overridden_for_session = false;
                playback.resync_predictor();
                // Land the scanner at the page-turn position if it had drifted
                // offscreen; no-op when it is already in view.
                viewport.follow_scroll_if_needed();
            }
            break;
        }
        case GuiKeys::C:      viewport.apply_zoom_change(kMinNumericLevel);
                        viewport.center_viewport_on_playhead();    break;
        case GuiKeys::Home:   playback_lifecycle.stop_playback_if_playing();
                        viewport.move_playhead_to(viewport.trim_begin_sample()); break;
        case GuiKeys::End:    playback_lifecycle.stop_playback_if_playing();
                        viewport.move_playhead_to(viewport.trim_end_sample() - 1); break;
        // b / e set the settings-side trim_begin / trim_end at the
        // current playhead position. Mode-agnostic. Re-press at the same
        // sample frame toggles off. Auto-swaps when the candidate would
        // invert the trim region; refuses equal-frame collisions.
        case GuiKeys::B: handle_trim_set_begin_at_playhead(); break;
        case GuiKeys::E: handle_trim_set_end_at_playhead();   break;
        default: break;
        }
    }
}

void GuiInputHandler::cycle_marker_focus_with_recenter(bool forward) {
    if (forward) selection.select_next_marker();
    else         selection.select_prev_marker();

    const int idx = app.last_selected_marker;
    if (idx < 0) return;

    const int sr = audio.sample_rate();
    int64_t src_sample = 0;
    if (app.active_markers_view == 'P') {
        // Render-view recenters on the displayed render-domain phase
        // resets; authoring recenters on the live store.
        const auto& tv = app.render_view_enabled
            ? app.render_view_phase_resets
            : app.phase_reset_markers.markers();
        if (idx >= static_cast<int>(tv.size())) return;
        src_sample = static_cast<int64_t>(std::nearbyint(
            tv[idx].time_seconds * static_cast<double>(sr)));
    } else {
        const auto& mv = app.render_view_enabled
            ? app.render_view_markers
            : app.warpmarkers.markers();
        if (idx >= static_cast<int>(mv.size())) return;
        src_sample = static_cast<int64_t>(std::nearbyint(
            mv[idx].time_seconds * static_cast<double>(sr)));
    }
    // Target view: forward-translate marker's source-frame to active-
    // domain (target-frame) so the playhead lands on the marker's
    // displayed position; the viewport recenter below also uses this
    // target-frame value via center_viewport_on_playhead.
    int64_t sample = src_sample;
    if (app.active_audio_view == 'T') {
        const auto tmap = build_target_view_frame_map(
            app, sr, static_cast<long>(audio.total_frames()));
        sample = to_domain_frame(app, src_sample, tmap);
    }

    playback_lifecycle.stop_playback_if_playing();

    // Capture the old playhead pixel-x before mutating, for the
    // no-scroll invalidation branch below.
    const double old_px = playhead_pixel_x(app, audio);
    const int64_t old_vp = app.viewport_start_sample;

    // Set the cursor directly — no move_playhead_to, which would scroll
    // the viewport a second time before centering. Mirror move_playhead_to's
    // scanner-sync invariant: when the scanner is inactive its sample
    // tracks the cursor (Tab always runs with playback stopped, so the
    // scanner is inactive here, but keep the guard for symmetry).
    app.playhead_cursor_sample = sample;
    if (!app.playhead_scanner_active) {
        app.playhead_scanner_sample = sample;
    }

    // Zoom to max-numeric if not already there (no-op at max zoom, where
    // this artifact occurs), then center. center_viewport_on_playhead is
    // now the SOLE viewport write in this path: it reads the cursor we
    // just set and scrolls once to center it, emitting one coherent set
    // of waveform + top-strip damage against the final viewport.
    viewport.apply_zoom_change(kMinNumericLevel);
    viewport.center_viewport_on_playhead();

    // center_viewport_on_playhead only invalidates when the viewport
    // actually moved. At end-of-file (viewport already clamped) it does
    // not move, so the cursor's column change still needs its own
    // invalidation — mirror move_playhead_to's no-scroll branch. When the
    // viewport did move, the playhead columns are already inside the
    // waveform-area damage center emitted, so only invalidate columns in
    // the unmoved case to avoid a redundant rect.
    if (app.viewport_start_sample == old_vp) {
        const double new_px = playhead_pixel_x(app, audio);
        viewport.invalidate_playhead_columns(old_px, new_px);
    }
    viewport.invalidate_timestamp_area();

    // Discrete jump: render the waveform synchronously and publish the
    // displayed fingerprint now, so this tick's stem/flag caches rebuild
    // once against the final viewport instead of blinking across the
    // async worker's rebuild window. The worker stays the path for
    // continuous gestures; we just don't route this one-shot jump
    // through it.
    paint_handler.force_synchronous_waveform_rebuild();
}

// X.7.8b-2: shared wheel handler. Verbatim from the lambda at the original
// main.cpp:1444 — only difference is the captured viewport / playhead
// helpers now resolve through this struct's reference members.
void GuiInputHandler::handle_wheel(GuiMouseButton button, int count,
                                   bool ctrl, bool alt,
                                   bool inside_waveform, bool inside_top) {
    if (!inside_waveform && !inside_top) return;
    // `count` is the net detent count coalesced for this pointer frame
    // (always >= 1 from the platform). Each chord scales its single per-step
    // action by that count and applies it in ONE viewport call, so the
    // damage / hover / worker-kick path fires once per frame regardless of
    // burst size. count == 1 reproduces the old single-detent behavior.
    if (count < 1) count = 1;
    if (ctrl && alt) {
        const int64_t step = std::max<int64_t>(
            1, samples_visible(app, audio) / 50);
        viewport.scroll_viewport((button == GuiMouseButton::WheelUp ? -step : +step) * count);
        return;
    }
    if (ctrl) {
        playback_lifecycle.stop_playback_if_playing();
        viewport.move_playhead_pixels((button == GuiMouseButton::WheelUp ? -count : +count));
        return;
    }
    if (alt) {
        const int64_t step = std::max<int64_t>(
            1, samples_visible(app, audio) / 10);
        viewport.scroll_viewport((button == GuiMouseButton::WheelUp ? -step : +step) * count);
        return;
    }
    // Plain wheel = zoom. WheelUp zooms out, WheelDown zooms in; apply the
    // net level change in a single apply_zoom_change (inside zoom_steps).
    viewport.zoom_steps(button == GuiMouseButton::WheelUp ? -count : +count);
}

// Coalesced wheel entry point. The platform delivers one of these per
// pointer frame carrying the net detent count (>= 1), instead of pumping a
// WheelUp/WheelDown through on_button_press once per detent. The gating here
// mirrors on_button_press's wheel-relevant guards (prompt / editor modals,
// loading, active drags, area hit-test) so a wheel event is swallowed in
// exactly the same situations as before; the wheel branch was identical in
// the render-view and source-view arms of on_button_press, so a single
// shared handler covers both.
void GuiInputHandler::on_wheel(GuiMouseButton dir, int count, int x, int y,
                               GuiInputState mods) {
    if constexpr (kDebugPerf) {
        app.last_input_event_time = std::chrono::steady_clock::now();
    }
    if (app.prompt.active) return;
    if (text_editor::is_active(app.settings_editor)) return;
    if (text_editor::is_active(app.top_flag_editor) &&
        app.top_flag_editor.kind == text_editor::Kind::BpmBracket) {
        return;
    }
    if (app.loading || audio.total_frames() <= 0) return;
    // A wheel event during an active drag is ignored, matching on_button_press.
    if (app.drag.active) return;
    if (app.trim_drag.active) return;

    const GuiRect area = waveform_area(app);
    const GuiRect top  = top_strip_area(app);
    const bool inside_waveform =
        x >= area.x && x < area.x + area.w &&
        y >= area.y && y < area.y + area.h;
    const bool inside_top =
        x >= top.x && x < top.x + top.w &&
        y >= top.y && y < top.y + top.h;

    handle_wheel(dir, count, mods.ctrl, mods.alt, inside_waveform, inside_top);
}

// X.7.8b-2: button-press handler. Verbatim from the lambda at the original
// main.cpp:1483; the captured operation-struct lambdas (begin_drag,
// drop_marker, drop_phase_reset_at_position, set_single_selection, etc.)
// are rewritten to direct method calls on the appropriate operation
// struct ref. The four hit_test_* lambdas are now free functions taking
// (app, audio, ...) explicit args. The handle_wheel lambda is now a
// private method on this struct.
void GuiInputHandler::on_button_press(GuiMouseButton button, int x, int y,
                                      GuiInputState mods) {
    if constexpr (kDebugPerf) {
        app.last_input_event_time = std::chrono::steady_clock::now();
    }
    // Prompt-modal input handling: while the bottom-strip prompt is
    // active, all mouse events are swallowed. Responses go through
    // the keyboard.
    if (app.prompt.active) return;

    // F2.1: mouse drag-to-select inside the active text editor. A press on
    // the active editor's text region places the caret and arms a selection
    // drag (anchor == caret until the pointer moves). Resolved before the
    // per-editor modal swallows below so the gesture reaches the settings /
    // BPM bottom-strip editors too. A press outside the active editor's
    // region falls through to the existing logic (target-switch, open-
    // another-flag, modal swallow) unchanged.
    if (button == GuiMouseButton::Left) {
        const ActiveEditorText g = active_editor_text(app, audio);
        if (g.valid) {
            bool in_region = false;
            if (g.bottom_strip) {
                const GuiRect bs = bottom_strip_area(app);
                in_region = x >= bs.x && x < bs.x + bs.w &&
                            y >= bs.y && y < bs.y + bs.h;
            } else {
                const GuiRect top = top_strip_area(app);
                const bool inside_top_strip =
                    x >= top.x && x < top.x + top.w &&
                    y >= top.y && y < top.y + top.h;
                in_region = inside_top_strip &&
                    hit_test_flag(app, audio, x, y) ==
                        app.top_flag_editor.target;
            }
            if (in_region) {
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
            // edited flag falls through to the existing target-switch / open /
            // exit handling below.
            if (g.bottom_strip) return;
        }
    }

    if (text_editor::is_active(app.settings_editor)) return;
    if (text_editor::is_active(app.top_flag_editor) &&
        app.top_flag_editor.kind == text_editor::Kind::BpmBracket) {
        // The BPM editor is a bottom-strip modal owner (like the settings
        // editor). Mouse input does not interact with it; it is dismissed
        // only by Enter (commit+render), Esc, or re-pressing M. Swallow the
        // press so it cannot drive a playhead drag / marker click / or tear
        // the editor down through the top-strip flag-edit routine below.
        return;
    }
    if (app.loading || audio.total_frames() <= 0) return;
    const GuiRect area = waveform_area(app);
    const GuiRect top  = top_strip_area(app);
    const bool inside_waveform =
        x >= area.x && x < area.x + area.w &&
        y >= area.y && y < area.y + area.h;
    const bool inside_top =
        x >= top.x && x < top.x + top.w &&
        y >= top.y && y < top.y + top.h;
    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;
    // (alt is consulted only by wheel chords, which now arrive via on_wheel.)

    // Defensive: a second press during a drag is ignored (left button
    // should still be held down for a drag to exist).
    if (app.drag.active) return;
    if (app.trim_drag.active) return;

    // Chunk W: render-view mouse gate. Left-click on a marker line
    // (in the waveform area) or a flag rect (in the top strip)
    // toggles selection and jumps the playhead to the marker;
    // left-click elsewhere in the waveform area positions the
    // playhead (with playback stop) and clears the selection unless
    // Shift is held. All wheel chords (zoom, Alt/Ctrl+Alt pan,
    // Ctrl+wheel playhead-move) are pure viewport / playhead ops and
    // pass through unchanged. Drag-create and top-strip playhead
    // movement are silent no-ops so the read-only invariant on
    // marker state is preserved. Hover-popup motion still runs in
    // the motion handler against render_view_markers.
    // Brief 3b: target-view mouse authoring is unblocked. Fall through
    // to the source-view handler; the input-to-source-frame boundary
    // translation lives in the per-gesture writers (drag
    // begin/motion, etc.) and in to_source_frame helpers used by
    // those writers.

    if (app.render_view_enabled) {
        // Wheel events arrive via on_wheel (coalesced per pointer frame), not
        // here; a stray wheel button is caught by the Left-only gate below.
        if (button != GuiMouseButton::Left) return;
        // Brief F Section 3: in phase reset sub-view, top-strip clicks
        // are silent no-ops (phase resets have no flag rects). Bail
        // before hit-testing so we don't attempt selection bookkeeping
        // on a non-existent flag pack.
        if (app.active_markers_view == 'P' && inside_top) return;
        // Top-strip clicks stop playback first: they can open the iter/
        // bpm/flag editors and continuing audio during text editing is
        // the wrong default. Waveform clicks keep playback alive — the
        // per-press reseek to the click sample happens at the playhead-
        // drag press sites below, gated on was_playing && sample !=
        // playhead_at_entry.
        const bool was_playing_rv = playback.is_playing();
        const int64_t playhead_at_entry_rv = app.playhead_cursor_sample;
        if (inside_top) playback_lifecycle.stop_playback_if_playing();
        int hit = -1;
        if (inside_waveform)  hit = hit_test_marker_line(app, audio, x);
        else if (inside_top)  hit = hit_test_flag(app, audio, x, y);
        else                  return;
        // Brief J.2 Section 3: live selection lives in the global
        // pair regardless of view domain. active_markers_view tells us
        // which marker list the indices map to.
        const bool sub_t = (app.active_markers_view == 'P');
        std::set<int>& sel = app.selected_markers;
        int& last_sel      = app.last_selected_marker;
        const int n = sub_t
            ? static_cast<int>(app.render_view_phase_resets.size())
            : static_cast<int>(app.render_view_markers.size());
        if (hit >= 0 && hit < n) {
            if (shift) {
                auto it = sel.find(hit);
                if (it == sel.end()) {
                    sel.insert(hit);
                    last_sel = hit;
                } else {
                    sel.erase(it);
                    if (last_sel == hit) {
                        last_sel = sel.empty()
                            ? -1
                            : *sel.rbegin();
                    }
                }
            } else {
                sel.clear();
                sel.insert(hit);
                last_sel = hit;
            }
            gui.invalidate_region(0, 0, app.width, app.height);
            const int sr = audio.sample_rate();
            int64_t sample;
            if (sub_t) {
                sample = static_cast<int64_t>(std::nearbyint(
                    app.render_view_phase_resets[hit].time_seconds *
                    static_cast<double>(sr)));
            } else {
                sample = static_cast<int64_t>(std::nearbyint(
                    app.render_view_markers[hit].time_seconds *
                    static_cast<double>(sr)));
            }
            viewport.move_playhead_to(sample);
            // Brief F Section 2: any waveform-area press starts a
            // playhead-drag gesture. Top-strip flag-click does not.
            if (inside_waveform) {
                if (was_playing_rv && sample != playhead_at_entry_rv) {
                    playback_lifecycle.reseek_keeping_alive(sample);
                }
                if (was_playing_rv) app.follow_overridden_for_session = true;
                app.playhead_drag.active = true;
                app.playhead_drag.press_marker_idx = hit;
                app.playhead_drag.last_swept_sample = sample;
            }
            return;
        }
        // Empty-space click in the waveform area: clear the active
        // sub-view's selection (unless Shift) and move the playhead.
        // Brief F Section 2: also start a playhead-drag gesture so
        // the motion handler's snap logic kicks in.
        if (inside_waveform) {
            if (!shift &&
                (!sel.empty() || last_sel != -1)) {
                sel.clear();
                last_sel = -1;
                gui.invalidate_region(0, 0, app.width, app.height);
            }
            const double spp = current_samples_per_pixel(app, audio);
            int rel = x - area.x;
            if (rel < 0) rel = 0;
            if (rel >= area.w) rel = area.w - 1;
            const int64_t sample =
                app.viewport_start_sample +
                static_cast<int64_t>(std::nearbyint(rel * spp));
            viewport.move_playhead_to(sample);
            if (was_playing_rv && sample != playhead_at_entry_rv) {
                playback_lifecycle.reseek_keeping_alive(sample);
            }
            if (was_playing_rv) app.follow_overridden_for_session = true;
            app.playhead_drag.active = true;
            app.playhead_drag.press_marker_idx = -1;
            app.playhead_drag.last_swept_sample = sample;
        }
        return;
    }

    if (button == GuiMouseButton::Left) {
        // Top-strip clicks stop playback first: they can open the iter/
        // bpm/flag editors and continuing audio during text editing is
        // the wrong default. Waveform clicks keep playback alive — the
        // per-press reseek to the click sample happens at the playhead-
        // drag press sites below, gated on was_playing && sample !=
        // playhead_at_entry. Capture the entry state up front so all
        // four downstream branches see the same snapshot.
        const bool was_playing = playback.is_playing();
        const int64_t playhead_at_entry = app.playhead_cursor_sample;
        if (inside_top) playback_lifecycle.stop_playback_if_playing();

        // V.A1 editor: mouse handling.
        //   click inside top strip on the editing target: re-position
        //     cursor at the clicked byte (handled inside enter_*_edit)
        //   click inside top strip on a different flag: switch target
        //   click anywhere else: exit edit (no commit), then fall
        //     through so the click routes through normal handling.
        // Brief B2: the iter/BPM bracket-popup switch routes have been
        // deleted along with the popup surfaces. A click in the top
        // strip while a bracket editor is active falls through to the
        // normal flag hit-test; D / E will re-wire bracket entry once
        // those modes are re-homed.
        if (text_editor::is_active(app.top_flag_editor)) {
            if (inside_top) {
                const int hit_now = hit_test_flag(app, audio, x, y);
                if (hit_now >= 0 && app.active_markers_view != 'P') {
                    flag_editor.enter_top_flag_edit(
                        hit_now, static_cast<double>(x));
                    arm_editor_text_drag_on_open();
                    return;
                }
                // Top strip click that isn't on a flag: exit and fall
                // through to normal handling.
                flag_editor.exit_top_flag_edit_no_commit();
            } else {
                flag_editor.exit_top_flag_edit_no_commit();
                // Fall through so the click can drive a playhead
                // drag, marker click, etc.
            }
        }

        // Brief B2: the iter/BPM popup-click priority block has been
        // deleted along with the popup surfaces. Clicks in iter/BPM
        // mode fall through to the consolidated flag/marker hit-test
        // below until D / E re-wire bracket entry.

        // Consolidated hit-test across waveform (marker line) and top
        // strip (flag rect). A flag click behaves exactly like a click
        // on its marker line.
        int hit = -1;
        bool in_click_region = false;
        if (inside_waveform) {
            hit = hit_test_marker_line(app, audio, x);
            // Brief C: a waveform press that misses every marker but lands
            // on a trim boundary stem routes to the trim gesture path.
            // Markers take priority on a shared column.
            if (hit < 0) {
                const TrimHit th = hit_test_trim_boundary(app, audio, x);
                if (th != TrimHit::None) {
                    handle_trim_boundary_press(th, ctrl, shift, x);
                    if (app.trim_drag.active && was_playing)
                        app.follow_overridden_for_session = true;
                    return;
                }
            }
            in_click_region = true;
        } else if (inside_top) {
            // F.trim.1: the trim stem is grabbable along its whole visible
            // extent in the top strip, mirroring the in-waveform stem.
            // Markers take priority on a shared column, so try the flag
            // hit-test first; only on a miss does the trim path fill in.
            hit = hit_test_flag(app, audio, x, y);
            if (hit < 0) {
                // F.trim.4: the b/e chip glyph is painted hl_pad RIGHT of the
                // bound's column, so a column-only test misses clicks on the
                // visible chip. In the upper row, test the painted chip RECT
                // first (mirroring regular-flag hit geometry); fall through to
                // the column test for the stem in the lower row, the inter-row
                // gap, and the rest of the strip, where the stem sits at the
                // true column. Both route to handle_trim_boundary_press.
                const TrimHit chip = hit_test_trim_chip(app, audio, x, y);
                const TrimHit th = (chip != TrimHit::None)
                                       ? chip
                                       : hit_test_trim_boundary(app, audio, x);
                if (th != TrimHit::None) {
                    handle_trim_boundary_press(th, ctrl, shift, x);
                    if (app.trim_drag.active && was_playing)
                        app.follow_overridden_for_session = true;
                    return;
                }
            }
            in_click_region = true;
        }

        if (!in_click_region) return;

        if (ctrl) {
            // Ctrl branch: marker-reposition drag or no-op on empty.
            // Read-only refuses the drag-begin so app.drag.active never
            // enters flight state; motion / release / Escape paths all
            // short-circuit on !app.drag.active.
            if (active_view_state(app).read_only) {
                return;
            }
            if (hit >= 0) {
                // begin_drag preserves the multi-selection if `hit` is in
                // it, else collapses to just `hit`. Motion decides whether
                // it actually becomes a drag vs. a plain click.
                const bool was_playing_ctrl = playback.is_playing();
                warpops.begin_drag(hit, x);
                if (was_playing_ctrl)
                    app.follow_overridden_for_session = true;
            }
            // else: Ctrl+press on empty space is a silent no-op.
            return;
        }

        // Non-Ctrl: plain or Shift press. In the waveform area this
        // starts a playhead-drag gesture. In the top strip (flag click) a
        // W-view plain click enters the warp canonical-line editor;
        // Shift+click keeps the legacy multi-select toggle + playhead move.
        // A P-view plain click is navigation (single-select + playhead),
        // falling through to the selection block below; phase resets have
        // no per-flag editor.
        if (inside_top) {
            if (hit >= 0) {
                if (!shift && app.active_markers_view != 'P') {
                    // Plain click on a W-view flag enters the warp
                    // canonical-line editor (which owns the selection +
                    // playhead update on its target-switching path).
                    // Read-only refuses the open (silent no-op). Shift+click
                    // keeps the legacy multi-select toggle below; Ctrl+click
                    // was handled by the reposition-drag branch above.
                    if (active_view_state(app).read_only) {
                        return;
                    }
                    flag_editor.enter_top_flag_edit(
                        hit, static_cast<double>(x));
                    arm_editor_text_drag_on_open();
                    return;
                }
                // P-view plain click and any Shift+click fall here.
                // Single-select is navigation, so it is allowed even in
                // read-only (no marker mutation).
                if (shift) selection.toggle_selection_membership(hit);
                else       selection.set_single_selection(hit);
                const int sr = audio.sample_rate();
                int64_t src_sample;
                if (app.active_markers_view == 'P') {
                    src_sample = static_cast<int64_t>(std::nearbyint(
                        app.phase_reset_markers.markers()[hit].time_seconds *
                        static_cast<double>(sr)));
                } else {
                    src_sample = static_cast<int64_t>(std::nearbyint(
                        app.warpmarkers.markers()[hit].time_seconds *
                        static_cast<double>(sr)));
                }
                int64_t sample = src_sample;
                if (app.active_audio_view == 'T') {
                    const auto tmap = build_target_view_frame_map(
                        app, sr, static_cast<long>(audio.total_frames()));
                    sample = to_domain_frame(app, src_sample, tmap);
                }
                viewport.move_playhead_to(sample);
            }
            return;
        }

        // Waveform-area press: start playhead drag gesture.
        {
            const int sr = audio.sample_rate();
            if (hit >= 0) {
                // Press on a marker (within 3px).
                if (!shift) {
                    selection.set_single_selection(hit);
                } else {
                    // Shift+press on marker: toggles membership in the
                    // selection, last_selected repaired by the helper.
                    // Plain press collapses to single selection.
                    selection.toggle_selection_membership(hit);
                }
                int64_t src_sample;
                if (app.active_markers_view == 'P') {
                    src_sample = static_cast<int64_t>(std::nearbyint(
                        app.phase_reset_markers.markers()[hit].time_seconds *
                        static_cast<double>(sr)));
                } else {
                    src_sample = static_cast<int64_t>(std::nearbyint(
                        app.warpmarkers.markers()[hit].time_seconds *
                        static_cast<double>(sr)));
                }
                int64_t sample = src_sample;
                if (app.active_audio_view == 'T') {
                    const auto tmap = build_target_view_frame_map(
                        app, sr, static_cast<long>(audio.total_frames()));
                    sample = to_domain_frame(app, src_sample, tmap);
                }
                viewport.move_playhead_to(sample);
                if (was_playing && sample != playhead_at_entry) {
                    playback_lifecycle.reseek_keeping_alive(sample);
                }
                if (was_playing) app.follow_overridden_for_session = true;
                app.playhead_drag.active = true;
                app.playhead_drag.press_marker_idx = hit;
                app.playhead_drag.last_swept_sample = sample;
            } else {
                // Press on empty waveform.
                const double spp = current_samples_per_pixel(app, audio);
                const int click_rel_x = x - area.x;
                if (click_rel_x < 0 || click_rel_x >= area.w) {
                    if (!shift) selection.clear_selection();
                    return;
                }
                const int64_t sample = app.viewport_start_sample +
                    static_cast<int64_t>(std::nearbyint(click_rel_x * spp));
                if (!shift) selection.clear_selection();
                viewport.move_playhead_to(sample);
                if (was_playing && sample != playhead_at_entry) {
                    playback_lifecycle.reseek_keeping_alive(sample);
                }
                if (was_playing) app.follow_overridden_for_session = true;
                app.playhead_drag.active = true;
                app.playhead_drag.press_marker_idx = -1;
                app.playhead_drag.last_swept_sample = sample;
            }
        }
    }
    // Wheel events no longer reach on_button_press; they arrive coalesced
    // per pointer frame through on_wheel -> handle_wheel.
}

// X.7.8b-2: button-release handler. Verbatim from the lambda at the
// original main.cpp:1835; commit_drag and set_single_selection are
// rewritten to direct method calls on warpops / selection respectively.
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

void GuiInputHandler::arm_editor_text_drag_on_open() {
    if (!text_editor::is_active(app.top_flag_editor)) return;
    // The caret was set from the click x inside enter_top_flag_edit;
    // a collapsed anchor (anchor == caret) becomes a real selection
    // only once the pointer moves, and on_button_release collapses it
    // back to a plain caret if the press never moved.
    app.top_flag_editor.selection_anchor =
        app.top_flag_editor.cursor_pos;
    app.editor_text_drag.active = true;
}

bool GuiInputHandler::apply_editor_clipboard(
        text_editor::KeyAction action, text_editor::State& s) {
    switch (action) {
        case text_editor::KeyAction::CopyRequested:
            gui.clipboard_set_text(text_editor::selected_text(s));
            return true;
        case text_editor::KeyAction::CutRequested:
            gui.clipboard_set_text(text_editor::selected_text(s));
            text_editor::replace_selection(s, std::string());
            return true;
        case text_editor::KeyAction::PasteRequested:
            text_editor::replace_selection(s, gui.clipboard_get_text());
            return true;
        default:
            return false;
    }
}

void GuiInputHandler::on_button_release(GuiMouseButton button, int /*x*/,
                                        int /*y*/, GuiInputState /*mods*/) {
    if (app.prompt.active) return;
    // F2.1: a left release ending an editor-text drag finalizes the
    // selection (or collapses to a caret) before the modal swallow below.
    if (button == GuiMouseButton::Left && app.editor_text_drag.active) {
        finalize_editor_text_drag();
        return;
    }
    if (text_editor::is_active(app.settings_editor)) return;
    if (button != GuiMouseButton::Left) return;
    if (app.playhead_drag.active) {
        // Selection is committed live during the drag (see on_motion); the
        // release only ends the gesture. A click without a drag keeps the
        // selection the press set, since no motion fired and this is a no-op
        // on selection.
        app.playhead_drag = PlayheadDragState{};
        return;
    }
    if (app.trim_drag.active) {
        commit_trim_drag();
        return;
    }
    if (!app.drag.active) return;
    warpops.commit_drag();
}

// X.7.8b-3: motion handler. Verbatim from the lambda at the original
// main.cpp:1319; the operation-struct method calls (apply_drag_motion,
// commit_drag, move_playhead_to, invalidate_top_strip,
// invalidate_timestamp_area, invalidate_playhead_columns) are rewritten
// to direct method calls on warpops / viewport. popup_eligible_marker
// (now in app_state.{h,cpp}) takes `app` as its first argument; the
// remaining free function calls (hit_test_marker_line, hit_test_flag,
// compute_hover_popup_text, waveform_area, current_samples_per_pixel,
// playhead_pixel_x, text_editor::is_active) keep their original spelling.
void GuiInputHandler::on_motion(int mouse_x, int mouse_y, GuiInputState mods) {
    if constexpr (kDebugPerf) {
        app.last_input_event_time = std::chrono::steady_clock::now();
    }
    // V.A3b Addendum 3: record latest cursor coords so viewport
    // mutators can re-evaluate hover at the cursor's last position.
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
        // !g.valid (flag scrolled off-view mid-drag): no-op this frame,
        // leaving the caret where it was.
        viewport.clear_hover_popup();
        return;
    }
    if (text_editor::is_active(app.settings_editor)) {
        viewport.clear_hover_popup();
        return;
    }
    // Brief C: trim-boundary drag motion. Handled before the render-view
    // and marker-drag branches; only ever active in source view (the only
    // place begin_trim_drag fires). A lost button commits at the current
    // position, mirroring the marker-drag motion handler.
    if (app.trim_drag.active) {
        viewport.clear_hover_popup();
        if (!mods.primary_button_held) {
            commit_trim_drag();
            return;
        }
        update_trim_drag(mouse_x);
        return;
    }
    // Brief 3b: target-view motion authoring is unblocked. Fall through
    // to source-view's drag / playhead-drag / hover handling; per-site
    // translation (drag anchor capture, motion delta conversion, hit
    // tests) lives in the handlers below.
    // Chunk W: render-view motion handler. Brief F Section 2 adds
    // playhead-drag snap support: when a drag is in flight, snap the
    // playhead to the visible sub-view's markers (3px epsilon),
    // matching source-view's gesture. Otherwise run hover popup
    // detection against render_view_markers (suppressed in phase reset
    // sub-view because hit_test_flag short-circuits to -1).
    if (app.render_view_enabled) {
        if (app.playhead_drag.active) {
            viewport.clear_hover_popup();
            if (!mods.primary_button_held) {
                app.playhead_drag = PlayheadDragState{};
                return;
            }
            const int sr = audio.sample_rate();
            if (sr <= 0) return;
            const GuiRect area = waveform_area(app);
            const double spp = current_samples_per_pixel(app, audio);
            if (spp <= 0.0) return;
            const int hit = hit_test_marker_line(app, audio, mouse_x);
            int64_t new_playhead;
            if (hit >= 0) {
                if (app.active_markers_view == 'P') {
                    new_playhead = static_cast<int64_t>(std::nearbyint(
                        app.render_view_phase_resets[hit].time_seconds *
                        static_cast<double>(sr)));
                } else {
                    new_playhead = static_cast<int64_t>(std::nearbyint(
                        app.render_view_markers[hit].time_seconds *
                        static_cast<double>(sr)));
                }
            } else {
                int rel = mouse_x - area.x;
                if (rel < 0) rel = 0;
                if (rel >= area.w) rel = area.w - 1;
                new_playhead = app.viewport_start_sample +
                    static_cast<int64_t>(std::nearbyint(rel * spp));
            }
            if (new_playhead != app.playhead_cursor_sample) {
                viewport.move_playhead_to(new_playhead);
            }
            // Live selection in render view. Same model as source view, written
            // through the global selection pair with a full-window invalidate to
            // match this path's existing selection writes.
            if (!mods.shift) {
                if (hit >= 0) {
                    const bool already_single =
                        app.selected_markers.size() == 1 &&
                        *app.selected_markers.begin() == hit;
                    if (!already_single) {
                        app.selected_markers.clear();
                        app.selected_markers.insert(hit);
                        app.last_selected_marker = hit;
                        gui.invalidate_region(0, 0, app.width, app.height);
                    }
                } else if (!app.selected_markers.empty() ||
                           app.last_selected_marker != -1) {
                    app.selected_markers.clear();
                    app.last_selected_marker = -1;
                    gui.invalidate_region(0, 0, app.width, app.height);
                }
            } else {
                // Endpoint add: unchanged hit-based pickup (3px epsilon).
                if (hit >= 0 &&
                    hit != app.playhead_drag.press_marker_idx &&
                    !app.selected_markers.count(hit)) {
                    app.selected_markers.insert(hit);
                    app.last_selected_marker = hit;
                    gui.invalidate_region(0, 0, app.width, app.height);
                }
                // Interval sweep: add every marker the playhead PASSED
                // since the last motion event (point-sampling skipped
                // markers at fast pointer speeds). Render-view positions
                // are already in the display domain — identity interval,
                // no map translation.
                const int64_t prev = app.playhead_drag.last_swept_sample;
                if (prev >= 0 && new_playhead != prev) {
                    int64_t a = prev, b = new_playhead;
                    const bool forward = (b >= a);
                    if (!forward) std::swap(a, b);
                    const double sr_d = static_cast<double>(sr);
                    const double lo_t = static_cast<double>(a) / sr_d;
                    const double hi_t = static_cast<double>(b) / sr_d;
                    const bool swept = (app.active_markers_view == 'P')
                        ? sweep_select_interval(
                              app, app.render_view_phase_resets,
                              lo_t, hi_t, forward,
                              app.playhead_drag.press_marker_idx)
                        : sweep_select_interval(
                              app, app.render_view_markers,
                              lo_t, hi_t, forward,
                              app.playhead_drag.press_marker_idx);
                    if (swept)
                        gui.invalidate_region(0, 0, app.width, app.height);
                }
            }
            // Keep the sweep anchor fresh on every motion event of the
            // drag, Shift or not (mirrors the source/target branch).
            app.playhead_drag.last_swept_sample = new_playhead;
            return;
        }
        const int hit = hit_test_flag(app, audio, mouse_x, mouse_y);
        if (hit != app.hover_popup.marker_index) {
            // Brief F: hover readout lives on the bottom strip now.
            // No dwell: show the instant the cursor lands on an eligible
            // flag. Recompute cached_text once per transition, derive
            // visible from it, damage the readout area when the old popup
            // was showing or the new one will.
            const bool was_visible = app.hover_popup.visible;
            app.hover_popup.marker_index = hit;
            app.hover_popup.cached_text =
                popup_eligible_marker(app, hit)
                    ? compute_hover_popup_text(
                          app.render_view_markers, hit,
                          app.render_view_src_sr)
                    : std::string();
            app.hover_popup.visible = !app.hover_popup.cached_text.empty();
            if (was_visible || app.hover_popup.visible)
                viewport.invalidate_timestamp_area();
        }
        return;
    }
    if (app.playhead_drag.active) {
        viewport.clear_hover_popup();
        // Left button must still be held; if not, the release was lost —
        // terminate the drag. Modifier changes mid-drag are ignored.
        if (!mods.primary_button_held) {
            app.playhead_drag = PlayheadDragState{};
            return;
        }
        const int sr = audio.sample_rate();
        if (sr <= 0) return;
        const GuiRect area = waveform_area(app);
        const double spp = current_samples_per_pixel(app, audio);
        if (spp <= 0.0) return;

        // Marker snap test — uses the same 3px epsilon as marker hit-test.
        // The snap is purely a playhead-positioning magnet; marker
        // selection is committed live below, from the hit index.
        const int hit = hit_test_marker_line(app, audio, mouse_x);
        int64_t new_playhead;
        if (hit >= 0) {
            int64_t src_sample;
            if (app.active_markers_view == 'P') {
                src_sample = static_cast<int64_t>(std::nearbyint(
                    app.phase_reset_markers.markers()[hit].time_seconds *
                    static_cast<double>(sr)));
            } else {
                src_sample = static_cast<int64_t>(std::nearbyint(
                    app.warpmarkers.markers()[hit].time_seconds *
                    static_cast<double>(sr)));
            }
            // Target view: forward-translate the snapped marker's
            // source-frame to active-domain so the playhead lands on
            // the marker's displayed position.
            if (app.active_audio_view == 'T') {
                const auto tmap = build_target_view_frame_map(
                    app, sr, static_cast<long>(audio.total_frames()));
                new_playhead = to_domain_frame(app, src_sample, tmap);
            } else {
                new_playhead = src_sample;
            }
        } else {
            // No marker within epsilon: playhead follows cursor freely.
            int rel = mouse_x - area.x;
            if (rel < 0) rel = 0;
            if (rel >= area.w) rel = area.w - 1;
            new_playhead = app.viewport_start_sample +
                static_cast<int64_t>(std::nearbyint(rel * spp));
        }

        if (new_playhead != app.playhead_cursor_sample) {
            viewport.move_playhead_to(new_playhead);
        }
        // Live selection: the playhead drag selects the marker under the
        // cursor as it moves. No-Shift tracks a single selection and clears
        // when the cursor leaves every marker; Shift adds markers passed over
        // and never clears. press_marker_idx is skipped under Shift so a
        // Shift-press toggle is not re-added by an incidental motion.
        bool sel_changed = false;
        if (!mods.shift) {
            if (hit >= 0) {
                const bool already_single =
                    app.selected_markers.size() == 1 &&
                    *app.selected_markers.begin() == hit;
                if (!already_single) {
                    selection.set_single_selection(hit);
                    sel_changed = true;
                }
            } else if (!app.selected_markers.empty() ||
                       app.last_selected_marker != -1) {
                selection.clear_selection();
                sel_changed = true;
            }
        } else {
            // Endpoint add: unchanged hit-based pickup (3px epsilon).
            if (hit >= 0 &&
                hit != app.playhead_drag.press_marker_idx &&
                !app.selected_markers.count(hit)) {
                app.selected_markers.insert(hit);
                app.last_selected_marker = hit;
                app.last_sel_group = LastSelGroup::Markers;
                sel_changed = true;
            }
            // Interval sweep: add every marker the playhead PASSED since
            // the last motion event. The per-event hit test only samples
            // the pointer's instantaneous position, so fast drags skipped
            // markers between samples (frame-rate dependent selection).
            // Interval endpoints translate to source domain once (the map
            // is monotone), then the time-ordered marker list is range-
            // scanned in travel direction so last_selected_marker ends on
            // the most recently passed marker.
            const int64_t prev = app.playhead_drag.last_swept_sample;
            if (prev >= 0 && new_playhead != prev) {
                int64_t a = prev, b = new_playhead;
                const bool forward = (b >= a);
                if (!forward) std::swap(a, b);
                int64_t lo = a, hi = b;
                if (app.active_audio_view == 'T') {
                    const auto& tm = target_view_timemap_cached(
                        app, sr,
                        static_cast<long>(audio.total_frames())).frame_map;
                    lo = to_source_frame(app, a, tm);
                    hi = to_source_frame(app, b, tm);
                    if (lo > hi) std::swap(lo, hi);
                }
                const double sr_d = static_cast<double>(sr);
                const double lo_t = static_cast<double>(lo) / sr_d;
                const double hi_t = static_cast<double>(hi) / sr_d;
                const bool swept = (app.active_markers_view == 'P')
                    ? sweep_select_interval(
                          app, app.phase_reset_markers.markers(),
                          lo_t, hi_t, forward,
                          app.playhead_drag.press_marker_idx)
                    : sweep_select_interval(
                          app, app.warpmarkers.markers(),
                          lo_t, hi_t, forward,
                          app.playhead_drag.press_marker_idx);
                if (swept) sel_changed = true;
            }
            if (sel_changed) viewport.invalidate_top_strip();
        }
        // Keep the sweep anchor fresh on every motion event of the drag,
        // Shift or not — so a mid-drag Shift press sweeps only from the
        // current position, never retroactively from the press.
        app.playhead_drag.last_swept_sample = new_playhead;
        if (sel_changed) viewport.invalidate_waveform_area();
        return;
    }
    if (!app.drag.active) {
        // No active gesture: run hover-popup detection. Only in warp
        // mode, with no editor, no dialog (already returned), no drag,
        // and not while iteration mode owns the popup space.
        // Visibility is set immediately on every transition into an
        // eligible rect (no dwell, no tick involvement).
        if (app.active_markers_view == 'W' &&
            !app.iteration_mode_enabled &&
            !text_editor::is_active(app.top_flag_editor) &&
            !app.queue_running) {
            const int hit = hit_test_flag(app, audio, mouse_x, mouse_y);
            if (hit != app.hover_popup.marker_index) {
                // Brief F: hover readout lives on the bottom strip now.
                // No dwell: show immediately on rect-entry; recompute
                // cached_text once, derive visible, damage when the old
                // popup was showing or the new one will.
                const bool was_visible = app.hover_popup.visible;
                app.hover_popup.marker_index = hit;
                app.hover_popup.cached_text =
                    popup_eligible_marker(app, hit)
                        ? compute_hover_popup_text(
                              app.warpmarkers.markers(), hit,
                              audio.sample_rate())
                        : std::string();
                app.hover_popup.visible = !app.hover_popup.cached_text.empty();
                if (was_visible || app.hover_popup.visible)
                    viewport.invalidate_timestamp_area();
            }
        } else {
            viewport.clear_hover_popup();
        }
        return;
    }
    // A drag is active — drop any pending popup.
    viewport.clear_hover_popup();
    // Left button must still be held down — otherwise release was lost.
    if (!mods.primary_button_held) {
        warpops.commit_drag();
        return;
    }
    const int sr = audio.sample_rate();
    if (sr <= 0) return;
    const GuiRect area = waveform_area(app);
    const double spp = current_samples_per_pixel(app, audio);
    const double sr_d = static_cast<double>(sr);
    // Target view: mouse-x → seconds passes through to_source_frame so
    // the delta (mouse_time - anchor_mouse_time_seconds) is a source-
    // seconds value, matching the source-domain anchor begin_drag
    // captured and the source-domain time_seconds the apply path
    // writes into.
    double mouse_time;
    if (app.active_audio_view == 'T') {
        const int64_t mouse_frame_active =
            app.viewport_start_sample +
            static_cast<int64_t>(std::nearbyint(
                static_cast<double>(mouse_x - area.x) * spp));
        const auto tmap = build_target_view_frame_map(
            app, sr, static_cast<long>(audio.total_frames()));
        const int64_t mouse_frame_src =
            to_source_frame(app, mouse_frame_active, tmap);
        mouse_time = static_cast<double>(mouse_frame_src) / sr_d;
    } else {
        const double vp_time =
            static_cast<double>(app.viewport_start_sample) / sr_d;
        mouse_time = vp_time +
            static_cast<double>(mouse_x - area.x) * spp / sr_d;
    }
    warpops.apply_drag_motion(mouse_time - app.drag.anchor_mouse_time_seconds);

    // Track the playhead with the grabbed marker. The hit marker's
    // proposed source-time lives in the drag overlay — under the
    // frozen-coord regime apply_drag_motion does not mutate the live
    // store during motion. In target view, forward-translate through
    // the frozen frame_map (the same map paint walks during motion) so
    // the playhead lands at the same screen column as the marker stem.
    // Viewport is deliberately not followed — the user can pan manually
    // if the drag runs past the edge.
    const int hit_idx = app.drag.hit_marker;
    int hit_pos = -1;
    for (size_t k = 0; k < app.drag.dragging_markers.size(); ++k) {
        if (app.drag.dragging_markers[k] == hit_idx) {
            hit_pos = static_cast<int>(k);
            break;
        }
    }
    if (hit_pos >= 0 &&
        static_cast<size_t>(hit_pos) < app.drag.moveable_times.size()) {
        const int64_t ph_src = static_cast<int64_t>(std::nearbyint(
            app.drag.moveable_times[hit_pos] * sr_d));
        const int64_t ph = (app.active_audio_view == 'T')
            ? to_domain_frame(app, ph_src, app.drag.frozen_frame_map)
            : ph_src;
        if (ph != app.playhead_cursor_sample) {
            const double old_px = playhead_pixel_x(app, audio);
            app.playhead_cursor_sample = ph;
            if (playback.is_playing()) playback.resync_predictor();
            const double new_px = playhead_pixel_x(app, audio);
            viewport.invalidate_playhead_columns(old_px, new_px);
            viewport.invalidate_timestamp_area();
        }
    }
}

// b / e key handlers. Both share the same shape: the playhead's current
// sample frame is the candidate. Re-press at the same frame as the
// existing trim toggles it off. A candidate equal-frame to the opposite
// trim refuses (would collapse the trim region). A candidate that would
// invert the trim ordering auto-swaps with the opposite trim. Otherwise
// a simple set. Per-tab: reads and writes the active tab's trim fields.
// Each mutation pushes a settings-undo entry so b/e/u are reversible.
//
// Side-parameterized to share the body between Begin and End. The
// load-bearing asymmetry is the auto-swap direction: Begin accepts a
// candidate past the existing trim_end, End accepts a candidate before
// the existing trim_begin. Both swaps then route cand_seconds into the
// opposite-side field and the old opposite-side seconds into this side.
void GuiInputHandler::handle_trim_set_at_playhead(TrimSide side) {
    const int sr = audio.sample_rate();
    if (audio.total_frames() <= 0 || sr <= 0) return;
    const double sr_d = static_cast<double>(sr);
    // Target view: playhead is target-domain; the trim store is
    // source-domain. Inverse-translate at the boundary so the
    // downstream toggle / collision / swap logic compares against the
    // source-frame domain the trim store lives in.
    std::vector<FrameMapSegment> tmap;
    if (app.active_audio_view == 'T') {
        tmap = build_target_view_frame_map(
            app, sr, static_cast<long>(audio.total_frames()));
    }
    const int64_t cand_frame =
        to_source_frame(app, app.playhead_cursor_sample, tmap);
    const double cand_seconds =
        static_cast<double>(cand_frame) / sr_d;
    ViewState& vs = active_view_state(app);

    bool&   this_has      = (side == TrimSide::Begin) ? vs.has_trim_begin     : vs.has_trim_end;
    double& this_seconds  = (side == TrimSide::Begin) ? vs.trim_begin_seconds : vs.trim_end_seconds;
    bool&   this_sel      = (side == TrimSide::Begin) ? vs.trim_begin_selected : vs.trim_end_selected;
    bool&   other_has     = (side == TrimSide::Begin) ? vs.has_trim_end       : vs.has_trim_begin;
    double& other_seconds = (side == TrimSide::Begin) ? vs.trim_end_seconds   : vs.trim_begin_seconds;
    const char letter     = (side == TrimSide::Begin) ? 'b' : 'e';

    // Toggle-off: same frame as the existing this-side trim.
    if (this_has) {
        const int64_t cur_frame = static_cast<int64_t>(
            std::nearbyint(this_seconds * sr_d));
        if (cur_frame == cand_frame) {
            SettingsSnapshot pre = capture_current_settings(app);
            this_has     = false;
            this_seconds = 0.0;
            this_sel     = false;
            undo.push_settings_undo(std::move(pre));
            viewport.invalidate_waveform_area();
            viewport.invalidate_timestamp_area();
            target_render.trigger();
            return;
        }
    }

    // Equal-frame collision with the opposite-side trim refuses (would
    // collapse the trim region). Past-the-other auto-swaps: candidate
    // becomes the opposite-side trim, old opposite-side seconds becomes
    // this-side trim.
    if (other_has) {
        const int64_t other_frame = static_cast<int64_t>(
            std::nearbyint(other_seconds * sr_d));
        if (other_frame == cand_frame) {
            std::fprintf(stderr,
                "warptempo_gui: %c refused: would collapse trim region\n",
                letter);
            return;
        }
        const bool cand_is_past_other = (side == TrimSide::Begin)
            ? (cand_frame > other_frame)
            : (cand_frame < other_frame);
        if (cand_is_past_other) {
            SettingsSnapshot pre = capture_current_settings(app);
            const double old_other = other_seconds;
            other_seconds = cand_seconds;
            this_seconds  = old_other;
            this_has      = true;
            undo.push_settings_undo(std::move(pre));
            viewport.invalidate_waveform_area();
            viewport.invalidate_timestamp_area();
            target_render.trigger();
            stop_playback_if_scanner_out_of_trim();
            return;
        }
    }

    SettingsSnapshot pre = capture_current_settings(app);
    this_has     = true;
    this_seconds = cand_seconds;
    undo.push_settings_undo(std::move(pre));
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    target_render.trigger();
    stop_playback_if_scanner_out_of_trim();
}

// After a trim set has resolved the new region, stop playback if the
// scanner has been left outside it. The cursor sits on the just-set
// bound, so only the moving scanner can fall out; widening or removing a
// bound can never push it out, so this is called only from the region-
// narrowing outcomes below. No-op when the scanner is inactive (stopped)
// or still in bounds, so the common case keeps playing like a set marker.
void GuiInputHandler::stop_playback_if_scanner_out_of_trim() {
    if (!app.playhead_scanner_active) return;
    const int64_t s = app.playhead_scanner_sample;
    if (s < viewport.trim_begin_sample() ||
        s >= viewport.trim_end_sample()) {
        playback_lifecycle.stop_playback_if_playing();
    }
}

void GuiInputHandler::handle_trim_set_begin_at_playhead() {
    handle_trim_set_at_playhead(TrimSide::Begin);
}

void GuiInputHandler::handle_trim_set_end_at_playhead() {
    handle_trim_set_at_playhead(TrimSide::End);
}

void GuiInputHandler::handle_trim_unset(TrimSide side) {
    ViewState& vs = active_view_state(app);
    bool&   this_has     = (side == TrimSide::Begin) ? vs.has_trim_begin     : vs.has_trim_end;
    double& this_seconds = (side == TrimSide::Begin) ? vs.trim_begin_seconds : vs.trim_end_seconds;
    bool&   this_sel     = (side == TrimSide::Begin) ? vs.trim_begin_selected : vs.trim_end_selected;
    if (!this_has) return;
    SettingsSnapshot pre = capture_current_settings(app);
    this_has     = false;
    this_seconds = 0.0;
    this_sel     = false;  // an unset bound can't stay selected
    undo.push_settings_undo(std::move(pre));
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    target_render.trigger();
}

void GuiInputHandler::handle_trim_unset_begin() {
    handle_trim_unset(TrimSide::Begin);
}

void GuiInputHandler::handle_trim_unset_end() {
    handle_trim_unset(TrimSide::End);
}

// --- Brief C: trim boundary mouse gestures ------------------------------

void GuiInputHandler::select_trim_boundary(TrimHit which, bool additive) {
    if (which == TrimHit::None) return;
    ViewState& vs = active_view_state(app);
    bool& this_sel  = (which == TrimHit::Begin) ? vs.trim_begin_selected
                                                : vs.trim_end_selected;
    bool& other_sel = (which == TrimHit::Begin) ? vs.trim_end_selected
                                                : vs.trim_begin_selected;
    if (additive) {
        // Toggle this bound's membership; leave the other bound as-is.
        this_sel = !this_sel;
    } else {
        // Single-select within the trim group: this bound on, other off.
        this_sel  = true;
        other_sel = false;
        // A fresh sole selection in the trim group drops marker selection —
        // orthogonal groups, but a single-select in one clears the other
        // (the symmetric counterpart of set_single_selection clearing trim).
        if (!app.selected_markers.empty() || app.last_selected_marker != -1) {
            app.selected_markers.clear();
            app.last_selected_marker = -1;
            viewport.invalidate_top_strip();
        }
    }
    app.last_sel_group = LastSelGroup::Trim;
    viewport.invalidate_waveform_area();
}

bool GuiInputHandler::trim_mouse_x_to_source_seconds(int mouse_x,
                                                     double& out_seconds) {
    const int sr = audio.sample_rate();
    if (sr <= 0 || audio.total_frames() <= 0) return false;
    const double sr_d = static_cast<double>(sr);
    const GuiRect area = waveform_area(app);
    const double spp = current_samples_per_pixel(app, audio);
    if (spp <= 0.0) return false;

    int rel = mouse_x - area.x;
    if (rel < 0) rel = 0;
    if (rel >= area.w) rel = area.w - 1;
    const int64_t domain_frame = app.viewport_start_sample +
        static_cast<int64_t>(std::nearbyint(rel * spp));

    // Target view: the cursor column is an active-domain frame; the trim
    // store is source-domain. Inverse-translate at the boundary, mirroring
    // handle_trim_set_at_playhead.
    std::vector<FrameMapSegment> tmap;
    if (app.active_audio_view == 'T') {
        tmap = build_target_view_frame_map(
            app, sr, static_cast<long>(audio.total_frames()));
    }
    const int64_t src_frame = to_source_frame(app, domain_frame, tmap);
    out_seconds = static_cast<double>(src_frame) / sr_d;
    return true;
}

void GuiInputHandler::begin_trim_drag(TrimHit which, int mouse_x) {
    if (which == TrimHit::None) return;
    ViewState& vs = active_view_state(app);
    const bool is_begin = (which == TrimHit::Begin);
    if (is_begin ? !vs.has_trim_begin : !vs.has_trim_end) {
        return;
    }
    app.trim_drag.active       = true;
    app.trim_drag.is_begin     = is_begin;
    app.trim_drag.moved        = false;
    // Group drag when both bounds are set AND both selected: dragging either
    // one rigidly translates the pair (region width preserved).
    app.trim_drag.group        = vs.trim_begin_selected && vs.trim_end_selected
                              && vs.has_trim_begin && vs.has_trim_end;
    app.trim_drag.orig_seconds = is_begin ? vs.trim_begin_seconds
                                          : vs.trim_end_seconds;
    app.trim_drag.orig_other_seconds = is_begin ? vs.trim_end_seconds
                                                 : vs.trim_begin_seconds;
    // Grab anchor: the press position in source-domain seconds. Motion moves
    // the bound by the cursor's displacement from here, so it tracks the grab
    // point with no snap (mirrors begin_drag's anchor_mouse_time_seconds).
    // A bad conversion leaves anchor_seconds at 0; harmless since the same
    // unusable state makes update_trim_drag early-return too.
    double anchor = 0.0;
    if (trim_mouse_x_to_source_seconds(mouse_x, anchor))
        app.trim_drag.anchor_seconds = anchor;
    app.trim_drag.pre          = capture_current_settings(app);
    app.last_sel_group         = LastSelGroup::Trim;
}

void GuiInputHandler::update_trim_drag(int mouse_x) {
    if (!app.trim_drag.active) return;
    const int sr = audio.sample_rate();
    if (sr <= 0 || audio.total_frames() <= 0) return;
    const double sr_d = static_cast<double>(sr);
    const double spp = current_samples_per_pixel(app, audio);
    if (spp <= 0.0) return;

    // Anchor-relative motion: the dragged bound moves by the cursor's
    // displacement from the grab point, not to the absolute cursor column.
    // cursor_seconds is converted identically to the begin-drag anchor, so
    // the bound stays the same distance under the cursor for the whole drag.
    double cursor_seconds = 0.0;
    if (!trim_mouse_x_to_source_seconds(mouse_x, cursor_seconds)) return;
    const double delta_seconds = cursor_seconds - app.trim_drag.anchor_seconds;

    const int64_t total = static_cast<int64_t>(audio.total_frames());
    ViewState& vs = active_view_state(app);

    if (app.trim_drag.group) {
        // Rigid pair translation: the dragged bound's desired source frame is
        // its pre-drag frame plus the anchor-relative delta; that delta moves
        // both bounds, preserving region width. The delta is clamped against
        // both file edges so the region stops as a unit rather than one bound
        // collapsing into the other.
        const int64_t orig_begin_f = static_cast<int64_t>(std::nearbyint(
            (app.trim_drag.is_begin ? app.trim_drag.orig_seconds
                                    : app.trim_drag.orig_other_seconds) * sr_d));
        const int64_t orig_end_f = static_cast<int64_t>(std::nearbyint(
            (app.trim_drag.is_begin ? app.trim_drag.orig_other_seconds
                                    : app.trim_drag.orig_seconds) * sr_d));

        int64_t delta = static_cast<int64_t>(std::nearbyint(delta_seconds * sr_d));
        const int64_t min_delta = -orig_begin_f;        // begin >= 0
        const int64_t max_delta = total - orig_end_f;    // end   <= total
        if (delta < min_delta) delta = min_delta;
        if (delta > max_delta) delta = max_delta;

        const double new_begin = static_cast<double>(orig_begin_f + delta) / sr_d;
        const double new_end   = static_cast<double>(orig_end_f   + delta) / sr_d;
        if (vs.trim_begin_seconds != new_begin ||
            vs.trim_end_seconds   != new_end) {
            vs.trim_begin_seconds = new_begin;
            vs.trim_end_seconds   = new_end;
            app.trim_drag.moved = true;
            viewport.invalidate_waveform_area();
            viewport.invalidate_timestamp_area();
        }
        return;
    }

    // Single-bound: pre-drag frame plus the anchor-relative delta.
    const int64_t orig_f = static_cast<int64_t>(
        std::nearbyint(app.trim_drag.orig_seconds * sr_d));
    int64_t src_frame = orig_f +
        static_cast<int64_t>(std::nearbyint(delta_seconds * sr_d));
    if (src_frame < 0) src_frame = 0;
    if (src_frame > total) src_frame = total;

    // Clamp against the other bound. F.trim.2 Defect 2: the dragged bound
    // stops kMarkerHitHalfPx pixels (at the current zoom) short of the other
    // bound, the same eps the warp drag uses (warpmarkers_ops.cpp ~429), so
    // the b/e stems never reach visual coincidence — matching the tightest
    // gap two regular marker stems can hold. This replaces the former
    // 1-frame clamp, which was sub-pixel at any normal zoom. This is the
    // trim-internal eps (begin vs end only), orthogonal to warp/phase eps.
    const int64_t eps_frames = static_cast<int64_t>(
        std::nearbyint(static_cast<double>(kMarkerHitHalfPx) * spp));
    if (app.trim_drag.is_begin) {
        if (vs.has_trim_end) {
            const int64_t end_f = static_cast<int64_t>(
                std::nearbyint(vs.trim_end_seconds * sr_d));
            if (src_frame > end_f - eps_frames) src_frame = end_f - eps_frames;
            if (src_frame < 0) src_frame = 0;
        }
    } else {
        if (vs.has_trim_begin) {
            const int64_t begin_f = static_cast<int64_t>(
                std::nearbyint(vs.trim_begin_seconds * sr_d));
            if (src_frame < begin_f + eps_frames) src_frame = begin_f + eps_frames;
            if (src_frame > total) src_frame = total;
        }
    }

    const double new_seconds = static_cast<double>(src_frame) / sr_d;
    double& field = app.trim_drag.is_begin ? vs.trim_begin_seconds
                                           : vs.trim_end_seconds;
    if (field != new_seconds) {
        field = new_seconds;
        app.trim_drag.moved = true;
        viewport.invalidate_waveform_area();
        viewport.invalidate_timestamp_area();
    }
}

void GuiInputHandler::commit_trim_drag() {
    if (!app.trim_drag.active) return;
    if (app.trim_drag.moved) {
        undo.push_settings_undo(std::move(app.trim_drag.pre));
        viewport.invalidate_waveform_area();
        viewport.invalidate_timestamp_area();
        target_render.trigger();
    } else {
        // Ctrl+press with no motion is a Ctrl+click: toggle the boundary's
        // selection (additive — coexists with marker selection).
        const TrimHit which = app.trim_drag.is_begin ? TrimHit::Begin
                                                      : TrimHit::End;
        select_trim_boundary(which, /*additive=*/true);
    }
    app.trim_drag = TrimDragState{};
}

void GuiInputHandler::delete_selected_trim() {
    ViewState& vs = active_view_state(app);
    if (vs.trim_begin_selected && vs.has_trim_begin) {
        handle_trim_unset(TrimSide::Begin);
    }
    if (vs.trim_end_selected && vs.has_trim_end) {
        handle_trim_unset(TrimSide::End);
    }
    vs.trim_begin_selected = false;
    vs.trim_end_selected   = false;
}

void GuiInputHandler::handle_trim_boundary_press(TrimHit which, bool ctrl,
                                                 bool shift, int mouse_x) {
    if (which == TrimHit::None) return;
    if (ctrl) {
        // Read-only refuses the drag-begin so app.trim_drag.active never
        // enters flight; motion / release / Escape all short-circuit on it.
        if (active_view_state(app).read_only) return;
        begin_trim_drag(which, mouse_x);
        return;
    }
    select_trim_boundary(which, /*additive=*/shift);
    // Mirror the marker flag/stem click: a plain or Shift click on a trim
    // boundary moves the playhead cursor to that boundary, so trim flags
    // navigate exactly like marker flags. The Ctrl branch above is a
    // reposition-drag grab and intentionally does not move the playhead,
    // matching the Ctrl+marker reposition. The hit-test that routed here only
    // fires when the boundary exists, so trim_begin/end_seconds is set.
    const ViewState& vs = active_view_state(app);
    const double sec = (which == TrimHit::Begin) ? vs.trim_begin_seconds
                                                 : vs.trim_end_seconds;
    const int sr = audio.sample_rate();
    const int64_t src_sample =
        static_cast<int64_t>(std::nearbyint(sec * static_cast<double>(sr)));
    int64_t sample = src_sample;
    if (app.active_audio_view == 'T') {
        const auto tmap = build_target_view_frame_map(
            app, sr, static_cast<long>(audio.total_frames()));
        sample = to_domain_frame(app, src_sample, tmap);
    }
    viewport.move_playhead_to(sample);
}

void GuiInputHandler::handle_active_audio_view_toggle() {
    // Audio must be loaded — `t` is a silent no-op in blank state.
    // (The blank/loading guard near the top of on_key already covers
    // this, but the helper is defensive in case future callers reach
    // it from elsewhere.)
    if (audio.total_frames() <= 0) return;

    // Build the current frame_map from the live warp marker store +
    // settings trim. Same resolve-then-build pipeline the render
    // pipeline runs, so the visible deformity in target view matches
    // what the engine would emit. An empty / failed build degenerates
    // to identity (the helpers return src_frame unchanged), which is
    // the right fallback when there are no qualifying markers.
    TimemapBuildInput tmin;
    tmin.markers      = resolve_markers_for_render(slice_to_warp_markers(app.warpmarkers.markers()));
    tmin.scale        = app.engine_settings.scale;
    tmin.sample_rate  = audio.sample_rate();
    tmin.total_frames = static_cast<long>(audio.total_frames());
    // Trim is a render-time cut, not a view-time concept. The toggle
    // translates source-frame viewport / playhead / total_frames across
    // the WHOLE song, so the frame_map must too — see the matching
    // comment in paint_handler.cpp's per-paint recompute. Passing the
    // active tab's trim here would shrink the segment list to the
    // exposition's source-frame range and identity-extrapolate the
    // post-exposition tail from the wrong tgt_frame anchor.
    tmin.has_trim_begin = false;
    tmin.trim_begin_sec = 0.0;
    tmin.has_trim_end   = false;
    tmin.trim_end_sec   = 0.0;
    TimemapBuildResult tmres;
    std::vector<FrameMapSegment> tmap;
    if (build_timemaps(tmin, tmres)) {
        tmap.reserve(tmres.standard.size());
        for (const auto& s : tmres.standard) {
            tmap.push_back(FrameMapSegment{s.src_frame, s.tgt_frame});
        }
    }

    // Playback is disabled in target view (Space is in the gate's
    // blocked set). Stop on every toggle so playback never finds
    // itself chasing a playhead in the other domain. Mirrors the
    // viewport-mutator pattern of "stop_playback_if_playing before
    // mutating playhead state".
    playback_lifecycle.stop_playback_if_playing();

    // Anchor the toggle on the playhead's pre-flip screen-pixel column.
    // Compute ph_px now, translate the playhead through the frame_map,
    // then derive the new viewport_start so the translated playhead
    // occupies the same column. zoom_level is preserved across the
    // flip (the visible time span will differ by the frame_map's net
    // stretch — that is the deformity made visible). clamp_viewport_start
    // below pins viewport_start to file bounds; when the playhead sits
    // near the start / end of the file, the clamp moves the playhead's
    // column toward the edge instead of forcing it inside the file.
    const double cur_spp = current_samples_per_pixel(app, audio);
    const double ph_px =
        (cur_spp > 0.0)
        ? (static_cast<double>(app.playhead_cursor_sample -
                               app.viewport_start_sample) / cur_spp)
        : 0.0;

    int64_t new_playhead = app.playhead_cursor_sample;

    bool going_to_target = false;
    if (app.active_audio_view == 'S') {
        // S → T: forward-translate the playhead. The deformed-domain
        // total is derived from the frame_map cache by live_total_frames,
        // so the post-flip viewport math needs no cached total here.
        const double tph = map_source_to_target(
            static_cast<size_t>(app.playhead_cursor_sample < 0
                                ? 0 : app.playhead_cursor_sample), tmap);
        new_playhead = static_cast<int64_t>(std::nearbyint(tph));

        app.active_audio_view = 'T';
        going_to_target = true;
    } else {
        // T → S: inverse-translate the playhead.
        const double sph = map_target_to_source(
            static_cast<size_t>(app.playhead_cursor_sample < 0
                                ? 0 : app.playhead_cursor_sample), tmap);
        new_playhead = static_cast<int64_t>(std::nearbyint(sph));

        app.active_audio_view              = 'S';
    }

    // Domain is flipped — current_samples_per_pixel below reads the
    // post-flip live_total_frames against the preserved zoom_level.
    app.playhead_cursor_sample = new_playhead;
    const double new_spp = current_samples_per_pixel(app, audio);
    const double new_vp_d =
        static_cast<double>(new_playhead) - ph_px * new_spp;
    app.viewport_start_sample =
        static_cast<int64_t>(std::nearbyint(new_vp_d));

    // Clamp viewport into the new domain's bounds, then full-window
    // invalidate so the bottom-strip S/T indicator, the waveform
    // surface, and the playhead column all repaint in one frame.
    clamp_viewport_start(app, audio);
    viewport.clear_hover_popup();
    // One-shot discrete jump with a domain change: is_target, the viewport, and
    // the frame_map hash all flip, so the displayed plate must change. Render it
    // synchronously and publish the displayed fingerprint now, so the
    // bottom-strip S/T indicator and the playhead column do not repaint a frame
    // ahead of the deformed waveform. The plate is built from source audio plus
    // the live frame_map, independent of the target render buffer, so this is
    // unaffected by the ensure_ready / rebind_to_source below.
    viewport.kick_waveform_sync();
    gui.invalidate_region(0, 0, app.width, app.height);

    if (going_to_target) {
        // S → T: ensure playback is bound to a current target buffer.
        // ensure_ready short-circuits to a clean rebind if no edits
        // have invalidated the cached buffer since the last successful
        // render; otherwise it falls through to trigger()'s cancel-
        // clear-dispatch sequence so a fresh render runs against the
        // current engine input.
        target_render.ensure_ready();
    } else {
        // T → S: cancel any in-flight target render and rebind
        // playback to source.wav. No replacement dispatch — source
        // view's playback reads source.wav across archival renders.
        target_render.rebind_to_source();
    }
}
