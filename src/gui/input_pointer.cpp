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

// Region-drag end: dissolve a resting region whose on-screen span is under the
// arm gate. The press-becomes-drag gate (kDragMovedThresholdPx) latches once
// and never re-engages, so a hand-jitter drag that crosses the gate then
// releases near the press — or wanders back toward it — can rest a sliver
// region a pixel or two wide. That was never an intentional window: a
// sub-threshold rest reads as a click, so it dissolves exactly as a plain
// click's would, clearing the wash and the split playhead (the cursor playhead
// returns when the region deactivates, which the same damage covers). Called
// from both region-drag end points (release and button-lost). Only the REST is
// gated — the live mid-drag extension paints slivers freely. A dissolved region
// also DROPS the drag's live selection (Direction A of the coupling): the
// press's deselect-all intent stands, so a jitter click must not leave a
// one-marker selection behind (architect 2026-07-23).
void end_region_drag_min_size_check(AppState& app, const GuiAudio& audio,
                                    Viewport& viewport, Selection& selection) {
    if (!app.region.active) return;
    const double spp = current_samples_per_pixel(app, audio);
    if (spp <= 0.0) return;   // no geometry -> leave the region as-is
    const double span_px =
        std::abs(static_cast<double>(app.region.a_frame - app.region.b_frame)) /
        spp;
    if (span_px < kDragMovedThresholdPx) {
        app.region = RegionState{};
        selection.clear_selection();
        viewport.invalidate_waveform_area();
    }
}

// LANDS the playhead exactly onto marker `hit` (active column's store), with
// NO viewport move — the sole difference from Tab (which recenters) and `c`
// (which re-zooms and recenters), so the view holds perfectly still while the
// playhead seats. Shared by the plain marker click (lands on ITS marker), the
// shift range click, and the ctrl toggle click (both land at the EARLIEST
// selected marker, `hit` = *selected_markers.begin(); an empty post-toggle
// selection lands nothing). The two-step placement
// basis the Tab family lands with (source_frame_to_active_domain then
// clamp_playhead_to_live_domain), against the active column's store, so the
// placement is exactly coincident for a subsequent nudge/drag ride. Direct
// cursor write mirroring jump_playhead_to_focused_marker's non-recenter part —
// NOT move_playhead_to, whose keep-visible edge-align could scroll for a
// half-offscreen flag, and the ruling is NO viewport write of any kind (the
// playhead may rest at a slightly offscreen column when the clicked flag hung
// half off the edge — accepted). Read-only allowed (selection + playhead are
// navigation). Callers stop playback first (the standing top-strip press stop),
// Tab-family symmetry.
void land_playhead_on_marker(AppState& app, const GuiAudio& audio,
                             Viewport& viewport, int hit) {
    int64_t src_frame = 0;
    bool valid = true;
    if (app.active_markers_view == 'P') {
        const auto& tv = app.phaseresetmarkers.markers();
        if (hit < 0 || hit >= static_cast<int>(tv.size())) valid = false;
        else src_frame = tv[hit].time_frame;
    } else {
        const auto& mv = app.warpmarkers.markers();
        if (hit < 0 || hit >= static_cast<int>(mv.size())) valid = false;
        else src_frame = mv[hit].time_frame;
    }
    if (valid) {
        int64_t sample = source_frame_to_active_domain(app, audio, src_frame);
        sample = clamp_playhead_to_live_domain(sample, app, audio);
        const double old_px = playhead_pixel_x(app, audio);
        app.playhead_cursor_sample = sample;
        // Navigation land: dissolve a resting region — the playhead just
        // jumped onto the marker, off any prior auditioning span.
        clear_region_highlight(app, viewport);
        viewport.invalidate_playhead_columns(
            old_px, playhead_pixel_x(app, audio));
        viewport.invalidate_timestamp_area();
    }
}

// Direction B of the selection<->highlight coupling (architect 2026-07-23): a
// multi-select CLICK that leaves 2+ markers selected sets the region to the
// selection's active-domain position extent [earliest, latest], so the
// highlight, the earliest-marker land, and Space's play-from-region-left-bound
// all agree by construction. A selection of <=1 sets NOTHING — the click's
// standing region clear (land_playhead_on_marker's dissolve, or the ctrl
// empty-branch's explicit clear_region_highlight) is the dissolve. Endpoints
// are clamped through clamp_playhead_to_live_domain (the region domain's
// playable-frame invariant, as every other former clamps). Shared by the
// shift-range and ctrl-toggle click paths; MUST run AFTER the land (which
// CLEARS any old region) — a reorder would let the clear kill this fresh
// highlight. Touches ONLY the region, never shift_range_anchor, so the
// shift-range path's anchor survives a direction-B set. Programmatic selections
// (undo/redo, paste, drops, Tab/`c`) do NOT call this — the coupling belongs to
// the two multi-select clicks.
void set_region_to_selection_extent(AppState& app, const GuiAudio& audio,
                                    Viewport& viewport) {
    if (app.selected_markers.size() < 2) return;
    const bool phase_reset = (app.active_markers_view == 'P');
    const auto& warp_vec = app.warpmarkers.markers();
    const auto& phase_reset_vec = app.phaseresetmarkers.markers();
    const int n = phase_reset
        ? static_cast<int>(phase_reset_vec.size())
        : static_cast<int>(warp_vec.size());
    int64_t lo = 0, hi = 0;
    bool have = false;
    for (int idx : app.selected_markers) {
        if (idx < 0 || idx >= n) continue;   // defensive; stale indices skipped
        const int64_t src_f = phase_reset
            ? phase_reset_vec[idx].time_frame
            : warp_vec[idx].time_frame;
        const int64_t pos = clamp_playhead_to_live_domain(
            source_frame_to_active_domain(app, audio, src_f), app, audio);
        if (!have) { lo = hi = pos; have = true; }
        else { if (pos < lo) lo = pos; if (pos > hi) hi = pos; }
    }
    if (!have) return;   // every selected index was stale (degenerate)
    app.region.active  = true;
    app.region.a_frame = lo;
    app.region.b_frame = hi;
    viewport.invalidate_waveform_area();
}

} // namespace

// One scrub ACT: kill-and-revive (architect 2026-07-23 — scrub is technically
// not keep-alive but revive-if-needed-or-kill-and-revive). Every scrub act is
// a FRESH session, never a positional seek inside the old one: a live session
// STOPS first (the standing stop machinery — side-effect-clean here, the scrub
// never moved the cursor), then the stopped launch path runs (the is_updating
// outer gate + scrub_launch_at, the revive), re-capturing the loop verdict and
// end bound freshly per scrub — a scrub after a mid-session trim edit
// auditions the NEW window instead of riding the stale launch capture. ONE
// degenerate skip, kept by explicit choice (re-launch-always EXCEPT the
// exact-same-frame case): a live session scrubbed to its scanner's exact
// current frame has nothing to re-launch, so the audition plays on
// uninterrupted (the scanner-sample read is gated on is_playing — playing
// implies scanner-active, so the field is meaningful). A refused revive
// (out-of-window frame; target update in flight) leaves playback stopped,
// silently — the "nothing to audition" family; a later act at a launchable
// frame revives (the revive-if-needed half).
void GuiInputHandler::scrub_act_at(int64_t frame) {
    if (playback.is_playing()) {
        if (frame == app.playhead_scanner_sample) return;  // same position ->
                                                           // nothing to re-launch
        playback_lifecycle.stop_playback_if_playing();     // the kill
    }
    // Outer is_updating gate, mirroring the two Space handlers: a NEW launch
    // while a target update is in flight would audition the stale target
    // buffer, which Space refuses — so the scrub revive refuses it too,
    // silently.
    if (!(app.active_audio_view == 'T' && target_render.is_updating()))
        playback_lifecycle.scrub_launch_at(frame);         // the revive
}

// The scanner scrub press body, shared by the waveform lower-half plain press
// and the marker-text-lane plain press (R3.3, architect 2026-07-23). See the
// declaration for the full contract. Both callers keep playback alive across
// the press (the waveform lower half is not a top-strip press; the text-lane
// scrub is exempted from the top-strip stop), so the scrub act sees the live
// session — load-bearing for its same-frame skip, which keeps an in-place
// audition uninterrupted instead of restarting it.
void GuiInputHandler::arm_scrub_at(int click_rel_x) {
    const GuiRect area = waveform_area(app);
    // Gutter / invalid column: no launch position exists, silent no-op.
    if (click_rel_x < 0 || click_rel_x >= area.w) return;
    const int64_t frame = clamp_playhead_to_live_domain(
        playhead_frame_at_click_column(app, audio, click_rel_x), app, audio);
    scrub_act_at(frame);
    app.scrub_area_drag = ScrubAreaDragState{};
    app.scrub_area_drag.active   = true;
    app.scrub_area_drag.last_col = click_rel_x;
}

// Button-press handler. Verbatim from the lambda at the original
// main.cpp:1483; the captured operation-struct lambdas (begin_drag,
// drop_marker, drop_phase_reset_at_position, set_single_selection, etc.)
// are rewritten to direct method calls on the appropriate operation
// struct ref. The four hit_test_* lambdas are now free functions taking
// (app, audio, ...) explicit args. The handle_wheel lambda is now a
// private method on this struct.
void GuiInputHandler::apply_strip_drag_at(int x, int y, bool final_event) {
    // Dual-axis strip drag, INCREMENTAL (the v6 model). Every event reads the
    // LIVE zoom level and viewport and applies its own dx/dy on top — there is no
    // press baseline to go stale across composed pan/zoom phases (the earlier
    // axis-lock model died of exactly that staleness). The song anchor
    // (anchor_sample) is the zoom focus; the pan re-derives its drifted column
    // each event and the edge trick rebinds it when it leaves the screen.
    StripDragState& sd = app.strip_drag;

    // (1) Per-event deltas from the previous motion position. The crossing event
    // folds the whole accumulated delta since the press (last_x/last_y were
    // seeded there and no sub-threshold event advanced them).
    const double dx = static_cast<double>(x - sd.last_x);
    const double dy = static_cast<double>(y - sd.last_y);
    sd.last_x = x;
    sd.last_y = y;

    // (2) The old spp is read from the LIVE level (never stored).
    const double spp_old = current_samples_per_pixel(app, audio);
    const GuiRect wf_area = waveform_area(app);
    const double W = static_cast<double>(wf_area.w);
    const int64_t total = live_total_frames(app, audio);

    // (3) Pan at the old level, in the double domain: grab sign — drag right
    // (dx>0) reveals earlier content, so the viewport moves left. The result is
    // WALL-CLAMPED here, at the old level, to the SAME right wall the downstream
    // clamp_viewport_start rests at — the shared max_viewport_start_grid owner
    // (the level mid-gesture is the live level, so it reads exactly the state the
    // chokepoint would). step (5) derives the anchor column and rebinds
    // anchor_sample from vp, so both must see the viewport that will actually
    // REST. The earlier `total − W·spp_old` form sat up to a pixel short of the
    // legal grid rest — pressing at the flush-right rest first pulled vp back to
    // that off-grid wall, the anchor column clamped at W−1, and the edge rebind
    // PERMANENTLY rewrote anchor_sample, so the at-wall no-op proof failed exactly
    // at the legal rest; sharing the wall makes vp derive at the true rest and the
    // no-op proof hold. The grid snap of arbitrary INTERIOR vp values is
    // deliberately NOT reproduced (the sub-pixel residue there self-heals on the
    // following event, exactly as step (5)'s live re-read does — only the WALL had
    // to be exact because the edge rebind is a lasting mutation).
    double vp = static_cast<double>(app.viewport_start_sample) - dx * spp_old;
    const double vp_lo = 0.0;
    const double vp_hi = static_cast<double>(max_viewport_start_grid(app, audio));
    if (vp < vp_lo) vp = vp_lo;
    if (vp > vp_hi) vp = vp_hi;

    // (4) Zoom INCREMENTALLY off the live level: this event's dy applies to the
    // current level (drag DOWN, dy>0, lowers the level → zooms in). No press
    // baseline, so a wall reversal responds immediately — the older absolute-dy
    // formula had a dead zone after a clamp (dy had to unwind all the way back
    // before the level moved); this incremental form has none.
    double new_level = app.zoom_level - dy / kZoomStripPxPerLevel;
    const double max_l = effective_max_zoom_level(
        W, total, audio.sample_rate());
    if (new_level < kMinZoom) new_level = kMinZoom;
    if (new_level > max_l)    new_level = max_l;

    // (5) The anchor's drifted column under the wall-clamped post-pan viewport,
    // with the Ableton EDGE TRICK: clamp the column into [0, W-1] (the effective
    // waveform width), and when the clamp engages REBIND anchor_sample to that
    // edge pixel's frame — the zoom focus never leaves the screen; a pan that
    // pushes it to an edge PINS it there and it becomes the edge's content.
    // Deriving the column from the wall-clamped vp is what makes pure pan an
    // exact identity (see below) even at a saturated wall, and reading the live
    // viewport each event is what lets the sub-pixel grid snap self-heal on the
    // following event.
    double anchor_col = (sd.anchor_sample - vp) / spp_old;
    const double col_max = W > 0.0 ? W - 1.0 : 0.0;
    double clamped_col = anchor_col;
    if (clamped_col < 0.0)     clamped_col = 0.0;
    if (clamped_col > col_max) clamped_col = col_max;
    if (clamped_col != anchor_col) {
        sd.anchor_sample = vp + clamped_col * spp_old;
        anchor_col = clamped_col;
    }

    // Drive the capture's release-restore x to the anchor stem's surface x — the
    // identical column->x math render_strip_anchor_stem paints at (area.x +
    // col + 0.5), so on release the cursor lands dead on the stem rather than at
    // its raw traveled position (which the edge rebind leaves past a pinned
    // stem). Fired every event; the last before release wins. A motionless
    // strip press-release never reaches here, so its override stays unset and it
    // restores at the press point.
    if (set_strip_capture_restore_x)
        set_strip_capture_restore_x(
            static_cast<double>(wf_area.x) + anchor_col + 0.5);

    // (6) Apply: place anchor_sample at anchor_col under the new level's spp and
    // clamp. IDENTITY PROOFS: pure pan (dy=0) is EXACT — new_level == old, so
    // apply reproduces vp = anchor_sample - anchor_col·spp_old bit-for-bit (the
    // column was derived from that same vp), and the level-unchanged dispatch
    // rides the synchronous incremental pan path. Off the walls the pan arithmetic
    // is unchanged, so the identity holds as before; AT a wall the clamped vp
    // equals the viewport that will rest, the anchor column re-derives against it
    // consistently, and apply reproduces the wall value — a saturated pan is a
    // true no-op. Pure zoom (dx=0) leaves the anchor's current (possibly
    // edge-pinned) column fixed and pivots the rescale around it. A both-unchanged
    // event (level and viewport identical after the clamp) is a true no-op the
    // entry point skips.
    viewport.apply_strip_drag_zoom(new_level, sd.anchor_sample, anchor_col,
                                   final_event);
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

    // A double-click is two CONSECUTIVE clicks: snapshot the pending candidate
    // and clear the shared field here, so ANY intervening press invalidates it.
    // The consume checks below read this snapshot; each surface then re-seeds
    // its own fresh candidate (ZoomRow / EditorText at a motionless release,
    // Marker at the press). One closed instrumentation point — the clear covers
    // every non-consuming press (a strip/region/chip arm, a modal swallow)
    // without a clear scattered on each path.
    const DoubleClickCandidate dc_at_press = app.double_click;
    app.double_click = DoubleClickCandidate{};

    // F2.1: mouse drag-to-select inside the active text editor. A press on
    // the active editor's text region places the caret and arms a selection
    // drag (anchor == caret until the pointer moves). Resolved before the
    // per-editor modal swallows below so the gesture reaches the settings /
    // BPM bottom-strip editors too. A press outside the active editor's
    // region falls through: the bottom-strip editors stay modal and swallow
    // it, while the top flag editor closes guard-free below and the press
    // then acts normally.
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
                // (flag_pending_text_left_x, the one caret-origin owner). Any
                // OTHER press is a non-caret click, which closes the editor
                // below and then routes normally (the guard-free lifecycle).
                const GuiRect lane = top_marker_text_row_area(app);
                const double run_w = static_cast<double>(
                    app.top_flag_editor.pending.size()) * g.advance;
                in_region = y >= lane.y && y < lane.y + lane.h &&
                    static_cast<double>(x) >= g.text_left &&
                    static_cast<double>(x) <= g.text_left + run_w;
            }
            if (in_region) {
                // Double-click: a second click within the window on this
                // editor's text selects the RUN of the clicked character class
                // (word / punctuation / whitespace) under the click — select_
                // word_at's own classifier, not just a word — arming no drag.
                // The surface tag keeps it from consuming a marker / zoom-row
                // candidate.
                const DoubleClickCandidate& dc = dc_at_press;
                if (dc.surface == DoubleClickSurface::EditorText &&
                    monotonic_ms() - dc.time_ms <= kDoubleClickMs &&
                    std::abs(x - dc.press_x) <= kDoubleClickSlackPx &&
                    std::abs(y - dc.press_y) <= kDoubleClickSlackPx) {
                    const int idx = text_editor::byte_index_from_click_x(
                        static_cast<double>(x), g.text_left, g.advance,
                        static_cast<int>(g.ed->pending.size()));
                    text_editor::select_word_at(*g.ed, idx);
                    if (g.bottom_strip) viewport.invalidate_timestamp_area();
                    else                viewport.invalidate_top_strip();
                    return;
                }
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
            // lane text falls through to the guard-free close below.
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
    // branch — the upper half clears the selection (it has no column to seat a
    // playhead, so that is all it does), the lower (scrub) half returns
    // silently (no launch position exists, and a scrub press touches no
    // selection anyway). The gutter is 0 px at the deployment widths
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
    if (app.tempo_drag.active) return;
    if (app.trim_drag.active) return;

    // Mouse authoring is home-view gated like the keyboard: a plain flag
    // press in W+target arms the TEMPO drag on an eligible marker instead
    // of the reposition drag (marker_drag.tempo_drag_predecessor; an
    // ineligible press still selects and LANDS the playhead, only the drag
    // arm is refused) — the pointer half of the home-view binding's one tempo
    // exception — while placement arming everywhere else is gated by
    // active_column_authoring_allowed, off-home selecting and landing but
    // arming nothing. The click-playhead / region-drag family below is
    // navigation, not authoring, and stays view-independent.

    if (button == GuiMouseButton::Left) {
        // Editor lifecycle, guard-free. A press in the editor's rendered lane
        // text already repositioned the caret / armed the text drag above (the
        // F2.1 block) and returned; ANY other left press with the top flag
        // editor open CLOSES it without committing — exactly Esc's teardown
        // (pending dropped; Enter is the only commit route, so closing is cheap
        // and non-destructive) — and then FALLS THROUGH so the press acts
        // normally (arm a strip drag, select a marker, arm a marker drag, land,
        // place the playhead, ...). Placed ahead of the zoom-row claim so the
        // close really is unconditional. Consequence: a double-click on the
        // open editor's own marker is close-then-reopen — the first click
        // closes + selects + seeds a Marker candidate (+ arms the pending drag
        // on the flag part, writable), and the second consumes into a fresh
        // open. That IS the documented "double-click opens the editor"; there
        // is no own-marker special case.
        if (text_editor::is_active(app.top_flag_editor))
            flag_editor.exit_top_flag_edit_no_commit();

        // Live top zoom-strip row (Ableton-style navigation), claimed ahead of
        // the top-strip playback-stop and the click routing below. It claims
        // ONLY the plain unmodified left press inside its exact half-open row
        // band; a modified press there is a strict no-op (nothing else lives on
        // the row). The claim is immediate — no motion threshold — and a
        // motionless press-release commits nothing. Navigation-class like the
        // wheel pan: allowed in read-only, never touches the playhead or
        // selection, does not stop playback, and does not override follow. It is
        // DUAL-AXIS (vertical motion zooms, horizontal motion pans, freely
        // composed — see apply_strip_drag_at). All modal gates (prompt,
        // bottom-strip editors, the loading/empty guard) sit above this point, so
        // a modal surface blocks the claim exactly as it blocks every other
        // pointer target.
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
                // consumes this press as a one-shot zoom command — no drag armed,
                // no pointer capture, playhead and selection untouched, allowed
                // in read-only (all modal gates sit above this claim). The
                // surface tag (ZoomRow) means a marker / editor candidate can
                // never consume here. The double-click DIVERGES from the bare
                // `0` key:
                // it runs run_zoom_double_click_command (zoom to the region /
                // trim / whole-song span), not run_zoom_toggle_command. The
                // candidate's first click briefly captured and hid the cursor at
                // its press and restored it at the motionless release; this
                // second press never captures.
                const DoubleClickCandidate& dc = dc_at_press;
                if (dc.surface == DoubleClickSurface::ZoomRow &&
                    monotonic_ms() - dc.time_ms <= kDoubleClickMs &&
                    std::abs(x - dc.press_x) <= kDoubleClickSlackPx) {
                    run_zoom_double_click_command();
                    return;
                }
                const double spp = current_samples_per_pixel(app, audio);
                app.strip_drag = StripDragState{};
                app.strip_drag.active    = true;
                // The press position: the drag-threshold reference, and the seed
                // for the incremental per-event deltas (last_x/last_y). Not a
                // baseline — every event reads the live level and viewport.
                app.strip_drag.press_x   = x;
                app.strip_drag.press_y   = y;
                app.strip_drag.last_x    = x;
                app.strip_drag.last_y    = y;
                // The song position painted under the press at press time — the
                // anchor the zoom pivots around. Rebindable: a pan that drives
                // its column offscreen re-pins it to the nearest visible pixel.
                app.strip_drag.anchor_sample =
                    static_cast<double>(app.viewport_start_sample) +
                    static_cast<double>(x) * spp;
                // Zoom-row origin: a motionless release seeds the ZoomRow
                // double-click candidate (the ctrl-exact waveform arm sets this
                // false so its release seeds nothing).
                app.strip_drag.double_click_seed = true;
                // Ableton-style pointer capture: hide and lock the cursor at
                // the press so motion feeds the gesture as unbounded virtual
                // coordinates (infinite pan/zoom travel). Self-guarding no-op
                // on a degraded compositor. The capture is shared by three
                // gestures (this zoom strip, the ctrl-exact waveform strip drag,
                // and the alt-exact pan); every exit path of each calls the
                // matching end hook exactly once (it is idempotent).
                begin_strip_pointer_capture();
                return;
            }
        }

        // Unified marker hit, computed ONLY on the path that consumes it. The
        // marker is ONE pointer item — flag shape OR rendered lane run
        // (marker_hit_at, the shared resolver in render.cpp the hover recompute
        // also reads) — and the TOP-STRIP hit feeds the plain/Shift/Ctrl
        // marker-click branches (plain = single-select + land the playhead on
        // the marker + double-click seed / consume + arm the pending marker
        // drag on the flag part, Shift = file-manager inclusive RANGE select
        // from the shift-held anchor to the clicked marker + land at the
        // earliest selected, Ctrl = the individual membership toggle + land at
        // the earliest selected), so it is resolved once here.
        // The editor lifecycle block above already closed any open flag editor,
        // so the lane run this resolves is the committed (non-editor) run. The
        // WAVEFORM never SELECTS a marker by HIT — a plain press splits by half
        // (upper: deselect-all + playhead placement + region-drag arm; lower:
        // the scanner scrub, which touches no selection at all), and a Shift
        // press FORMS a region waveform-wide (from the playhead, or a marker
        // DEMOTE that clears the selection; see the waveform block below) — so
        // no marker scan runs on the waveform at all (the invisible stem is
        // not a grab target). The plain DRAG does live-select the span's markers
        // (Direction A of the coupling), but by CONTAINMENT of the active-domain
        // span, not a stem hit. Trim bounds are grabbed only by their top-strip chips /
        // the inter-chip bridge on a PLAIN chip-row press (route_trim_chip_press
        // below); a click over a bound's waveform stem is an ordinary waveform
        // click (the stem grab retired), so no trim hit test runs on the
        // waveform at all. Resolved BEFORE the top-strip stop below because the
        // text-lane scrub exemption depends on it.
        MarkerHit mh;
        if (inside_top) mh = marker_hit_at(app, audio, x, y);

        // Top-strip clicks stop playback first: they select markers and can
        // close an open flag editor, and continuing audio during authoring /
        // text editing is the wrong default. Waveform clicks keep playback
        // alive — the per-press reseek to the click sample happens at the
        // playhead-drag press site below, gated on was_playing && sample !=
        // playhead_at_entry. Capture the entry state up front so the downstream
        // branches see the same snapshot.
        // ONE exemption: a plain press in the marker-text lane with no run under
        // it is the text-lane scrub (R3.3) — a playback navigation, not
        // authoring, so it reaches the shared scrub act (kill-and-revive) with
        // the session still live, exactly like the waveform lower-half scrub —
        // load-bearing for the act's same-frame skip, which keeps an in-place
        // audition uninterrupted. Every other
        // top-strip press stops as before.
        const bool was_playing = playback.is_playing();
        const int64_t playhead_at_entry = app.playhead_cursor_sample;
        bool text_lane_scrub_press = false;
        if (inside_top && mh.index < 0 && !ctrl && !shift && !alt) {
            const GuiRect text_lane = top_marker_text_row_area(app);
            text_lane_scrub_press =
                (y >= text_lane.y && y < text_lane.y + text_lane.h);
        }
        if (inside_top && !text_lane_scrub_press)
            playback_lifecycle.stop_playback_if_playing();

        // Clicks in iter/BPM mode route through the unified marker
        // hit-test below.

        // Only presses inside the waveform or the top strip do anything.
        if (!inside_waveform && !inside_top) return;

        // Alt-exact left press: on the waveform it arms the captured grab-pan;
        // alt-exact anywhere else (a top-strip marker included) does nothing
        // further HERE — the land now lives on the PLAIN marker click below.
        // On a markerless (or top-strip) spot that "nothing" is not a strict
        // no-op in the playback sense: the standing top-strip playback stop
        // above (every top-strip press stops playback) has already run, so
        // playback is halted even though no pan fires; alt+drag from a marker
        // is inert by ruling.
        //
        // The waveform grab-pan: continuous 1:1 pan of the viewport by the
        // per-event pixel delta (see on_motion). It CAPTURES the pointer
        // (begin_strip_pointer_capture, the same cursor-hide + lock the zoom
        // strip uses) so the pan travels infinitely under unbounded virtual
        // coordinates while the viewport clamps at the song walls — pan-only, no
        // zoom axis and no anchor stem (the stem is the zoom pivot affordance,
        // gated on strip_drag.active). Navigation-class: allowed in read-only,
        // never touches the playhead or selection. It deliberately does NOT
        // override follow — a pan during playback moves the view along with the
        // audio rather than signaling a stop, unlike the marker / trim / playhead
        // gestures. A motionless Alt press-release commits nothing but the brief
        // cursor hide/reappear (the scroll happens on motion).
        if (alt && !ctrl && !shift) {
            if (inside_waveform) {
                app.scroll_drag = ScrollDragState{};
                app.scroll_drag.active = true;
                app.scroll_drag.last_x = x;
                begin_strip_pointer_capture();
            }
            return;
        }

        // Ctrl-exact left press splits by surface. On a top-strip MARKER it is
        // the individual membership toggle + land at the earliest selected (the
        // marker claim below). On the WAVEFORM it arms the dual-axis strip drag — the SAME
        // gesture the zoom row arms (StripDragState / apply_strip_drag_at),
        // triggered here for reach: it gets the cursor capture ("swallow"), the
        // anchor stem, the edge clamp, and dual-axis zoom+pan for free. That arm
        // is byte-identical to the zoom-row arm (anchor_sample from the click
        // song position, press/last seeds, begin_strip_pointer_capture),
        // diverging at ONE point — double_click_seed=false, so a motionless
        // ctrl+waveform press-release seeds no ZoomRow double-click candidate
        // (that affordance stays zoom-row-only). The waveform strip-drag half is
        // navigation-class: allowed in read-only, never touches the playhead or
        // selection — and a MOTIONLESS ctrl+waveform press-release commits
        // nothing at all (the R3.4 selection clear is RETIRED, architect
        // 2026-07-23: ctrl is purely the zoom modifier on the waveform).
        // Ctrl-exact on a MARKERLESS top-strip spot is a strict no-op except
        // the chip row's BEGIN bound set (the claim below).
        if (ctrl && !alt && !shift) {
            // Ctrl-exact on a top-strip MARKER is the individual membership
            // toggle (the former shift behavior) — this AMENDS the "Ctrl keeps
            // only the letter chords" rule for the one marker surface (architect
            // 2026-07-23). It arms no drag, seeds/consumes no double-click, opens
            // no editor. Whether the toggle ADDED or REMOVED, the playhead lands
            // at the EARLIEST selected marker afterward (*selected_markers.begin()
            // — the set is index-ordered and markers rest time-sorted, so the
            // smallest index is the earliest in time; architect 2026-07-23), so a
            // Space auditions from the selection's start regardless of which
            // marker was clicked. An emptied selection (the last member toggled
            // off) lands nothing and drops the region (the empty branch's
            // explicit clear_region_highlight). A land dissolves the old region,
            // then — under Direction B of the selection<->highlight coupling
            // (architect 2026-07-23) — a resulting selection of 2+ markers sets
            // the region to the selection's [earliest, latest] extent
            // (set_region_to_selection_extent, AFTER the land's clear); a single
            // survivor leaves the land's dissolve standing. Read-only allowed
            // (selection + playhead are navigation). The ctrl-exact WAVEFORM
            // press keeps the zoom-strip drag below (different surface, no
            // collision); a markerless top-strip ctrl press claims only the
            // chip row (BEGIN bound set, next block) and is a strict no-op on
            // every other lane. The standing
            // top-strip press stop already halted playback above.
            if (inside_top && mh.index >= 0) {
                selection.toggle_selection_membership(mh.index);
                if (!app.selected_markers.empty())
                    land_playhead_on_marker(app, audio, viewport,
                                            *app.selected_markers.begin());
                else
                    clear_region_highlight(app, viewport);
                set_region_to_selection_extent(app, audio, viewport);
                return;
            }
            // Markerless top-strip ctrl-exact press: the CHIP ROW sets the
            // BEGIN trim bound at the click (R4.6 as corrected by R5 — ctrl is
            // BEGIN and ctrl+shift is END, the architect's intended pair;
            // set_trim_bound_at_click refuses read-only silently and runs the
            // coupling sync). EVERY other lane — an empty flag or triangle
            // lane included — is a strict no-op, falling through to the return
            // below (the R3.2 ctrl-clear is RETIRED, architect 2026-07-23:
            // ctrl-click in Ableton is just click, and ctrl stays the zoom
            // modifier here; the PLAIN empty flag/triangle-lane clear below is
            // the surviving clear). The zoom row (lane 0) was claimed above
            // and never reaches here.
            if (inside_top) {
                const GuiRect chip_row = top_upper_row_area(app);
                if (y >= chip_row.y && y < chip_row.y + chip_row.h) {
                    set_trim_bound_at_click(/*is_begin=*/true, x);
                    return;
                }
            }
            if (inside_waveform) {
                const double spp = current_samples_per_pixel(app, audio);
                app.strip_drag = StripDragState{};
                app.strip_drag.active  = true;
                app.strip_drag.press_x = x;
                app.strip_drag.press_y = y;
                app.strip_drag.last_x  = x;
                app.strip_drag.last_y  = y;
                app.strip_drag.anchor_sample =
                    static_cast<double>(app.viewport_start_sample) +
                    static_cast<double>(x) * spp;
                // Waveform origin: never seeds a zoom-row double-click candidate.
                app.strip_drag.double_click_seed = false;
                begin_strip_pointer_capture();
            }
            return;
        }

        // Ctrl+Shift-exact: the chip row is its ONE claim — set the END trim
        // bound at the click (R5: ctrl is BEGIN, ctrl+shift is END;
        // set_trim_bound_at_click refuses read-only silently and runs the
        // coupling sync). Everywhere else Ctrl+Shift stays a strict no-op,
        // falling to the return below.
        if (ctrl && shift && !alt && inside_top) {
            const GuiRect chip_row = top_upper_row_area(app);
            if (y >= chip_row.y && y < chip_row.y + chip_row.h) {
                set_trim_bound_at_click(/*is_begin=*/false, x);
                return;
            }
        }

        // Strict modifier matching: the marker reposition arm lives on the plain
        // flag press and trim's chip/bridge drags on the plain chip-row press, so
        // every remaining modified combination — Ctrl+Alt (now a strict no-op),
        // Ctrl+Shift off the chip row (its one claim is the END bound set
        // above), Shift+Alt, Ctrl+Alt+Shift, ... — no-ops here. Only a plain
        // or Shift-on-the-top-strip base press proceeds (Shift adjusts the
        // marker selection). The Alt+wheel pan and the Alt keyboard chords are untouched
        // (separate handlers).
        if (ctrl || alt) return;

        // Plain or Shift press. In the waveform area a plain press splits by
        // HALF: the UPPER half clears the marker selection (deselect-all),
        // places the playhead at the clicked column, and arms the
        // region-select drag; the LOWER half is the scrub surface (scanner
        // launch/reseek, nothing else) — neither ever SELECTS a marker. A
        // Shift press on the waveform instead FORMS a region waveform-wide
        // (the former / marker demote, one-shot — see the waveform block). In the top strip a plain
        // CHIP-ROW press arms a trim chip/bridge drag (claimed ahead of the
        // marker select); otherwise (a marker click on EITHER part — flag shape
        // or lane run) selection is the whole interface, BOTH views. Plain click:
        // single-select, LAND the playhead on the marker (below), and — on the
        // FLAG part only — ARM a pending marker drag (moves the marker if the
        // pointer crosses the threshold, else a pure click). Shift+click: a
        // file-manager INCLUSIVE RANGE select from the shift-held anchor to the
        // clicked marker (the range end = FOCUS), which LANDS the playhead at the
        // range's EARLIEST member (not the clicked end — focus and land diverge).
        // The individual membership TOGGLE moved to Ctrl+click (the ctrl-exact
        // marker claim in the earlier branch; whether it adds or removes it lands
        // at the earliest selected, an empty selection landing nothing). The
        // plain / shift / ctrl land makes every such marker click a land route
        // alongside the Tab family and `c` (which additionally recenter /
        // re-zoom); the subsequent drag or nudge then tows the coincident
        // playhead onto the FOCUSED marker by construction.
        if (inside_top) {
            // The chip row (top_upper_row_area, lane 1) is trim's lane and is
            // claimed BEFORE the marker single-select. The chip row, the marker
            // text lane, and the flag/triangle lanes are disjoint y-bands, so
            // this contends with nothing: a marker-part press falls to the marker
            // handling below. The PLAIN click either arms a chip/bridge drag or,
            // on an unclaimed spot, selects + highlights the trim window (R4.5);
            // the bound-set clicks are the ctrl (BEGIN) / ctrl+shift (END)
            // claims above (R5). A SHIFT-exact chip-row press claims nothing —
            // trim is transparent to it, and the shift fall-through below is
            // inert here (marker_hit_at's y-bands exclude the chip row), so it
            // ends at the top-strip return.
            const GuiRect chip_row = top_upper_row_area(app);
            const bool in_chip_row =
                (y >= chip_row.y && y < chip_row.y + chip_row.h);
            if (!shift && in_chip_row) {
                // Plain chip-row press (R4.5). Read-only cannot ARM a trim drag,
                // but the select+highlight is navigation, so it runs directly
                // there (route_trim_chip_press would claim-without-arming and
                // skip it). In a writable tab a chip/bridge hit arms the drag (a
                // motionless release then runs this same R4.5 click action at
                // on_button_release), and an UNCLAIMED chip-row spot runs the
                // R4.5 select+highlight now. With BOTH bounds set the window is
                // taken; lone/no trim is a silent no-op. Either way the chip row
                // consumes the press — it never falls to the marker handling.
                if (active_view_state(app).read_only) {
                    if (app.trim.has_begin && app.trim.has_end)
                        sync_highlight_to_trim_window();
                    return;
                }
                if (route_trim_chip_press(x, y)) return;
                if (app.trim.has_begin && app.trim.has_end)
                    sync_highlight_to_trim_window();
                return;
            }
            if (mh.index >= 0) {
                const int hit = mh.index;
                if (shift) {
                    // Shift+click is a file-manager INCLUSIVE RANGE select
                    // (architect 2026-07-23): the first shift-click of a
                    // continuous shift-held interaction anchors on the marker
                    // (selection becomes {hit}, hit the anchor + focus); each
                    // successive shift-click replaces the selection with the
                    // inclusive index range between the shift-held anchor and
                    // hit. The clicked marker becomes the range end = FOCUS
                    // (last_selected), but the playhead LANDS at the range's
                    // EARLIEST member (*selected_markers.begin() — smallest
                    // index = earliest in time; architect 2026-07-23), so a
                    // Space after a range select auditions from the range's
                    // start. FOCUS and the land target thus DIVERGE here: the
                    // unconditional nudge/drag follow then tows the playhead onto
                    // the FOCUSED (clicked) range end as ever. On the anchoring
                    // first click the selection is {hit}, so *begin() == hit and
                    // the land is unchanged. Ctrl+click is the individual
                    // membership toggle (above, landing at the earliest selected
                    // too). It arms no drag, seeds/consumes no double-click, opens
                    // no editor. Allowed in read-only (selection + playhead are
                    // navigation). Direction B of the coupling: a range leaving
                    // 2+ selected sets the region to the [earliest, latest]
                    // extent (set_region_to_selection_extent, AFTER the land's
                    // region clear), so highlight, land, and Space's left-bound
                    // launch agree; the anchoring first click ({hit}) leaves <=1
                    // selected and sets no region.
                    selection.select_range_from_anchor(hit);
                    if (!app.selected_markers.empty())
                        land_playhead_on_marker(app, audio, viewport,
                                                *app.selected_markers.begin());
                    set_region_to_selection_extent(app, audio, viewport);
                } else {
                    // Plain marker click single-selects (both views; W's
                    // click-to-edit is retired — the editor now opens on Enter or
                    // this double-click). Selection is navigation, allowed in
                    // read-only.
                    selection.set_single_selection(hit);
                    // ...and LANDS the playhead exactly onto the marker (shared
                    // helper; see land_playhead_on_marker). Runs on EVERY plain
                    // marker click — the double-click-consume path below (the
                    // first click already landed, so the second's land is a
                    // same-value repeat, harmless) and the plain-select path;
                    // both parts of the unified item land (flag shape or lane
                    // run — hit is the one index).
                    land_playhead_on_marker(app, audio, viewport, hit);
                    // Double-click: a Marker candidate for the SAME index within
                    // the window opens the flag editor, exactly like Enter on the
                    // focused marker (the click above already single-selected it).
                    // The surface + target tag prevents any zoom-row / editor
                    // candidate from consuming here, and a candidate seeded on
                    // one part consumes on the other — one surface. Read-only,
                    // P view (phase resets have no per-flag editor), and the
                    // off-home column (active_column_authoring_allowed false —
                    // the warp editor is source-view-only) refuse silently,
                    // matching Enter's allowlist / view refusal — the candidate
                    // is cleared and the press stays a plain second select. On a
                    // consumed open nothing is armed and no fresh candidate
                    // seeds (the editor now owns input).
                    bool opened_editor = false;
                    const DoubleClickCandidate& dc = dc_at_press;
                    if (dc.surface == DoubleClickSurface::Marker &&
                        dc.target == hit &&
                        monotonic_ms() - dc.time_ms <= kDoubleClickMs &&
                        std::abs(x - dc.press_x) <= kDoubleClickSlackPx &&
                        std::abs(y - dc.press_y) <= kDoubleClickSlackPx) {
                        if (app.active_markers_view != 'P' &&
                            !active_view_state(app).read_only &&
                            active_column_authoring_allowed(app)) {
                            // Every open route opens fully SELECTED (open-
                            // selected), so there is no clicked-glyph caret to
                            // seat — the flag-shape vs lane-run press are the
                            // same open. A specific caret spot is a click inside
                            // the already-open editor (the F2.1 path).
                            flag_editor.enter_top_flag_edit(hit);
                            opened_editor = true;
                        }
                    }
                    if (!opened_editor) {
                        // SEED a Marker candidate at this PRESS — the one seed
                        // timing for the whole marker surface (the release-time
                        // seeding is retired). Press-seeding is safe for the
                        // flag part because a press that becomes a real drag
                        // drops the candidate at the threshold crossing (see
                        // on_motion's pending-marker-drag branch).
                        app.double_click = DoubleClickCandidate{
                            .surface = DoubleClickSurface::Marker,
                            .time_ms = monotonic_ms(),
                            .press_x = x, .press_y = y,
                            .target  = hit};
                        // A writable tab arms a pending drag on a plain FLAG
                        // press only (the flag is the sole drag handle; the
                        // lane run selects but never arms); read-only selects
                        // but never arms (marker mutation refused). WHICH drag
                        // arms is the home-view split: in the column's home
                        // view the press arms the marker REPOSITION drag; in W
                        // view + TARGET view exactly, it instead arms the
                        // TEMPO drag on an eligible marker — the pointer half
                        // of the home-view binding's tempo exception
                        // (architect 2026-07-22; the keyboard half is the
                        // owner-only Alt+Up/Down step), an Ableton-style
                        // stretch that rewrites the GROUP PREDECESSOR's tempo.
                        // Coincident groups drag as ONE — dragging any member
                        // stretches against the marker before the stack (the
                        // walk in tempo_drag_predecessor). An ineligible W+T
                        // press (marker at the store's earliest frame, non-owner
                        // predecessor, or a coincident-collapsed predecessor
                        // whose tempo is render-inert — tempo_drag_predecessor
                        // returns -1), and the P column off ITS home (P view in
                        // source view), select and LAND the playhead but arm
                        // nothing — the silent read-only convention, marker
                        // motion / tempo authoring being authoring.
                        if (mh.on_flag && !active_view_state(app).read_only) {
                            if (active_column_authoring_allowed(app)) {
                                app.pending_marker_drag = PendingMarkerDrag{};
                                app.pending_marker_drag.active  = true;
                                app.pending_marker_drag.marker  = hit;
                                app.pending_marker_drag.press_x = x;
                                app.pending_marker_drag.press_y = y;
                            } else if (app.active_markers_view == 'W' &&
                                       app.active_audio_view == 'T') {
                                // The walk is purely the eligibility test here
                                // (>= 0 means armable); the crossing re-walks in
                                // begin_tempo_drag, equivalent because nothing
                                // mutates the store between press and crossing.
                                if (marker_drag.tempo_drag_predecessor(hit)
                                        >= 0) {
                                    app.pending_tempo_drag = PendingTempoDrag{};
                                    app.pending_tempo_drag.active      = true;
                                    app.pending_tempo_drag.marker      = hit;
                                    app.pending_tempo_drag.press_x     = x;
                                    app.pending_tempo_drag.press_y     = y;
                                }
                            }
                        }
                    }
                }
            } else if (!shift) {
                // Empty top-strip spot — no marker run/flag under the point (the
                // chip row already returned above). Plain-only (R3.1 / R3.3):
                const GuiRect text_lane = top_marker_text_row_area(app);
                if (y >= text_lane.y && y < text_lane.y + text_lane.h) {
                    // R3.3: the marker-text lane is the play-from-here SCRUB. The
                    // rendered run keeps precedence — a run hit resolved above as
                    // mh.index >= 0, so this is reached only on an EMPTY text-lane
                    // spot. Shares the waveform lower-half scrub body; playback
                    // stayed alive via the text-lane-scrub stop exemption above,
                    // so the scrub act (kill-and-revive) sees the live session
                    // and its same-frame skip can keep an in-place audition
                    // uninterrupted. Touches no
                    // selection (the scrub convention).
                    arm_scrub_at(x - area.x);
                    return;
                }
                const GuiRect flag_lane = top_flag_row_area(app);
                const GuiRect tri_lane  = top_triangle_row_area(app);
                if ((y >= flag_lane.y && y < flag_lane.y + flag_lane.h) ||
                    (y >= tri_lane.y  && y < tri_lane.y  + tri_lane.h)) {
                    // R3.1: an empty flag or triangle lane click clears the
                    // marker selection (navigation, read-only allowed).
                    selection.clear_selection();
                    return;
                }
            }
            return;
        }

        // Waveform-area press: marker-blind for SELECTION (it never SELECTS a
        // hit marker — the invisible stem is not a grab target; marker_hit_at
        // runs only for top-strip presses). The PLAIN press splits by HALF
        // (architect 2026-07-23): the UPPER half keeps the placement press —
        // CLEARS the selection (the deselect-all: a waveform click dismisses
        // the marker selection, the Ableton behaviour), drops the playhead at
        // the clicked column (no marker snap — the 3px marker-snap magnet
        // already died with the retired plain-drag scrub), reseeks a live
        // scanner to it, overrides follow, and arms the region drag — which
        // also DISSOLVES any resting highlight at this mouse-down
        // (arm_region_drag_at clears it after snapshotting the pre-press
        // extent for an Esc-mid-drag restore), so the wash vanishes on press
        // whether the gesture becomes a click or a fresh drag. The LOWER half
        // is the SCRUB surface (the branch below): the press runs one
        // kill-and-revive scrub act — a fresh SCANNER session from the clicked
        // frame — and arms the scrub drag,
        // touching nothing else. ONLY the plain press splits — a Shift press
        // instead FORMS a region waveform-wide (the demote path below), never
        // a plain press's playhead placement, and ctrl/alt already claimed
        // their waveform-wide gestures above.
        {
            const int click_rel_x = x - area.x;
            if (shift) {
                // Waveform shift+click: the region former / the DEMOTE (this is
                // one of the DROP formers — it clears the selection; the
                // coupling's promote direction lives on the plain drag and the
                // multi-select clicks, not here; architect 2026-07-23, replacing
                // the reserved strict no-op). With NO markers
                // selected it forms a region from the PLAYHEAD to the clicked
                // column; with markers selected it DEMOTES — the selection
                // clears and the region spans from the selected marker FURTHEST
                // from the click to the click (one rule: furthest =
                // argmax |pos - click| over the selection in active-domain
                // frames, which covers the between-the-series case as the
                // longest side). It moves NO playhead, stops NO playback,
                // reseeks nothing, overrides no follow, arms NO drag, seeds no
                // double-click (one-shot). Read-only allowed (the region is
                // transient navigation; the demote's deselect is selection =
                // navigation). ctrl/alt returned earlier, so this is
                // shift-exact — a shift+modified combination never reaches here.
                // The deselect runs FIRST, before the gutter early-return, so
                // an inert-gutter shift+click (no column to form a region from)
                // still drops the marker selection — every waveform click drops
                // it, mirroring the plain branch's gutter clear.
                if (click_rel_x < 0 || click_rel_x >= area.w) {
                    selection.clear_selection();
                    return;
                }
                // Region endpoints hold PLAYABLE live-domain frames only: the
                // display-state validator (clamp_display_state_to_live_domain)
                // defines an endpoint >= total as invalid and clears the whole
                // highlight, and the forward map rounds unclamped, so an EOF
                // item's image (a marker at total-1 under a fast map) can land
                // one past the wall — clamping here through the land's own
                // helper keeps every former inside the one region domain. The
                // click_frame is clamped for the same conformance (the plain
                // press path clamps it through move_playhead_to; the region
                // former stored it raw).
                const int64_t click_frame = clamp_playhead_to_live_domain(
                    playhead_frame_at_click_column(app, audio, click_rel_x),
                    app, audio);
                int64_t endpoint = app.playhead_cursor_sample;
                if (!app.selected_markers.empty()) {
                    // Demote: the region's far endpoint is the selected marker
                    // whose active-domain position is furthest from the click.
                    // Stale indices are skipped defensively; if every index was
                    // stale (degenerate) the playhead endpoint stands.
                    int64_t best_dist = -1;
                    for (int idx : app.selected_markers) {
                        int64_t src_frame;
                        if (app.active_markers_view == 'P') {
                            const auto& tv = app.phaseresetmarkers.markers();
                            if (idx < 0 || idx >= static_cast<int>(tv.size()))
                                continue;
                            src_frame = tv[idx].time_frame;
                        } else {
                            const auto& mv = app.warpmarkers.markers();
                            if (idx < 0 || idx >= static_cast<int>(mv.size()))
                                continue;
                            src_frame = mv[idx].time_frame;
                        }
                        // Clamp the forward-map image into the live domain (see
                        // the click_frame comment above): an EOF marker's image
                        // can round to total, which the display-state validator
                        // rejects — the land's own helper keeps this endpoint a
                        // playable frame.
                        const int64_t pos = clamp_playhead_to_live_domain(
                            source_frame_to_active_domain(app, audio, src_frame),
                            app, audio);
                        const int64_t dist = pos > click_frame
                                                 ? pos - click_frame
                                                 : click_frame - pos;
                        if (dist > best_dist) {
                            best_dist = dist;
                            endpoint  = pos;
                        }
                    }
                    // Deselect — this demote is a DROP former (the shift-click
                    // waveform demote drops the selection by explicit ruling,
                    // unlike the plain drag and the multi-select clicks that
                    // promote). This also dissolves the shift-range anchor,
                    // correct here: this shift interaction is on a DIFFERENT
                    // surface (the waveform) than the marker range select, so no
                    // range is being extended.
                    selection.clear_selection();
                }
                // Sliver rule (mirrors end_region_drag_min_size_check): a span
                // narrower than the drag threshold — a click at the playhead,
                // hand jitter — leaves no region window.
                const double spp = current_samples_per_pixel(app, audio);
                if (spp <= 0.0) return;
                if (std::abs(static_cast<double>(endpoint - click_frame)) / spp
                        < kDragMovedThresholdPx)
                    return;
                // RegionState endpoints are unordered — the painter and Space
                // normalize lo/hi. Damage the waveform (the region paints as a
                // direct overlay), matching the region-drag extend.
                app.region.active  = true;
                app.region.a_frame = endpoint;
                app.region.b_frame = click_frame;
                viewport.invalidate_waveform_area();
                return;
            }
            // THE HALF TEST, first thing on the plain path — before the
            // deselect, because a scrub press must not deselect. Lower half of
            // the waveform area (y >= a.y + a.h/2) = the SCRUB surface: the
            // press drives the SCANNER (not the cursor) — the scanner fields
            // are meaningful only while active (the standing contract), and
            // this gesture is exactly the launches-the-scanner-independently-
            // of-the-cursor consumer that contract anticipated. Every scrub
            // act is KILL-AND-REVIVE (scrub_act_at): a live session stops and
            // a fresh one launches from the clicked frame — never a positional
            // seek inside the old session — with one degenerate skip (same
            // frame as the live scanner -> nothing to re-launch). A refused
            // revive is a silent no-op, exactly Space's conventions. It touches
            // NOTHING else: no selection change, no region change, no cursor
            // write, no follow override, no double-click seed, no other drag
            // arm. Read-only allowed (playback is navigation). The gutter
            // (click_rel_x outside [0, area.w)) returns silently — no launch
            // position exists there, unlike the upper half's
            // deselect-then-return.
            if (y >= area.y + area.h / 2) {
                // Shared scrub body (arm_scrub_at): gutter no-op, clamped frame
                // from the column, one kill-and-revive scrub act, arm the
                // scrub-area drag. The same body serves
                // the marker-text-lane scrub (R3.3).
                arm_scrub_at(click_rel_x);
                return;
            }
            // Upper half: the placement press. The clear runs FIRST, before
            // the gutter early-return below, so an inert-gutter click (no
            // column to seat a playhead) still deselects.
            selection.clear_selection();
            if (click_rel_x < 0 || click_rel_x >= area.w) return;
            // Clamp the click column's frame into the live domain ONCE and
            // pass that same clamped value to both the playhead move and the
            // region arm: move_playhead_to clamps internally, but the region
            // former stored the raw value. At a fractional flush-right zoom the
            // painter-quantized wall (q = nearbyint(spp*W)/W) differs from the
            // click conversion's current_samples_per_pixel, so the last visible
            // column's frame can compute to domain_total — one past
            // [0, domain_total-1], which the display-state validator then
            // clears wholesale (the round-3 "viewport-bounded columns cannot
            // reach the wall" claim is disproven; the formers all clamp now).
            const int64_t sample = clamp_playhead_to_live_domain(
                playhead_frame_at_click_column(app, audio, click_rel_x),
                app, audio);
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
    // Clear any resting region immediately at press: a plain upper-half
    // waveform press dissolves an existing highlight on mouse-down (the wash
    // repaints away now, not at release; a lower-half scrub press never
    // reaches here — it arms no region drag and leaves the region alone). A
    // moved drag rebuilds a fresh region live; a
    // motionless press-release simply leaves it cleared. pre_region above keeps
    // the pre-press extent for the Esc-mid-drag restore. Same dissolve shape as
    // the navigation clears, so it shares clear_region_highlight.
    clear_region_highlight(app, viewport);
}

void GuiInputHandler::on_button_release(GuiMouseButton button, int x,
                                        int y, GuiInputState /*mods*/) {
    if (app.prompt.active) return;
    // F2.1: a left release ending an editor-text drag finalizes the
    // selection (or collapses to a caret) before the modal swallow below.
    if (button == GuiMouseButton::Left && app.editor_text_drag.active) {
        const ActiveEditorText g = active_editor_text(app, audio);
        finalize_editor_text_drag();
        // Double-click seeding: a MOTIONLESS release (a pure click that left a
        // caret, no selection) seeds an editor-text candidate so a second click
        // within the window selects the clicked character class's run (word /
        // punctuation / whitespace). A drag that made a selection seeds nothing.
        if (g.valid && !text_editor::has_selection(*g.ed)) {
            app.double_click = DoubleClickCandidate{
                .surface = DoubleClickSurface::EditorText,
                .time_ms = monotonic_ms(), .press_x = x, .press_y = y,
                .target = -1};
        }
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
        // second click of a double-click. Only the ZOOM-ROW arm seeds
        // (double_click_seed): a motionless ctrl+waveform press-release commits
        // NOTHING — no seed, no selection change (the R3.4 clear is RETIRED,
        // architect 2026-07-23: the ctrl-waveform press is purely the zoom-strip
        // drag).
        if (app.strip_drag.moved) {
            apply_strip_drag_at(x, y, /*final_event=*/true);
            app.double_click = DoubleClickCandidate{};
        } else if (app.strip_drag.double_click_seed) {
            app.double_click = DoubleClickCandidate{
                .surface = DoubleClickSurface::ZoomRow,
                .time_ms = monotonic_ms(), .press_x = x, .press_y = y,
                .target = -1};
        }
        app.strip_drag = StripDragState{};
        // reappear the cursor at the anchor-stem column (y frozen at the press
        // row) — the restore x override the drag set each event.
        end_strip_pointer_capture();
        return;
    }
    if (app.scroll_drag.active) {
        // Alt+drag grab-pan end: the pan applied incrementally during motion, so
        // there is nothing to finalize but the predictor. The continuous pan
        // deferred per-event resyncs, so re-anchor the predictor once here. The
        // pan captured the pointer at its arm, so end the capture (reappear the
        // cursor at the raw traveled virtual_pointer_x_, y frozen at the press
        // row — the pan sets no anchor-stem override); idempotent, so a degraded
        // compositor that never captured is unharmed.
        if (playback.is_playing()) playback.resync_predictor();
        app.scroll_drag = ScrollDragState{};
        end_strip_pointer_capture();
        return;
    }
    if (app.scrub_area_drag.active) {
        // Scrub-area release KEEPS PLAYING — that IS the release semantics:
        // the press/motion scrub acts already launched the current session, so
        // the
        // release just ends the gesture (no capture to end, no finalize, no
        // seed — the scrub never seeds a double-click candidate).
        app.scrub_area_drag = ScrubAreaDragState{};
        return;
    }
    if (app.region_drag.active) {
        // The region is extended live during the drag (see on_motion); a drag
        // that moved rests the region at its final extent. A MOTIONLESS
        // press-release (never crossed the threshold) needs no collapse here:
        // the press already cleared any resting highlight at mouse-down (see
        // arm_region_drag_at), so a plain click leaves the region cleared and
        // there is nothing to do at release but disarm. A jitter drag that
        // crossed the gate but rests a sub-threshold sliver dissolves like a
        // click (end_region_drag_min_size_check).
        app.region_drag = RegionDragState{};
        end_region_drag_min_size_check(app, audio, viewport, selection);
        return;
    }
    if (app.tempo_drag.active) {
        // Tempo-drag release: the final synchronous re-warp already ran on the
        // last committed cent step, so the finalize only settles history (the
        // one undo entry + dirty + the deferred preview trigger, net-change
        // gated inside end_tempo_drag).
        marker_drag.end_tempo_drag();
        return;
    }
    if (app.pending_tempo_drag.active) {
        // The pending tempo drag never crossed the threshold: a pure flag
        // click. The press already single-selected its marker AND seeded the
        // Marker double-click candidate (press-time seeding), so there is
        // nothing to commit or seed — just disarm, exactly like the pending
        // marker drag below.
        app.pending_tempo_drag = PendingTempoDrag{};
        return;
    }
    if (app.trim_drag.active) {
        commit_trim_drag();
        return;
    }
    if (app.pending_trim_drag.active) {
        // The pending trim drag never crossed the threshold: a motionless
        // chip/bridge press. Under the lane-click model that is the trim-lane
        // CLICK (R4.5) — select + highlight the trim window. The pending only
        // arms on the full pair (route_trim_chip_press gates it), so the window
        // exists; the sync takes it. (A crossed pending became app.trim_drag and
        // commits through the branch above; read-only never armed a pending, so
        // this branch is writable-only — the read-only R4.5 click ran at press.)
        app.pending_trim_drag = PendingTrimDrag{};
        sync_highlight_to_trim_window();
        return;
    }
    if (app.pending_marker_drag.active) {
        // The pending marker drag never crossed the threshold: a pure flag
        // click. The press already single-selected its marker AND seeded the
        // Marker double-click candidate (press-time seeding, the one timing
        // for the whole marker surface), so there is nothing to commit or seed
        // — just disarm. (A crossed pending became app.drag — dropping the
        // candidate at the threshold crossing — and commits through the branch
        // below.)
        app.pending_marker_drag = PendingMarkerDrag{};
        return;
    }
    if (!app.drag.active) return;
    marker_drag.commit_drag();
}

// Motion handler. Drives the active pointer gesture: editor-text drag,
// strip-row zoom/pan drag, scrub-area drag (one scrub act per column), trim drag (or
// a pending trim drag arming past the threshold), region-select drag, or
// marker reposition drag (or a pending
// marker drag); with no gesture it recomputes hover at the cursor. The
// marker drag applies the pointer delta to the grabbed marker; the playhead
// follows the grabbed marker unconditionally (apply_drag_motion owns that —
// the arming plain click already landed the playhead on the marker, so the
// drag tows it by construction).
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
    // Dual-axis strip drag (the incremental v6 model; see apply_strip_drag_at).
    // Each event pans by its dx at the live level and zooms by its dy off the
    // live level, pivoting the zoom around the (edge-rebindable) song anchor. The
    // repaint is SYNCHRONOUS (final_event=false): a full rebuild when the level
    // changed, the incremental pan fast-path when only the viewport moved, a true
    // no-op when neither did — affordable because the platform coalesces captured
    // motion to one event per pointer frame. The release runs the one synchronous
    // rebuild plus the predictor resync. A lost button finalizes like release.
    if (app.strip_drag.active) {
        if (!mods.primary_button_held) {     // button lost -> end like release
            if (app.strip_drag.moved) {
                apply_strip_drag_at(mouse_x, mouse_y, /*final_event=*/true);
            }
            // A motionless press ends with NO click action from either origin
            // (the R3.4 ctrl-waveform clear is RETIRED — a motionless
            // ctrl+waveform press-release commits nothing on the clean release
            // too, so this abnormal end matches it for free), and a motionless
            // zoom-row press seeds NOTHING on this abnormal end (unlike the
            // clean release), matching the double_click clear below.
            app.strip_drag = StripDragState{};
            // An abnormal termination (button lost, not a clean release) seeds
            // no double-click candidate and drops any pending one.
            app.double_click = DoubleClickCandidate{};
            end_strip_pointer_capture();
            return;
        }
        // Sub-pixel capture jitter must not promote a click to a drag: while the
        // press has not yet become a drag, apply nothing until the pointer has
        // travelled at least the Chebyshev threshold from the press, leaving the
        // drag armed but unmoved. This gate decides only WHETHER the press
        // becomes a drag — once moved it never re-engages, so dragging back near
        // the press mid-drag has no dead zone. last_x/last_y stay at the press
        // until the crossing, so the crossing event folds the whole accumulated
        // delta since the press and no travel is lost.
        if (!app.strip_drag.moved &&
            std::max(std::abs(mouse_x - app.strip_drag.press_x),
                     std::abs(mouse_y - app.strip_drag.press_y)) <
                kDragMovedThresholdPx) {
            return;
        }
        app.strip_drag.moved = true;
        apply_strip_drag_at(mouse_x, mouse_y, /*final_event=*/false);
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
            end_strip_pointer_capture();     // reappear the cursor (idempotent)
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
    // Scrub-area drag motion (the held lower-half plain press): re-scrub at
    // COLUMN granularity — each column change is one fresh scrub act
    // (scrub_act_at, the same kill-and-revive the press runs): a live session
    // stops and re-launches at the new column's frame, and a DEAD one just
    // revives — the old inert-when-not-playing rule is retired with the
    // keep-alive reseek (revive-if-needed: dragging out of the launchable
    // window silently stops, dragging back in relaunches). No threshold (the
    // press already acted; scrubbing is continuous by design), no capture, no
    // cursor hide. A lost
    // button ends the gesture like release: nothing else, playback continues
    // (release keeps playing — that IS the release semantics).
    if (app.scrub_area_drag.active) {
        viewport.clear_hover_popup();
        if (!mods.primary_button_held) {     // button lost -> end like release
            app.scrub_area_drag = ScrubAreaDragState{};
            return;
        }
        const GuiRect area = waveform_area(app);
        if (area.w <= 0) return;
        int rel = mouse_x - area.x;
        if (rel < 0) rel = 0;
        if (rel >= area.w) rel = area.w - 1;
        if (rel != app.scrub_area_drag.last_col) {
            // The press's clamped-frame spelling (domain conformance).
            const int64_t frame = clamp_playhead_to_live_domain(
                playhead_frame_at_click_column(app, audio, rel), app, audio);
            scrub_act_at(frame);
            app.scrub_area_drag.last_col = rel;
        }
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
            // The motionless chip/bridge press is the trim-lane CLICK (R4.5):
            // run the same select+highlight sync the clean release does, so the
            // same physical click cannot rest differently depending on which
            // path ended it. The pending only arms on the full pair (writable),
            // so the window exists.
            app.pending_trim_drag = PendingTrimDrag{};
            sync_highlight_to_trim_window();
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
    // Motion just continues whatever the press already armed — the
    // home-view gate (active_column_authoring_allowed, plus the tempo
    // drag's own eligibility check) ran once at arm time in
    // on_button_press, so nothing here re-checks view or column; the
    // region drag below is navigation, not authoring, and was never
    // gated. Per-site translation (drag anchor capture, motion delta
    // conversion, hit tests) lives in the handlers below.
    if (app.region_drag.active) {
        viewport.clear_hover_popup();
        // Left button must still be held; if not, the release was lost —
        // end the gesture, resting the region at its current extent (as a
        // clean release would). Modifier changes mid-drag are ignored. A
        // sub-threshold sliver rest dissolves like a click, exactly as the
        // clean release branch does (end_region_drag_min_size_check).
        if (!mods.primary_button_held) {
            app.region_drag = RegionDragState{};
            end_region_drag_min_size_check(app, audio, viewport, selection);
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
        // span survives pan/zoom mid-drag and at rest. Also clamped into the
        // live domain: at a fractional flush-right zoom the painter-quantized
        // wall differs from the click conversion, so the last visible column's
        // frame can land at domain_total — one past [0, domain_total-1] — which
        // the display-state validator would clear wholesale (same rule as the
        // press-site anchor; the round-3 no-clamp provenance is disproven).
        int rel = mouse_x - area.x;
        if (rel < 0) rel = 0;
        if (rel >= area.w) rel = area.w - 1;
        const int64_t far_frame = clamp_playhead_to_live_domain(
            playhead_frame_at_click_column(app, audio, rel), app, audio);
        // Column-change gate: the span (and thus its contained selection)
        // changes only when the far endpoint moves to a new frame. A same-frame
        // motion event (sub-pixel jitter within one column) is a no-op — skip
        // the selection recompute and the repaint. The anchor is fixed for the
        // gesture, so the far endpoint alone decides the span. On the first
        // extend event the arm cleared the region (active == false), so this
        // proceeds and seeds it.
        if (app.region.active && far_frame == app.region.b_frame) return;
        app.region.active  = true;
        app.region.a_frame = app.region_drag.anchor_frame;
        app.region.b_frame = far_frame;
        // Direction A of the selection<->highlight coupling (architect
        // 2026-07-23): the live drag SELECTS every active-column marker inside
        // the span, updating as it extends/shrinks (an emptied span clears;
        // focus = highest contained index). The helper owns the top-strip /
        // timestamp damage and clears the shift-range anchor; the waveform-wash
        // damage stays below.
        const int64_t lo = std::min(app.region.a_frame, app.region.b_frame);
        const int64_t hi = std::max(app.region.a_frame, app.region.b_frame);
        selection.select_contained_in_span(lo, hi);
        viewport.invalidate_waveform_area();
        return;
    }
    // Target-view tempo drag motion: each event re-solves the pointer's
    // target position to a predecessor-tempo candidate and commits changed
    // candidates live with a synchronous re-warp (apply_tempo_drag_motion —
    // vertical motion is ignored there). A lost button finalizes like
    // release, mirroring the marker-drag motion handler.
    if (app.tempo_drag.active) {
        viewport.clear_hover_popup();
        if (!mods.primary_button_held) {
            marker_drag.end_tempo_drag();
            return;
        }
        marker_drag.apply_tempo_drag_motion(mouse_x);
        return;
    }
    // Pending tempo drag (armed by a plain flag press in W + target view on
    // an eligible marker): the tempo drag begins only once the pointer
    // travels past the marker-specific Chebyshev threshold — the SAME
    // kMarkerDragMovedThresholdPx grab slop the reposition drag uses. A lost
    // button before the crossing ends it as a plain click. Placed after the
    // tempo_drag branch above: on the crossing this begins the drag AND
    // applies its first solve inline (the solve is absolute — pointer x ->
    // tempo — so applying at the CURRENT x needs no press-anchor catch-up
    // fold), and does not fall back into that branch this event.
    if (app.pending_tempo_drag.active) {
        if (!mods.primary_button_held) {   // button lost -> just the click
            app.pending_tempo_drag = PendingTempoDrag{};
            viewport.clear_hover_popup();
            return;
        }
        if (std::max(std::abs(mouse_x - app.pending_tempo_drag.press_x),
                     std::abs(mouse_y - app.pending_tempo_drag.press_y)) <
                kMarkerDragMovedThresholdPx) {
            return;   // still a click; leave the pending armed, do nothing
        }
        const int marker = app.pending_tempo_drag.marker;
        app.pending_tempo_drag = PendingTempoDrag{};
        // A moved tempo drag drops any double-click candidate: this press is
        // a drag, not a click of a marker double-click — the same
        // load-bearing clear the pending marker drag's crossing does (the
        // arming press seeded a Marker candidate at press time).
        app.double_click = DoubleClickCandidate{};
        if (!marker_drag.begin_tempo_drag(marker)) {
            viewport.clear_hover_popup();
            return;   // begin refused (eligibility re-check): drop the gesture
        }
        marker_drag.apply_tempo_drag_motion(mouse_x);
        return;
    }
    // Pending marker drag (armed by a plain flag press): the marker was
    // single-selected at press; the reposition begins only once the pointer
    // travels past the marker-specific Chebyshev threshold
    // (kMarkerDragMovedThresholdPx, larger than the strip / region / trim
    // gate). Handled before the hover
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
                kMarkerDragMovedThresholdPx) {
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
        // A moved marker drag drops any double-click candidate: this press is a
        // drag, not a click of a marker double-click. The arming press seeded a
        // Marker candidate (press-time seeding), so this clear is what keeps a
        // moved drag from carrying one — the load-bearing half of seeding at
        // the press.
        app.double_click = DoubleClickCandidate{};
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
    // The delta handed to apply_drag_motion is an ACTIVE-domain frame delta:
    // mouse_frame is the pointer's plain active-domain position — one
    // expression, both views, no inverse map anywhere in its derivation.
    // The displayed-map hops that carry the delta into the source domain
    // live inside apply_drag_motion, which anchors the proposal in the
    // DISPLAYED target domain so the painted flag tracks the pointer 1:1.
    const double mouse_frame = static_cast<double>(app.viewport_start_sample) +
        static_cast<double>(mouse_x - area.x) * spp;
    marker_drag.apply_drag_motion(mouse_frame - app.drag.anchor_mouse_time_frame);
    // Playhead rule: the playhead follows the grabbed marker through the drag
    // inside apply_drag_motion (the arming click landed it on the marker, so
    // the drag tows it by construction — the DragState ruling). apply_drag_motion
    // above already latched app.drag.moved and collapsed the selection onto the
    // grabbed marker on the first real move; nothing further tracks here.
}
