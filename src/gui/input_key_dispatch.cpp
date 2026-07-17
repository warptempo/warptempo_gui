// on_key dispatch helpers. Each is a GuiInputHandler method declared in
// input_handler.h; on_key calls them in sequence (if (handle_X(...))
// return;). Grouped here to keep input_handler.cpp focused on the event
// entry points and the pointer / wheel paths.

#include "input_handler.h"

#include "file_loader.h"     // apply_settings_engine_and_prefs (shared with load)
#include "paint_handler.h"
#include "render.h"
#include "render_output_naming.h"
#include "render_pipeline.h"
#include "settings_io.h"
#include "text_editor.h"
#include "time_format.h"
#include "warpmarkers.h"

#include <signal.h>
#include <spawn.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <limits>
#include <map>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

// The child's environment for the external audio-player spawn below. POSIX
// exposes the process environment through this global; passing it as
// posix_spawnp's envp gives the launched player the GUI's own environment.
extern char** environ;

namespace {

// Launch `player` DETACHED with `wavs` as its arguments, searching $PATH so a
// bare binary name (e.g. `audacious`) works and an absolute path works too.
// Fire-and-forget: the GUI neither tracks nor waits on the child (SIGCHLD is
// SIG_IGN from startup, so it auto-reaps). The child, however, is spawned with
// SIGCHLD RESET TO DEFAULT (SETSIGDEF): the parent's SIG_IGN would otherwise
// survive exec and give a player that waitpid()s its own helper/decoder an
// ECHILD, breaking its sequencing. posix_spawnp wants a NULL-terminated
// char* const argv[]; the backing std::strings (player and the wavs vector)
// stay alive across the call, so const_cast'ing their c_str() pointers is safe
// — POSIX does not modify them. Returns true iff the spawn started.
bool spawn_audio_player(const std::string& player,
                        const std::vector<std::string>& wavs) {
    std::vector<char*> argv;
    argv.reserve(wavs.size() + 2);
    argv.push_back(const_cast<char*>(player.c_str()));
    for (const std::string& w : wavs) {
        argv.push_back(const_cast<char*>(w.c_str()));
    }
    argv.push_back(nullptr);

    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    sigset_t def;
    sigemptyset(&def);
    sigaddset(&def, SIGCHLD);
    posix_spawnattr_setsigdefault(&attr, &def);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGDEF);

    pid_t pid = 0;
    const int rc = posix_spawnp(&pid, player.c_str(), nullptr, &attr,
                                argv.data(), environ);
    posix_spawnattr_destroy(&attr);
    return rc == 0;
}

}  // namespace

// Source-view read-only allowlist. True when key+mods is not on the allowlist
// and should be dropped.
// Authoring-mutation chords are blocked here at the gate, not admitted for a
// deeper owner refusal: undo/redo (Ctrl+Z / Ctrl+Shift+Z), the trim gestures
// (x / Shift+X), Delete, and every propagate command all drop at this gate.
// Ctrl+S (save) is likewise NOT on the allowlist: read-only means no save, so
// it drops here like the authoring chords. Gesture-owned state changed in a
// locked tab (the read-only flag, trim, view state, font size, playback speed)
// reaches disk only after unlocking (bare o) or via Ctrl+S from the writable
// tab — never by saving from the locked tab itself.
// ALL propagate commands are read-only-blocked: the copy (Ctrl+P) explicitly,
// the paste pair (Ctrl+Alt+P and Ctrl+Alt+Shift+P) structurally — their
// ctrl+alt modifier combinations match no allowlist predicate. The deeper
// owner refusals — do_undo / do_redo's per-entry target-tab check
// (undo.cpp), and the read-only drag refusals (input_pointer.cpp) — stay as
// backstops for the mouse and cross-tab paths, no longer the primary surface
// for these keyboard chords.
bool GuiInputHandler::read_only_key_blocked(GuiKey key, GuiInputState mods) {
    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;
    const bool alt   = mods.alt;
    const bool is_o =
        (key == GuiKeys::O && !ctrl && !shift && !alt);
    const bool is_play_pause = is_play_pause_key(key);
    const bool is_scrub =
        ((key == GuiKeys::Left || key == GuiKeys::Right) &&
         !ctrl && !shift && !alt);
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
    // Ctrl+Shift+=/- steps the GUI font size. Pure display state, the sibling
    // of the bare =/- zoom aliases this gate already admits, so it is honored
    // in a read-only tab (blocking authoring mutations is not its concern).
    const bool is_font_size_step =
        ((key == GuiKeys::Equal || key == GuiKeys::Minus) &&
         ctrl && shift && !alt);
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
        (!ctrl && !alt && key == GuiKeys::Tab) ||
        (!ctrl && !alt && key == GuiKeys::IsoLeftTab);
    const bool is_ctrl_tab =
        (ctrl && !shift && !alt && key == GuiKeys::Tab);
    const bool is_ctrl_shift_tab =
        (ctrl && shift && !alt && key == GuiKeys::Tab);
    const bool is_esc = (key == GuiKeys::Escape);
    const bool is_ctrl_q =
        (ctrl && !shift && !alt && key == GuiKeys::Q);
    // Ctrl+Z (undo) and Ctrl+Shift+Z (redo) are NOT on the allowlist: they
    // drop at this gate. The old design admitted them because an undo entry
    // may target the OTHER (writable) tab, deferring the real decision to
    // do_undo / do_redo's per-entry target-tab peek. Under the gate-block,
    // undoing from a read-only tab first requires switching to the writable
    // tab (Ctrl+Tab) — accepted for gate legibility, so that authoring
    // mutations stop uniformly at the gate. The target-tab peek in undo.cpp
    // survives as a backstop for entries that outlive a mid-history lock.
    // The trim gestures (x / Shift+X), Delete, and the propagate copy/paste
    // chords are likewise absent (blocked here).
    return !(is_o || is_play_pause || is_scrub ||
             is_home_end || is_page_updown ||
             is_zoom || is_zoom_symbol || is_font_size_step || is_zero ||
             is_follow || is_center || is_sub_t || is_sub_p ||
             is_tab_cycle || is_ctrl_tab || is_ctrl_shift_tab ||
             is_esc || is_ctrl_q);
}

// Modal bottom-strip editor predicate. Modal surfaces are bottom-strip
// surfaces: the two bottom-strip editors — the settings editor and the bpm
// editor (top_flag_editor reused with Kind::BpmBracket, painted in the
// bottom strip) — and the prompts (which own input through their own gates
// in on_key and the pointer handlers). The top-strip flag editor
// (Kind::FlagPayload, the iter grammar included) is deliberately NOT modal:
// it stays red-flash-or-exit-without-commit, and every command punches
// through it unchanged.
bool GuiInputHandler::modal_bottom_strip_editor_active() const {
    return text_editor::is_active(app.settings_editor) ||
           text_editor::is_active(app.commit_editor) ||
           (text_editor::is_active(app.top_flag_editor) &&
            app.top_flag_editor.kind == text_editor::Kind::BpmBracket);
}

// Any text editor consuming printable keys — the two bottom-strip editors
// plus the top-strip flag editor in EITHER kind (the FlagPayload editor takes
// typed letters too). The platform layer's kLeftClickKey probe: while this is
// true that key types a normal letter rather than emulating the left button.
bool GuiInputHandler::any_text_editor_active() const {
    return text_editor::is_active(app.settings_editor) ||
           text_editor::is_active(app.commit_editor) ||
           text_editor::is_active(app.top_flag_editor);
}

// Bottom-strip modal-editor key gate, the sibling of
// read_only_key_blocked's allowlist shape. True when key+mods is not on
// the allowlist and should be dropped. While a bottom-strip editor is open
// the user can reach the editor itself, Esc (exit), Ctrl+S (save; the
// editor stays open), and Ctrl+Q (close routing) —
// nothing else: Space-as-playback, zoom, mode toggles, tab switches,
// undo/redo, and the marker / trim chords all drop here. "The editor
// itself" mirrors text_editor::handle_key's consumption exactly — Escape
// and Enter unconditionally, Ctrl+A/C/X/V (select-all + clipboard;
// handle_key tests only Ctrl, so the Shift/Alt variants mirror through),
// the cursor and editing keys Left / Right / Home / End / BackSpace /
// Delete under any modifiers, and printable insertion (no Ctrl/Alt, ASCII
// codepoint; Space lands in the buffer as a typed character, not as
// playback) — plus the settings editor's own bare-Tab value autocomplete
// (handle_settings_editor_key intercepts it before handle_key; the bpm
// editor has no Tab route, so bare Tab drops while it is open). Admitted
// keys flow into the existing editor routing unchanged, so the only
// NotConsumed keys that can reach the editors' command tails are the three
// allowlisted chords.
bool GuiInputHandler::modal_editor_key_blocked(GuiKey key,
                                               GuiInputState mods) {
    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;
    const bool alt   = mods.alt;
    const bool is_esc    = (key == GuiKeys::Escape);
    const bool is_commit =
        (key == GuiKeys::Return || key == GuiKeys::KpEnter);
    const bool is_editor_ctrl_chord =
        (ctrl && (key == GuiKeys::A || key == GuiKeys::C ||
                  key == GuiKeys::X || key == GuiKeys::V));
    const bool is_editor_motion_or_edit =
        (key == GuiKeys::Left || key == GuiKeys::Right ||
         key == GuiKeys::Home || key == GuiKeys::End ||
         key == GuiKeys::BackSpace || key == GuiKeys::Delete);
    const bool is_printable =
        (!ctrl && !alt &&
         mods.codepoint >= 0x20 && mods.codepoint <= 0x7e);
    const bool is_settings_autocomplete =
        (text_editor::is_active(app.settings_editor) &&
         key == GuiKeys::Tab && !ctrl && !shift && !alt);
    // The render-commit editor's bare-Tab entry-name autocomplete
    // (handle_commit_editor_key intercepts it before handle_key), the sibling
    // of the settings editor's value autocomplete.
    const bool is_commit_autocomplete =
        (text_editor::is_active(app.commit_editor) &&
         key == GuiKeys::Tab && !ctrl && !shift && !alt);
    const bool is_save =
        (ctrl && !shift && !alt && key == GuiKeys::S);
    const bool is_ctrl_q =
        (ctrl && !shift && !alt && key == GuiKeys::Q);
    return !(is_esc || is_commit || is_editor_ctrl_chord ||
             is_editor_motion_or_edit || is_printable ||
             is_settings_autocomplete || is_commit_autocomplete ||
             is_save || is_ctrl_q);
}

// The Esc-cancel semantics as a callable body, used by the Esc key
// handler below. Requesting cancellation has two effects:
//   1. async_renderer.request_cancel() sets the worker's cancel flag,
//      which do_render passes through to the engine.
//   2. app.queue_cancel_requested = true so that on_batch_entry_complete
//      finalizes the batch instead of dispatching the next entry.
// Both are needed: (1) interrupts the current render mid-stream;
// (2) stops the batch state machine from advancing after the
// cancelled render's on_done fires.
// Cancel also disarms both parked slots that the worker-idle pump would
// otherwise resurrect: the parked archival command (app.pending_archival)
// and the parked target preview (GuiTargetRender::pending_, cleared via
// cancel_in_flight_update).
bool GuiInputHandler::cancel_archival_session() {
    if (async_renderer.is_busy()) {
        async_renderer.request_cancel();
        app.queue_cancel_requested = true;
        // Cancel means stop rendering: a parked archival command (a
        // dispatch that killed this render and is waiting out its drain)
        // AND a parked target preview are both disarmed, or the
        // worker-idle pump (finalize_render_run to maybe_dispatch_pending)
        // would resurrect a render the moment the cancel lands. The
        // preview slot is cleared through cancel_in_flight_update, which
        // also covers the updating... progress text and the case where
        // the busy render is the preview's own.
        app.pending_archival = {};
        target_render.cancel_in_flight_update();
        return true;
    }
    if (app.queue_running) {
        // Render-state housekeeping flag survives a frame past the
        // worker's actual completion (worker_state_ transitions
        // Running -> CompletionPending while is_busy() still returns
        // true; once on_completion_event fires it goes Idle). The
        // is_busy() branch above covers that window. This branch is
        // the rare case where queue_running is set but the worker has
        // already cleared — defensive, mirrors the prior behavior.
        // Both parked slots are disarmed here too, same pump-resurrection
        // reason as the is_busy() branch.
        app.queue_cancel_requested = true;
        app.pending_archival = {};
        target_render.cancel_in_flight_update();
        return true;
    }
    return false;
}

// Esc-cancel handlers for in-flight operations. See the declaration in
// input_handler.h for routing order.
bool GuiInputHandler::handle_escape_cancels(GuiKey key) {
    if (key != GuiKeys::Escape) return false;
    return cancel_archival_session();
}

// Esc during a pointer drag stops the gesture. Marker and trim drags
// are stopped before their deferred commit, so their pending change is
// discarded and the live state returns to pre-drag; scroll and playhead
// drags apply their motion continuously, so stopping just ends them
// where they are. One verb across all four: Esc means stop.
void GuiInputHandler::cancel_active_drags() {
    if (app.drag.active) {
        // The live marker store was untouched during motion (proposed
        // positions lived in moveable_times, read by paint through
        // DragOverlay), so no marker revert is needed. The motion handler
        // did track the playhead onto the grabbed marker's proposed
        // position, so restore the pre-drag playhead captured at
        // begin_drag before resetting, then repaint the committed positions.
        // Deliberately unclamped: this restores a previously-resting value,
        // already inside the playhead's [0, total - 1] domain
        // (move_playhead_to holds the ruling).
        app.playhead_cursor_sample = app.drag.pre_drag_playhead_sample;
        if (playback.is_playing()) playback.resync_predictor();
        app.drag = DragState{};
        viewport.invalidate_waveform_area();
        viewport.invalidate_top_strip();
        viewport.invalidate_timestamp_area();
    }
    if (app.trim_drag.active) {
        // Trim motion mutates app.trim live but keeps the pre-drag
        // bounds in orig_begin/orig_end, so restore them before clearing
        // the gesture.
        if (app.trim_drag.moved) {
            app.trim.begin_frame = app.trim_drag.orig_begin_frame;
            app.trim.end_frame   = app.trim_drag.orig_end_frame;
        }
        app.trim_drag = TrimDragState{};
        viewport.invalidate_waveform_area();
        viewport.invalidate_timestamp_area();
    }
    // Scroll and playhead drags have already applied their motion, so
    // stopping is just ending the gesture at its current position.
    if (app.scroll_drag.active)   app.scroll_drag = ScrollDragState{};
    if (app.playhead_drag.active) app.playhead_drag = PlayheadDragState{};
}

// Render-trigger chords. See the declaration for the chord list.
bool GuiInputHandler::handle_render_dispatch_keys(GuiKey key,
                                                  GuiInputState mods) {
    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;
    const bool alt   = mods.alt;
    // Ctrl+Alt+R: single render into the source directory using `title`
    // from settings. Empty batch_folder/batch_basename selects the
    // source-directory naming convention inside do_render. A successful
    // sibling wav publish emits
    // the .fingerprint sidecar, but not batch-only sidecars
    // (.warpmarkers / .phaseresetmarkers / .settings).
    // Title-not-set is a hard error surfaced from do_render.
    if (ctrl && alt && !shift &&
        key == GuiKeys::R) {
        if (app.source_audio_path.empty()) return true;

        // Dispatch validates nothing: the render worker's own resolve->build
        // chain is the tripwire surface (the resolver normalizes ambiguous
        // marker arrangements to tempo 1.00, and trim never refuses — crossed
        // cannot rest, an ambiguous trim falls back to untrimmed inside
        // do_render, maps ignore trim), with its stderr as the backstop.

        // Empty batch_folder/basename selects the source-dir naming
        // convention inside do_render.
        RenderRequest req = build_render_request(
            app.source_audio_path, app.warpmarkers.markers(),
            app.phaseresetmarkers.markers(), app.engine_settings,
            app.trim.has_begin, app.trim.begin_frame,
            app.trim.has_end,   app.trim.end_frame);
        req.authoring = snapshot_current_authoring_state();
        attach_shared_render_resources(req);

        if (async_renderer.is_busy()) {
            // A render dispatch kills the running render. Park this command;
            // the worker-idle pump dispatches it once the cancellation drains.
            AppState::PendingArchivalCommand cmd;
            cmd.single      = true;
            cmd.reqs.push_back(std::move(req));
            kill_running_render_and_park(std::move(cmd));
            return true;
        }

        // The dispatch hands the request to the worker thread; on_done
        // fires on the GUI thread when the render finishes (success,
        // failure, or cancel).
        dispatch_single_archival_render(std::move(req));
        return true;
    }

    // Ctrl+Alt+Shift+R (miscellaneous render): render the current authoring
    // state — the SAME recipe Ctrl+Alt+R captures (live stores + the active
    // tab's trim) — into a numbered cell inside a `_miscellaneous` batch folder
    // under renders/. This moved off its former `e`-based chord because `e` is
    // now the click key (kLeftClickKey), so an e-chord is swallowed at the
    // platform boundary and can never reach dispatch as a command.
    // This is Ctrl+Alt+R with an extra mkdir and a different output
    // location: no queue, no batch runner, one request through the same
    // single-dispatch path. Folder logic (in allocate_miscellaneous_cell):
    // look at the most-recent folder BY INDEX in renders/; if it is a
    // `_miscellaneous` folder, append into it; otherwise (or renders/
    // empty/missing) create `<max+1>_miscellaneous`. The cell is the next
    // `<N>.wav` inside that folder. Because the target is a batch folder,
    // do_render writes the FULL entry sidecar set (.warpmarkers /
    // .phaseresetmarkers / .settings / .fingerprint), so each misc cell is a
    // first-class `l`-auditionable, `Shift+.`-adoptable entry. Repeat presses
    // with unchanged state are DELIBERATE — each is an explicit command that
    // produces one more cell; identical bytes come cheap from do_render's reuse
    // rungs (render_cache, then the on-disk artifact against its .fingerprint).
    //
    // The AUTHORING recipe (markers, settings, trim, snapshot, resources) is
    // frozen here at command time; only the OUTPUT naming (batch_folder /
    // batch_basename) is late-bound, at dispatch-to-worker time, on BOTH
    // routes. Late binding is load-bearing on the busy route: the running
    // render this command kills can still publish into renders/ during its
    // cancellation drain (after any command-time scan but before the cancel
    // flag lands, through do_render's reuse-rung renames), so a cell name
    // scanned at command time could be stolen and then overwritten — two
    // successful publications collapsing to one pathname. Allocating only
    // once the worker is confirmed idle makes the scan exact: idle drains the
    // whole CompletionPending interval, so worker publication is fully done
    // before the scan, and every other renders/ mutation (batch-folder
    // creation, the adopt wipe) runs on this same GUI thread, so none can
    // interleave with it. The idle route allocates here inline for the same
    // one implementation.
    if (ctrl && alt && shift &&
        key == GuiKeys::R) {
        if (app.source_audio_path.empty()) return true;

        // Dispatch validates nothing (same as Ctrl+Alt+R): the render worker's
        // own resolve->build chain is the tripwire surface.

        // Build EXACTLY the Ctrl+Alt+R request; batch_folder/basename stay
        // empty here and are assigned at dispatch-to-worker time.
        RenderRequest req = build_render_request(
            app.source_audio_path, app.warpmarkers.markers(),
            app.phaseresetmarkers.markers(), app.engine_settings,
            app.trim.has_begin, app.trim.begin_frame,
            app.trim.has_end,   app.trim.end_frame);
        req.authoring = snapshot_current_authoring_state();
        attach_shared_render_resources(req);

        // Same single-dispatch path as Ctrl+Alt+R: kill the running render and
        // park (newest-wins) when busy, else dispatch now.
        if (async_renderer.is_busy()) {
            AppState::PendingArchivalCommand cmd;
            cmd.single        = true;
            cmd.miscellaneous = true;   // late-bind the cell at the pump
            cmd.reqs.push_back(std::move(req));
            kill_running_render_and_park(std::move(cmd));
            return true;
        }
        std::string folder, basename;
        if (!allocate_miscellaneous_cell(folder, basename)) {
            // Folder creation failed; the stderr line is already printed.
            return true;
        }
        req.batch_folder   = std::move(folder);
        req.batch_basename = std::move(basename);
        dispatch_single_archival_render(std::move(req));
        return true;
    }

    // Ctrl+Alt+I renders the Cartesian product of the per-marker iter ranges
    // authored in iteration mode. Output lands in
    // `<source_parent>/renders/<N>_iterations/`, one cell per product
    // point with basename `<seq>_<delta_csv>`; each cell renders one `.wav`.
    // The CSV holds the swept markers' deltas
    // in timeline order, formatted `%+0.2f`; markers with no iter range
    // authored are excluded from the CSV and contribute one fixed value (their
    // authored tempo_cents) to the product. Per-cell progress and Esc
    // cancellation are handled by the batch runner (start_render_batch and the
    // ActiveBatch lifecycle). Silent no-op outside iteration mode.
    if (ctrl && alt && !shift &&
        key == GuiKeys::I) {
        if (app.source_audio_path.empty()) return true;
        if (!app.iteration_mode_enabled) return true;

        // Dispatch validates nothing: the render worker's own resolve->build
        // chain is the tripwire surface (marker arrangements normalize to
        // tempo 1.00, trim never refuses), and its per-cell tempo_cents
        // mutations stay on the async stderr backstop.

        // Snapshot markers in timeline order (the GuiWarpMarkers store is
        // sorted by time_frame, with ties legal). For each owning marker
        // build its per-cell delta list in integer cents: a single 0 when
        // no iter range is authored, otherwise the cents enumeration from
        // iter_start_cents to iter_end_cents inclusive. Deltas and tempos
        // share the one integer-cents domain, so the per-cell base + delta
        // below is plain integer addition — no conversion anywhere.
        const std::vector<GuiWarpMarker> base_warp_markers =
            app.warpmarkers.markers();
        std::vector<int>                  eligible_indices;
        std::vector<std::vector<int64_t>> per_marker_delta_cents;
        std::vector<bool>                 is_swept;
        for (int i = 0; i < static_cast<int>(base_warp_markers.size()); ++i) {
            const GuiWarpMarker& m = base_warp_markers[i];
            if (!iter_popup_eligible_marker(m)) continue;
            eligible_indices.push_back(i);
            const bool swept =
                m.iter_start_cents.has_value() && m.iter_end_cents.has_value();
            is_swept.push_back(swept);
            std::vector<int64_t> delta_cents;
            if (swept) {
                const int64_t start_cents = *m.iter_start_cents;
                const int64_t end_cents   = *m.iter_end_cents;
                // The editor commit enforces start <= end and the bracket is
                // session-only (wiped on mode exit), so an inverted bracket
                // here is an internal breach — refuse the dispatch loudly and
                // enqueue nothing, repairing no iter state (the state is
                // evidence; the bracket lifecycle owns wiping). Pre-mutation:
                // nothing above has touched app state or the queue.
                if (start_cents > end_cents) {
                    std::fprintf(stderr,
                        "warptempo_gui: render-iterations refused: marker %d "
                        "iter bracket start exceeds end\n", i);
                    return true;
                }
                for (int64_t c = start_cents; c <= end_cents; ++c) {
                    delta_cents.push_back(c);
                }
            } else {
                delta_cents.push_back(0);
            }
            per_marker_delta_cents.push_back(std::move(delta_cents));
        }

        bool any_swept = false;
        for (bool s : is_swept) {
            if (s) { any_swept = true; break; }
        }
        if (!any_swept) {
            std::fprintf(stderr,
                "warptempo_gui: render-iterations: no iter ranges "
                "authored; nothing to render\n");
            return true;
        }

        // Cap the Cartesian product before it can overflow or exhaust
        // memory: each cell is a full archival render, so a real sweep is
        // tens to hundreds of cells. The per-axis brackets don't bound the
        // product — a handful of markers each with a wide bracket multiply
        // into billions of cells, which narrows to a negative int at the
        // reserve/enumeration site (std::length_error) or exhausts memory
        // materializing RenderRequests. Accumulate with a CHECKED product
        // that refuses the instant the running total exceeds the cap, so no
        // overflow can occur (the cap sits far below any integer boundary).
        // kMaxIterSweepCells is the architect-ruled cap.
        constexpr size_t kMaxIterSweepCells = 1000;
        size_t total_cells = 1;
        bool over_cap = false;
        for (const auto& d : per_marker_delta_cents) {
            total_cells *= d.size();
            if (total_cells > kMaxIterSweepCells) { over_cap = true; break; }
        }
        if (total_cells == 0) return true;
        if (over_cap) {
            // Refuse before any allocation, batch-folder creation, request
            // materialization, or render kill/park. `total_cells` here is an
            // accurate lower bound on the true product (the running product
            // already exceeded the cap before every axis was folded in), so
            // report "more than <cap>" rather than computing the full
            // product. Iteration mode and the brackets survive for
            // correction — the wipe-and-exit tail below does not run.
            prompt.open_error_notice(
                "iteration sweep refused: more than " +
                std::to_string(kMaxIterSweepCells) +
                " cells (cap " + std::to_string(kMaxIterSweepCells) +
                "). narrow the marker brackets and retry.");
            return true;
        }

        std::filesystem::path src(app.source_audio_path);
        std::filesystem::path src_parent = src.parent_path();
        if (src_parent.empty()) src_parent = std::filesystem::path(".");
        const std::filesystem::path queue_root = src_parent / "renders";

        // Resolve the next batch index: max+1 over `<digits>_<anything>`
        // entries (the shared renders/ batch scan).
        std::error_code ec;
        const int next_index =
            max_renders_batch_index(queue_root).max_index + 1;

        const std::string command_tag = "iterations";
        const std::filesystem::path batch_folder =
            queue_root /
            (std::to_string(next_index) + "_" + command_tag);
        // The batch folder is created BEFORE requests are built here, the
        // reverse of the bpm sweep (which creates AFTER building): the
        // iteration sweep's delta enumeration is total, so no cell can be
        // rejected and the folder can never end up empty. The bpm sweep's
        // cells can be bracket-rejected, so it creates after building to
        // avoid leaving an empty folder behind.
        std::filesystem::create_directories(batch_folder, ec);
        if (ec) {
            std::fprintf(stderr,
                "warptempo_gui: render-iterations: could not create "
                "'%s': %s\n",
                batch_folder.string().c_str(), ec.message().c_str());
            return true;
        }

        // The cap check above bounds total_cells at kMaxIterSweepCells
        // (<= 1000), so this narrowing to int is exact — no truncation and
        // no negative wrap can reach the reserve/enumeration below.
        const int total = static_cast<int>(total_cells);
        int pad_width = 1;
        for (int n = total; n >= 10; n /= 10) ++pad_width;
        if (pad_width > 9) pad_width = 9;

        // Snapshot phase resets once — every cell shares the same
        // phase reset configuration, only marker tempo_cents values
        // differ across cells.
        const std::vector<GuiPhaseResetMarker> base_phase_resets =
            app.phaseresetmarkers.markers();

        // Cartesian product enumeration. `indices[k]` holds the
        // current cell coordinate along the k-th eligible marker
        // (timeline order). Rightmost dimension increments fastest:
        // consecutive cells differ in the last marker's delta first.
        const size_t num_dims = per_marker_delta_cents.size();
        std::vector<size_t> indices(num_dims, 0);

        std::vector<RenderRequest> reqs;
        reqs.reserve(total);
        for (int cell = 0; cell < total; ++cell) {
            std::string delta_csv;
            for (size_t k = 0; k < num_dims; ++k) {
                if (!is_swept[k]) continue;
                // Signed two-decimal text straight from cents — no double
                // round-trip (format_signed_delta_cents, warpmarkers.h).
                if (!delta_csv.empty()) delta_csv += ',';
                delta_csv += format_signed_delta_cents(
                    per_marker_delta_cents[k][indices[k]]);
            }

            char num_buf[16];
            std::snprintf(num_buf, sizeof(num_buf),
                          "%0*d", pad_width, cell + 1);
            std::string basename = num_buf;
            basename += '_';
            basename += delta_csv;

            std::vector<GuiWarpMarker> cell_warp_markers = base_warp_markers;
            for (size_t k = 0; k < num_dims; ++k) {
                const int mi = eligible_indices[k];
                // Per-cell tempo is a computed value, not an authored one,
                // so it takes no bracket gate: with deltas up to
                // +-kIterDeltaMaxCents a cell tempo can go non-positive,
                // and build_warp_frame_map's existing refusal on the async
                // render path (stderr) is the backstop. Base and delta live
                // in the one integer-cents domain, so the sum is plain
                // integer addition and the cell sidecar's N.NN spelling
                // re-parses to exactly this value — render-entry promotion
                // (Shift+.) stays closed under the grammar by type.
                cell_warp_markers[mi].tempo_cents =
                    base_warp_markers[mi].tempo_cents +
                    per_marker_delta_cents[k][indices[k]];
                // The engine doesn't consume iter values; clear them
                // so the request is quiet.
                cell_warp_markers[mi].iter_start_cents.reset();
                cell_warp_markers[mi].iter_end_cents.reset();
            }

            RenderRequest req = build_render_request(
                app.source_audio_path, std::move(cell_warp_markers), base_phase_resets,
                app.engine_settings,
                app.trim.has_begin, app.trim.begin_frame,
                app.trim.has_end,   app.trim.end_frame,
                batch_folder.string(), std::move(basename));
            req.authoring = snapshot_current_authoring_state();
            attach_shared_render_resources(req);
            reqs.push_back(std::move(req));

            // Increment rightmost dimension; carry left on overflow.
            // The last cell leaves indices in an overflowed state but
            // the loop exits before that's read.
            for (int k = static_cast<int>(num_dims) - 1; k >= 0; --k) {
                ++indices[k];
                if (indices[k] < per_marker_delta_cents[k].size()) break;
                indices[k] = 0;
            }
        }

        if (async_renderer.is_busy()) {
            // A render dispatch kills the running render. Park the fully
            // built batch for the worker-idle pump.
            AppState::PendingArchivalCommand cmd;
            cmd.reqs        = std::move(reqs);
            cmd.batch_label = "render iterations";
            kill_running_render_and_park(std::move(cmd));
        } else {
            start_render_batch(std::move(reqs), "render iterations");
        }
        // The sweep is committed to run either way (dispatched, or parked
        // behind the killed render's drain): iteration mode turns off after
        // fire, and exiting the mode IS the bracket clear (wipe_iter_state,
        // the chokepoint every other iter-mode exit runs). Safe here: every
        // request above carries its own per-cell marker copies, so nothing
        // dispatched reads the live iter fields.
        flag_editor.wipe_iter_state();
        app.iteration_mode_enabled = false;
        viewport.invalidate_top_strip();
        return true;
    }

    // The BPM sweep render fires from render_bpm_sweep(), triggered by Enter
    // in the bottom-strip BPM editor after a successful commit; there is no
    // key-dispatch handler for it here.

    return false;
}

// Identify a render entry by its path relative to renders/ —
// `<batch_dir>/<basename>.wav` — always folder-qualified. One path per file,
// so the id is unique by filesystem construction; Tab autocomplete then
// discriminates on the short leading batch-folder name instead of deep value
// decimals inside near-identical cell basenames, and the painted
// `commit: ./renders/<id>` line is the entry's real on-disk path. The Shift+.
// commit editor resolves the typed identifier against these strings.
static std::string render_entry_id(const AppState::RenderEntry& e) {
    return e.batch_folder.filename().string() + "/" + e.basename + ".wav";
}

// -- Standalone render-entry adoption (the Shift+. commit editor) --------
//
// Adopt render entry `e`'s frozen sidecar recipe as the new authoring
// baseline, view-agnostic: callable from source OR target authoring view. It
// takes an explicit entry and stays SILENT on a read failure (the caller
// red-flashes).
//
// Reads-then-checks BEFORE any mutation: the entry wav must exist and all
// three sidecars (.settings, .warpmarkers, .phaseresetmarkers) must read and
// validate. On ANY failure — missing wav, or a malformed / unreadable
// sidecar — return false with NO stderr and NO state mutation, so a failure
// leaves authoring untouched. The entry sidecars are trusted (written once at
// dispatch), so a genuine read failure is the only refusal. Returns true
// after the recipe is applied and renders/ wiped.
bool GuiInputHandler::adopt_render_entry(
        const AppState::RenderEntry& e) {
    // Self-guard on the standalone mutator: a successful adopt wipes renders/,
    // which must never race a batch publishing into it. The Shift+. opener
    // already refuses on this same condition, so the keyboard route never
    // reaches here; this backstop protects any other caller.
    if (app.queue_running || app.pending_archival.armed) return false;

    // The bottom-strip modal already froze playback at the editor's open;
    // stop explicitly so this is correct from any caller.
    playback_lifecycle.stop_playback_if_playing();

    // -- Read + validate every input BEFORE touching a store. --
    std::error_code ec;
    if (!std::filesystem::is_regular_file(e.wav_path, ec)) return false;

    const std::filesystem::path sidecar = renders_dir.settings_path(e);
    const auto settings = read_settings_file(sidecar.string());
    if (!settings) return false;

    std::vector<GuiWarpMarker>       src_warp;
    std::vector<GuiPhaseResetMarker> src_phase_resets;
    {
        GuiWarpMarkers m;
        const std::filesystem::path wm =
            e.batch_folder / (e.basename + ".warpmarkers");
        auto r = m.load(wm.string());
        if (!r) return false;
        src_warp = m.markers();
    }
    {
        GuiPhaseResetMarkers t;
        const std::filesystem::path tm =
            e.batch_folder / (e.basename + ".phaseresetmarkers");
        auto r = t.load(tm.string());
        if (!r) return false;
        src_phase_resets = t.markers();
    }

    // Every input is in hand and valid. Apply the recipe wholesale. The commit
    // tab is the tab the entry was dispatched from; its view-state band carries
    // the recipe trim that shaped this render.
    const char commit_tab = settings->active_tab_view;

    std::vector<GuiWarpMarker>       warp_pre  = app.warpmarkers.markers();
    std::vector<GuiPhaseResetMarker> phase_reset_pre =
        app.phaseresetmarkers.markers();
    const int hint_last = app.last_selected_marker;

    app.warpmarkers.markers_mut()       = std::move(src_warp);
    app.phaseresetmarkers.markers_mut() = std::move(src_phase_resets);
    app.selected_markers.clear();
    app.last_selected_marker = -1;
    // Wholesale authoring reset: every per-tab per-mode selection slot
    // referencing the replaced marker stores is stale.
    {
        auto clear_marker_slots = [](ViewState& t) {
            t.warp_selected.clear();
            t.warp_last_selected        = -1;
            t.phase_reset_selected.clear();
            t.phase_reset_last_selected = -1;
        };
        clear_marker_slots(app.tab_a);
        clear_marker_slots(app.tab_b);
    }

    // One cross-file undo entry: the marker pair plus the pre-commit engine
    // settings (captured inside push_undo_both). The inherited prefs and view
    // state ride OUTSIDE undo — the same convention that keeps view state and
    // trim out of history.
    const char commit_marker_mode = app.active_markers_view;
    undo.push_undo_both(std::move(warp_pre), std::move(phase_reset_pre),
                        commit_marker_mode, hint_last, commit_tab);
    undo.recompute_dirty();

    const std::filesystem::path src(app.source_audio_path);
    std::filesystem::path src_parent = src.parent_path();
    if (src_parent.empty()) src_parent = std::filesystem::path(".");
    const std::filesystem::path renders_root = src_parent / "renders";

    // Wholesale authoring reset: clear every marker's session-only iteration
    // state and the bpm state, and turn off both sweep modes' visibility.
    {
        auto& mv = app.warpmarkers.markers_mut();
        for (auto& m : mv) {
            m.iter_start_cents.reset();
            m.iter_end_cents.reset();
        }
    }
    flag_editor.wipe_bpm_state();
    app.iteration_mode_enabled = false;
    app.bpm_mode_enabled       = false;

    // Both tab bands from the file (view_state_from_settings_tab: viewport /
    // zoom / playhead, read_only, and the trim pair; a parsed band carries no
    // selection, matching the marker-selection reset above). This clean
    // whole-band replace is equivalent to a source load's per-key apply plus
    // trim plus read_only for an all-keys render-entry sidecar.
    app.tab_a = view_state_from_settings_tab(settings->tab_a);
    app.tab_b = view_state_from_settings_tab(settings->tab_b);

    // Engine block plus the scalar session prefs, VALUES ONLY, through the one
    // routine a source load also calls — so adopt applies engine_settings,
    // follow, active_audio_view, active_markers_view, active_tab_view,
    // playback_speed, font_size, and audio_player 1:1 with load. There is NO
    // W/P carve-out: active_markers_view is now applied from the file like
    // every other key. The live selection and both tabs' per-mode selection
    // slots were cleared above, so landing on the file's marker mode carries
    // an empty selection, exactly as a fresh load's empty-selection state.
    //
    // This replaces the four LIVE env hashes with the entry's. The hashes are
    // history-less, no-dirty GUI-kind state (like the other adopted view
    // prefs), so this replacement marks nothing dirty on its own; adopt is
    // dirty via its cross-file history push regardless, and the adopted hashes
    // ride the next ordinary Ctrl+S.
    apply_settings_engine_and_prefs(app, *settings);

    // Clamp both adopted tab bands' playheads into the live domain (the
    // shared chokepoint, clamp_playhead_to_live_domain), mirroring the source
    // load's tab-snapshot clamp at the same point in the sequence: the
    // adopted S/T domain is computable here (active_audio_view and the
    // markers/engine settings the target total derives from are all applied
    // above; one global domain, one total clamps both). Entry sidecars are
    // trusted (written once at dispatch from an in-domain live state), so
    // this is a no-op there — it keeps the adopt 1:1 with a source load of
    // the same sidecars, which clamps at this point too.
    app.tab_a.playhead_cursor_sample = clamp_playhead_to_live_domain(
        app.tab_a.playhead_cursor_sample, app, audio);
    app.tab_b.playhead_cursor_sample = clamp_playhead_to_live_domain(
        app.tab_b.playhead_cursor_sample, app, audio);

    // Activate the file's tab band. active_tab_view was just set by the shared
    // routine (== commit_tab) and both bands are already the file's, so pull
    // the live fields straight from the active band with no double-apply (NOT
    // switch_active_tab_view_to).
    {
        const ViewState& band = (app.active_tab_view == 'B')
                                ? app.tab_b : app.tab_a;
        app.viewport_start_sample  = band.viewport_start_sample;
        app.zoom_level             = band.zoom_level;
        // Already clamped into the live domain by the band clamp above, so
        // the live copy is in [0, total - 1] by construction.
        app.playhead_cursor_sample = band.playhead_cursor_sample;
        app.trim                = band.trim;
        app.trim_begin_selected = band.trim_begin_selected;
        app.trim_end_selected   = band.trim_end_selected;
        app.last_selected_trim  = band.last_selected_trim;
        app.last_sel_group      = band.last_sel_group;
    }

    // Caller-side side effects the shared routine deliberately omits, run after
    // the live band is in place — the same order and the same point a source
    // load runs them: push the speed to the engine, the font size to the
    // renderer, then the geometry-and-cache rebuild on_resize performs.
    playback.set_speed(app.playback_speed);
    set_gui_font_size_pt(app.font_size);
    paint_handler.on_resize(app.width, app.height);

    if (!app.playhead_scanner_active) {
        app.playhead_scanner_sample = app.playhead_cursor_sample;
    }
    clamp_viewport_start(app, audio);
    viewport.clear_hover_popup();
    viewport.kick_waveform_sync();
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();

    // The tail's trigger owns the rebind for a 'T' landing: it marks the
    // buffer stale and dispatches the adopted target preview, which rebinds
    // playback on completion.
    target_render.trigger();

    // Wipe renders/ AFTER the successful adopt. The committed render survives
    // through the render cache, not as a folder artifact.
    if (std::filesystem::is_directory(renders_root, ec)) {
        std::filesystem::remove_all(renders_root, ec);
        if (ec) {
            std::fprintf(stderr,
                "warptempo_gui: commit: wipe failed for '%s': %s\n",
                renders_root.string().c_str(), ec.message().c_str());
        }
    }

    std::fprintf(stderr,
        "warptempo_gui: commit: committed render and wiped renders/\n");
    gui.invalidate_region(0, 0, app.width, app.height);
    return true;
}

// Open the Shift+. render-commit prompt. No-op with no source loaded. An empty
// renders/ reports a one-line bottom-strip status and does not open. Stops
// playback only when the modal actually opens (after every guard), so a
// refused open leaves a listening session running.
void GuiInputHandler::open_commit_editor() {
    if (text_editor::is_active(app.commit_editor)) return;
    if (app.source_audio_path.empty()) return;
    // Running-render guard: adopt wipes renders/, which would race a background
    // sweep writing into it. Refuse, don't cancel — a running batch may be
    // irreplaceable queued work; Esc is the explicit cancel.
    if (app.queue_running || app.pending_archival.armed) {
        app.transient_status_message = "render running; esc cancels it";
        viewport.invalidate_timestamp_area();
        return;
    }
    std::vector<AppState::RenderEntry> list =
        renders_dir.enumerate_render_entries();
    if (list.empty()) {
        app.transient_status_message = "no renders to commit";
        viewport.invalidate_timestamp_area();
        return;
    }
    // Stop playback only now that the modal is definitely opening. Each guard
    // above (no source, running/parked render, empty renders/) returns without
    // touching playback, so a refused open never interrupts a listening
    // session. Space is inside the modal blocked set, so once open, playback
    // cannot restart until the editor closes.
    playback_lifecycle.stop_playback_if_playing();
    text_editor::enter(app.commit_editor,
                       /*target=*/0,
                       /*locked_prefix=*/"",
                       /*initial_pending=*/"",
                       text_editor::Kind::RenderCommit);
    viewport.invalidate_timestamp_area();
}

void GuiInputHandler::commit_editor_exit_no_commit() {
    if (!text_editor::is_active(app.commit_editor)) return;
    viewport.invalidate_timestamp_area();
    text_editor::deactivate(app.commit_editor);
}

// Tab handler: extend the pending to the longest common prefix of the entry
// identifiers that start with it. No-op when nothing matches or when the
// common prefix does not advance past what is already typed (mirrors the
// settings editor's no-op-on-ambiguity Tab). A unique matching candidate
// completes fully — its whole string is the common prefix of the singleton.
void GuiInputHandler::commit_editor_autocomplete() {
    if (!text_editor::is_active(app.commit_editor)) return;
    const std::string pending = app.commit_editor.pending;

    std::vector<AppState::RenderEntry> list =
        renders_dir.enumerate_render_entries();

    std::string lcp;
    bool have = false;
    for (const auto& e : list) {
        const std::string c = render_entry_id(e);
        if (c.size() < pending.size() ||
            c.compare(0, pending.size(), pending) != 0) continue;
        if (!have) { lcp = c; have = true; }
        else {
            const size_t n = std::min(lcp.size(), c.size());
            size_t i = 0;
            while (i < n && lcp[i] == c[i]) ++i;
            lcp.resize(i);
        }
    }
    if (!have) return;                          // no candidate has this prefix
    if (lcp.size() <= pending.size()) return;   // common prefix does not advance

    app.commit_editor.pending          = std::move(lcp);
    app.commit_editor.cursor_pos       =
        static_cast<int>(app.commit_editor.pending.size());
    app.commit_editor.selection_anchor = -1;
    app.commit_editor.red              = false;
    viewport.invalidate_timestamp_area();
}

// Enter handler: resolve the pending to exactly one entry and adopt it.
// Resolution accepts exactly the entry's canonical id
// (`<batch_dir>/<basename>.wav`); ids are unique by filesystem construction
// (one path per file), so the first match resolves. On a resolve,
// adopt_render_entry runs; a true result closes the editor, a false result
// (bad sidecar / missing wav) red-flashes and stays open. Zero matches
// red-flash and stay open.
void GuiInputHandler::commit_editor_commit() {
    if (!text_editor::is_active(app.commit_editor)) return;
    const std::string pending = app.commit_editor.pending;

    auto reject = [&]() {
        app.commit_editor.red = true;
        viewport.invalidate_timestamp_area();
    };

    std::vector<AppState::RenderEntry> list =
        renders_dir.enumerate_render_entries();

    const AppState::RenderEntry* found = nullptr;
    for (const auto& e : list) {
        if (render_entry_id(e) == pending) {
            found = &e;
            break;
        }
    }
    if (!found) { reject(); return; }

    // Copy the entry before adopting: adopt wipes renders/ at its tail, and
    // the copy is self-contained (paths + basename), so it stays valid.
    const AppState::RenderEntry entry = *found;
    if (adopt_render_entry(entry)) {
        viewport.invalidate_timestamp_area();
        text_editor::deactivate(app.commit_editor);
    } else {
        reject();
    }
}

// Shared key route for the modal bottom-strip editors — the settings
// prompt, the render-commit prompt, and the bpm bracket editor. All three
// spell one modal contract: the on_key gate (modal_editor_key_blocked)
// admits only the editor's own keys plus Esc, Ctrl+S, and Ctrl+Q, so a
// NotConsumed key here is one of those three chords. Ctrl+S saves with
// the editor left open (save is not an exit); Ctrl+Q runs the caller's
// teardown and returns false so on_key runs the close routing; anything
// else is swallowed as a backstop. `autocomplete` is the optional
// bare-Tab hook — only an unmodified Tab is intercepted (Shift / Ctrl /
// Alt + Tab fall through to handle_key unchanged); the bpm editor passes
// an empty hook, but bare Tab never reaches this route for it at all —
// the on_key modal gate (modal_editor_key_blocked) swallows it first.
// All three surfaces draw in the bottom strip, so the clipboard /
// Consumed repaint is uniformly invalidate_timestamp_area; commit and
// cancel own their own invalidations.
bool GuiInputHandler::route_modal_editor_key(
        text_editor::State& ed, GuiKey key, GuiInputState mods,
        const std::function<void()>& autocomplete,
        const std::function<void()>& commit,
        const std::function<void()>& cancel,
        const std::function<void()>& ctrl_q_teardown) {
    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;
    const bool alt   = mods.alt;
    if (autocomplete && key == GuiKeys::Tab && !ctrl && !shift && !alt) {
        autocomplete();
        return true;
    }
    const auto action = text_editor::handle_key(ed, key, mods);
    if (action == text_editor::KeyAction::CommitRequested) {
        commit();
        return true;
    }
    if (action == text_editor::KeyAction::CancelRequested) {
        cancel();
        return true;
    }
    if (apply_editor_clipboard(action, ed)) {
        // Same repaint as the Consumed branch — text may have changed
        // (cut / paste); copy repaints harmlessly.
        viewport.invalidate_timestamp_area();
        return true;
    }
    if (action == text_editor::KeyAction::Consumed) {
        viewport.invalidate_timestamp_area();
        return true;
    }
    if (ctrl && !shift && !alt && key == GuiKeys::S) {
        save_ops.save();
        return true;
    }
    if (ctrl && !shift && !alt && key == GuiKeys::Q) {
        ctrl_q_teardown();
        return false;  // let on_key run the close routing
    }
    return true;  // modal: swallow
}

// Routes a key to the active render-commit editor through the shared modal
// route; bare Tab autocompletes the entry identifier.
bool GuiInputHandler::handle_commit_editor_key(GuiKey key,
                                               GuiInputState mods) {
    return route_modal_editor_key(
        app.commit_editor, key, mods,
        [this] { commit_editor_autocomplete(); },
        [this] { commit_editor_commit(); },
        [this] { commit_editor_exit_no_commit(); },
        [this] { commit_editor_exit_no_commit(); });
}

// P / I / M letter-key handlers. See the declaration for the chord list.
bool GuiInputHandler::handle_mode_keys(GuiKey key, GuiInputState mods) {
    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;
    const bool alt   = mods.alt;

    // Ctrl+P: copy phase reset placements from a two-warp-marker
    // selection into the session clipboard. W-mode only; phase reset
    // mode is a silent no-op. Off-count selection in W-mode emits a
    // one-line stderr nudge.
    if (key == GuiKeys::P && ctrl && !shift && !alt) {
        if (app.active_markers_view != 'W') return true;
        if (app.selected_markers.size() != 2) {
            std::fprintf(stderr,
                "warptempo_gui: phase_reset copy: select exactly two warp "
                "markers\n");
            return true;
        }
        phase_reset_propagate.copy_from_selection();
        return true;
    }

    // Ctrl+Alt+P: paste clipboard phase resets onto the destination
    // anchored at the single selected warp marker. W-mode only; phase
    // reset mode is a silent no-op. Empty clipboard is a silent no-op.
    // Opens a confirmation prompt before any mutation.
    if (key == GuiKeys::P && ctrl && !shift && alt) {
        if (app.active_markers_view != 'W') return true;
        if (app.phase_reset_clipboard.empty()) return true;
        if (app.selected_markers.size() != 1) {
            std::fprintf(stderr,
                "warptempo_gui: phase_reset paste: select exactly one warp "
                "marker\n");
            return true;
        }
        phase_reset_propagate.open_paste_confirmation();
        return true;
    }

    // Ctrl+Alt+Shift+P: propagate the enabled/disabled *state* of
    // clipboard placements onto the matching destination region's
    // phase resets, in order. Positions are not modified. W-mode only;
    // phase reset mode is a silent no-op. Empty clipboard is a silent
    // no-op. Unlike Ctrl+Alt+P, no confirmation prompt — applies
    // directly. Divergence/mismatch is reported via the bottom-strip
    // transient status message rather than a modal dialog.
    if (key == GuiKeys::P && ctrl && shift && alt) {
        if (app.active_markers_view != 'W') return true;
        if (app.phase_reset_clipboard.empty()) return true;
        if (app.selected_markers.size() != 1) {
            std::fprintf(stderr,
                "warptempo_gui: phase_reset state-paste: select exactly one "
                "warp marker\n");
            return true;
        }
        phase_reset_propagate.paste_state_apply();
        return true;
    }

    // `p` (no modifiers) toggles phase reset view globally.
    if (key == GuiKeys::P && !ctrl && !shift && !alt) {
        active_views.toggle_active_markers_view();
        return true;
    }

    // `i` (no modifiers) toggles iteration mode in warp. Silent
    // no-op in phase reset view (phase reset flags carry no tempo to
    // iterate). The editor-active branch above already swallows any
    // keystroke while a popup edit is in flight, so this code only
    // runs with no active editor. Toggling repaints the top strip
    // so iteration popups appear or vanish in one frame.
    if (key == GuiKeys::I && !ctrl && !shift && !alt) {
        if (app.active_markers_view == 'W') {
            const bool turning_on = !app.iteration_mode_enabled;
            if (!turning_on) {
                // Turning iteration mode OFF wipes every marker's
                // session-only iter bracket — exiting the mode is the
                // clear (wipe_iter_state, shared with enter_bpm_mode's
                // forced iter-off so the two exit routes cannot drift).
                // Runs before the flag flips.
                flag_editor.wipe_iter_state();
            }
            app.iteration_mode_enabled = !app.iteration_mode_enabled;
            viewport.clear_hover_popup();
            viewport.invalidate_top_strip();
        }
        return true;
    }

    // `m` (no modifiers): open the BPM editor on the earlier of two
    // selected markers that define an explicit span. Warp view only; silent
    // no-op in phase reset view. Mutual exclusion with iter mode is handled
    // inside enter_bpm_mode. The gate requires exactly two selected markers
    // with no label_ref anywhere in the span; any other selection is a
    // silent no-op. There is no toggle-off branch: the bpm editor is a
    // modal bottom-strip surface, so while it is open `m` never reaches
    // this dispatch — it is just a typed character the bracket grammar
    // rejects — and bpm mode never rests without its editor (the mode's
    // only exits are the editor's own: Esc, and Enter's dispatch tail).
    if (key == GuiKeys::M && !ctrl && !shift && !alt) {
        if (app.active_markers_view != 'W') return true;
        // Two-marker span gate. Exactly two markers must be selected; the
        // earlier owns, the later closes the span. Neither endpoint nor any
        // span-internal marker may be a label_ref — commit rewrites every
        // in-span tempo and a ref cannot take a manual tempo.
        if (app.selected_markers.size() != 2) return true;
        const auto& mv = app.warpmarkers.markers();
        auto it = app.selected_markers.begin();
        const int owner    = *it++;       // std::set: ascending, so owner is
        const int endpoint = *it;         // the earlier index, endpoint later
        if (owner < 0 || endpoint >= static_cast<int>(mv.size())) return true;
        // No label_ref anywhere in [owner, endpoint] inclusive (endpoint
        // included in the eligibility scan even though its section is not in
        // the rendered region — a ref endpoint still cannot bound the span
        // cleanly). Disabled markers ARE allowed and remain in-span.
        for (int i = owner; i <= endpoint; ++i) {
            if (!mv[i].label_ref.empty()) return true;   // silent no-op
        }
        // Owner must still satisfy the BPM-eligibility predicate (e.g. not
        // itself a label_ref — already covered — and any other standing
        // condition bpm_popup_eligible_marker encodes).
        if (!bpm_popup_eligible_marker(mv[owner])) return true;
        // enter_bpm_mode tags the owner and flips the mode flag. It no
        // longer auto-selects a next-marker cue; the span endpoint is
        // explicit, so record it on the owner and keep both selected
        // markers highlighted as the span cue.
        flag_editor.enter_bpm_mode();
        if (!app.bpm_mode_enabled) return true;   // gate inside bailed
        {
            auto& mvw = app.warpmarkers.markers_mut();
            mvw[owner].bpm_endpoint = endpoint;
        }
        const std::set<int> span_selection = app.selected_markers;
        // The bpm editor is a modal bottom-strip surface: stop playback at
        // its open. Space is inside the modal blocked set, so playback
        // cannot restart until the editor closes.
        playback_lifecycle.stop_playback_if_playing();
        flag_editor.enter_bpm_edit(owner);
        bool restored = false;
        for (int s : span_selection) {
            if (app.selected_markers.insert(s).second) restored = true;
        }
        if (restored) viewport.invalidate_top_strip();
        return true;
    }

    // `l` (no modifiers): "Listen to renders" — launch the external audio
    // player (the audio_player setting, default "audacious") with every
    // rendered wav under <source_parent>/renders/, in the numeric order
    // enumerate_render_entries returns them. Fire-and-forget; the GUI's own
    // playback is unaffected. The modal / editor / read-only gates in on_key
    // run before this handler, so `l` is inert while any of them owns the
    // keyboard (like p/i/m). An explicitly-blank player (the deliberate
    // opt-out) or an empty render set reports a one-line bottom-strip status
    // and does nothing.
    if (key == GuiKeys::L && !ctrl && !shift && !alt) {
        if (app.audio_player.empty()) {
            app.transient_status_message = "no audio_player set";
            viewport.invalidate_timestamp_area();
            return true;
        }
        std::vector<AppState::RenderEntry> list =
            renders_dir.enumerate_render_entries();
        if (list.empty()) {
            app.transient_status_message = "no renders to play";
            viewport.invalidate_timestamp_area();
            return true;
        }
        std::vector<std::string> wavs;
        wavs.reserve(list.size());
        for (const auto& e : list) wavs.push_back(e.wav_path.string());
        if (!spawn_audio_player(app.audio_player, wavs)) {
            std::fprintf(stderr,
                "warptempo_gui: could not launch audio_player '%s'\n",
                app.audio_player.c_str());
        }
        return true;
    }

    return false;
}

// Tab-key family. See the declaration for the chord list.
bool GuiInputHandler::handle_tab_switch_keys(GuiKey key, GuiInputState mods) {
    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;
    const bool alt   = mods.alt;

    // Ctrl+Tab toggles A/B navigational tabs. Stops playback, saves
    // current viewport/zoom/playhead to the leaving tab, restores the
    // target tab. Does not mark the document dirty. Alt-strict: an Alt
    // held alongside makes the chord an unbound no-op, never this binding.
    if (ctrl && !shift && !alt && key == GuiKeys::Tab) {
        active_views.switch_active_tab_view_to(app.active_tab_view == 'A' ? 'B' : 'A');
        target_render.trigger();
        return true;
    }

    // Ctrl+Shift+Tab: advance both tabs' marker focus and end on the
    // opposite tab. Composes bare Tab and Ctrl+Tab so the user can
    // march paired tabs forward in lockstep with one chord.
    if (ctrl && shift && !alt && key == GuiKeys::Tab) {
        cycle_marker_focus_with_recenter(true);
        active_views.switch_active_tab_view_to(app.active_tab_view == 'A' ? 'B' : 'A');
        cycle_marker_focus_with_recenter(true);
        target_render.trigger();
        return true;
    }

    // Bare Tab / Shift+Tab / IsoLeftTab: cycle focus and recenter on the
    // focused marker at the current zoom. The Ctrl+Tab branch above runs first and
    // returns, so Ctrl+Tab is consumed before reaching here; the explicit
    // !ctrl guards below ensure Ctrl+Shift+Tab does not slip into the
    // cycle path either. Alt-strict everywhere: an Alt held makes the chord
    // an unbound no-op rather than falling into the cycle.
    if (!ctrl && !alt && key == GuiKeys::Tab && !shift) {
        cycle_marker_focus_with_recenter(true);  return true;
    }
    if (!ctrl && !alt && key == GuiKeys::Tab && shift)  {
        cycle_marker_focus_with_recenter(false); return true;
    }
    if (!ctrl && !alt && key == GuiKeys::IsoLeftTab)    {
        cycle_marker_focus_with_recenter(false); return true;
    }

    return false;
}

// Bare-key (no-modifier) dispatch. See the declaration for the binding list;
// the caller gates on no modifiers held.
void GuiInputHandler::handle_plain_bare_keys(GuiKey key) {
    switch (key) {
    case GuiKeys::Escape: /* top-level Escape is a no-op */ break;
    case GuiKeys::Left:
        playback_lifecycle.stop_playback_if_playing();
        if (!app.selected_markers.empty() || app.last_selected_marker != -1) {
            selection.clear_selection();
            viewport.invalidate_waveform_area();
        }
        viewport.move_playhead_pixels(-1);
        break;
    case GuiKeys::Right:
        playback_lifecycle.stop_playback_if_playing();
        if (!app.selected_markers.empty() || app.last_selected_marker != -1) {
            selection.clear_selection();
            viewport.invalidate_waveform_area();
        }
        viewport.move_playhead_pixels(+1);
        break;
    case GuiKeys::Up:     viewport.zoom_in();                        break;
    case GuiKeys::Down:   viewport.zoom_out();                       break;
    case GuiKeys::F:
        // Toggle follow mode. The full body (off→on edge resync) lives in
        // GuiPlaybackLifecycle::set_follow_mode, shared with the settings
        // editor's `follow=` commit.
        playback_lifecycle.set_follow_mode(!app.follow_mode);
        break;
    case GuiKeys::C:      viewport.apply_zoom_change(kSnapZoomLevel);
                    viewport.center_viewport_on_playhead();    break;
    case GuiKeys::Home:   playback_lifecycle.stop_playback_if_playing();
                    viewport.move_playhead_to(viewport.trim_begin_sample());
                    if (app.trim.has_begin)
                        selection.select_trim_bound('B');
                    break;
    case GuiKeys::End:    playback_lifecycle.stop_playback_if_playing();
                    viewport.move_playhead_to(viewport.trim_end_sample() - 1);
                    if (app.trim.has_end)
                        selection.select_trim_bound('E');
                    break;
    default: break;
    }
}

// Top-flag editor key routing. See the declaration for the consumed/command
// contract. The two kinds split up front: the bpm bracket editor draws in
// the bottom strip (like the settings editor) and is modal, so it takes
// the shared modal route — with no bare-Tab hook, and bare Tab never
// reaching this route at all (the on_key modal gate swallows it first);
// Ctrl+S saves with the editor (and the bpm session) left open, and Esc
// and Enter are the mode's only exits. The FlagPayload editor keeps its
// own non-modal flow below.
bool GuiInputHandler::handle_top_flag_editor_key(GuiKey key,
                                                 GuiInputState mods) {
    if (app.top_flag_editor.kind == text_editor::Kind::BpmBracket) {
        return route_modal_editor_key(
            app.top_flag_editor, key, mods,
            /*autocomplete=*/nullptr,
            [this] {
                // Enter commits + renders + closes in one action.
                // A successful commit stores the values on the owner and
                // closes the editor; only then does the BPM sweep fire. A
                // parse failure leaves the editor open (red) and renders
                // nothing.
                if (flag_editor.commit_bpm_edit()) {
                    // render_bpm_sweep owns the mode teardown on its success
                    // path: after the batch is built and accepted (dispatched,
                    // or parked behind a killed render's drain — a busy worker
                    // no longer bails) it wipes the session-only bpm state and
                    // exits bpm mode, so an accepted sweep leaves no marker
                    // carrying bpm state and the next M on this marker seeds
                    // []. A guard-bail (return false) is an environmental
                    // backstop — batch-folder creation failure, no valid
                    // cells; the stale-endpoint class is
                    // unreachable because the modal bpm session freezes the
                    // store between mode entry and this dispatch. The commit
                    // already closed the editor, and bpm mode is exactly its
                    // editor session, so a bail exits the mode here —
                    // mode-without-editor stays unreachable.
                    if (!render_bpm_sweep()) {
                        flag_editor.exit_bpm_mode();
                    }
                } else if (!text_editor::is_active(app.top_flag_editor)) {
                    // commit_bpm_edit closed the editor without committing
                    // (the invalid-target backstop): take the mode down with
                    // it. A red-flash refusal leaves the editor open and
                    // deliberately does not land here.
                    flag_editor.exit_bpm_mode();
                }
            },
            [this] {
                flag_editor.exit_top_flag_edit_no_commit();
                flag_editor.exit_bpm_mode();
                viewport.invalidate_timestamp_area();
            },
            [this] {
                // Ctrl+Q tears the editor and the mode down together
                // (mode-without-editor stays unreachable).
                flag_editor.exit_top_flag_edit_no_commit();
                flag_editor.exit_bpm_mode();
            });
    }
    const auto action = text_editor::handle_key(
        app.top_flag_editor, key, mods);
    if (action == text_editor::KeyAction::CommitRequested) {
        // Iteration editing is a widened-grammar FlagPayload
        // commit (commit_top_flag_edit), not a separate bracket editor.
        flag_editor.commit_top_flag_edit();
        return true;
    }
    if (action == text_editor::KeyAction::CancelRequested) {
        flag_editor.exit_top_flag_edit_no_commit();
        return true;
    }
    if (apply_editor_clipboard(action, app.top_flag_editor)) {
        // Same repaint as the Consumed branch — text may have changed
        // (cut / paste); copy repaints harmlessly.
        viewport.invalidate_top_strip();
        return true;
    }
    if (action == text_editor::KeyAction::Consumed) {
        // FlagPayload draws in the top strip.
        viewport.invalidate_top_strip();
        return true;
    }
    // Top-strip flag editor (deliberately non-modal): the key is a command.
    // Cancel the edit (Esc-discard: no commit, no validation), using the
    // same teardown Esc uses, then fall through (no return) so the key
    // reaches the global command dispatch below and runs. This is how every
    // command (Ctrl+Q/S, Ctrl+Z, Ctrl+Tab, Ctrl+P, ...) works
    // mid-edit: exit first, then the command. No command list — the editor
    // owns only its editing keymap and everything else punches through.
    flag_editor.exit_top_flag_edit_no_commit();
    return false;  // not the editor's key — let on_key run the command
}

// Settings-prompt editor key routing, through the shared modal route.
// Bare Tab autocompletes the value side of `key=` with the key's current
// stored value — every settings key, engine and GUI-kind alike
// (recall_gui_setting_value / format_nonengine_value in settings_io) —
// for recall and editing.
bool GuiInputHandler::handle_settings_editor_key(GuiKey key,
                                                 GuiInputState mods) {
    return route_modal_editor_key(
        app.settings_editor, key, mods,
        [this] { settings_editor.autocomplete_value(); },
        [this] { settings_editor.commit(); },
        [this] { settings_editor.exit_no_commit(); },
        [this] { settings_editor.exit_no_commit(); });
}
