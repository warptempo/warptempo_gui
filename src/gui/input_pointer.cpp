#include "input_handler.h"

#include "gui_display_context.h"
#include "warp_frame_map_view.h"
#include "marker_drag.h"
#include "paint_handler.h"
#include "render.h"
#include "text_editor.h"
#include "warpmarkers.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

// GuiInputHandler pointer-gesture handlers (on_button_press,
// on_button_release, on_motion) and the editor-text drag finalizer
// (finalize_editor_text_drag), lifted
// verbatim from input_handler.cpp. The methods are declared on
// GuiInputHandler in input_handler.h, and the platform layer's pointer
// callbacks dispatch to them unchanged, so this is a pure definition move.
//
// The file-local ActiveEditorText / active_editor_text /
// set_editor_caret_from_x helpers live here because the press / motion /
// release editor-text drag-select is the only consumer; press, motion, and
// release all resolve active_editor_text so they agree on the editor's text
// origin and which strip to repaint.
//
// apply_editor_clipboard is intentionally NOT here — it is a keyboard
// clipboard helper used only by on_key, and stays in input_handler.cpp.

// F2.1: mouse drag-to-select for the three text editors. The selection
// highlight is already painted from the editor State's selection_anchor /
// cursor_pos, so the whole gesture is input-side: a press sets the anchor
// and arms the drag, motion moves cursor_pos (extending the highlight),
// release finalizes. The only per-editor geometry the mouse path needs is
// each editor's char-0 text origin; advance is the shared monospace cell.
namespace {

// CLOCK_MONOTONIC milliseconds — the time base for strip-row double-click
// detection (steady_clock is CLOCK_MONOTONIC on this platform). Press-driven,
// so the synthesized e-key button composes with it automatically.
int64_t monotonic_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Active-domain playhead frame at click column `col`. SOURCE view: the exact
// source grid (source_grid_position_at_column via painter q), matching marker
// commits so a drop-at-playhead lands where a drag/nudge would. TARGET view:
// the domain spp form — the source-frame commit routes through the inverse map,
// so there is no source-grid claim there.
int64_t playhead_frame_at_click_column(const AppState& app,
                                       const GuiAudio& audio, int col) {
    const GuiDisplayContext& ctx = active_display_context(app, audio);
    if (ctx.domain == GuiDisplayDomain::Source) {
        const double q = painter_samples_per_pixel(app, audio, waveform_area(app));
        if (q > 0.0)
            return static_cast<int64_t>(std::nearbyint(
                source_grid_position_at_column(app.viewport_start_sample, col, q)));
    }
    const double spp = current_samples_per_pixel(app, audio);
    return app.viewport_start_sample +
        static_cast<int64_t>(std::nearbyint(static_cast<double>(col) * spp));
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
        g.text_left = static_cast<double>(timestamp_pad_x()) +
            std::strlen(kSettingsEditorPrefix) * adv;
        g.bottom_strip = true;
    } else if (text_editor::is_active(app.commit_editor)) {
        g.ed = &app.commit_editor;
        g.text_left = static_cast<double>(timestamp_pad_x()) +
            std::strlen(kCommitEditorPrefix) * adv;
        g.bottom_strip = true;
    } else if (text_editor::is_active(app.top_flag_editor) &&
               app.top_flag_editor.kind == text_editor::Kind::BpmBracket) {
        g.ed = &app.top_flag_editor;
        g.text_left = static_cast<double>(timestamp_pad_x()) +
            std::strlen(kBpmEditorPrefix) * adv;
        g.bottom_strip = true;
    } else if (text_editor::is_active(app.top_flag_editor)) {
        // FlagPayload — marker-text lane. text_left is the lane run's left
        // edge (flag_pending_text_left_x); it is -1 only for an invalid editor
        // target (a valid off-view marker still yields a clamped onscreen
        // origin, so the caret math keeps working while the text stays visible).
        const double tl = flag_pending_text_left_x(
            app, audio, app.top_flag_editor.target);
        if (tl < 0.0) return g;   // invalid target: leave invalid
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

// Button-press handler. Verbatim from the lambda at the original
// main.cpp:1483; the captured operation-struct lambdas (begin_drag,
// drop_marker, drop_phase_reset_at_position, set_single_selection, etc.)
// are rewritten to direct method calls on the appropriate operation
// struct ref. The four hit_test_* lambdas are now free functions taking
// (app, audio, ...) explicit args. The handle_wheel lambda is now a
// private method on this struct.
void GuiInputHandler::apply_strip_drag_at(int y, bool final_event) {
    // Zoom-only, SONG-anchored zoom (Ableton's model). anchor_sample is the song
    // position painted under press_x at the press, FIXED for the whole drag, and
    // with no pan it stays pinned to press_x — so the anchor x passed below is
    // simply press_x. The level tracks the ABSOLUTE dy since press: drag DOWN
    // (y grows) lowers the level → zooms in; drag UP raises it → zooms out.
    // Horizontal motion is ignored. Pan lives on the Alt+drag waveform grab.
    StripDragState& sd = app.strip_drag;
    double new_level = sd.press_level -
        static_cast<double>(y - sd.press_y) / kZoomStripPxPerLevel;
    const double max_l = effective_max_zoom_level(
        waveform_area(app).w, live_total_frames(app, audio),
        audio.sample_rate());
    if (new_level < kMinZoom) new_level = kMinZoom;
    if (new_level > max_l)    new_level = max_l;

    // The viewport entry point (clamp chokepoint) sets the level, computes
    // viewport_start = anchor_sample - press_x·spp(new_level), and clamps, so
    // anchor_sample stays painted at press_x under the new level's spp: the zoom
    // pivots around the anchor's fixed press column. anchor_sample is never
    // touched afterwards — it stays the fixed press-time song position.
    viewport.apply_strip_drag_zoom(new_level, sd.anchor_sample,
                                   static_cast<double>(sd.press_x), final_event);
}

void GuiInputHandler::on_button_press(GuiMouseButton button, int x, int y,
                                      GuiInputState mods) {
    // Command-adjacency bump (see on_key): a pointer button press is a discrete
    // command, so it breaks a same-gesture nudge/tempo-step burst. NOT bumped
    // on release or motion — those are not discrete commands, and a drag is
    // already fenced by the press that began it here.
    ++app.command_seq;
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
                // FlagPayload: the editable text lives in the marker-text lane,
                // centered on the target marker's column. A press within the
                // rendered run's x-extent (in the lane's y-band) repositions the
                // caret and arms the drag; g.text_left is that run's left edge
                // (flag_pending_text_left_x, the one caret-origin owner). A press
                // on the marker's own flag or elsewhere is not the lane text, so
                // it falls through to the no-op / discard handling below.
                const GuiRect lane = top_marker_text_row_area(app);
                const double run_w = static_cast<double>(
                    app.top_flag_editor.pending.size()) * g.advance;
                in_region = y >= lane.y && y < lane.y + lane.h &&
                    static_cast<double>(x) >= g.text_left &&
                    static_cast<double>(x) <= g.text_left + run_w;
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
            // lane text falls through to the no-op (own flag) / discard
            // (elsewhere) handling below.
            if (g.bottom_strip) return;
        }
    }

    if (text_editor::is_active(app.settings_editor)) return;
    if (text_editor::is_active(app.commit_editor)) return;
    if (text_editor::is_active(app.top_flag_editor) &&
        app.top_flag_editor.kind == text_editor::Kind::BpmBracket) {
        // The BPM editor is a bottom-strip modal owner (like the settings
        // editor). Mouse input does not interact with it beyond its own
        // click-to-cursor region; the session ends only through Esc or the
        // Enter dispatch path (`m` is just a typed character now). Swallow
        // the press so it cannot drive a region drag / marker click / or
        // tear the editor down through the top-strip flag-edit routine
        // below.
        return;
    }
    if (app.loading || audio.total_frames() <= 0) return;
    const GuiRect area = waveform_area(app);
    const GuiRect top  = top_strip_area(app);
    // The waveform BAND spans the full window width (top.w), not the effective
    // width (area.w): the <=15 px inert right gutter counts as a waveform click
    // by the user's lights, so a plain press there still reaches the waveform
    // branch and clears the selection (it has no column to seat a playhead, so
    // that is all it does). The gutter is 0 px at the deployment widths
    // (1920/2560/3840 are multiples of 16), so this only matters off-deployment.
    const bool inside_waveform =
        x >= area.x && x < top.w &&
        y >= area.y && y < area.y + area.h;
    const bool inside_top =
        x >= top.x && x < top.x + top.w &&
        y >= top.y && y < top.y + top.h;
    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;
    const bool alt   = mods.alt;

    // Defensive: a second press during a drag is ignored (left button
    // should still be held down for a drag to exist).
    if (app.drag.active) return;
    if (app.trim_drag.active) return;

    // Target-view mouse authoring is unblocked. Fall through
    // to the source-view handler; the input-to-source-frame boundary
    // translation lives in the per-gesture writers (drag
    // begin/motion, etc.) and in the active_domain_to_source_frame
    // helper used by those writers.

    if (button == GuiMouseButton::Left) {
        // Live top zoom-strip row (Ableton-style navigation), claimed ahead of
        // the top-strip playback-stop and the click routing below. It claims
        // ONLY the plain unmodified left press inside its exact half-open row
        // band; a modified press there is a strict no-op (nothing else lives on
        // the row). The claim is immediate — no motion threshold — and a
        // motionless press-release commits nothing. Navigation-class like the
        // wheel pan: allowed in read-only, never touches the playhead or
        // selection, does not stop playback, and does not override follow. It is
        // ZOOM-ONLY (vertical motion drives the level, horizontal motion is
        // ignored — pan lives on the Alt+drag waveform grab). All modal gates
        // (prompt, bottom-strip editors, the loading/empty guard) sit above this
        // point, so a modal surface blocks the claim exactly as it blocks every
        // other pointer target.
        {
            const GuiRect zoom_row = top_zoom_row_area(app);
            const bool in_zoom_row =
                x >= zoom_row.x && x < zoom_row.x + zoom_row.w &&
                y >= zoom_row.y && y < zoom_row.y + zoom_row.h;
            if (in_zoom_row) {
                if (ctrl || shift || alt) return;  // modified: strict no-op
                // Double-click detection, BEFORE arming the drag: a candidate
                // seeded by the previous motionless zoom-row release, within
                // kDoubleClickMs and kDoubleClickSlackPx of the recorded x,
                // consumes this press as a one-shot zoom toggle — no drag armed,
                // no pointer capture, playhead and selection untouched, allowed
                // in read-only (all modal gates sit above this claim). The
                // toggle is byte-identical to the bare `0` key
                // (run_zoom_toggle_command): at the working zoom → full zoom-out
                // (whole song); anywhere else → the working zoom. The
                // candidate's first click briefly captured and hid the cursor at
                // its press and restored it at the motionless release; this
                // second press never captures.
                const StripDoubleClickCandidate& dc = app.strip_double_click;
                if (dc.valid &&
                    monotonic_ms() - dc.time_ms <= kDoubleClickMs &&
                    std::abs(x - dc.press_x) <= kDoubleClickSlackPx) {
                    app.strip_double_click = StripDoubleClickCandidate{};
                    run_zoom_toggle_command();
                    return;
                }
                const double spp = current_samples_per_pixel(app, audio);
                app.strip_drag = StripDragState{};
                app.strip_drag.active    = true;
                app.strip_drag.press_y   = y;
                // The press column: the drag-threshold reference AND the fixed
                // screen column the anchor stays pinned to (the row is zoom-only,
                // so the anchor never leaves press_x).
                app.strip_drag.press_x   = x;
                // The song position painted under press_x at press time,
                // unclamped and FIXED for the whole drag — the anchor the zoom
                // pivots around (pinned to press_x, no pan).
                app.strip_drag.anchor_sample =
                    static_cast<double>(app.viewport_start_sample) +
                    static_cast<double>(x) * spp;
                // Capture the level verbatim so the drag walks the one continuous
                // domain from wherever it rests.
                app.strip_drag.press_level = app.zoom_level;
                // Ableton-style pointer capture: hide and lock the cursor at
                // the press so motion feeds the gesture as unbounded virtual
                // coordinates (infinite pan/zoom travel). Self-guarding no-op
                // on a degraded compositor. Every strip-drag exit path calls
                // the matching end hook exactly once.
                begin_strip_pointer_capture();
                return;
            }
        }

        // Top-strip clicks stop playback first: they select markers and can
        // retarget an open flag editor, and continuing audio during authoring /
        // text editing is the wrong default. Waveform clicks keep playback
        // alive — the per-press reseek to the click sample happens at the
        // playhead-drag press site below, gated on was_playing && sample !=
        // playhead_at_entry. Capture the entry state up front so the downstream
        // branches see the same snapshot.
        const bool was_playing = playback.is_playing();
        const int64_t playhead_at_entry = app.playhead_cursor_sample;
        if (inside_top) playback_lifecycle.stop_playback_if_playing();

        // Editor: mouse handling. The editable text lives in the marker-text
        // lane now, not on the flag, so a press within the lane's rendered run
        // already repositioned the caret and armed the drag above (the F2.1
        // block) and returned. What reaches here is a press that is NOT the lane
        // text:
        //   press on the SAME marker's own flag: NO-OP — the text is not there
        //     anymore, so it neither repositions the caret nor discards; swallow
        //     it, leaving the editor open as-is.
        //   press anywhere else (a DIFFERENT flag, a non-flag top-strip spot, or
        //     off the strip): DISCARD the open editor without committing — Esc's
        //     teardown exactly (pending dropped; Enter is the only commit route)
        //     — then fall through so the click routes through normal handling. A
        //     click on a different flag thereby single-selects that flag below,
        //     rather than retargeting the editor (the old retarget was a bug).
        // The no-op path returns; every discard path falls through to the normal
        // flag hit-test / waveform handling below.
        if (text_editor::is_active(app.top_flag_editor)) {
            const int hit_now = inside_top ? hit_test_flag(app, audio, x, y) : -1;
            if (inside_top && hit_now == app.top_flag_editor.target &&
                app.active_markers_view != 'P') {
                // The edited marker's own flag: no-op (swallow, editor untouched).
                return;
            }
            // Different flag / non-flag / off-strip: discard (Esc teardown) and
            // fall through so the click drives normal handling (a different
            // flag single-selects; a waveform click deselects + places playhead).
            flag_editor.exit_top_flag_edit_no_commit();
        }

        // Clicks in iter/BPM mode route through the consolidated
        // flag/marker hit-test below.

        // Only presses inside the waveform or the top strip do anything.
        if (!inside_waveform && !inside_top) return;

        // Flag hit test, computed ONLY on the path that consumes it. The
        // TOP-STRIP flag hit feeds the plain/Shift flag-click branches (plain =
        // single-select + arm the pending marker drag, Shift = toggle
        // membership), so it is resolved once here. The WAVEFORM never SELECTS a
        // marker — a plain press is deselect-all + playhead placement +
        // region-drag arm, a Shift press a no-op — so no marker scan runs on the
        // waveform at all (the invisible stem is not a grab target). Trim bounds
        // are grabbed only by their top-strip chips / the inter-chip bridge on a
        // PLAIN chip-row press (route_trim_chip_press below); a click over a
        // bound's waveform stem is an ordinary waveform click (the stem grab
        // retired), so no trim hit test runs on the waveform at all.
        int hit = -1;
        if (inside_top) hit = hit_test_flag(app, audio, x, y);

        // Alt-exact left press on the waveform arms the incremental grab-pan —
        // the ONE Alt pointer gesture. Continuous 1:1 pan of the viewport by the
        // per-event pixel delta (see on_motion). Navigation-class: allowed in
        // read-only, never touches the playhead or selection. It deliberately
        // does NOT override follow — a pan during playback moves the view along
        // with the audio rather than signaling a stop, unlike the marker / trim /
        // playhead gestures. No pointer capture (absolute motion, the viewport
        // walls clamp naturally). A motionless Alt press-release commits nothing
        // (the scroll happens on motion). Alt-exact anywhere else (the top strip
        // included) is a strict no-op below.
        if (alt && !ctrl && !shift) {
            if (inside_waveform) {
                app.scroll_drag = ScrollDragState{};
                app.scroll_drag.active = true;
                app.scroll_drag.last_x = x;
            }
            return;
        }

        // Strict modifier matching: the marker reposition arm lives on the plain
        // flag press and trim's chip/bridge drags on the plain chip-row press, so
        // every remaining modified combination — Ctrl+Alt, Ctrl, Ctrl+Shift,
        // Shift+Alt, ... — no-ops here. Only a plain or Shift-modified base press
        // proceeds (Shift adjusts the selection). The Alt+wheel pan and the Alt
        // keyboard chords are untouched (separate handlers).
        if (ctrl || alt) return;

        // Plain or Shift press. In the waveform area a plain press clears the
        // marker selection (deselect-all), places the playhead at the clicked
        // column, and arms the region-select drag — it never SELECTS a marker; a
        // Shift press on the waveform is a strict no-op. In the top strip a plain
        // CHIP-ROW press arms a trim chip/bridge drag (claimed ahead of the
        // marker flag select); otherwise (flag click) selection is the
        // whole interface, BOTH views: a plain click single-selects and ARMS a
        // pending marker drag (moves the marker if the pointer crosses the
        // threshold, else a pure click); Shift+click toggles multi-select
        // membership only. Neither moves the playhead — only the Tab family and
        // `c` land the playhead on a marker.
        if (inside_top) {
            // Plain unmodified chip-row press arms a trim chip/bridge drag,
            // claimed BEFORE the marker flag single-select. The chip row and the
            // marker flag row are disjoint y-bands, so this contends with
            // nothing: a flag-row press yields no chip/bridge claim and falls to
            // the flag handling. Shift never touches trim (a chip/bridge stays
            // transparent to it) — it falls to the toggle. A claim (armed or
            // read-only) returns without touching the selection.
            if (!shift && route_trim_chip_press(x, y)) return;
            if (hit >= 0) {
                if (shift) {
                    // Shift+click toggles membership; it never arms a drag.
                    // Allowed in read-only (selection is navigation).
                    selection.toggle_selection_membership(hit);
                } else {
                    // Plain flag click single-selects (both views; W's
                    // click-to-edit is retired — the editor now opens on Enter).
                    // Selection is navigation, allowed in read-only. A writable
                    // tab additionally arms the pending marker drag; read-only
                    // selects but never arms (marker mutation refused), so the
                    // press stays a pure click there.
                    selection.set_single_selection(hit);
                    if (!active_view_state(app).read_only) {
                        app.pending_marker_drag = PendingMarkerDrag{};
                        app.pending_marker_drag.active  = true;
                        app.pending_marker_drag.marker  = hit;
                        app.pending_marker_drag.press_x = x;
                        app.pending_marker_drag.press_y = y;
                    }
                }
            }
            return;
        }

        // Waveform-area press: marker-blind for SELECTION (it never SELECTS a
        // hit marker — the invisible stem is not a grab target), but a plain
        // press CLEARS the selection (the deselect-all: a waveform click
        // dismisses the marker selection, the Ableton behaviour). The press does
        // NOT consult `hit`. Then it drops the playhead at the clicked column (no
        // marker snap — the 3px marker-snap magnet already died with the scrub in
        // the prior phase), reseeks a live scanner to it, overrides follow, and
        // arms the region drag — which also DISSOLVES any resting highlight at
        // this mouse-down (arm_region_drag_at clears it after snapshotting the
        // pre-press extent for an Esc-mid-drag restore), so the wash vanishes on
        // press whether the gesture becomes a click or a fresh drag. A Shift
        // press is a strict no-op anywhere on the waveform, selection included.
        {
            if (shift) return;
            // The clear runs FIRST, before the gutter early-return below, so an
            // inert-gutter click (no column to seat a playhead) still deselects.
            selection.clear_selection();
            const int click_rel_x = x - area.x;
            if (click_rel_x < 0 || click_rel_x >= area.w) return;
            const int64_t sample =
                playhead_frame_at_click_column(app, audio, click_rel_x);
            viewport.move_playhead_to(sample);
            if (was_playing && sample != playhead_at_entry) {
                playback_lifecycle.reseek_keeping_alive(sample);
            }
            if (was_playing) app.follow_overridden_for_session = true;
            arm_region_drag_at(sample, x, y);
        }
    }
    // Wheel events no longer reach on_button_press; they arrive coalesced
    // per pointer frame through on_wheel -> handle_wheel.
}

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

void GuiInputHandler::arm_region_drag_at(int64_t anchor_frame, int x, int y) {
    app.region_drag = RegionDragState{};
    app.region_drag.active       = true;
    app.region_drag.anchor_frame = anchor_frame;
    app.region_drag.press_x      = x;
    app.region_drag.press_y      = y;
    // Snapshot the resting region BEFORE clearing it, so an Esc cancel of a
    // live drag still restores the pre-press highlight.
    app.region_drag.pre_region   = app.region;
    // Clear any resting region immediately at press: a plain waveform press
    // dissolves an existing highlight on mouse-down (the wash repaints away
    // now, not at release). A moved drag rebuilds a fresh region live; a
    // motionless press-release simply leaves it cleared. pre_region above keeps
    // the pre-press extent for the Esc-mid-drag restore.
    if (app.region.active) {
        app.region = RegionState{};
        viewport.invalidate_waveform_area();
    }
}

void GuiInputHandler::on_button_release(GuiMouseButton button, int x,
                                        int y, GuiInputState /*mods*/) {
    if (app.prompt.active) return;
    // F2.1: a left release ending an editor-text drag finalizes the
    // selection (or collapses to a caret) before the modal swallow below.
    if (button == GuiMouseButton::Left && app.editor_text_drag.active) {
        finalize_editor_text_drag();
        return;
    }
    if (text_editor::is_active(app.settings_editor)) return;
    if (text_editor::is_active(app.commit_editor)) return;
    if (button != GuiMouseButton::Left) return;
    if (app.strip_drag.active) {
        // Terminating event: if the drag moved, run the final apply with
        // final=true and the one synchronous rebuild (resync + kick_waveform_sync,
        // inside apply_strip_drag_zoom's final path) so the rest state is exact. A
        // motionless press-release finalizes nothing.
        // Double-click seeding: a MOTIONLESS zoom-row release records a
        // candidate (this release x equals the press x); a release that MOVED
        // records nothing and clears any candidate, so a drag can never seed the
        // second click of a double-click.
        if (app.strip_drag.moved) {
            apply_strip_drag_at(y, /*final_event=*/true);
            app.strip_double_click = StripDoubleClickCandidate{};
        } else {
            app.strip_double_click = StripDoubleClickCandidate{
                .valid = true, .time_ms = monotonic_ms(), .press_x = x};
        }
        app.strip_drag = StripDragState{};
        end_strip_pointer_capture();  // reappear the cursor at the press point
        return;
    }
    if (app.scroll_drag.active) {
        // Alt+drag grab-pan end: the pan applied incrementally during motion, so
        // there is nothing to finalize but the predictor. The continuous pan
        // deferred per-event resyncs, so re-anchor the predictor once here.
        if (playback.is_playing()) playback.resync_predictor();
        app.scroll_drag = ScrollDragState{};
        return;
    }
    if (app.region_drag.active) {
        // The region is extended live during the drag (see on_motion); a drag
        // that moved rests the region at its final extent. A MOTIONLESS
        // press-release (never crossed the threshold) needs no collapse here:
        // the press already cleared any resting highlight at mouse-down (see
        // arm_region_drag_at), so a plain click leaves the region cleared and
        // there is nothing to do at release but disarm.
        app.region_drag = RegionDragState{};
        return;
    }
    if (app.trim_drag.active) {
        commit_trim_drag();
        return;
    }
    if (app.pending_trim_drag.active) {
        // The pending trim drag never crossed the threshold: a motionless
        // chip-row press. Trim has no click action and the press mutated
        // nothing, so there is nothing to commit — just disarm. (A crossed
        // pending became app.trim_drag and commits through the branch above.)
        app.pending_trim_drag = PendingTrimDrag{};
        return;
    }
    if (app.pending_marker_drag.active) {
        // The pending marker drag never crossed the threshold: a pure flag
        // click. The press already single-selected its marker, so there is
        // nothing to commit — just disarm. (A crossed pending became app.drag
        // and commits through the branch below.)
        app.pending_marker_drag = PendingMarkerDrag{};
        return;
    }
    if (!app.drag.active) return;
    marker_drag.commit_drag();
}

// Motion handler. Drives the active pointer gesture: editor-text drag,
// strip-row zoom/pan drag, trim drag (or a pending trim drag arming past the
// threshold), region-select drag, or marker reposition drag (or a pending
// marker drag); with no gesture it recomputes hover at the cursor. The
// marker drag applies the pointer delta to the grabbed marker only — it does
// NOT move the playhead (only the Tab family and `c` land the playhead on a
// marker).
void GuiInputHandler::on_motion(int mouse_x, int mouse_y, GuiInputState mods) {
    // Record latest cursor coords so viewport mutators can re-evaluate hover
    // at the cursor's last position.
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
        // !g.valid (only an invalid editor target — the lane text stays
        // onscreen even off-view): no-op this frame, leaving the caret put.
        viewport.clear_hover_popup();
        return;
    }
    if (text_editor::is_active(app.settings_editor) ||
        text_editor::is_active(app.commit_editor)) {
        viewport.clear_hover_popup();
        return;
    }
    // Zoom-strip drag, SONG-anchored zoom (the Ableton model; see
    // apply_strip_drag_at). anchor_sample — the song position under press_x at
    // the press — is FIXED for the whole drag and stays pinned to press_x (the
    // row is zoom-only, no pan); the level tracks the absolute dy since press.
    // The repaint is SYNCHRONOUS (final_event=false — a full rebuild when the
    // level changed, a no-op when it did not), affordable because the platform
    // coalesces captured motion to one event per pointer frame. The release runs
    // the one synchronous rebuild plus the predictor resync. A lost button
    // finalizes like release.
    if (app.strip_drag.active) {
        if (!mods.primary_button_held) {     // button lost -> end like release
            if (app.strip_drag.moved)
                apply_strip_drag_at(mouse_y, /*final_event=*/true);
            app.strip_drag = StripDragState{};
            // An abnormal termination (button lost, not a clean release) seeds
            // no double-click candidate and drops any pending one.
            app.strip_double_click = StripDoubleClickCandidate{};
            end_strip_pointer_capture();
            return;
        }
        // Sub-pixel capture jitter must not promote a click to a drag: while the
        // press has not yet become a drag, apply nothing until the pointer has
        // travelled at least the Chebyshev threshold from the press, leaving the
        // drag armed but unmoved. This gate decides only WHETHER the press
        // becomes a drag — once moved it never re-engages, so dragging back near
        // the anchor mid-drag has no dead zone. The zoom reads the absolute dy
        // since press_y regardless of when it crosses, so the catch-up never
        // exceeds the real hand motion.
        if (!app.strip_drag.moved &&
            std::max(std::abs(mouse_x - app.strip_drag.press_x),
                     std::abs(mouse_y - app.strip_drag.press_y)) <
                kDragMovedThresholdPx) {
            return;
        }
        app.strip_drag.moved = true;
        apply_strip_drag_at(mouse_y, /*final_event=*/false);
        viewport.clear_hover_popup();
        return;
    }
    // Alt+drag grab-pan (continuous 1:1). The viewport snaps to whole pixels in
    // clamp_viewport_start (reached through scroll_viewport), so a per-event pan
    // re-anchored by that snap tracks the cursor 1:1 without drift — no carried
    // sample remainder. scroll_viewport drives the incremental shift-and-strip
    // fast-path, so per-event work is a memmove plus a dx-wide strip render. A
    // lost button ends it like release (re-anchor the predictor once). The
    // wheel keeps its quantized detent step; only the drag is continuous.
    if (app.scroll_drag.active) {
        if (!mods.primary_button_held) {     // button lost -> end like release
            if (playback.is_playing()) playback.resync_predictor();
            app.scroll_drag = ScrollDragState{};
            return;
        }
        const double spp = current_samples_per_pixel(app, audio);
        const int    dx  = mouse_x - app.scroll_drag.last_x;
        app.scroll_drag.last_x = mouse_x;
        const int64_t delta =
            static_cast<int64_t>(std::nearbyint(static_cast<double>(dx) * spp));
        if (delta != 0) {
            // Grab-pan: drag right (dx>0) reveals earlier content, viewport moves
            // left. The pan rides the SYNCHRONOUS pan mode (drain-when-busy): the
            // old design deferred a busy-worker frame to the async worker, which
            // was later convicted as a mid-gesture staleness mechanism (the frame
            // could paint over a stale-basis plate), so this reverted pan takes
            // the reviewer-verified drain path instead — the one departure from
            // the pre-retirement design.
            viewport.scroll_viewport(-delta, /*continuous=*/true,
                                     /*synchronous=*/true);
        }
        viewport.clear_hover_popup();
        return;
    }
    // Trim-boundary drag motion. Handled before the marker-drag branch;
    // active in BOTH views (begin_trim_drag has no view gate, and
    // update_trim_drag / commit_trim_drag carry the target-view cached-map
    // machinery). A lost button commits at the current position, mirroring the
    // marker-drag motion handler.
    if (app.trim_drag.active) {
        viewport.clear_hover_popup();
        if (!mods.primary_button_held) {
            commit_trim_drag();
            return;
        }
        update_trim_drag(mouse_x);
        return;
    }
    // Pending trim drag (armed by a plain chip-row press): the trim reposition
    // begins only once the pointer travels past the shared Chebyshev threshold.
    // A lost button before the crossing ends it as a motionless click (nothing
    // committed). Placed after the trim_drag branch above: on the crossing this
    // begins the drag AND applies its first update inline, so it does not fall
    // back into that branch this event.
    if (app.pending_trim_drag.active) {
        viewport.clear_hover_popup();
        if (!mods.primary_button_held) {   // button lost -> just the click
            app.pending_trim_drag = PendingTrimDrag{};
            return;
        }
        if (std::max(std::abs(mouse_x - app.pending_trim_drag.press_x),
                     std::abs(mouse_y - app.pending_trim_drag.press_y)) <
                kDragMovedThresholdPx) {
            return;   // still a click; leave the pending armed, do nothing
        }
        // Threshold crossed: begin the trim drag anchored at the PRESS column so
        // the bound(s) track from the grab, this first update folding the whole
        // press->crossing delta (the marker-pending / strip catch-up pattern).
        // begin_trim_drag captures the anchor at press_x now — exact, since
        // nothing mutated the trim store between press and crossing — and sets
        // app.trim_drag.active.
        const bool is_begin = app.pending_trim_drag.is_begin;
        const bool both     = app.pending_trim_drag.both;
        const int  press_x  = app.pending_trim_drag.press_x;
        app.pending_trim_drag = PendingTrimDrag{};
        begin_trim_drag(is_begin ? TrimHit::Begin : TrimHit::End, press_x, both);
        if (!app.trim_drag.active) return;  // begin refused (no pair / no audio)
        update_trim_drag(mouse_x);
        return;
    }
    // Target-view motion authoring is unblocked. Fall through
    // to source-view's drag / region-drag / hover handling; per-site
    // translation (drag anchor capture, motion delta conversion, hit
    // tests) lives in the handlers below.
    if (app.region_drag.active) {
        viewport.clear_hover_popup();
        // Left button must still be held; if not, the release was lost —
        // end the gesture, resting the region at its current extent (as a
        // clean release would). Modifier changes mid-drag are ignored.
        if (!mods.primary_button_held) {
            app.region_drag = RegionDragState{};
            return;
        }
        const GuiRect area = waveform_area(app);
        if (area.w <= 0) return;
        // Sub-threshold: the press has not yet become a drag. Below the shared
        // Chebyshev gate nothing extra happens — the press already did the
        // click and cleared any resting region at mouse-down. Once a drag,
        // always a drag (moved never re-engages).
        if (!app.region_drag.moved &&
            std::max(std::abs(mouse_x - app.region_drag.press_x),
                     std::abs(mouse_y - app.region_drag.press_y)) <
                kDragMovedThresholdPx) {
            return;
        }
        app.region_drag.moved = true;
        // Far endpoint at the pointer column, through the same click->frame
        // basis as the anchor, clamped to the visible strip like the other
        // drags' live tracking. Endpoints are active-domain frames, so the
        // span survives pan/zoom mid-drag and at rest.
        int rel = mouse_x - area.x;
        if (rel < 0) rel = 0;
        if (rel >= area.w) rel = area.w - 1;
        const int64_t far_frame =
            playhead_frame_at_click_column(app, audio, rel);
        app.region.active  = true;
        app.region.a_frame = app.region_drag.anchor_frame;
        app.region.b_frame = far_frame;
        viewport.invalidate_waveform_area();
        return;
    }
    // Pending marker drag (armed by a plain flag press): the marker was
    // single-selected at press; the reposition begins only once the pointer
    // travels past the shared Chebyshev threshold. Handled before the hover
    // fallthrough below and after the other drag branches (a pending drag and
    // any other pointer gesture are mutually exclusive — the arming press does
    // no other work). A lost button before the crossing ends it as a plain
    // click.
    if (app.pending_marker_drag.active) {
        if (!mods.primary_button_held) {   // button lost -> just the click
            app.pending_marker_drag = PendingMarkerDrag{};
            viewport.clear_hover_popup();
            return;
        }
        if (std::max(std::abs(mouse_x - app.pending_marker_drag.press_x),
                     std::abs(mouse_y - app.pending_marker_drag.press_y)) <
                kDragMovedThresholdPx) {
            return;   // still a click; leave the pending armed, do nothing
        }
        // Threshold crossed: begin the drag anchored at the PRESS column so the
        // marker tracks the pointer 1:1, this first apply folding the whole
        // press->crossing delta (the strip/region catch-up pattern). begin_drag
        // captures the pre-drag snapshot / selection / walls now — exact, since
        // nothing mutated the store between press and crossing — and sets
        // app.drag.active. Fall through (no return) so this same motion event
        // applies the first delta through the marker-drag branch below.
        const int marker  = app.pending_marker_drag.marker;
        const int press_x = app.pending_marker_drag.press_x;
        app.pending_marker_drag = PendingMarkerDrag{};
        if (!marker_drag.begin_drag(marker, press_x)) {
            viewport.clear_hover_popup();
            return;   // begin refused (bad index / no audio): drop the gesture
        }
        // No follow override needed: the marker drag always begins from a
        // top-strip flag press, which already stopped playback (see the
        // top-strip stop above), so there is no live playhead to chase.
    }
    if (!app.drag.active) {
        // No active gesture: hover recomputation is owned by
        // recompute_hover_at_cursor (one implementation for motion and
        // viewport mutation), suppressions included. The branches above
        // already returned on the suppressions it re-checks (prompt, the
        // editors, the other drags), so those re-checks are harmless; its
        // W-mode / iter-mode / top_flag_editor / queue_running arms clear
        // the popup exactly like this path's own else-clear did.
        viewport.recompute_hover_at_cursor();
        return;
    }
    // A drag is active — drop any pending popup.
    viewport.clear_hover_popup();
    // Left button must still be held down — otherwise release was lost.
    if (!mods.primary_button_held) {
        marker_drag.commit_drag();
        return;
    }
    const int sr = audio.sample_rate();
    if (sr <= 0) return;
    const GuiRect area = waveform_area(app);
    const double spp = current_samples_per_pixel(app, audio);
    // Target view: mouse-x → frames passes through
    // active_domain_to_source_frame so
    // the delta (mouse_frame - anchor_mouse_time_frame) is a source-
    // frame value, matching the source-domain anchor begin_drag
    // captured and the source-domain time_frame the apply path
    // writes into.
    double mouse_frame;
    if (active_display_context(app, audio).domain != GuiDisplayDomain::Source) {
        const int64_t mouse_frame_active =
            app.viewport_start_sample +
            static_cast<int64_t>(std::nearbyint(
                static_cast<double>(mouse_x - area.x) * spp));
        const int64_t mouse_frame_src =
            active_domain_to_source_frame(app, audio, mouse_frame_active);
        mouse_frame = static_cast<double>(mouse_frame_src);
    } else {
        mouse_frame = static_cast<double>(app.viewport_start_sample) +
            static_cast<double>(mouse_x - area.x) * spp;
    }
    marker_drag.apply_drag_motion(mouse_frame - app.drag.anchor_mouse_time_frame);
    // The grabbed marker does NOT tow the playhead: a marker drag is a
    // fine-tuning gesture, and the playhead stays parked wherever it was so an
    // upstream audition point survives the move. Only the Tab family and `c`
    // land the playhead on a marker. apply_drag_motion above already latched
    // app.drag.moved and collapsed the selection onto the grabbed marker on the
    // first real move; nothing further tracks here.
}
