#include "selection.h"

#include "audio.h"
#include "playback.h"
#include "warp_frame_map_view.h"
#include "warp_frame_map.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <vector>

void Selection::repair_last_selected() {
    if (app.last_selected_marker < 0) return;
    if (app.selected_markers.count(app.last_selected_marker)) return;
    if (app.selected_markers.empty()) {
        app.last_selected_marker = -1;
    } else {
        // Pick the largest remaining index in selected_markers (or -1
        // if empty).
        app.last_selected_marker = *app.selected_markers.rbegin();
    }
}

void Selection::set_single_selection(int idx) {
    ++app.selection_gen;   // user reselection: breaks any gesture-coalesce burst
    app.selected_markers.clear();
    if (idx >= 0) app.selected_markers.insert(idx);
    app.last_selected_marker = (idx >= 0) ? idx : -1;
    // A marker-selecting gesture makes Markers the group that
    // Delete / Ctrl+drag act on.
    app.last_sel_group = LastSelGroup::Markers;
    // A fresh single-select in the marker group drops any trim-boundary
    // selection — the two groups are orthogonal, but selecting a marker as
    // the sole selection means trim is no longer selected.
    const bool had_trim = app.trim_begin_selected || app.trim_end_selected;
    app.trim_begin_selected = false;
    app.trim_end_selected   = false;
    viewport.invalidate_top_strip();
    if (had_trim) viewport.invalidate_waveform_area();
}

void Selection::clear_selection() {
    const bool had_trim = app.trim_begin_selected || app.trim_end_selected;
    const bool had_markers =
        !app.selected_markers.empty() || app.last_selected_marker != -1;
    if (!had_trim && !had_markers) return;   // nothing selected anywhere

    ++app.selection_gen;   // user reselection: breaks any gesture-coalesce burst
    app.selected_markers.clear();
    app.last_selected_marker = -1;
    app.trim_begin_selected = false;
    app.trim_end_selected   = false;
    app.last_selected_trim  = 0;
    app.last_sel_group = LastSelGroup::Markers;

    viewport.invalidate_top_strip();
    // Trim stems live in the stem/waveform-area cache, so repaint it when a
    // trim bound was deselected (amber stem returns from kSelected).
    if (had_trim) viewport.invalidate_waveform_area();
}

bool Selection::toggle_selection_membership(int idx) {
    if (idx < 0) return false;
    ++app.selection_gen;   // user reselection: breaks any gesture-coalesce burst
    app.last_sel_group = LastSelGroup::Markers;
    bool added;
    auto it = app.selected_markers.find(idx);
    if (it == app.selected_markers.end()) {
        app.selected_markers.insert(idx);
        app.last_selected_marker = idx;
        added = true;
    } else {
        app.selected_markers.erase(it);
        if (app.last_selected_marker == idx) repair_last_selected();
        added = false;
    }
    viewport.invalidate_top_strip();
    return added;
}

void Selection::sanitize_selection_after_restore(int n) {
    std::set<int> cleaned;
    for (int idx : app.selected_markers) {
        if (idx >= 0 && idx < n) cleaned.insert(idx);
    }
    app.selected_markers = std::move(cleaned);
    if (!app.selected_markers.count(app.last_selected_marker)) {
        app.last_selected_marker = -1;
    }
}

void Selection::cycle_selection(bool forward) {
    const bool phase_reset = (app.active_markers_view == 'P');

    // Render-view cycles the displayed entry's authored-domain snapshot
    // collections; normal authoring cycles the live authoring stores.
    // Mirrors the branch in prune_live_selection. Bind const refs once so
    // the count, frame_of, and is_disabled reads below all index the same
    // vectors.
    const std::vector<GuiWarpMarker>& warp_vec =
        app.render_view.enabled ? app.render_view.warp_markers
                                : app.warpmarkers.markers();
    const std::vector<GuiPhaseResetMarker>& phase_reset_vec =
        app.render_view.enabled ? app.render_view.phase_resets
                                : app.phaseresetmarkers.markers();

    const int n = phase_reset
        ? static_cast<int>(phase_reset_vec.size())
        : static_cast<int>(warp_vec.size());
    // No early return on an empty marker list: trim bounds can still be cycle
    // stops. frame_of / is_disabled below are only invoked for indices in
    // [0, n), so n == 0 simply yields no marker candidate.

    // Helper to read frame-of-index in the active domain. Source view:
    // marker source-frame == active-domain frame (identity). Target and
    // render view: forward-translate through the display context (live
    // map, or snapshot map) so frame_of values are
    // comparable to playhead_cursor_sample / viewport_start_sample below.
    auto frame_of = [&](int i) -> int64_t {
        int64_t src_f;
        if (phase_reset) {
            src_f = static_cast<int64_t>(std::nearbyint(
                phase_reset_vec[i].time_frame));
        } else {
            src_f = static_cast<int64_t>(std::nearbyint(
                warp_vec[i].time_frame));
        }
        return source_frame_to_active_domain(app, audio, src_f);
    };

    // Disabled-skip predicate. Warp side respects label_ref cascade via
    // effective_disabled; phase reset has no cascade and reads the bool. In
    // render view a row whose window-axis image falls outside the entry wav is
    // absent from the display, so it is not a Tab stop either — fold the
    // membership rule (render_view_position_in_window) in here, the one hook
    // every scan below already honors (frame scan, in-group step,
    // first_marker_at_ph). Without it a pre-window marker (negative image) or a
    // past-window one could be selected by the frame scan.
    auto is_disabled = [&](int i) -> bool {
        if (app.render_view.enabled) {
            const int64_t src_f = phase_reset
                ? phase_reset_vec[i].time_frame
                : warp_vec[i].time_frame;
            if (!render_view_position_in_window(app, src_f)) return true;
        }
        if (phase_reset) {
            return phase_reset_vec[i].disabled;
        }
        return effective_disabled(warp_vec, i);
    };

    // The playhead frame is the sole cycle anchor. Strict frame inequalities
    // in the scan below prevent re-landing on the stop we are standing on;
    // markers and trim bounds sharing one active-domain frame are traversed
    // by the in-group step so every member is Tab-reachable — legal stacks
    // exist (loaded-but-unresolved states before their defect series runs;
    // cross-column and trim-on-marker ties, which impair no picking).
    // Disabled markers are skipped as if absent from the active mode's list.
    const int64_t ph_f = app.playhead_cursor_sample;

    // Trim stops are AUTHORING-only: trim is a project-level authoring tool,
    // orthogonal to the marker list, and render view displays the rendered
    // artifact with no trim overlay and no trim pick — so its stops are gated
    // off entirely there (has_b / has_e forced false). The authoring views walk
    // the live app.trim pair; each set bound has one active-domain frame,
    // projected through source_frame_to_active_domain exactly as the marker
    // frames are. The group-order comment below (begin bound, end bound, then
    // markers; the half-open [begin, end) rationale) applies to the authoring
    // views.
    auto bound_frame = [&](char which) -> int64_t {
        const int64_t src_f = (which == 'B') ? app.trim.begin_frame
                                             : app.trim.end_frame;
        return source_frame_to_active_domain(app, audio, src_f);
    };
    const bool has_b = !app.render_view.enabled && app.trim.has_begin;
    const bool has_e = !app.render_view.enabled && app.trim.has_end;
    const int64_t bf = has_b ? bound_frame('B') : 0;
    const int64_t ef = has_e ? bound_frame('E') : 0;

    // Group order within one active-domain frame, forward: begin bound, end
    // bound, then markers by ascending index; backward is the exact reverse
    // (markers descending, end bound, begin bound). This expresses the
    // authored render window as half-open [begin, end): walking forward you
    // meet each wall before what lies beyond it — at the begin bound the
    // markers inside the window, at the end bound the markers whose authored
    // effect falls outside. A warp marker exactly at the trim begin governs
    // the deliverable's opening (the start anchor sits on its own segment
    // line), so its effect is inside; a warp marker exactly at the trim end is
    // excluded by the slicer's strict source filter, and a phase reset at
    // either bound typically drops. So a bound precedes its coincident markers
    // in the forward walk at both walls. Uniform tie rule: at equal frames
    // forward Tab lands on the bound before any marker, and backward
    // Shift+Tab therefore lands on markers before the bound. Every press
    // selects exactly one stop — a bound is an ordinary stop, never lit
    // together with a marker.

    // Current stop, checked bound-first: the bound-first check keys the
    // current stop off the group owner (LastSelGroup::Trim means a bound, not
    // a marker, is selected). A bound is the current stop only when its group
    // owns the selection, its named side is set and selected, and it sits on
    // the playhead frame.
    char cur_bound  = 0;    // 'B' / 'E' / 0
    int  cur_marker = -1;
    if (app.last_sel_group == LastSelGroup::Trim) {
        if (app.last_selected_trim == 'B' && has_b &&
            app.trim_begin_selected && bf == ph_f) {
            cur_bound = 'B';
        } else if (app.last_selected_trim == 'E' && has_e &&
                   app.trim_end_selected && ef == ph_f) {
            cur_bound = 'E';
        }
    }
    if (cur_bound == 0) {
        const int last = app.last_selected_marker;
        if (last >= 0 && last < n && frame_of(last) == ph_f) cur_marker = last;
    }

    // Lowest-index non-disabled marker sharing the playhead frame. Markers are
    // frame-sorted, so the group at ph_f is a contiguous run.
    auto first_marker_at_ph = [&]() -> int {
        for (int i = 0; i < n; ++i) {
            const int64_t f = frame_of(i);
            if (f < ph_f) continue;
            if (f > ph_f) break;
            if (!is_disabled(i)) return i;
        }
        return -1;
    };

    // In-group step, tried before the frame scan. When the previous Tab landed
    // on a stop, the caller synced the playhead onto it, so that stop's frame
    // equals ph_f (a playhead moved elsewhere by a click breaks the equality
    // and disables this branch naturally). Advance one place within the shared
    // frame in the cycle direction, following the group order above.
    int  land_marker = -1;
    char land_bound  = 0;
    if (forward) {
        if (cur_bound == 'B') {
            if (has_e && ef == ph_f) land_bound = 'E';
            else                     land_marker = first_marker_at_ph();
        } else if (cur_bound == 'E') {
            land_marker = first_marker_at_ph();
        } else if (cur_marker >= 0) {
            for (int i = cur_marker + 1; i < n; ++i) {
                if (frame_of(i) != ph_f) break;   // frame-sorted: group ends
                if (is_disabled(i)) continue;
                land_marker = i; break;
            }
            // Bounds at ph_f precede markers in group order; they are never
            // revisited going forward (the frame scan's strict > excludes them).
        }
    } else {
        if (cur_marker >= 0) {
            for (int i = cur_marker - 1; i >= 0; --i) {
                if (frame_of(i) != ph_f) break;
                if (is_disabled(i)) continue;
                land_marker = i; break;
            }
            if (land_marker < 0) {
                // Below the lowest marker in the group, reverse order reaches
                // the end bound, then the begin bound.
                if (has_e && ef == ph_f)      land_bound = 'E';
                else if (has_b && bf == ph_f) land_bound = 'B';
            }
        } else if (cur_bound == 'E') {
            if (has_b && bf == ph_f) land_bound = 'B';
        }
        // cur_bound == 'B' backward: nothing precedes it in the group; fall to
        // the frame scan.
    }

    // Frame scan: nearest stop strictly past the playhead in the walk
    // direction. Markers are frame-sorted, so the first in-direction hit is the
    // nearest. Each set trim bound is weighed independently.
    if (land_marker < 0 && land_bound == 0) {
        int     marker_sel   = -1;
        int64_t marker_frame = 0;
        if (forward) {
            for (int i = 0; i < n; ++i) {
                if (frame_of(i) > ph_f && !is_disabled(i)) {
                    marker_sel = i; marker_frame = frame_of(i); break;
                }
            }
        } else {
            for (int i = n - 1; i >= 0; --i) {
                if (frame_of(i) < ph_f && !is_disabled(i)) {
                    marker_sel = i; marker_frame = frame_of(i); break;
                }
            }
        }

        char    trim_sel   = 0;   // 'B' / 'E' / 0
        int64_t trim_frame = 0;
        auto consider = [&](char which, bool has, int64_t f) {
            if (!has) return;
            const bool in_dir = forward ? (f > ph_f) : (f < ph_f);
            if (!in_dir) return;
            // Nearer in-direction wins. Crossed bounds are just two stops in
            // time order — each is weighed at its own frame with no role
            // ordering. On a bound-vs-bound tie (equal authored bounds, or
            // target-view compression mapping both to one frame) the group
            // order decides: forward keeps begin (considered first),
            // backward takes end (>= lets the later-considered win).
            const bool closer = (trim_sel == 0) ||
                (forward ? (f < trim_frame) : (f >= trim_frame));
            if (closer) { trim_sel = which; trim_frame = f; }
        };
        consider('B', has_b, bf);
        consider('E', has_e, ef);

        const bool have_marker = (marker_sel >= 0);
        const bool have_trim   = (trim_sel != 0);
        if (have_marker || have_trim) {
            bool take_trim;
            if (have_marker && have_trim) {
                if (trim_frame == marker_frame) {
                    // Marker and bound share this frame. Forward lands on the
                    // bound (begin before end, already resolved in trim_sel);
                    // backward lands on the marker (the scan's highest index
                    // at that frame).
                    take_trim = forward;
                } else {
                    take_trim = forward ? (trim_frame < marker_frame)
                                        : (trim_frame > marker_frame);
                }
            } else {
                take_trim = have_trim;
            }
            if (take_trim) land_bound  = trim_sel;
            else           land_marker = marker_sel;
        }
    }

    if (land_marker < 0 && land_bound == 0) return;   // nothing ahead

    // A Tab / Shift+Tab that actually moves the focus is a user reselection:
    // break any gesture-coalesce burst. Bumped here, past the nothing-ahead
    // guard, so a no-op cycle does not touch it. The marker branch below
    // delegates to set_single_selection, which bumps again — harmless, the
    // generation is monotonic and compared only for equality.
    ++app.selection_gen;

    // Selection only. Viewport positioning is owned entirely by the sole
    // caller (cycle_marker_focus_with_recenter), which centers the focused
    // stop in one write. A scroll-into-view here would be a redundant
    // intermediate viewport write — overridden by that centering in the same
    // keypress — and the resulting damage, accumulated against a non-final
    // viewport, is what produced the outline-blink / cursor-hop artifact.
    if (land_bound != 0) {
        // Single-select this trim bound, mirroring select_trim_boundary's
        // non-additive branch: this bound on, the other off, marker selection
        // dropped, group set to Trim.
        app.trim_begin_selected  = (land_bound == 'B');
        app.trim_end_selected    = (land_bound == 'E');
        app.last_selected_trim   = land_bound;
        app.selected_markers.clear();
        app.last_selected_marker = -1;
        app.last_sel_group       = LastSelGroup::Trim;
        viewport.invalidate_top_strip();
        viewport.invalidate_waveform_area();
    } else {
        set_single_selection(land_marker);
    }
}

void Selection::select_next_marker() { cycle_selection(true);  }
void Selection::select_prev_marker() { cycle_selection(false); }

void Selection::prune_live_selection() {
    int n = 0;
    if (app.render_view.enabled) {
        n = (app.active_markers_view == 'P')
            ? static_cast<int>(app.render_view.phase_resets.size())
            : static_cast<int>(app.render_view.warp_markers.size());
    } else {
        n = (app.active_markers_view == 'P')
            ? static_cast<int>(app.phaseresetmarkers.markers().size())
            : static_cast<int>(app.warpmarkers.markers().size());
    }
    for (auto it = app.selected_markers.begin();
         it != app.selected_markers.end();) {
        if (*it < 0 || *it >= n) {
            it = app.selected_markers.erase(it);
        } else {
            ++it;
        }
    }
    if (app.last_selected_marker < 0 ||
        app.last_selected_marker >= n ||
        !app.selected_markers.count(app.last_selected_marker)) {
        app.last_selected_marker =
            app.selected_markers.empty()
                ? -1
                : *app.selected_markers.rbegin();
    }
}

void Selection::sync_playhead_to_last_selected(bool edge_follow) {
    // Callers are authoring mutations and never run in render view; this guard
    // keeps the authoring-store choice locally safe.
    if (app.render_view.enabled) return;

    const int sr = audio.sample_rate();
    if (sr <= 0) return;
    const int last = app.last_selected_marker;
    if (last < 0) return;

    int64_t src_sample = 0;
    if (app.active_markers_view == 'P') {
        const auto& tv = app.phaseresetmarkers.markers();
        if (last >= static_cast<int>(tv.size())) return;
        src_sample = static_cast<int64_t>(std::nearbyint(
            tv[last].time_frame));
    } else {
        const auto& mv = app.warpmarkers.markers();
        if (last >= static_cast<int>(mv.size())) return;
        src_sample = static_cast<int64_t>(std::nearbyint(
            mv[last].time_frame));
    }
    // Target view: the marker time_frame is source-domain but the
    // playhead is active-domain. Forward-translate so the playhead
    // lands at the marker's displayed (target-frame) position.
    const int64_t target_sample =
        source_frame_to_active_domain(app, audio, src_sample);
    if (edge_follow) {
        // Ctrl+Left/Right marker nudge: the marker stepped one pixel, so move
        // the playhead to it through the same edge-follow path bare Left/Right
        // uses (move_playhead_to), scrolling the viewport at most one pixel to
        // keep the marker just inside the edge. The default path keeps
        // jump_playhead_to's center-on-offscreen, which suits a jump to a
        // possibly-distant marker (undo, navigate) but is jarring for a nudge.
        viewport.move_playhead_to(target_sample);
    } else {
        jump_playhead_to(target_sample);
    }
}

void Selection::jump_playhead_to(int64_t target_sample) {
    // Playhead domain clamp, mirroring move_playhead_to exactly (same
    // live_total_frames read; the domain ruling lives there): a jump onto
    // trim end — legal at total — rests at total - 1.
    const int64_t live_total = live_total_frames(app, audio);
    if (target_sample < 0) target_sample = 0;
    if (live_total > 0 && target_sample >= live_total) {
        target_sample = live_total - 1;
    }
    app.playhead_cursor_sample = target_sample;

    const int64_t visible = samples_visible(app, audio);
    const bool offscreen =
        target_sample <  app.viewport_start_sample ||
        target_sample >= app.viewport_start_sample + visible;
    if (offscreen) {
        app.viewport_start_sample = target_sample - visible / 2;
        clamp_viewport_start(app, audio);
    }
    if (playback.is_playing()) playback.resync_predictor();
}

void Selection::select_trim_bound(char which) {
    const bool has = (which == 'B') ? app.trim.has_begin : app.trim.has_end;
    if (!has) return;
    const int sr = audio.sample_rate();
    if (sr <= 0) return;

    ++app.selection_gen;   // user reselection: breaks any gesture-coalesce burst

    // The bound is the sole selection (group Trim); any marker selection is
    // dropped so exactly one thing is selected, matching the Tab walk where a
    // bound is an ordinary single stop.
    app.trim_begin_selected  = (which == 'B');
    app.trim_end_selected    = (which == 'E');
    app.last_selected_trim   = which;
    app.selected_markers.clear();
    app.last_selected_marker = -1;
    app.last_sel_group       = LastSelGroup::Trim;
    viewport.invalidate_top_strip();
    viewport.invalidate_waveform_area();
}
