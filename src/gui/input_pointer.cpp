#include "input_handler.h"

#include "gui_display_context.h"
#include "warp_frame_map_view.h"
#include "marker_drag.h"
#include "paint_handler.h"
#include "render.h"
#include "text_editor.h"
#include "warpmarkers.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

// GuiInputHandler pointer-gesture handlers (on_button_press,
// on_button_release, on_motion) and their two editor-text drag-lifecycle
// helpers (arm_editor_text_drag_on_open, finalize_editor_text_drag), lifted
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
        // FlagPayload — top strip.
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

// Button-press handler. Verbatim from the lambda at the original
// main.cpp:1483; the captured operation-struct lambdas (begin_drag,
// drop_marker, drop_phase_reset_at_position, set_single_selection, etc.)
// are rewritten to direct method calls on the appropriate operation
// struct ref. The four hit_test_* lambdas are now free functions taking
// (app, audio, ...) explicit args. The handle_wheel lambda is now a
// private method on this struct.
void GuiInputHandler::apply_strip_drag_at(int x, int y, bool final_event) {
    const StripDragState& sd = app.strip_drag;
    double new_level = sd.press_level;
    if (sd.zoom_axis) {
        // Drag DOWN (y grows) lowers the level → zooms in; drag UP raises it →
        // zooms out. Clamp into [kMinZoom, effective per-file ceiling]; the pan
        // row keeps press_level.
        new_level = sd.press_level -
            static_cast<double>(y - sd.press_y) / kZoomStripPxPerLevel;
        const double max_l = effective_max_zoom_level(
            waveform_area(app).w, live_total_frames(app, audio),
            audio.sample_rate());
        if (new_level < kMinZoom) new_level = kMinZoom;
        if (new_level > max_l)    new_level = max_l;
    }
    viewport.apply_strip_drag_zoom(new_level, sd.anchor_sample, x, final_event);
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
    if (text_editor::is_active(app.commit_editor)) return;
    if (text_editor::is_active(app.top_flag_editor) &&
        app.top_flag_editor.kind == text_editor::Kind::BpmBracket) {
        // The BPM editor is a bottom-strip modal owner (like the settings
        // editor). Mouse input does not interact with it beyond its own
        // click-to-cursor region; the session ends only through Esc or the
        // Enter dispatch path (`m` is just a typed character now). Swallow
        // the press so it cannot drive a playhead drag / marker click / or
        // tear the editor down through the top-strip flag-edit routine
        // below.
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
        // Live strip rows (Ableton-style navigation), claimed ahead of the
        // top-strip playback-stop and the click routing below. The top zoom row
        // and the bottom pan row claim ONLY the plain unmodified left press
        // inside their exact half-open row band; a modified press there is a
        // strict no-op (nothing else lives on these rows). The claim is
        // immediate — no motion threshold — and a motionless press-release
        // commits nothing. Navigation-class like the wheel pan: allowed in
        // read-only, never touches the playhead or selection, does not stop
        // playback, and does not override follow. All modal gates (prompt,
        // bottom-strip editors, the loading/empty guard) sit above this point,
        // so a modal surface blocks the claim exactly as it blocks every other
        // pointer target.
        {
            const GuiRect zoom_row = top_zoom_row_area(app);
            const GuiRect pan_row  = bottom_pan_row_area(app);
            const bool in_zoom_row =
                x >= zoom_row.x && x < zoom_row.x + zoom_row.w &&
                y >= zoom_row.y && y < zoom_row.y + zoom_row.h;
            const bool in_pan_row =
                x >= pan_row.x && x < pan_row.x + pan_row.w &&
                y >= pan_row.y && y < pan_row.y + pan_row.h;
            if (in_zoom_row || in_pan_row) {
                if (ctrl || shift || alt) return;  // modified: strict no-op
                // Double-click detection, BEFORE arming the drag: a candidate
                // seeded by the previous motionless press-release in the SAME
                // row, within kDoubleClickMs and kDoubleClickSlackPx of the
                // recorded x, consumes this press as a one-shot navigation
                // action — no drag armed, no pointer capture, playhead and
                // selection untouched, allowed in read-only (all modal gates sit
                // above this claim). The zoom row jumps to full zoom-out (whole
                // song); the pan row jumps to the working zoom recentered on the
                // playhead (the trailing center covers apply_zoom_change's
                // early-return when already at the working zoom, the same shape
                // the c handler uses). The candidate's first click briefly
                // captured and hid the cursor at its press and restored it at
                // the motionless release; this second press never captures.
                const StripDoubleClickCandidate& dc = app.strip_double_click;
                if (dc.valid && dc.zoom_axis == in_zoom_row &&
                    monotonic_ms() - dc.time_ms <= kDoubleClickMs &&
                    std::abs(x - dc.press_x) <= kDoubleClickSlackPx) {
                    app.strip_double_click = StripDoubleClickCandidate{};
                    if (in_zoom_row) {
                        viewport.apply_zoom_change(effective_max_zoom_level(
                            waveform_area(app).w, live_total_frames(app, audio),
                            audio.sample_rate()));
                    } else {
                        viewport.apply_zoom_change(kWorkingZoomLevel);
                        viewport.center_viewport_on_playhead();
                    }
                    return;
                }
                const double spp = current_samples_per_pixel(app, audio);
                app.strip_drag = StripDragState{};
                app.strip_drag.active    = true;
                app.strip_drag.zoom_axis = in_zoom_row;
                app.strip_drag.press_y   = y;
                // The song position painted under the press x, unclamped.
                app.strip_drag.anchor_sample =
                    static_cast<double>(app.viewport_start_sample) +
                    static_cast<double>(x) * spp;
                // Capture the level verbatim on both rows: the zoom row walks
                // the one continuous domain from wherever it rests, and the pan
                // row never changes it.
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

        // Editor: mouse handling.
        //   click inside top strip on the editing target: re-position
        //     cursor at the clicked byte (handled inside enter_*_edit)
        //   click inside top strip on a different flag: switch target
        //   click anywhere else: exit edit (no commit), then fall
        //     through so the click routes through normal handling.
        // A click in the top strip while a flag editor is active routes
        // through the normal flag hit-test below.
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

        // Clicks in iter/BPM mode route through the consolidated
        // flag/marker hit-test below.

        // Consolidated hit-test across waveform (marker line) and top
        // strip (flag rect). A flag click behaves exactly like a click
        // on its marker line. Trim bounds are transparent to PLAIN and SHIFT
        // presses (a click over a bound is an ordinary waveform click); the Alt
        // branch below consults the trim hit tests only after this marker hit
        // test misses, so no trim hit test runs on the plain/Shift path.
        int hit = -1;
        bool in_click_region = false;
        if (inside_waveform) {
            hit = hit_test_marker_line(app, audio, x);
            in_click_region = true;
        } else if (inside_top) {
            hit = hit_test_flag(app, audio, x, y);
            in_click_region = true;
        }

        if (!in_click_region) return;

        if (alt && !ctrl && !shift) {
            // Alt+drag (exact) is hit-area-dependent. In precedence order:
            //   1. Marker stem (waveform) or flag (top strip), hit >= 0 → the
            //      marker reposition drag (the mouse counterpart of the
            //      Alt+Left / Alt+Right nudge). A marker within its halo BEATS a
            //      trim stem in the same halo — markers are the denser primary
            //      objects, and a contested bound still has its upper-row chip
            //      as an unambiguous handle.
            //   2. Trim geometry (stem/chip single hit, or the top-strip span
            //      strictly between the two chips) → that bound's / the pair's
            //      drag, via route_trim_alt_press. The router CLAIMS the press
            //      (no pan fallback) whenever it lands on trim geometry.
            //   3. Empty waveform → nothing (pan lives on the strip rows now,
            //      claimed by the plain left press above; Alt over empty
            //      waveform falls through).
            // Read-only refuses the marker AND trim arms silently. The
            // marker/trim arms override follow when playing.
            if (hit >= 0) {
                if (active_view_state(app).read_only) {
                    return;
                }
                // begin_drag preserves the multi-selection if `hit` is in it,
                // else collapses to just `hit`. Motion decides whether it
                // actually becomes a drag vs. a plain click.
                const bool was_playing_alt = playback.is_playing();
                marker_drag.begin_drag(hit, x);
                if (was_playing_alt)
                    app.follow_overridden_for_session = true;
                return;
            }
            // Trim: the marker hit test missed, so consult the trim hit tests.
            // A claimed press (trim geometry, armed or read-only-refused) ends
            // here — no pan fallback. The follow override fires only when a drag
            // actually armed and playback was live at press time (a top-strip
            // press has already stopped playback, so was_playing is the
            // pre-stop snapshot captured up top).
            if (route_trim_alt_press(x, y, inside_top)) {
                if (app.trim_drag.active && was_playing)
                    app.follow_overridden_for_session = true;
                return;
            }
            // Empty waveform: nothing to arm. Pan is a strip-row gesture now,
            // so an Alt press here just ends without effect.
            return;
        }

        // Strict modifier matching: Alt-exact is hit-routed above (marker
        // reposition or trim arm on a hit, nothing on empty waveform — pan is a
        // strip-row gesture now). Ctrl+Alt is
        // no longer a pointer gesture — it falls here into the strict no-op (the
        // Alt+wheel chip-row trim-end move is a wheel gesture, unaffected). Every
        // other modifier combination — Ctrl on empty, Ctrl+Shift, Shift+Alt, ...
        // — no-ops here too. Only the plain or Shift-modified base press
        // proceeds (Shift adjusts the selection).
        if (ctrl || alt) return;

        // Plain or Shift press. In the waveform area this starts a
        // playhead-drag gesture. In the top strip (flag click) a W-view plain
        // click enters the warp canonical-line editor; Shift+click keeps the
        // multi-select toggle + playhead move. A P-view plain click is
        // navigation (single-select + playhead), falling through to the
        // selection block below; phase resets have no per-flag editor.
        if (inside_top) {
            if (hit >= 0) {
                if (!shift && app.active_markers_view != 'P') {
                    // Plain click on a W-view flag enters the warp
                    // canonical-line editor (which owns the selection +
                    // playhead update on its target-switching path).
                    // Read-only refuses the open (silent no-op). Shift+click
                    // keeps the multi-select toggle below; the Alt reposition-
                    // drag was handled by the branch above.
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
                int64_t src_sample;
                if (app.active_markers_view == 'P') {
                    src_sample = app.phaseresetmarkers.markers()[hit].time_frame;
                } else {
                    src_sample = app.warpmarkers.markers()[hit].time_frame;
                }
                // Land on the marker's clamped playhead image — the F1
                // chokepoint every press-path landing routes through, so no
                // consumer acts on a position the cursor did not land on.
                const int64_t sample =
                    playhead_image_of_authored_frame(app, audio, src_sample);
                viewport.move_playhead_to(sample);
            }
            return;
        }

        // Waveform-area press: start playhead drag gesture.
        {
            if (hit >= 0) {
                // Press on a marker (within 3px). Selection is press-time-only:
                // a plain press single-selects, a Shift press toggles membership
                // (a click — Shift+drag is not a defined gesture).
                if (!shift) {
                    selection.set_single_selection(hit);
                } else {
                    selection.toggle_selection_membership(hit);
                }
                int64_t src_sample;
                if (app.active_markers_view == 'P') {
                    src_sample = app.phaseresetmarkers.markers()[hit].time_frame;
                } else {
                    src_sample = app.warpmarkers.markers()[hit].time_frame;
                }
                // The marker's clamped playhead image — the value the cursor and
                // the reseek share, so a wall-adjacent marker reseeks to where
                // the cursor lands instead of past the range.
                const int64_t sample =
                    playhead_image_of_authored_frame(app, audio, src_sample);
                viewport.move_playhead_to(sample);
                if (was_playing && sample != playhead_at_entry) {
                    playback_lifecycle.reseek_keeping_alive(sample);
                }
                if (was_playing) app.follow_overridden_for_session = true;
                // A plain press arms the playhead scrub; a Shift press is a
                // click only (no drag), so it does not arm.
                if (!shift) app.playhead_drag.active = true;
            } else {
                // Press on empty waveform. Shift is a strict no-op (Shift is a
                // selection-toggle click, undefined on empty space).
                if (shift) return;
                const int click_rel_x = x - area.x;
                if (click_rel_x < 0 || click_rel_x >= area.w) {
                    selection.clear_selection();
                    return;
                }
                const int64_t sample =
                    playhead_frame_at_click_column(app, audio, click_rel_x);
                selection.clear_selection();
                viewport.move_playhead_to(sample);
                if (was_playing && sample != playhead_at_entry) {
                    playback_lifecycle.reseek_keeping_alive(sample);
                }
                if (was_playing) app.follow_overridden_for_session = true;
                app.playhead_drag.active = true;
            }
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
        // Terminating event: if the drag moved, re-derive the level/anchor at
        // the release position and run the one synchronous rebuild (resync +
        // kick_waveform_sync, inside apply_strip_drag_zoom's final path) so the
        // rest state is exact. A motionless press-release finalizes nothing.
        // Double-click seeding: a MOTIONLESS release records a candidate (this
        // release x equals the press x); a release that MOVED records nothing
        // and clears any candidate, so a drag can never seed the second click of
        // a double-click.
        if (app.strip_drag.moved) {
            apply_strip_drag_at(x, y, /*final_event=*/true);
            app.strip_double_click = StripDoubleClickCandidate{};
        } else {
            app.strip_double_click = StripDoubleClickCandidate{
                .valid = true, .time_ms = monotonic_ms(),
                .zoom_axis = app.strip_drag.zoom_axis, .press_x = x};
        }
        app.strip_drag = StripDragState{};
        end_strip_pointer_capture();  // reappear the cursor at the press point
        return;
    }
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
    marker_drag.commit_drag();
}

// Motion handler. Verbatim from the lambda at the original
// main.cpp:1319; the operation-struct method calls (apply_drag_motion,
// commit_drag, move_playhead_to, invalidate_top_strip,
// invalidate_timestamp_area, invalidate_playhead_columns) are rewritten
// to direct method calls on warpops / viewport. popup_eligible_marker
// (now in app_state.{h,cpp}) takes `app` as its first argument; the
// remaining free function calls (hit_test_marker_line, hit_test_flag,
// compute_hover_popup_text, waveform_area, current_samples_per_pixel,
// playhead_pixel_x, text_editor::is_active) keep their original spelling.
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
        // !g.valid (flag scrolled off-view mid-drag): no-op this frame,
        // leaving the caret where it was.
        viewport.clear_hover_popup();
        return;
    }
    if (text_editor::is_active(app.settings_editor) ||
        text_editor::is_active(app.commit_editor)) {
        viewport.clear_hover_popup();
        return;
    }
    // Strip-row zoom/pan drag: the pressed song position sticks to the pointer.
    // The top zoom row also zooms around that point from vertical motion; the
    // bottom pan row keeps the level fixed. Every motion event re-derives the
    // level (zoom row) and re-anchors the viewport at the pointer, riding the
    // ASYNC supersede slot (final_event=false) — a stale plate converging is
    // accepted under the pointer torrent; the release runs the one synchronous
    // rebuild. A lost button finalizes like release.
    if (app.strip_drag.active) {
        if (!mods.primary_button_held) {     // button lost -> end like release
            if (app.strip_drag.moved)
                apply_strip_drag_at(mouse_x, mouse_y, /*final_event=*/true);
            app.strip_drag = StripDragState{};
            // An abnormal termination (button lost, not a clean release) seeds
            // no double-click candidate and drops any pending one.
            app.strip_double_click = StripDoubleClickCandidate{};
            end_strip_pointer_capture();
            return;
        }
        app.strip_drag.moved = true;
        apply_strip_drag_at(mouse_x, mouse_y, /*final_event=*/false);
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
    // Target-view motion authoring is unblocked. Fall through
    // to source-view's drag / playhead-drag / hover handling; per-site
    // translation (drag anchor capture, motion delta conversion, hit
    // tests) lives in the handlers below.
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
        // The snap is purely a playhead-positioning magnet; the drag moves the
        // playhead only and never touches selection (selection is press-time-
        // only).
        const int hit = hit_test_marker_line(app, audio, mouse_x);
        int64_t new_playhead;
        if (hit >= 0) {
            int64_t src_sample;
            if (app.active_markers_view == 'P') {
                src_sample = app.phaseresetmarkers.markers()[hit].time_frame;
            } else {
                src_sample = app.warpmarkers.markers()[hit].time_frame;
            }
            // Land on the snapped marker's clamped playhead image (the F1
            // chokepoint): forward-translate the source frame to the displayed
            // domain, then clamp into the playhead domain, so the
            // new_playhead != cursor compare agrees with the value the cursor
            // actually holds — a compressed final segment that rounds a
            // wall-adjacent marker to the target total lands on the reachable
            // total - 1 instead.
            new_playhead = playhead_image_of_authored_frame(app, audio, src_sample);
        } else {
            // No marker within epsilon: playhead follows cursor freely.
            int rel = mouse_x - area.x;
            if (rel < 0) rel = 0;
            if (rel >= area.w) rel = area.w - 1;
            new_playhead = playhead_frame_at_click_column(app, audio, rel);
        }

        if (new_playhead != app.playhead_cursor_sample) {
            viewport.move_playhead_to(new_playhead);
        }
        return;
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

    // Track the playhead with the grabbed marker. The hit marker's
    // proposed source-time lives in the drag overlay — under the
    // frozen-coord regime apply_drag_motion does not mutate the live
    // store during motion. In target view, forward-translate through
    // the display context's target map (the same map paint walks during
    // motion, stable for the drag's lifetime) so the playhead lands at the
    // same screen column as the marker stem.
    // Viewport is deliberately not followed — the user can pan manually
    // if the drag runs past the edge. The commit completes this tracking
    // by snapping the playhead onto the committed marker frame (commit_drag).
    //
    // Gated on app.drag.moved, read AFTER this event's apply_drag_motion
    // call (that call is what latches moved on the first real position
    // change): motion tracking engages only once the drag has actually
    // moved a marker time, so a gesture that never moves anything —
    // vertical-only pointer motion at a fixed x, or pushing outward
    // against a wall that clamps the delta to zero — mutates no view
    // state. moved latches on the first moving event and never clears, so
    // tracking engages in that same event (no one-event lag) and keeps
    // tracking through a wander back to the origin. This is the same moved
    // gate the release synchronization (commit_drag) and the Esc-cancel
    // playhead restore already apply — one consistent moved-not-net_changed
    // story across all three.
    if (!app.drag.moved) return;
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
            app.drag.moveable_times[hit_pos]));
        // to_domain_frame decides Source-identity vs mapped forward-map off
        // the active display context, so the call is unconditional: the
        // display context's map translates in a mapped domain and is inert in
        // the Source domain (the outer flag test was a duplicate of that
        // internal short-circuit).
        int64_t ph = to_domain_frame(
            app, audio, ph_src,
            *active_display_context(app, audio).warp_frame_map);
        // Playhead domain clamp through clamp_playhead_to_live_domain (the
        // domain ruling). With both marker walls at total - 1 the source-view
        // value is already in domain; this closes the target-view case
        // where to_domain_frame of a wall-resting marker rounds to the
        // target total.
        ph = clamp_playhead_to_live_domain(ph, app, audio);
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
