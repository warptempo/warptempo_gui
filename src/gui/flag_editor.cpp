#include "flag_editor.h"

#include "frame_format.h"

#include "target_render.h"

#include "audio.h"
#include "phaseresetmarkers.h"
#include "render.h"
#include "text_editor.h"
#include "time_format.h"
#include "warp_frame_map_view.h"
#include "warp_frame_map.h"
#include "warpmarkers_ops.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

// Strict signed two-decimal parse (sign, >=1 integer digit, '.',
// exactly two fraction digits). Leading/trailing ASCII whitespace is
// trimmed first so the bracket's `, ` separator round-trips. Lifted from
// the retired commit_iter_edit lambda.
bool parse_signed_2dp(const std::string& raw, double& out) {
    size_t a = 0, b = raw.size();
    while (a < b && std::isspace(static_cast<unsigned char>(raw[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(raw[b - 1]))) --b;
    const std::string v = raw.substr(a, b - a);
    if (v.size() < 4) return false;
    if (v[0] != '+' && v[0] != '-') return false;
    const auto dot = v.find('.', 1);
    if (dot == std::string::npos) return false;
    if (dot == 1) return false;
    if (v.size() - dot - 1 != 2) return false;
    for (size_t i = 1; i < v.size(); ++i) {
        if (i == dot) continue;
        if (!std::isdigit(static_cast<unsigned char>(v[i]))) return false;
    }
    try { out = std::stod(v); }
    catch (...) { return false; }
    return true;
}

// Extract the inline iteration bracket from a flag payload edited
// under the widened grammar. Searches for the `+[` segment after the
// tempo token; on match, removes `+[ ... ]` from `payload` and writes the
// parsed bounds (lo <= hi) to `lo_out`/`hi_out`. The all-zero blank
// (`+[+0.00, +0.00]`) and an absent bracket both yield NaN (clear). The
// FlagPayload tempo/scale/label vocabulary never produces a `+`, so `+[`
// is an unambiguous marker. Returns false on a malformed bracket (caller
// red-flashes); true otherwise.
bool extract_iter_bracket(std::string& payload, double& lo_out, double& hi_out) {
    const double kNaN = std::numeric_limits<double>::quiet_NaN();
    lo_out = kNaN;
    hi_out = kNaN;
    const auto open = payload.find("+[");
    if (open == std::string::npos) return true;          // absent → clear
    const auto close = payload.find(']', open + 2);
    if (close == std::string::npos) return false;        // unterminated
    const std::string inner = payload.substr(open + 2, close - (open + 2));
    const auto comma = inner.find(',');
    if (comma == std::string::npos) return false;
    double lo, hi;
    if (!parse_signed_2dp(inner.substr(0, comma), lo)) return false;
    if (!parse_signed_2dp(inner.substr(comma + 1), hi)) return false;
    if (lo > hi) return false;
    payload.erase(open, close - open + 1);
    // All-zero blank is the cleared state, not a zero-width sweep.
    if (lo != 0.0 || hi != 0.0) {
        lo_out = lo;
        hi_out = hi;
    }
    return true;
}

} // namespace

// Flag-editor cluster. Method bodies map onto the original main.cpp
// lambdas via these mechanical rewrites:
//
//   push_undo                      → undo.push_undo
//   recompute_dirty                → undo.recompute_dirty
//   invalidate_top_strip           → viewport.invalidate_top_strip
//   invalidate_waveform_area       → viewport.invalidate_waveform_area
//   invalidate_timestamp_area      → viewport.invalidate_timestamp_area
//   clear_hover_popup              → viewport.clear_hover_popup
//   exit_top_flag_edit_no_commit   → this->exit_top_flag_edit_no_commit
//   build_locked_prefix            → this->build_locked_prefix
//
// Free-function calls (text_editor::*, iter_popup_eligible_marker,
// bpm_popup_eligible_marker, format_iter_bracket_inline,
// format_bpm_bracket_text, flag_text_for_marker, flag_text_iter,
// warpmarkers_internal::parse_single_canonical_line, parse_bpm_bracket,
// effective_disabled) keep their original spelling — the popup helpers
// moved from main.cpp's anonymous namespace into warpmarkers.h alongside
// effective_disabled, so this TU sees them via #include "warpmarkers.h".

// Build the locked-prefix string for `m`. This is the DISPLAY rendering,
// not the serializer's bytes: the serializer now writes the frame double
// (frame_format.h), while the prefix keeps the human-readable MM:SS.mmm
// form derived as format_timestamp(frame / sample_rate). The editor
// renders this prefix outside the editable rect (left-anchored at the
// marker column); the pipe is part of the prefix but visually anchors to
// the marker line. Because the prefix is display-only, the commit path
// assembles its parse candidate from the marker's own fields in
// serializer form rather than from these bytes.
std::string GuiFlagEditor::build_locked_prefix(const GuiWarpMarker& m) {
    std::string out;
    if (m.disabled) out += '#';
    const double sr_d = static_cast<double>(audio.sample_rate());
    out += format_timestamp(sr_d > 0.0 ? m.time_frame / sr_d : 0.0);
    out += '|';
    return out;
}

void GuiFlagEditor::exit_top_flag_edit_no_commit() {
    if (!text_editor::is_active(app.top_flag_editor)) return;
    text_editor::deactivate(app.top_flag_editor);
    viewport.invalidate_top_strip();
}

// Shared core for the enter-editor flows. Wrappers below
// (enter_top_flag_edit, enter_bpm_edit) own the kind-specific eligibility
// gates and seed-text builders, then delegate here. `iter_grammar` widens
// the FlagPayload editor's accepted vocabulary/cap for the inline
// iteration bracket. `text_left_x < 0` falls back to
// flag_pending_text_left_x(app, audio, idx) — that path serves the
// top-flag editor whose layout is computed on the fly; the popup
// editors get the value from the click hit-test and pass it in.
void GuiFlagEditor::enter_text_edit(int idx,
                                    text_editor::Kind kind,
                                    std::string locked_prefix,
                                    std::string initial_pending,
                                    double click_x,
                                    double text_left_x,
                                    bool iter_grammar) {
    if (idx < 0) return;
    const auto& mv = app.warpmarkers.markers();
    if (idx >= static_cast<int>(mv.size())) return;

    const bool same_target =
        text_editor::is_active(app.top_flag_editor) &&
        app.top_flag_editor.kind == kind &&
        app.top_flag_editor.target == idx;

    if (same_target) {
        // Re-click on the active editor: update cursor only,
        // preserve pending text and any in-progress state.
        if (click_x >= 0.0) {
            const double advance = monospace_advance();
            const double text_left = (text_left_x >= 0.0)
                ? text_left_x
                : flag_pending_text_left_x(app, audio, idx);
            if (advance > 0.0 && text_left >= 0.0) {
                app.top_flag_editor.cursor_pos =
                    text_editor::byte_index_from_click_x(
                        click_x, text_left, advance,
                        static_cast<int>(
                            app.top_flag_editor.pending.size()));
                app.top_flag_editor.selection_anchor = -1;
            }
        }
        viewport.invalidate_top_strip();
        return;
    }

    // Target-switching path. Centralize selection + playhead update
    // here so every call path (in-edit-active switch and pre-edit
    // plain click) keeps the marker-column outline and the rest of
    // the UI in sync with the new editor target.
    selection.set_single_selection(idx);
    {
        const int64_t src_sample = static_cast<int64_t>(std::nearbyint(
            mv[idx].time_frame));
        // Target view: marker time_frame is source-domain; playhead
        // is active-domain. Forward-translate so the playhead lands on
        // the marker's displayed (target-frame) position.
        const int64_t sample = source_frame_to_active_domain(app, audio, src_sample);
        viewport.move_playhead_to(sample);
    }

    // Discard any prior edit silently before switching targets.
    if (text_editor::is_active(app.top_flag_editor) &&
        app.top_flag_editor.target != idx) {
        text_editor::deactivate(app.top_flag_editor);
    }
    text_editor::enter(
        app.top_flag_editor, idx,
        std::move(locked_prefix),
        std::move(initial_pending),
        kind,
        iter_grammar);

    if (click_x >= 0.0) {
        const double advance = monospace_advance();
        const double text_left = (text_left_x >= 0.0)
            ? text_left_x
            : flag_pending_text_left_x(app, audio, idx);
        if (advance > 0.0 && text_left >= 0.0) {
            app.top_flag_editor.cursor_pos =
                text_editor::byte_index_from_click_x(
                    click_x, text_left, advance,
                    static_cast<int>(
                        app.top_flag_editor.pending.size()));
        }
    }

    viewport.clear_hover_popup();
    viewport.invalidate_top_strip();
}

void GuiFlagEditor::enter_top_flag_edit(int idx, double click_x) {
    if (idx < 0) return;
    const auto& mv = app.warpmarkers.markers();
    if (idx >= static_cast<int>(mv.size())) return;
    // In iteration mode the whole-flag editor opens over the
    // bracketed flag (seed = the iteration-aware composed text) and runs
    // the widened grammar. Eligibility mirrors the display gate so pass /
    // label_ref flags edit as plain canonical lines even with iter on.
    const bool iter_on =
        app.iteration_mode_enabled &&
        app.active_markers_view == 'W' &&
        iter_popup_eligible_marker(mv[idx]);
    this->enter_text_edit(
        idx,
        text_editor::Kind::FlagPayload,
        this->build_locked_prefix(mv[idx]),
        flag_text_iter(mv, idx, iter_on),
        click_x,
        /*text_left_x=*/-1.0,
        /*iter_grammar=*/iter_on);
}

// Validate `pending` as a single canonical line and, on success, write
// the parsed marker's fields back onto markers_[idx]. Cascade-renames
// label_def changes onto every other marker that referenced the old
// name. Pushes one undo entry covering all touched markers.
//
// On failure: sets `red`, leaves pending/cursor intact, leaves the
// editor active.
void GuiFlagEditor::commit_top_flag_edit() {
    if (!text_editor::is_active(app.top_flag_editor)) return;
    const int idx = app.top_flag_editor.target;
    const auto& mv_const = app.warpmarkers.markers();
    if (idx < 0 || idx >= static_cast<int>(mv_const.size())) {
        // Editor target became invalid (e.g. file reload). Drop edit.
        this->exit_top_flag_edit_no_commit();
        return;
    }

    // In iteration mode the buffer may carry the inline bracket
    // after `tempo_base`. Strip and capture it here (iteration-mode
    // wrapper) so parse_single_canonical_line stays bracket-unaware. NaN
    // bounds mean "blank/clear"; a malformed bracket red-flashes without
    // touching the marker.
    const bool iter_grammar = app.top_flag_editor.iter_grammar;
    std::string payload = app.top_flag_editor.pending;
    double iter_lo = std::numeric_limits<double>::quiet_NaN();
    double iter_hi = std::numeric_limits<double>::quiet_NaN();
    if (iter_grammar) {
        if (!extract_iter_bracket(payload, iter_lo, iter_hi)) {
            app.top_flag_editor.red = true;
            viewport.invalidate_top_strip();
            std::fprintf(stderr,
                "warptempo_gui: edit rejected: malformed iteration "
                "bracket: %s\n", app.top_flag_editor.pending.c_str());
            return;
        }
    }

    // Assemble the parse candidate in SERIALIZER form — the locked prefix
    // is a display rendering (MM:SS.mmm) and no longer the serializer's
    // bytes, so the position field is rebuilt as the frame double the
    // canonical line grammar expects. Position and disabled both come from
    // the marker itself (both live in the locked, uneditable prefix).
    std::string candidate;
    if (mv_const[idx].disabled) candidate += '#';
    candidate += format_frame_double(mv_const[idx].time_frame);
    candidate += '|';
    candidate += payload;

    GuiWarpMarker parsed;
    std::string err;
    auto result = warpmarkers_internal::parse_single_canonical_line(candidate);
    bool ok = result.has_value();
    if (ok) {
        static_cast<WarpMarker&>(parsed) = std::move(*result);
    } else {
        err = std::move(result.error());
    }

    // Cross-marker check (edit target excluded). A dangling label_ref is
    // deliberately not gated here — ref resolvability is a render verdict
    // (build_warp_frame_map's "label ref has no matching label def",
    // surfaced by the defect-resolution series at the commit funnel, render
    // dispatch, and target-view gate); def uniqueness is a load rule, so
    // the editor still gates it.
    if (ok && !parsed.label_def.empty()) {
        for (int i = 0; i < static_cast<int>(mv_const.size()); ++i) {
            if (i == idx) continue;
            if (mv_const[i].label_def == parsed.label_def) {
                ok = false;
                err = "duplicate label definition: " + parsed.label_def;
                break;
            }
        }
    }
    // Removing a label_def whose refs remain is legal: the refs go
    // dangling, keep their `a.NN` payload (so the file still parses),
    // and are refused at the render boundary — the dangling ref surfaces
    // through the defect-resolution series at the commit funnel, render
    // dispatch, and target-view gate. No editor-side gate; the rename
    // cascade below stays, scoped to non-empty new names.
    if (!ok) {
        app.top_flag_editor.red = true;
        viewport.invalidate_top_strip();
        std::fprintf(stderr,
            "warptempo_gui: edit rejected: %s\n", err.c_str());
        return;
    }

    // No first-marker special case: a marker at time 0 accepts any payload
    // the grammar allows — pass, label ref, label def. The rule that the
    // marker at frame 0 must be an enabled, tempo-owning numeric marker
    // is validate_first_marker_render_grammar's render-boundary check,
    // surfaced by the defect-resolution series at the commit funnel, render
    // dispatch, and target-view gate, never by this editor.

    std::vector<GuiWarpMarker> proposed = mv_const;
    GuiWarpMarker& m = proposed[idx];
    const std::string old_def = m.label_def;
    const std::string new_def = parsed.label_def;

    // Snapshot the canonical (serialized) fields before writing so we can
    // tell whether the engine/dirty state actually moved. Iteration-only
    // commits (bracket changed, tempo/scale/label unchanged) must not mark
    // dirty or trigger a render — iter values are session-only.
    const GuiWarpMarker before = m;

    // Time stays locked; preserve it (parse already produced the
    // same value via the locked prefix, but be explicit).
    const double preserved_time = m.time_frame;

    // Cache-free: typing `pass` writes inert defaults into
    // tempo_base/tempo_scale; typing an explicit tempo writes the
    // owned value. label_def is independent of tempo source —
    // `pass:LABEL` carries a def at this position while inheriting
    // the tempo from a prior owning marker.
    if (parsed.tempo_inherits) {
        m.tempo_inherits = true;
        m.tempo_base     = 1.0;
        m.tempo_scale.reset();
        m.label_def      = parsed.label_def;
        m.label_ref.clear();
    } else {
        m.tempo_inherits = false;
        m.tempo_base     = parsed.tempo_base;
        m.tempo_scale    = parsed.tempo_scale;
        m.label_def      = parsed.label_def;
        m.label_ref      = parsed.label_ref;
    }
    m.time_frame = preserved_time;
    // disabled lives in the locked prefix — parse_single_canonical_line
    // populated it; reapply.
    m.disabled      = parsed.disabled;

    // Cascade rename: if label_def changed to another NON-EMPTY name,
    // every other marker that referenced old_def gets its ref updated to
    // the new name. Removal (new_def empty) deliberately does NOT
    // cascade: clearing a ref would leave that marker serializing as an
    // owning 0.00 line (a pure ref parses with tempo_base 0.0), which the
    // parser rejects on reload. Dangling refs, by contrast, load fine and
    // are refused at the render boundary — so the refs are left pointing
    // at the removed name.
    int n_refs_renamed = 0;
    if (!old_def.empty() && !new_def.empty() && old_def != new_def) {
        for (int i = 0; i < static_cast<int>(proposed.size()); ++i) {
            if (i == idx) continue;
            if (proposed[i].label_ref == old_def) {
                proposed[i].label_ref = new_def;
                ++n_refs_renamed;
            }
        }
    }

    // Apply the parsed iteration bracket. Session-only; NaN
    // bounds clear the sweep. The accepted live-vector assignment bumps
    // the warp generation, so the flag cache repaints the bracket
    // regardless of whether the canonical fields moved.
    if (iter_grammar) {
        m.iter_start = iter_lo;
        m.iter_end   = iter_hi;
    }

    // Did any serialized field change? Cascade renames imply a label_def
    // change, already covered by the field compare below.
    const bool canonical_changed =
        m.tempo_inherits != before.tempo_inherits ||
        m.tempo_base     != before.tempo_base ||
        m.tempo_scale    != before.tempo_scale ||
        m.label_def      != before.label_def ||
        m.label_ref      != before.label_ref ||
        m.disabled       != before.disabled ||
        n_refs_renamed > 0;

    // Did the session-only iteration bracket move? NaN-aware: two bounds
    // are equal when both are NaN or when they compare equal under ==.
    // A bracket-only edit does not mark dirty (iter values are session-
    // only), but it is still a real undoable change — the snapshot
    // restores the iter values — so its push must not be skipped.
    auto iter_bound_equal = [](double a, double b) {
        return (std::isnan(a) && std::isnan(b)) || a == b;
    };
    const bool bracket_changed =
        iter_grammar &&
        (!iter_bound_equal(iter_lo, before.iter_start) ||
         !iter_bound_equal(iter_hi, before.iter_end));

    // An undo entry represents a state change, not a gesture. A commit
    // that moves neither a canonical field nor the bracket is a no-op:
    // it pushes nothing and touches no dirty/render state. The live-
    // vector assignment (which bumps the warp generation so the flag
    // cache repaints) and the editor deactivation below still run.
    const bool store_changed = canonical_changed || bracket_changed;

    // Capture pre-state for undo BEFORE mutating.
    std::vector<GuiWarpMarker> pre_state = mv_const;
    const int              hint_last = app.last_selected_marker;
    app.warpmarkers.markers_mut() = std::move(proposed);

    if (n_refs_renamed > 0) {
        std::fprintf(stderr,
            "[warptempo_gui] renamed label_def '%s' -> '%s'; "
            "updated %d refs\n",
            old_def.c_str(), new_def.c_str(), n_refs_renamed);
    }

    if (store_changed) {
        undo.push_undo_warp(std::move(pre_state), hint_last);
    }

    text_editor::deactivate(app.top_flag_editor);

    if (!store_changed) return;

    // The non-iteration commit path is untouched: it always recomputes
    // dirty and fires a render (existing behavior). Only the iteration-
    // mode wrapper gates on canonical_changed so a bracket-only edit
    // stays session-only — no dirty, no engine render.
    if (!iter_grammar || canonical_changed) {
        undo.recompute_dirty();
        viewport.invalidate_waveform_area();
        viewport.invalidate_timestamp_area();
        target_render.trigger();
    }
}

// Bulk-clear the session-only iter values across all warp markers.
// Triggered by Shift+I while iteration mode is on. Single undo
// entry. No-op when no marker carries iter values (avoids a noise
// entry on the undo stack).
void GuiFlagEditor::bulk_clear_iter_values() {
    if (!app.iteration_mode_enabled) return;
    if (app.active_markers_view != 'W') return;
    auto& mv = app.warpmarkers.markers_mut();
    bool any = false;
    for (const auto& m : mv) {
        if (!std::isnan(m.iter_start) || !std::isnan(m.iter_end)) {
            any = true;
            break;
        }
    }
    if (!any) return;
    std::vector<GuiWarpMarker> pre_state = app.warpmarkers.markers();
    const int              hint_last = app.last_selected_marker;
    for (auto& m : mv) {
        m.iter_start = std::numeric_limits<double>::quiet_NaN();
        m.iter_end   = std::numeric_limits<double>::quiet_NaN();
    }
    undo.push_undo_warp(std::move(pre_state), hint_last);
    viewport.invalidate_top_strip();
}

// Open the bottom-strip BPM editor on `idx`. Seed pending is the
// current bracket text (`"[]"` when blank, else `"<beats>@[<lo>,<hi>]"`).
// Reuses top_flag_editor with Kind::BpmBracket so the keyboard vocabulary
// swaps to digits + `@`/`,`/`[`/`]`; the bottom-strip paint branch supplies
// the visible "bpm: " prefix, so the editor's locked_prefix stays "".
void GuiFlagEditor::enter_bpm_edit(int idx, double click_x,
                                   double text_left_x) {
    if (idx < 0) return;
    if (!app.bpm_mode_enabled) return;
    const auto& mv = app.warpmarkers.markers();
    if (idx >= static_cast<int>(mv.size())) return;
    if (!bpm_popup_eligible_marker(mv[idx])) return;
    this->enter_text_edit(
        idx,
        text_editor::Kind::BpmBracket,
        /*locked_prefix=*/"",
        format_bpm_bracket_text(mv[idx]),
        click_x,
        text_left_x);
    // enter_text_edit's tail invalidates the top strip, but the BPM editor
    // draws in the bottom strip. Invalidate it so the freshly opened editor
    // actually paints.
    viewport.invalidate_timestamp_area();
}

// Commit the BPM editor's pending buffer. Strict syntax via
// parse_bpm_bracket. On parse failure the editor stays open with a red
// outline and false is returned; on success the parsed values are stored
// on the marker (the marker is already the BPM owner), the editor closes,
// and true is returned. No undo entry — BPM values are session-only,
// treated like view state. The Enter dispatch fires render_bpm_sweep()
// when this returns true.
bool GuiFlagEditor::commit_bpm_edit() {
    if (!text_editor::is_active(app.top_flag_editor)) return false;
    if (app.top_flag_editor.kind !=
            text_editor::Kind::BpmBracket) return false;
    const int idx = app.top_flag_editor.target;
    const auto& mv_const = app.warpmarkers.markers();
    if (idx < 0 || idx >= static_cast<int>(mv_const.size())) {
        text_editor::deactivate(app.top_flag_editor);
        viewport.invalidate_timestamp_area();
        return false;
    }
    const std::string& s = app.top_flag_editor.pending;
    int    beats = 0;
    double lo = 0.0, hi = 0.0;
    if (!parse_bpm_bracket(s, beats, lo, hi)) {
        app.top_flag_editor.red = true;
        viewport.invalidate_timestamp_area();
        std::fprintf(stderr,
            "warptempo_gui: bpm edit rejected: invalid syntax: %s\n",
            s.c_str());
        return false;
    }
    // Single-owner invariant: clear bpm_owner on every other marker before
    // stamping this one. The toggle handler maintains the invariant on mode
    // entry, but the editor can target a different marker than the one
    // originally stamped, so reassert it here.
    std::vector<GuiWarpMarker> proposed = mv_const;
    for (int i = 0; i < static_cast<int>(proposed.size()); ++i) {
        if (i == idx) continue;
        if (proposed[i].bpm_owner) {
            proposed[i].bpm_owner = false;
            proposed[i].bpm_beats = 0;
            proposed[i].bpm_lo    = 0;
            proposed[i].bpm_hi    = 0;
            proposed[i].bpm_endpoint = -1;
        }
    }
    proposed[idx].bpm_owner = true;
    proposed[idx].bpm_beats = beats;
    proposed[idx].bpm_lo    = lo;
    proposed[idx].bpm_hi    = hi;
    app.warpmarkers.markers_mut() = std::move(proposed);
    text_editor::deactivate(app.top_flag_editor);
    viewport.invalidate_timestamp_area();
    return true;
}

// Full mode-on transition for BPM mode. Validates the activation gate, toggles
// iter mode off if active, maintains the single-owner invariant, and marks the
// earlier of the two selected markers as the BPM owner (preserving prior
// values when re-toggling on the same owner), then flips the mode flag. The
// span endpoint is now explicit — supplied by the `m` handler and recorded on
// the owner's bpm_endpoint — so this no longer auto-selects an endpoint cue.
// The `m` handler calls this and then opens the bottom-strip BPM editor on the
// owner.
void GuiFlagEditor::enter_bpm_mode() {
    if (app.bpm_mode_enabled) return;
    if (app.active_markers_view != 'W') return;
    if (app.selected_markers.size() != 2) return;
    const int owner = *app.selected_markers.begin();   // earlier of the two
    const auto& mv_const = app.warpmarkers.markers();
    if (owner < 0 || owner >= static_cast<int>(mv_const.size())) return;
    if (!bpm_popup_eligible_marker(mv_const[owner])) return;

    if (app.iteration_mode_enabled) {
        app.iteration_mode_enabled = false;
    }

    auto& mv = app.warpmarkers.markers_mut();
    for (int i = 0; i < static_cast<int>(mv.size()); ++i) {
        if (i == owner) continue;
        if (mv[i].bpm_owner) {
            mv[i].bpm_owner = false;
            mv[i].bpm_beats          = 0;
            mv[i].bpm_lo             = 0;
            mv[i].bpm_hi             = 0;
            mv[i].bpm_endpoint       = -1;
        }
    }
    // Tag owner with bpm_owner=true if not already set. Sentinel-zero
    // values stay zero; format_bpm_bracket_text renders "[]" for that
    // state, which seeds the bottom-strip editor. Re-toggling on the same
    // owner preserves any previously-committed values (the flag stays true
    // and the values aren't touched).
    if (!mv[owner].bpm_owner) {
        mv[owner].bpm_owner = true;
        mv[owner].bpm_beats          = 0;
        mv[owner].bpm_lo             = 0;
        mv[owner].bpm_hi             = 0;
    }

    app.bpm_mode_enabled = true;
    viewport.clear_hover_popup();
    viewport.invalidate_top_strip();
}

void GuiFlagEditor::exit_bpm_mode() {
    if (!app.bpm_mode_enabled) return;
    app.bpm_mode_enabled = false;
    viewport.invalidate_top_strip();
}
