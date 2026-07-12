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

// sweep_select_interval (the Shift playhead-drag sweep) lives in app_state.h
// so input_render_view.cpp can call it too; the source/target sweeps below
// resolve it from there.

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
    } else if (text_editor::is_active(app.top_flag_editor) &&
               app.top_flag_editor.kind == text_editor::Kind::BpmBracket) {
        g.ed = &app.top_flag_editor;
        g.text_left = static_cast<double>(timestamp_pad_x()) +
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

// Button-press handler. Verbatim from the lambda at the original
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

    // Render-view mouse gate. Left-click on a marker line
    // (in the waveform area) or a flag rect (in the top strip)
    // toggles selection and jumps the playhead to the marker;
    // left-click elsewhere in the waveform area positions the
    // playhead (with playback stop) and clears the selection unless
    // Shift is held. All wheel chords (zoom, Alt pan) are pure viewport
    // ops and pass through unchanged. Drag-create and top-strip playhead
    // movement are silent no-ops so the read-only invariant on
    // marker state is preserved. Render view runs no hover popup — the
    // motion handler clears any popup still showing from source view
    // (a recorded asymmetry at handle_render_view_motion's tail).
    // Target-view mouse authoring is unblocked. Fall through
    // to the source-view handler; the input-to-source-frame boundary
    // translation lives in the per-gesture writers (drag
    // begin/motion, etc.) and in the active_domain_to_source_frame
    // helper used by those writers.

    if (app.render_view.enabled) {
        handle_render_view_press(button, x, y, inside_top, inside_waveform,
                                 mods);
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
        // on its marker line.
        int hit = -1;
        bool in_click_region = false;
        // A trim-boundary press is consumed only for its recognized gestures:
        // a Ctrl-exact reposition-drag, a Ctrl+Shift move-both-bounds drag, or
        // a plain / Shift select+navigate. Alt is reserved for panning
        // (Alt+drag over a trim stem pans, ignoring the boundary). Any other
        // unrecognized combo falls through to no-op at the strict guard below.
        const bool trim_gesture = !alt;
        if (inside_waveform) {
            hit = hit_test_marker_line(app, audio, x);
            // A waveform press that misses every marker but lands
            // on a trim boundary stem routes to the trim gesture path.
            // Markers take priority on a shared column.
            if (hit < 0) {
                const TrimHit th = hit_test_trim_boundary(app, audio, x);
                if (th != TrimHit::None && trim_gesture) {
                    handle_trim_boundary_press(th, ctrl, shift, x);
                    if (app.trim_drag.active && was_playing)
                        app.follow_overridden_for_session = true;
                    return;
                }
            }
            in_click_region = true;
        } else if (inside_top) {
            // The trim stem is grabbable along its whole visible
            // extent in the top strip, mirroring the in-waveform stem.
            // Markers take priority on a shared column, so try the flag
            // hit-test first; only on a miss does the trim path fill in.
            hit = hit_test_flag(app, audio, x, y);
            if (hit < 0) {
                // The b/e chip glyph is painted hl_pad RIGHT of the
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
                if (th != TrimHit::None && trim_gesture) {
                    handle_trim_boundary_press(th, ctrl, shift, x);
                    if (app.trim_drag.active && was_playing)
                        app.follow_overridden_for_session = true;
                    return;
                }
            }
            in_click_region = true;
        }

        if (!in_click_region) return;

        if (alt && !ctrl && !shift) {
            // Alt+drag (exact) pans the viewport — a stepped scroll-drag on the
            // waveform, regardless of whether the press landed on a marker, so a
            // pan never grabs a marker. No-op in the top strip; the scroll
            // happens on motion. The scroll-drag only moves the viewport, so it
            // is allowed in read-only. It deliberately does NOT override follow
            // mode: a pan during playback moves the view along with the audio
            // rather than signaling a stop, unlike the marker / trim / playhead
            // drags, which override follow for the session.
            if (inside_waveform) {
                app.scroll_drag.active        = true;
                app.scroll_drag.last_x        = x;
                app.scroll_drag.accum_samples = 0.0;
            }
            return;
        }

        if (ctrl && !alt && !shift && hit >= 0) {
            // Ctrl+drag (exact) on a marker repositions it (the mouse
            // counterpart of the Ctrl+Left / Ctrl+Right nudge). Ctrl is the
            // marker modifier; panning is Alt+drag. Ctrl on empty waveform is
            // not a gesture and no-ops at the strict guard below. The marker
            // drag mutates marker positions, so read-only refuses the
            // drag-begin (app.drag.active never enters flight state; motion /
            // release / Escape paths all short-circuit on !app.drag.active).
            if (active_view_state(app).read_only) {
                return;
            }
            // begin_drag preserves the multi-selection if `hit` is in
            // it, else collapses to just `hit`. Motion decides whether
            // it actually becomes a drag vs. a plain click.
            const bool was_playing_ctrl = playback.is_playing();
            marker_drag.begin_drag(hit, x);
            if (was_playing_ctrl)
                app.follow_overridden_for_session = true;
            return;
        }

        // Strict modifier matching: pan is Alt-exact and marker reposition is
        // Ctrl-exact (both handled above). Any remaining modifier combination
        // — Ctrl on empty, Ctrl+Alt, Shift+Alt, Ctrl+Shift, ... — is not a
        // recognized waveform gesture and no-ops here. Only the plain or
        // Shift-modified base press proceeds (Shift adjusts the selection).
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
                int64_t src_sample;
                if (app.active_markers_view == 'P') {
                    src_sample = static_cast<int64_t>(std::nearbyint(
                        app.phaseresetmarkers.markers()[hit].time_frame));
                } else {
                    src_sample = static_cast<int64_t>(std::nearbyint(
                        app.warpmarkers.markers()[hit].time_frame));
                }
                const int64_t sample =
                    source_frame_to_active_domain(app, audio, src_sample);
                viewport.move_playhead_to(sample);
            }
            return;
        }

        // Waveform-area press: start playhead drag gesture.
        {
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
                        app.phaseresetmarkers.markers()[hit].time_frame));
                } else {
                    src_sample = static_cast<int64_t>(std::nearbyint(
                        app.warpmarkers.markers()[hit].time_frame));
                }
                const int64_t sample =
                    source_frame_to_active_domain(app, audio, src_sample);
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
    if (app.scroll_drag.active) {
        if (playback.is_playing()) playback.resync_predictor();
        app.scroll_drag = ScrollDragState{};
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
    if constexpr (kDebugPerf) {
        app.last_input_event_time = std::chrono::steady_clock::now();
    }
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
    if (text_editor::is_active(app.settings_editor)) {
        viewport.clear_hover_popup();
        return;
    }
    // Ctrl+drag on empty waveform: continuous 1:1 viewport pan. Each motion
    // event pans by its exact pixel delta (dx * samples-per-pixel); the
    // fractional sample remainder is carried in accum_samples so a long drag
    // does not drift off 1:1. scroll_viewport drives the incremental
    // shift-and-strip fast-path, so per-event work is a memmove plus a dx-wide
    // strip render. The wheel keeps its quantized detent step; only the drag is
    // continuous.
    if (app.scroll_drag.active) {
        if (!mods.primary_button_held) {     // button lost -> end like release
            if (playback.is_playing()) playback.resync_predictor();
            app.scroll_drag = ScrollDragState{};
            return;
        }
        const double spp = current_samples_per_pixel(app, audio);
        const int    dx  = mouse_x - app.scroll_drag.last_x;
        app.scroll_drag.last_x = mouse_x;
        app.scroll_drag.accum_samples += static_cast<double>(dx) * spp;
        const int64_t whole =
            static_cast<int64_t>(app.scroll_drag.accum_samples);  // trunc to 0
        if (whole != 0) {
            app.scroll_drag.accum_samples -= static_cast<double>(whole);
            // Grab-pan: drag right (dx > 0) reveals earlier content, so the
            // viewport moves left.
            viewport.scroll_viewport(-whole, /*continuous=*/true);
        }
        viewport.clear_hover_popup();
        return;
    }
    // Trim-boundary drag motion. Handled before the render-view
    // and marker-drag branches; active in BOTH views (begin_trim_drag has
    // no view gate, and update_trim_drag / commit_trim_drag carry the
    // target-view cached-map machinery). A lost button commits at the
    // current position, mirroring the marker-drag motion handler.
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
    // Render-view motion handler with playhead-drag snap support:
    // when a drag is in flight, snap the
    // playhead to the visible sub-view's markers (3px epsilon),
    // matching source-view's gesture. Otherwise run hover popup
    // detection against render_view.warp_markers (suppressed in phase reset
    // sub-view because hit_test_flag short-circuits to -1).
    if (app.render_view.enabled) {
        handle_render_view_motion(mouse_x, mouse_y, mods);
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
                    app.phaseresetmarkers.markers()[hit].time_frame));
            } else {
                src_sample = static_cast<int64_t>(std::nearbyint(
                    app.warpmarkers.markers()[hit].time_frame));
            }
            // Target view: forward-translate the snapped marker's
            // source-frame to active-domain so the playhead lands on
            // the marker's displayed position.
            new_playhead = source_frame_to_active_domain(app, audio, src_sample);
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
                int64_t lo = active_domain_to_source_frame(app, audio, a);
                int64_t hi = active_domain_to_source_frame(app, audio, b);
                if (lo > hi) std::swap(lo, hi);
                // Sweep endpoints widen to doubles for the interval
                // compare against the stores' int64 frames.
                const double lo_t = static_cast<double>(lo);
                const double hi_t = static_cast<double>(hi);
                const bool swept = (app.active_markers_view == 'P')
                    ? sweep_select_interval(
                          app, app.phaseresetmarkers.markers(),
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
                // Hover readout lives on the bottom strip now.
                // No dwell: show immediately on rect-entry; recompute
                // cached_text once, derive visible, damage when the old
                // popup was showing or the new one will.
                const bool was_visible = app.hover_popup.visible;
                app.hover_popup.marker_index = hit;
                app.hover_popup.copy_payload.clear();
                app.hover_popup.cached_text =
                    popup_eligible_marker(app, hit)
                        ? compute_hover_popup_text(
                              slice_to_warp_markers(app.warpmarkers.markers()), hit,
                              audio.sample_rate(), &app.hover_popup.copy_payload)
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
    // the frozen warp_frame_map (the same map paint walks during motion) so
    // the playhead lands at the same screen column as the marker stem.
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
        // frozen map translates in a mapped domain and is inert in the
        // Source domain (the outer flag test was a duplicate of that
        // internal short-circuit).
        int64_t ph = to_domain_frame(app, audio, ph_src,
                                     app.drag.frozen_warp_frame_map);
        // Playhead domain clamp, mirroring move_playhead_to (the ruling
        // lives there). With both marker walls at total - 1 the source-view
        // value is already in domain; this closes the target-view case
        // where to_domain_frame of a wall-resting marker rounds to the
        // target total.
        const int64_t live_total = live_total_frames(app, audio);
        if (ph < 0) ph = 0;
        if (live_total > 0 && ph >= live_total) ph = live_total - 1;
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
