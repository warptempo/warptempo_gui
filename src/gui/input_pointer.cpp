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
// gated — the live mid-drag extension paints slivers freely.
void end_region_drag_min_size_check(AppState& app, const GuiAudio& audio,
                                    Viewport& viewport) {
    if (!app.region.active) return;
    const double spp = current_samples_per_pixel(app, audio);
    if (spp <= 0.0) return;   // no geometry -> leave the region as-is
    const double span_px =
        std::abs(static_cast<double>(app.region.a_frame - app.region.b_frame)) /
        spp;
    if (span_px < kDragMovedThresholdPx) {
        app.region = RegionState{};
        viewport.invalidate_waveform_area();
    }
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
    if (app.tempo_drag.active) return;
    if (app.trim_drag.active) return;

    // Mouse authoring is home-view gated like the keyboard: a plain flag
    // press in W+target arms the TEMPO drag on an eligible marker instead
    // of the reposition drag (marker_drag.tempo_drag_predecessor; an
    // ineligible press just selects) — the pointer half of the home-view
    // binding's one tempo exception — while placement arming everywhere
    // else is gated by active_column_authoring_allowed, off-home selecting
    // but arming nothing. The click-playhead / region-drag family below is
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

        // Top-strip clicks stop playback first: they select markers and can
        // close an open flag editor, and continuing audio during authoring /
        // text editing is the wrong default. Waveform clicks keep playback
        // alive — the per-press reseek to the click sample happens at the
        // playhead-drag press site below, gated on was_playing && sample !=
        // playhead_at_entry. Capture the entry state up front so the downstream
        // branches see the same snapshot.
        const bool was_playing = playback.is_playing();
        const int64_t playhead_at_entry = app.playhead_cursor_sample;
        if (inside_top) playback_lifecycle.stop_playback_if_playing();

        // Clicks in iter/BPM mode route through the unified marker
        // hit-test below.

        // Only presses inside the waveform or the top strip do anything.
        if (!inside_waveform && !inside_top) return;

        // Unified marker hit, computed ONLY on the path that consumes it. The
        // marker is ONE pointer item — flag shape OR rendered lane run
        // (marker_hit_at, the shared resolver in render.cpp the hover recompute
        // also reads) — and the TOP-STRIP hit feeds the plain/Shift
        // marker-click branches (plain = single-select + double-click seed /
        // consume + arm the pending marker drag on the flag part, Shift = toggle
        // membership) and the alt-exact land below, so it is resolved once here.
        // The editor lifecycle block above already closed any open flag editor,
        // so the lane run this resolves is the committed (non-editor) run. The
        // WAVEFORM never SELECTS a marker — a plain press is deselect-all +
        // playhead placement + region-drag arm, a Shift press a no-op — so no
        // marker scan runs on the waveform at all (the invisible stem is not a
        // grab target). Trim bounds are grabbed only by their top-strip chips /
        // the inter-chip bridge on a PLAIN chip-row press (route_trim_chip_press
        // below); a click over a bound's waveform stem is an ordinary waveform
        // click (the stem grab retired), so no trim hit test runs on the
        // waveform at all.
        MarkerHit mh;
        if (inside_top) mh = marker_hit_at(app, audio, x, y);

        // Alt-exact left press: on a top-strip MARKER (flag shape or lane run —
        // the one unified item) it LANDS — the THIRD land-onto-marker route
        // beside the Tab family and `c` — and on the waveform it arms the
        // captured grab-pan; alt-exact anywhere else does nothing further HERE.
        // On a markerless TOP-STRIP spot that "nothing" is not a strict no-op in
        // the playback sense: the standing top-strip playback stop above (every
        // top-strip press stops playback) has already run, so playback is
        // halted even though no land/pan fires.
        //
        // The LAND: single-select the hit marker (the plain-click selection
        // half) AND move the playhead exactly onto it, with NO viewport move —
        // the sole difference from Tab (which recenters) and `c` (which
        // re-zooms and recenters), so the view holds perfectly still while
        // the playhead seats. The action fires entirely AT THE PRESS and arms
        // NOTHING — alt+drag from a marker is a strict no-op by architect
        // ruling: subsequent motion with the button held is inert. Both
        // columns, both views; navigation-class (selection + playhead, no
        // authoring), so read-only tabs allow it.
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
            if (inside_top && mh.index >= 0) {
                const int hit = mh.index;
                // Tab-family symmetry: a land route stops playback first (the
                // top-strip stop above already ran, so this is a no-op here;
                // the land stays self-contained).
                playback_lifecycle.stop_playback_if_playing();
                selection.set_single_selection(hit);
                // The two-step placement basis the Tab family lands with
                // (source_frame_to_active_domain then
                // clamp_playhead_to_live_domain), against the active column's
                // store, so this placement is exactly coincident for a
                // subsequent nudge/drag ride.
                int64_t src_frame = 0;
                if (app.active_markers_view == 'P') {
                    const auto& tv = app.phaseresetmarkers.markers();
                    if (hit >= static_cast<int>(tv.size())) return;
                    src_frame = tv[hit].time_frame;
                } else {
                    const auto& mv = app.warpmarkers.markers();
                    if (hit >= static_cast<int>(mv.size())) return;
                    src_frame = mv[hit].time_frame;
                }
                int64_t sample =
                    source_frame_to_active_domain(app, audio, src_frame);
                sample = clamp_playhead_to_live_domain(sample, app, audio);
                // Direct cursor write mirroring jump_playhead_to_focused_marker's
                // non-recenter part — NOT move_playhead_to, whose keep-visible
                // edge-align could scroll for a half-offscreen flag, and the
                // ruling is NO viewport write of any kind (the playhead may
                // rest at a slightly offscreen column when the clicked flag
                // hung half off the edge — accepted).
                const double old_px = playhead_pixel_x(app, audio);
                app.playhead_cursor_sample = sample;
                // Navigation land: dissolve a resting region — the playhead
                // just jumped onto the marker, off any prior auditioning span.
                clear_region_highlight(app, viewport);
                viewport.invalidate_playhead_columns(
                    old_px, playhead_pixel_x(app, audio));
                viewport.invalidate_timestamp_area();
                return;
            }
            if (inside_waveform) {
                app.scroll_drag = ScrollDragState{};
                app.scroll_drag.active = true;
                app.scroll_drag.last_x = x;
                begin_strip_pointer_capture();
            }
            return;
        }

        // Ctrl-exact left press on the waveform arms the dual-axis strip drag —
        // the SAME gesture the zoom row arms (StripDragState / apply_strip_drag_at),
        // triggered here for reach: it gets the cursor capture ("swallow"), the
        // anchor stem, the edge clamp, and dual-axis zoom+pan for free. The arm is
        // byte-identical to the zoom-row arm (anchor_sample from the click song
        // position, press/last seeds, begin_strip_pointer_capture), diverging at
        // ONE point — double_click_seed=false, so a motionless ctrl+waveform
        // press-release seeds no ZoomRow double-click candidate (that affordance
        // stays zoom-row-only). Navigation-class: allowed in read-only, never
        // touches the playhead or selection. Ctrl-exact anywhere else (the top
        // strip included) is a strict no-op below.
        if (ctrl && !alt && !shift) {
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

        // Strict modifier matching: the marker reposition arm lives on the plain
        // flag press and trim's chip/bridge drags on the plain chip-row press, so
        // every remaining modified combination — Ctrl+Alt (now a strict no-op),
        // Ctrl+Shift, Shift+Alt, Ctrl+Alt+Shift, ... — no-ops here. Only a plain
        // or Shift-on-the-top-strip base press proceeds (Shift adjusts the
        // marker selection). The Alt+wheel pan and the Alt keyboard chords are untouched
        // (separate handlers).
        if (ctrl || alt) return;

        // Plain or Shift press. In the waveform area a plain press clears the
        // marker selection (deselect-all), places the playhead at the clicked
        // column, and arms the region-select drag — it never SELECTS a marker; a
        // Shift press on the waveform is a strict no-op. In the top strip a plain
        // CHIP-ROW press arms a trim chip/bridge drag (claimed ahead of the
        // marker select); otherwise (a marker click on EITHER part — flag shape
        // or lane run) selection is the whole interface, BOTH views: a plain
        // click single-selects and, on the FLAG part only, ARMS a pending marker
        // drag (moves the marker if the pointer crosses the threshold, else a
        // pure click); Shift+click toggles multi-select membership only. Neither
        // moves the playhead — the land routes are the Tab family, `c`, and the
        // alt-exact marker click above (and a drag only RIDES a playhead already
        // exactly on the grabbed marker).
        if (inside_top) {
            // Plain unmodified chip-row press arms a trim chip/bridge drag,
            // claimed BEFORE the marker single-select. The chip row, the marker
            // text lane, and the flag lane are disjoint y-bands, so this
            // contends with nothing: a marker-part press yields no chip/bridge
            // claim and falls to the marker handling. Shift never touches trim
            // (a chip/bridge stays transparent to it) — it falls to the toggle.
            // A claim (armed or read-only) returns without touching the
            // selection.
            if (!shift && route_trim_chip_press(x, y)) return;
            if (mh.index >= 0) {
                const int hit = mh.index;
                if (shift) {
                    // Shift+click toggles membership; it never arms a drag.
                    // Allowed in read-only (selection is navigation).
                    selection.toggle_selection_membership(hit);
                } else {
                    // Plain marker click single-selects (both views; W's
                    // click-to-edit is retired — the editor now opens on Enter or
                    // this double-click). Selection is navigation, allowed in
                    // read-only.
                    selection.set_single_selection(hit);
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
                        // source view), select and arm nothing — the silent
                        // read-only convention, marker motion / tempo authoring
                        // being authoring.
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
            }
            return;
        }

        // Waveform-area press: marker-blind for SELECTION (it never SELECTS a
        // hit marker — the invisible stem is not a grab target), but a plain
        // press CLEARS the selection (the deselect-all: a waveform click
        // dismisses the marker selection, the Ableton behaviour). The press
        // consults no marker hit (marker_hit_at runs only for top-strip
        // presses). Then it drops the playhead at the clicked column (no
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
        // (double_click_seed): a ctrl+waveform strip drag shares this branch but
        // carries the double-click nowhere, so a motionless one commits and seeds
        // nothing.
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
        end_region_drag_min_size_check(app, audio, viewport);
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
        // chip-row press. Trim has no click action and the press mutated
        // nothing, so there is nothing to commit — just disarm. (A crossed
        // pending became app.trim_drag and commits through the branch above.)
        app.pending_trim_drag = PendingTrimDrag{};
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
// strip-row zoom/pan drag, trim drag (or a pending trim drag arming past the
// threshold), region-select drag, or marker reposition drag (or a pending
// marker drag); with no gesture it recomputes hover at the cursor. The
// marker drag applies the pointer delta to the grabbed marker; a playhead
// parked off the marker is not towed (only a playhead exactly coincident at
// grab RIDES the marker — apply_drag_motion owns that; the land routes are
// the Tab family, `c`, and the alt-exact flag click).
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
            if (app.strip_drag.moved)
                apply_strip_drag_at(mouse_x, mouse_y, /*final_event=*/true);
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
            end_region_drag_min_size_check(app, audio, viewport);
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
    // Playhead rule: a playhead parked OFF the grabbed marker stays parked —
    // an upstream audition point survives the move — while one exactly on the
    // marker at grab rode along inside apply_drag_motion (the coincident-ride
    // ruling at DragState). apply_drag_motion above already latched
    // app.drag.moved and collapsed the selection onto the grabbed marker on the
    // first real move; nothing further tracks here.
}
