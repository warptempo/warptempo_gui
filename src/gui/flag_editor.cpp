#include "flag_editor.h"

#include "frame_format.h"

#include "target_render.h"

#include "audio.h"
#include "input_handler.h"
#include "render.h"
#include "text_editor.h"
#include "time_format.h"
#include "warp_frame_map_view.h"
#include "warpmarkers_ops.h"

#include <cctype>
#include <cstdio>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

// Strict signed two-decimal parse straight to integer cents (sign, >=1
// integer digit, '.', exactly two fraction digits; direct digit-to-cents
// conversion — no strtod, no doubles). Leading/trailing ASCII whitespace
// is trimmed first so the bracket's `, ` separator round-trips. A digit
// run whose cents would overflow int64 is refused (unreachable under the
// caller's delta bracket; adversarial typing earns the plain refusal).
bool parse_signed_2dp_cents(const std::string& raw, int64_t& out) {
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
    constexpr int64_t kMax = std::numeric_limits<int64_t>::max();
    int64_t whole = 0;
    for (size_t i = 1; i < dot; ++i) {
        if (whole > (kMax - 9) / 10) return false;       // overflow refused
        whole = whole * 10 + (v[i] - '0');
    }
    if (whole > (kMax - 99) / 100) return false;         // overflow refused
    const int64_t mag =
        whole * 100 + (v[dot + 1] - '0') * 10 + (v[dot + 2] - '0');
    out = (v[0] == '-') ? -mag : mag;
    return true;
}

// Extract the inline iteration bracket from a flag payload edited
// under the widened grammar. Searches for the `+[` segment after the
// tempo token; on match, removes `+[ ... ]` from `payload` and writes the
// parsed bounds in integer cents (lo <= hi, each within
// [-kIterDeltaMaxCents, kIterDeltaMaxCents]) to `lo_out`/`hi_out`. The
// all-zero blank (`+[+0.00, +0.00]`) and an absent bracket both yield
// nullopt (clear). The FlagPayload tempo/scale/label vocabulary never
// produces a `+`, so `+[` is an unambiguous marker. Returns false on a
// malformed bracket (caller red-flashes); true otherwise.
bool extract_iter_bracket(std::string& payload,
                          std::optional<int64_t>& lo_out,
                          std::optional<int64_t>& hi_out) {
    lo_out.reset();
    hi_out.reset();
    const auto open = payload.find("+[");
    if (open == std::string::npos) return true;          // absent → clear
    const auto close = payload.find(']', open + 2);
    if (close == std::string::npos) return false;        // unterminated
    const std::string inner = payload.substr(open + 2, close - (open + 2));
    const auto comma = inner.find(',');
    if (comma == std::string::npos) return false;
    int64_t lo = 0, hi = 0;
    if (!parse_signed_2dp_cents(inner.substr(0, comma), lo)) return false;
    if (!parse_signed_2dp_cents(inner.substr(comma + 1), hi)) return false;
    if (lo > hi) return false;
    // Iteration deltas live in [-kIterDeltaMaxCents, kIterDeltaMaxCents]
    // (value_format.h) — exact integer compares, like every tempo-domain
    // bracket.
    if (lo < -kIterDeltaMaxCents || lo > kIterDeltaMaxCents ||
        hi < -kIterDeltaMaxCents || hi > kIterDeltaMaxCents) {
        return false;
    }
    payload.erase(open, close - open + 1);
    // All-zero blank is the cleared state, not a zero-width sweep.
    if (lo != 0 || hi != 0) {
        lo_out = lo;
        hi_out = hi;
    }
    return true;
}

} // namespace

// Flag-editor cluster: the top-strip flag editor's enter / commit / exit
// paths, the iter grammar, and the bpm-bracket editor session, reaching
// undo and viewport through the struct's reference members. The popup
// eligibility and flag-text helpers (iter_popup_eligible_marker,
// bpm_popup_eligible_marker, ...) live in
// warpmarkers.h alongside effective_disabled, so this TU sees them via
// #include "warpmarkers.h".

// Build the locked-prefix string for `m`. This is the DISPLAY rendering,
// not the serializer's bytes: the serializer writes the whole-frame
// position as integer text (frame_format.h), while the prefix keeps the
// human-readable MM:SS.mmm form derived as
// format_timestamp(frame / sample_rate). The editor
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
    // A close while the editor is RED un-flashes the marker's STEM, and a stem
    // is a waveform pixel (the flash reaches it since 2026-08-01 —
    // GuiPaintHandler::paint_marker_stems), so the strip repaint below is not
    // the whole damage. Gated on the flash rather than paid on every close: the
    // ordinary close changes no waveform pixel at all. This is the chokepoint
    // the POINTER close comes through (any left press with the editor open
    // closes it, input_pointer.cpp) — the keyboard flips are edge-damaged at
    // handle_top_flag_editor_key, and the two overlap harmlessly on Esc/Ctrl+Q.
    // (Kind-exact: this teardown serves the BPM bracket session too, and that
    // editor's flash recolors the modal dialog's FIELD, with no stem behind it.)
    if (app.top_flag_editor.red &&
        app.top_flag_editor.kind == text_editor::Kind::FlagPayload) {
        viewport.invalidate_waveform_area();
    }
    text_editor::deactivate(app.top_flag_editor);
    viewport.invalidate_top_strip();
}

// Shared core for the enter-editor flows. Wrappers below
// (enter_top_flag_edit, enter_bpm_edit) own the kind-specific eligibility
// gates and seed-text builders, then delegate here. `iter_grammar` widens
// the FlagPayload editor's accepted vocabulary/cap for the inline
// iteration bracket. The live open routes are: Enter on the focused
// marker, the marker double-click (both -> enter_top_flag_edit), and the
// BPM editor open (enter_bpm_edit). Every route opens the editor with its
// SEEDED content fully selected (open-selected) — typing replaces
// wholesale, bare Left/Right collapse to the extremes — so there is no
// clicked-glyph caret to seat; a specific caret spot is a click inside the
// already-open editor (input_pointer's F2.1 path). A blank seed selects
// nothing and rests at caret 0, the same rule degenerating for the
// blank-seeded bottom editors.
void GuiFlagEditor::enter_text_edit(int idx,
                                    text_editor::Kind kind,
                                    std::string locked_prefix,
                                    std::string initial_pending,
                                    bool iter_grammar) {
    if (idx < 0) return;
    const auto& mv = app.warpmarkers.markers();
    if (idx >= static_cast<int>(mv.size())) return;

    const bool same_target =
        text_editor::is_active(app.top_flag_editor) &&
        app.top_flag_editor.kind == kind &&
        app.top_flag_editor.target == idx;

    if (same_target) {
        // Re-open on the active editor's own target: preserve pending
        // text and any in-progress state, just repaint.
        viewport.invalidate_top_strip();
        return;
    }

    // Target-switching path. Single-select the new editor target so the
    // marker-column outline follows it, and LAND the playhead on it: the marker
    // lane owns the playhead (the rule is stated in full at
    // land_playhead_on_marker, input_pointer.cpp), and an editor open is exactly
    // a route that hands the lane a new focus. This brings the one
    // set_single_selection caller that had opted out back onto the pointer
    // clicks' adjacency convention (selection then land, on the next line).
    // Reached only in W + source view — all three open routes gate the marker
    // view to the WARP column and take active_column_authoring_allowed — so `idx`
    // resolves against the warp store the helper reads, in the identity domain.
    // AND CLEAR ANY RESTING SPAN: an open moves the playhead onto one marker, so
    // the trim scratch the user drew for some other purpose goes with it —
    // unconditionally, never gated on the land having moved anything. This one
    // chokepoint covers every open and retarget (bare Return, the pointer
    // double-click, `m`, a pointer retarget of the live editor), which is why no
    // opener carries a clear of its own; `m` re-derives NOTHING after it since
    // 2026-07-30 (the extent owner died with the SPAN FORM). The same_target
    // early return above skips both: the
    // playhead is already there from the first open and nothing changed.
    selection.set_single_selection(idx);
    land_playhead_on_marker(app, audio, viewport, idx);
    clear_region_highlight(app, viewport);

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

    // Open-selected: fully select the seeded content (mirrors Ctrl+A's
    // anchor=0 / cursor=end assignments) so the first keystroke replaces it
    // wholesale and bare Left/Right collapse to the extremes; a blank seed
    // selects nothing and rests at caret 0. text_editor::enter already
    // refreshed blink_epoch, so no touch_blink is needed here.
    if (!app.top_flag_editor.pending.empty()) {
        app.top_flag_editor.selection_anchor = 0;
        app.top_flag_editor.cursor_pos =
            static_cast<int>(app.top_flag_editor.pending.size());
    } else {
        app.top_flag_editor.selection_anchor = -1;
        app.top_flag_editor.cursor_pos = 0;
    }

    viewport.invalidate_top_strip();
}

// The two flag-editor open routes (bare Return and the marker double-click) end
// here. NO PLAYBACK STOP, and that is an explicit exemption rather than an
// omission: the top-strip flag editor is the one modal surface that keeps
// playing, so a live audition survives the open. The decision and its rationale
// are recorded at GuiPlaybackLifecycle::stop_playback_for_modal_open, the one
// owner of the modal-open stop the dialog surfaces call.
void GuiFlagEditor::enter_top_flag_edit(int idx) {
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
        // Editor target became invalid (e.g. the marker was removed
        // underneath the editor by an undo or delete). Drop edit.
        this->exit_top_flag_edit_no_commit();
        return;
    }

    // In iteration mode the buffer may carry the inline bracket
    // after the tempo. Strip and capture it here (iteration-mode
    // wrapper) so parse_single_canonical_line stays bracket-unaware.
    // nullopt bounds mean "blank/clear"; a malformed bracket red-flashes
    // without touching the marker.
    const bool iter_grammar = app.top_flag_editor.iter_grammar;
    std::string payload = app.top_flag_editor.pending;
    std::optional<int64_t> iter_lo;
    std::optional<int64_t> iter_hi;
    if (iter_grammar) {
        if (!extract_iter_bracket(payload, iter_lo, iter_hi)) {
            app.top_flag_editor.red = true;
            viewport.invalidate_top_strip();
            std::fprintf(stderr,
                "warptempo_gui: Edit rejected: malformed iteration "
                "bracket: %s\n", app.top_flag_editor.pending.c_str());
            return;
        }
    }

    // Assemble the parse candidate in SERIALIZER form — the locked prefix
    // is a display rendering (MM:SS.mmm) and no longer the serializer's
    // bytes, so the position field is rebuilt as the integer frame text the
    // canonical line grammar expects. Position and disabled both come from
    // the marker itself (both live in the locked, uneditable prefix).
    std::string candidate;
    if (mv_const[idx].disabled) candidate += '#';
    candidate += format_authored_frame(mv_const[idx].time_frame);
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
    // deliberately not gated here — the parser resolver normalizes a
    // dangling ref to a plain 1.00 owner at render/preview time (one
    // stderr line per timestamp); def uniqueness is a load rule, so the
    // editor still gates it.
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
    // and the parser resolver normalizes each to a plain 1.00 owner at
    // render/preview time (one stderr line per timestamp). No editor-side
    // gate; the rename cascade below stays, scoped to non-empty new names.
    if (!ok) {
        app.top_flag_editor.red = true;
        viewport.invalidate_top_strip();
        std::fprintf(stderr,
            "warptempo_gui: Edit rejected: %s\n", err.c_str());
        return;
    }

    // Iteration cell-range gate, an exact integer-cents compare. Every
    // sweep cell mutates this owner's tempo_cents to tempo_cents + delta
    // (input_key_dispatch.cpp), and the deltas run from iter_start_cents to
    // iter_end_cents inclusive, so the two bracket endpoints bound every
    // cell. If the committed base tempo plus either bound lands outside the
    // tempo bracket [kTempoMinCents, kTempoMaxCents], some cell would
    // render a marker whose tempo cannot re-parse at a later promote (the
    // strict sidecar parse), so the bracket is refused at its own input
    // surface — the same red-flash a malformed bracket earns. THE BRACKET
    // TYPED HERE IS AUTHORED INPUT, and that is the whole reason this gate is
    // LOUD: it answers what the user just typed. A LATER base-tempo change
    // under a live bracket is not answered here at all — it silently drags
    // the bracket with it (THE BRACKET RIDES ITS BASE, architect 2026-08-02:
    // clamp_iter_bracket_to_tempo_bracket, warpmarkers.h, called below on this
    // very commit path and by the Up/Down cent step). The division is
    // deliberate: typed brackets gate loud, later base motion clamps silent —
    // and between them no sweep cell can leave the tempo bracket, so nothing
    // rides on a downstream backstop. The cleared/blank bracket (nullopt
    // bounds) and any marker the sweep would skip (a pass or label_ref,
    // ineligible per iter_popup_eligible_marker — the base the sweep reads
    // is tempo_cents of an owning numeric marker) carry no cells, so both
    // skip the check.
    if (iter_grammar && iter_lo.has_value() && iter_hi.has_value() &&
        !parsed.tempo_inherits && parsed.label_ref.empty()) {
        const int64_t base_cents = parsed.tempo_cents;
        if (base_cents + *iter_lo < kTempoMinCents ||
            base_cents + *iter_hi > kTempoMaxCents) {
            app.top_flag_editor.red = true;
            viewport.invalidate_top_strip();
            std::fprintf(stderr,
                "warptempo_gui: Edit rejected: iteration bracket cells "
                "leave the tempo bracket [%s, %s]: %s\n",
                format_tempo_cents(kTempoMinCents).c_str(),
                format_tempo_cents(kTempoMaxCents).c_str(),
                app.top_flag_editor.pending.c_str());
            return;
        }
    }

    // No first-marker special case: a marker at time 0 accepts any payload
    // the grammar allows — pass, label ref, label def. The parser resolver
    // normalizes the frame-0 arrangement at render/preview time (a missing
    // or ambiguous frame-0 owner becomes a plain 1.00 owner, one stderr
    // line per timestamp), never this editor.

    std::vector<GuiWarpMarker> proposed = mv_const;
    GuiWarpMarker& m = proposed[idx];
    const std::string old_def = m.label_def;
    const std::string new_def = parsed.label_def;

    // Snapshot the canonical (serialized) fields before writing so we can
    // tell whether the engine/dirty state actually moved. An iteration-only
    // commit (bracket changed, tempo/scale/label unchanged) does not mark
    // dirty — its undo entry pushes with affects_persistence=false, which
    // recompute_dirty honors — while the commit tail below still repaints and
    // triggers unconditionally like any store mutation.
    const GuiWarpMarker before = m;

    // Time stays locked; preserve it (parse already produced the
    // same value via the locked prefix, but be explicit).
    const int64_t preserved_time = m.time_frame;

    // Cache-free: typing `pass` writes inert defaults into
    // tempo_cents/tempo_scale; typing an explicit tempo writes the
    // owned value. label_def is independent of tempo source —
    // `pass:LABEL` carries a def at this position while inheriting
    // the tempo from a prior owning marker.
    if (parsed.tempo_inherits) {
        m.tempo_inherits = true;
        m.tempo_cents    = 100;
        m.tempo_scale.reset();
        m.label_def      = parsed.label_def;
        m.label_ref.clear();
    } else {
        m.tempo_inherits = false;
        m.tempo_cents    = parsed.tempo_cents;
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
    // owning 0.00 line (a pure ref parses with tempo_cents 0), which the
    // parser rejects on reload. Dangling refs, by contrast, load fine and
    // normalize to a plain 1.00 owner at the render boundary — so the
    // refs are left pointing at the removed name.
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

    // Apply the parsed iteration bracket. Session-only; nullopt bounds
    // clear the sweep. The accepted live-vector assignment bumps the warp
    // generation, so the flag cache repaints the bracket regardless of
    // whether the canonical fields moved.
    //
    // Invariant: an INELIGIBLE committed marker never keeps a bracket. A
    // bracket exists only while iteration mode paints it and its owner is
    // iter-eligible; a commit that makes the marker ineligible (a pass, or
    // a &ref) clears both bounds unconditionally — regardless of
    // iter_grammar, so a mode-off pass conversion of an undo-restored
    // bracketed owner also drops the fields. This mirrors Ctrl+N's
    // owner->pass / ref->pass eligibility-loss clears; undo is the sole
    // sanctioned route that resurrects a cleared bracket.
    if (iter_grammar) {
        m.iter_start_cents = iter_lo;
        m.iter_end_cents   = iter_hi;
    }
    if (!iter_popup_eligible_marker(m)) {
        m.iter_start_cents.reset();
        m.iter_end_cents.reset();
    }
    // THE BRACKET RIDES ITS BASE: this commit may have moved the tempo under a
    // bracket it did not type — the mode-off commit (iter_grammar false, so the
    // bounds above were left alone) over a bracket an undo restored, since undo
    // is deliberately ungated and outlives the mode's exit wipe. Fold the
    // surviving bracket onto the committed base (the one owner,
    // clamp_iter_bracket_to_tempo_bracket in warpmarkers.h). A no-op for the
    // bracket this commit DID type: the loud gate above already proved both its
    // ends land inside the tempo bracket at this very base. Placed before the
    // change compares below, so a clamp that moves a bound counts as a bracket
    // change and earns its repaint and its undo entry.
    clamp_iter_bracket_to_tempo_bracket(m);

    // Did any serialized field change? Cascade renames imply a label_def
    // change, already covered by the field compare below.
    const bool canonical_changed =
        m.tempo_inherits != before.tempo_inherits ||
        m.tempo_cents    != before.tempo_cents ||
        m.tempo_scale    != before.tempo_scale ||
        m.label_def      != before.label_def ||
        m.label_ref      != before.label_ref ||
        m.disabled       != before.disabled ||
        n_refs_renamed > 0;

    // Did the session-only iteration bracket move? Compare the ACTUAL
    // final fields, not the parsed input: this is correct on every path,
    // including an ineligibility clear that resets the bounds on a commit
    // whose canonical fields did not move (the mode-off pass conversion).
    // optional<int64_t> equality: two bounds are equal when both are
    // nullopt or when the held cents compare equal. A bracket-only edit
    // does not mark dirty (iter values are session-only), but it is still a
    // real undoable change — the snapshot restores the iter values — so its
    // push must not be skipped.
    const bool bracket_changed =
        m.iter_start_cents != before.iter_start_cents ||
        m.iter_end_cents   != before.iter_end_cents;

    // An undo entry represents a state change, not a gesture. A commit
    // that moves neither a canonical field nor the bracket is a no-op:
    // it pushes nothing and touches no dirty/render state. The live-
    // vector assignment (which bumps the warp generation so the flag
    // cache repaints) and the editor deactivation below still run.
    const bool store_changed = canonical_changed || bracket_changed;

    // Capture pre-state for undo BEFORE mutating.
    std::vector<GuiWarpMarker> pre_state = mv_const;
    app.warpmarkers.markers_mut() = std::move(proposed);

    if (n_refs_renamed > 0) {
        std::fprintf(stderr,
            "warptempo_gui: Renamed label_def '%s' -> '%s'; "
            "updated %d refs\n",
            old_def.c_str(), new_def.c_str(), n_refs_renamed);
    }

    if (store_changed) {
        undo.push_undo_warp(std::move(pre_state),
                            /*affects_persistence=*/canonical_changed);
    }

    text_editor::deactivate(app.top_flag_editor);

    if (!store_changed) return;

    // Unconditional by ruling — rationale at GuiTargetRender::trigger. Any
    // store change repaints and triggers, a bracket-only commit included (its
    // undo entry already did): recompute_dirty derives dirty purely from
    // affects_persistence entries, so a bracket-only push (affects_persistence
    // false) leaves dirty untouched, and the trigger's re-derive is
    // identity-unchanged for session-only iteration scratch (excluded from the
    // render recipe) and lands on dispatch_render_now's reuse rungs.
    undo.recompute_dirty();
    viewport.invalidate_waveform_area();
    viewport.invalidate_status_row_area();
    // No synchronous re-warp: the flag editor is a warp authoring surface that
    // exists only in warp's SOURCE home view (the home-view binding, architect
    // 2026-07-22 — both open routes gate on active_column_authoring_allowed, and
    // the S->T toggle closes any open editor WITHOUT committing before entering
    // target view), so a commit can never run with active_audio_view == 'T' and
    // there is no displayed target plate to re-warp. The former canonical_changed
    // sync branch was unreachable and is gone; canonical_changed survives as the
    // undo push's affects_persistence gate above. The trigger keeps its
    // store_changed gating (it re-derives identity-unchanged for an
    // iter-bracket-only commit and lands on the reuse rungs).
    target_render.trigger();
}

// Wipe every marker's session-only iter bracket. The single clear every
// iteration-mode exit route shares: the `i` toggle's turning-off branch,
// enter_bpm_mode's forced iter-off, and the iteration sweep's success tail —
// exiting the mode is the clear on every route, so a bracket exists only while
// the mode paints it on the flags. THE S->T AUDIO-VIEW TOGGLE IS NO LONGER ONE
// OF THEM (2026-08-07): iteration mode is target-legal, so entering target view
// neither exits the mode nor clears anything (the record is at
// handle_active_audio_view_toggle, input_handler.cpp). Two of the three
// surviving callers can now run in TARGET view, where the write is the granted
// home-view-binding exception argued at the sweep's tail
// (run_iteration_sweep_render, input_key_dispatch.cpp).
// Pushes one undo entry when something was cleared and no-ops otherwise, so
// a bracketless exit leaves the undo stack untouched; plain undo is
// deliberately ungated and may restore a previously accepted bracket set.
// Callers own the mode-flag flip and the repaint invalidation.
void GuiFlagEditor::wipe_iter_state() {
    auto& mv = app.warpmarkers.markers_mut();
    bool any = false;
    for (const auto& m : mv) {
        if (m.iter_start_cents.has_value() || m.iter_end_cents.has_value()) {
            any = true;
            break;
        }
    }
    if (!any) return;
    std::vector<GuiWarpMarker> pre_state = app.warpmarkers.markers();
    for (auto& m : mv) {
        m.iter_start_cents.reset();
        m.iter_end_cents.reset();
    }
    undo.push_undo_warp(std::move(pre_state),
                        /*affects_persistence=*/false);
}

// Wipe every marker's session-only bpm state — owner flag, beats, bracket
// bounds, and span endpoint back to their defaults. Runs on every bpm-mode
// exit (the single chokepoint exit_bpm_mode) and after a sweep dispatches, so
// a bracket exists only while the mode is live; re-entering bpm mode always
// seeds a blank "[]".
//
// History-less on purpose: bpm values are session-only with no undo of their
// own (see commit_bpm_edit), so no undo entry is pushed. Unlike iter state,
// undo can never resurrect bpm state: the bpm session is modal, so no
// snapshot-taking op can run while a marker carries live bpm fields, and
// outside the session every marker is already wiped — every snapshot in
// history carries default bpm state. Callers own the repaint.
void GuiFlagEditor::wipe_bpm_state() {
    auto& mv = app.warpmarkers.markers_mut();
    for (auto& m : mv) {
        m.bpm_owner    = false;
        m.bpm_beats    = 0;
        m.bpm_lo       = 0.0;
        m.bpm_hi       = 0.0;
        m.bpm_endpoint = -1;
    }
}

// Open the BPM editor on `idx` — a modal on the bottom row since 2026-08-13.
// Seed pending is the
// current bracket text (`"[]"` when blank, else `"<beats>@[<lo>,<hi>]"`).
// Reuses top_flag_editor with Kind::BpmBracket so the keyboard vocabulary
// swaps to digits + `@`/`,`/`[`/`]`; the dialog painter supplies the visible
// "BPM: " LABEL beside the field (kBpmEditorPrefix, paint_handler.h), so the
// editor's locked_prefix stays "".
void GuiFlagEditor::enter_bpm_edit(int idx) {
    if (idx < 0) return;
    if (!app.bpm_mode_enabled) return;
    const auto& mv = app.warpmarkers.markers();
    if (idx >= static_cast<int>(mv.size())) return;
    if (!bpm_popup_eligible_marker(mv[idx])) return;
    this->enter_text_edit(
        idx,
        text_editor::Kind::BpmBracket,
        /*locked_prefix=*/"",
        format_bpm_bracket_text(mv[idx]));
    // enter_text_edit's tail invalidates the top strip, but the BPM editor
    // draws in the bottom row's modal, whose rect does not exist before its
    // first paint — so a MODAL OPEN damages the whole window (the settings
    // opener carries the rule).
    viewport.invalidate_all();
}

// Commit the BPM editor's pending buffer. Strict syntax via
// parse_bpm_bracket, then the derived-tempo bracket gate below. On
// refusal the editor stays open with a red
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
        viewport.invalidate_status_row_area();
        return false;
    }
    const std::string& s = app.top_flag_editor.pending;
    int    beats = 0;
    double lo = 0.0, hi = 0.0;
    if (!parse_bpm_bracket(s, beats, lo, hi)) {
        app.top_flag_editor.red = true;
        viewport.invalidate_status_row_area();
        std::fprintf(stderr,
            "warptempo_gui: BPM edit rejected: invalid syntax: %s\n",
            s.c_str());
        return false;
    }
    // Derived-value bracket gate. Every sweep cell carries a derived base
    // tempo into its cell markers and a derived scale into its cell
    // .settings, and the derivation (compute_base_tempo_scale) is monotone
    // in bpm, so the bracket ends bound every cell: if either endpoint bpm
    // refuses — the derived base tempo lands outside [kTempoMinCents,
    // kTempoMaxCents]
    // or the derived scale outside [kScaleMin, kScaleMax] — the commit
    // red-flashes like any invalid editor value. Never clamp: a clamped
    // derivation would silently mistune the span. Gated on a well-formed
    // span (owner before endpoint, positive duration); without one,
    // render_bpm_sweep early-bails and derives nothing. The bpm editor is
    // modal, so the store cannot change between `m` and this commit — n here
    // equals the store size the `m` gate recorded bpm_endpoint against.
    {
        const int endpoint_idx = mv_const[idx].bpm_endpoint;
        const int n = static_cast<int>(mv_const.size());
        // The span-end frame is the boundary marker's time when one exists,
        // else the song end (bpm_endpoint == n is the song-end sentinel).
        // Guarded on endpoint_idx > idx and endpoint_idx <= n.
        if (audio.sample_rate() > 0 &&
            endpoint_idx > idx &&
            endpoint_idx <= n) {
            const int64_t span_end_frame =
                (endpoint_idx < n) ? mv_const[endpoint_idx].time_frame
                                   : audio.total_frames();
            const double duration_seconds =
                (span_end_frame - mv_const[idx].time_frame) /
                static_cast<double>(audio.sample_rate());
            if (duration_seconds > 0.0 &&
                (!compute_base_tempo_scale(duration_seconds, beats, lo) ||
                 !compute_base_tempo_scale(duration_seconds, beats, hi))) {
                app.top_flag_editor.red = true;
                viewport.invalidate_status_row_area();
                std::fprintf(stderr,
                    "warptempo_gui: BPM edit rejected: derived tempo or "
                    "scale outside its bracket (tempo [%s, %s], scale "
                    "[%s, %s]): %s\n",
                    format_tempo_cents(kTempoMinCents).c_str(),
                    format_tempo_cents(kTempoMaxCents).c_str(),
                    format_value_double(kScaleMin, 4).c_str(),
                    format_value_double(kScaleMax, 4).c_str(),
                    s.c_str());
                return false;
            }
        }
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
    viewport.invalidate_status_row_area();
    return true;
}

// Full mode-on transition for BPM mode. Validates the activation gate, toggles
// iter mode off if active, maintains the single-owner invariant, and marks the
// FIRST selected marker as the BPM owner (mode exit wipes the bpm state, so a
// fresh entry always seeds a blank bracket), then flips the mode flag. The
// span endpoint is explicit — supplied by the `m` handler and recorded on the
// owner's bpm_endpoint (section-based, architect 2026-07-23) — so this does not
// auto-select an endpoint cue. The full section gate (non-empty, contiguous,
// ref-free) lives in the `m` handler; this route re-checks only that at least
// one marker is selected and the owner is eligible. The `m` handler calls this
// and then opens the BPM editor on the owner.
void GuiFlagEditor::enter_bpm_mode() {
    if (app.bpm_mode_enabled) return;
    if (app.active_markers_view != 'W') return;
    if (app.selected_markers.empty()) return;
    const int owner = *app.selected_markers.begin();   // first selected
    const auto& mv_const = app.warpmarkers.markers();
    if (owner < 0 || owner >= static_cast<int>(mv_const.size())) return;
    if (!bpm_popup_eligible_marker(mv_const[owner])) return;

    if (app.iteration_mode_enabled) {
        // Forced iter-off is a mode exit like any other and takes the same
        // wipe the `i` toggle's turning-off branch runs — otherwise `m`
        // would smuggle live brackets out of iteration mode invisibly, and
        // the BPM commit's tempo rewrite would land on markers still
        // carrying them.
        wipe_iter_state();
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
    // state, which seeds the BPM dialog editor. Every prior mode exit
    // wiped the bpm state, so a fresh entry always finds an untagged owner
    // and seeds a blank bracket.
    if (!mv[owner].bpm_owner) {
        mv[owner].bpm_owner = true;
        mv[owner].bpm_beats          = 0;
        mv[owner].bpm_lo             = 0;
        mv[owner].bpm_hi             = 0;
    }

    app.bpm_mode_enabled = true;
    viewport.invalidate_top_strip();
}

// The single bpm-mode-off chokepoint: every route that turns bpm mode off
// funnels here. Wipes the session-only bpm state (mode exit IS the clear, so
// no marker carries bpm state once the mode is down) and repaints both the
// top strip and the status row — whose damage owner carries the modal dialog's
// stashed box as a rider, which is what covers the bpm editor's own pixels.
void GuiFlagEditor::exit_bpm_mode() {
    if (!app.bpm_mode_enabled) return;
    app.bpm_mode_enabled = false;
    wipe_bpm_state();
    viewport.invalidate_top_strip();
    viewport.invalidate_status_row_area();
}
