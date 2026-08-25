#include "flag_editor.h"

#include "frame_format.h"

#include "target_render.h"

#include "audio.h"
#include "input_handler.h"
#include "render.h"
#include "text_editor.h"
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
    // Reached in the WARP column alone — all three open routes gate the marker
    // view to it — so `idx` resolves against the warp store this helper reads.
    // BOTH AUDIO VIEWS SINCE 2026-08-24 (the payload editor is the home-view
    // binding's fifth ruled exception; the inventory is at
    // active_column_authoring_allowed, app_state.h), so the domain is no
    // longer the identity one: land_playhead_on_marker translates through the
    // ACTIVE domain itself (source_frame_to_active_domain at its body), so the
    // land is right in target view with nothing to add here.
    // THE LAND HIDES THE TRIM REGION OVERLAY: an open moves the playhead onto
    // one marker, so the overlay goes with it — unconditionally, never gated on
    // the land having moved anything, and discarding nothing (the trim stands
    // and a later `[` re-shows the same overlay). Since 2026-08-19 that is the
    // LAND'S own act rather than a second call here (the rule and its two
    // movement owners are at clear_region_highlight, input_handler.h). This one
    // chokepoint covers every open and retarget (bare Return, the pointer
    // double-click, `m`, a pointer retarget of the live editor); `m` re-derives
    // NOTHING after it since
    // 2026-07-30 (the extent owner died with the SPAN FORM). The same_target
    // early return above skips both: the
    // playhead is already there from the first open and nothing changed.
    selection.set_single_selection(idx);
    land_playhead_on_marker(app, audio, viewport, idx);

    // Discard any prior edit silently before switching targets.
    if (text_editor::is_active(app.top_flag_editor) &&
        app.top_flag_editor.target != idx) {
        text_editor::deactivate(app.top_flag_editor);
    }
    text_editor::enter(
        app.top_flag_editor, idx,
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
        flag_text_iter(mv, idx, iter_on),
        /*iter_grammar=*/iter_on);
}

// THE MEASURE EDITOR'S OPEN. The contract is at the declaration; this is the
// mechanics, and they are enter_text_edit's shape written for TWO stores
// instead of one.
//
// NO PLAYBACK STOP, the top-strip family's recorded exemption: this editor is
// keyboard-modal and pointer/wheel-transparent exactly as the payload editor
// is, so a live audition survives the open. The decision table is at
// GuiPlaybackLifecycle::stop_playback_for_modal_open, which this surface — like
// its sibling — deliberately does not call.
void GuiFlagEditor::enter_measure_edit(char column, int idx) {
    if (idx < 0) return;
    const bool phase = (column == 'P');
    const int  n = phase
        ? static_cast<int>(app.phaseresetmarkers.markers().size())
        : static_cast<int>(app.warpmarkers.markers().size());
    if (idx >= n) return;

    if (text_editor::is_active(app.top_flag_editor) &&
        app.top_flag_editor.kind == text_editor::Kind::MeasureText &&
        app.top_flag_editor.target == idx) {
        // Re-open on the live session's own target: preserve the pending text
        // and any in-progress state, just repaint (the payload editor's rule).
        viewport.invalidate_top_strip();
        return;
    }

    // THE FOCUS IS REPAIRED FIRST, the bare-Return arm's twin (input_handler.cpp
    // — a stale focus outside the selection moves to the largest remaining
    // member before anything reads it). The key ROUTE repairs before resolving
    // `idx` from the focus; this second call is the pointer route's, where the
    // index came from a hit test, and it is idempotent on an already-consistent
    // focus.
    selection.repair_last_selected();

    // Target-switching: single-select the new target so the marker column's
    // outline follows it, and LAND the playhead on it — the marker lane owns
    // the playhead, and an editor open hands the lane a new focus (the rule is
    // at land_playhead_on_marker, input_pointer.cpp). Both are column-agnostic:
    // selection indices and the land alike resolve against the ACTIVE column's
    // store, which is the column this open was called with.
    selection.set_single_selection(idx);
    land_playhead_on_marker(app, audio, viewport, idx);

    // Discard any prior edit silently before switching surfaces.
    if (text_editor::is_active(app.top_flag_editor)) {
        text_editor::deactivate(app.top_flag_editor);
    }
    // THE SEED IS THE MARKER'S OWN MEASURE — the only measure there is. A
    // display-time inheritance down the label cascade existed for one day and
    // the architect reversed it on 2026-08-20 (the field is a POSITION in the
    // score, wrong at a reference sitting bars later), so what a flag paints
    // and what this editor opens with are the same one field on both columns.
    // That the seed is the marker's OWN is now trivially true and is still
    // spelled here, because it is what makes an empty commit a REMOVAL of this
    // marker's measure and nothing else's.
    const std::string seed = phase
        ? app.phaseresetmarkers.markers()[static_cast<size_t>(idx)].measure
        : app.warpmarkers.markers()[static_cast<size_t>(idx)].measure;
    text_editor::enter(app.top_flag_editor, idx, seed,
                       text_editor::Kind::MeasureText);

    // Open-selected, the family's rule: the seeded text is fully selected so
    // the first keystroke replaces it wholesale; a blank seed selects nothing
    // and rests at caret 0.
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

// THE MEASURE COMMIT, and it HAS a validator arm (2026-08-20, with the field's
// rebrand from the free-text comment): the measure is a GRAMMAR, so the buffer
// is judged HERE against validate_marker_measure — the same one judge the two
// file parsers and both history delta extractors use, which is what keeps
// "loadable iff it commits" exact rather than merely likely. The type-time
// filters cannot carry the grammar the way they carried the old byte class: a
// half-typed `12 4` is a legal prefix of a legal token, so refusal belongs at
// the commit and nowhere earlier. There is deliberately NO Kind-dependent
// keystroke filter; typing stays free and the commit decides.
//
// THE REFUSAL IS THIS EDITOR'S OWN SHAPE, not a dialog's: `red = true`, a
// top-strip repaint, one stderr line, and RETURN WITHOUT DEACTIVATING, so the
// session stands with the offending text in place for correction. The damage
// is the top strip alone — the stem flash that the FlagPayload refusal drives
// is gated on that Kind at the painter, so a MeasureText red never reaches a
// waveform pixel and there is no waveform-area edge to invalidate.
//
// AN EMPTY BUFFER REMOVES THE MEASURE and is exempt from the grammar (the
// validator has no "empty is fine" reading — an empty tail on disk is
// load-fatal), which is what makes the bare ` //` suffix a state the GUI can
// never write.
void GuiFlagEditor::commit_measure_edit() {
    if (!text_editor::is_active(app.top_flag_editor)) return;
    if (app.top_flag_editor.kind != text_editor::Kind::MeasureText) return;
    const int idx = app.top_flag_editor.target;
    const std::string next = app.top_flag_editor.pending;

    if (!next.empty()) {
        std::string measure_err;
        if (!validate_marker_measure(next, measure_err)) {
            app.top_flag_editor.red = true;
            viewport.invalidate_top_strip();
            std::fprintf(stderr,
                "warptempo_gui: Measure rejected: %s: %s\n",
                measure_err.c_str(), next.c_str());
            return;
        }
    }
    // THE COLUMN IS READ LIVE AND IT IS THE OPEN'S COLUMN, because the view
    // CANNOT MOVE under an open session: every column-switching key (`p`, `t`,
    // the 1/2/3 selectors, Ctrl+Tab) is dropped at the keyboard-modal gate
    // while any editor stands, and every column-switching BUTTON acts at the
    // LIFT whose own PRESS already closed this editor
    // (close_top_flag_editor_for_outside_press). So the session needs no stored
    // column of its own.
    const bool phase = (app.active_markers_view == 'P');

    // The target may have gone out from under the editor (an undo or a delete
    // while it stood): drop the edit, exactly as the payload commit does.
    const int n = phase
        ? static_cast<int>(app.phaseresetmarkers.markers().size())
        : static_cast<int>(app.warpmarkers.markers().size());
    if (idx < 0 || idx >= n) {
        this->exit_top_flag_edit_no_commit();
        return;
    }

    // A COMMIT THAT CHANGES NOTHING IS NOT A CHANGE: no undo entry, no dirty
    // bit, no store bump — the shape every no-op commit in the product takes.
    const std::string& before = phase
        ? app.phaseresetmarkers.markers()[static_cast<size_t>(idx)].measure
        : app.warpmarkers.markers()[static_cast<size_t>(idx)].measure;
    if (before == next) {
        this->exit_top_flag_edit_no_commit();
        return;
    }

    // ONE UNDO ENTRY, affects_persistence TRUE: a measure is serialized content
    // and its edit dirties the tab like any other authored change. The snapshot
    // is taken before the write, the store's own convention.
    if (phase) {
        std::vector<GuiPhaseResetMarker> pre = app.phaseresetmarkers.markers();
        GuiPhaseResetMarker* m = app.phaseresetmarkers.marker_mut(idx);
        if (m) m->measure = next;
        undo.push_undo_phase_reset(std::move(pre));
    } else {
        std::vector<GuiWarpMarker> pre = app.warpmarkers.markers();
        GuiWarpMarker* m = app.warpmarkers.marker_mut(idx);
        if (m) m->measure = next;
        undo.push_undo_warp(std::move(pre));
    }
    undo.recompute_dirty();

    // NO RE-RENDER AND NO MAP REBUILD: a measure reaches neither the engine nor
    // the render fingerprint (the field's own contract at WarpMarker::measure),
    // so the box is the only thing that moved and the strip is the only damage.
    text_editor::deactivate(app.top_flag_editor);
    viewport.invalidate_top_strip();
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

    // Assemble the parse candidate in SERIALIZER form. The editor holds the
    // PAYLOAD alone, so the two fields ahead of the pipe come from the marker's
    // own state rather than from any typed bytes: `disabled` from the marker's
    // flag, and the position as the integer frame text the canonical line
    // grammar expects (format_authored_frame, frame_format.h — never the
    // MM:SS.mmm form the GUI shows elsewhere).
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

    // Time stays locked; preserve it (the candidate assembled above carried
    // the marker's own frame, so the parse produced the same value, but be
    // explicit).
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
    // disabled is not the editor's field — the candidate carried the marker's
    // own bit, parse_single_canonical_line populated it; reapply.
    m.disabled      = parsed.disabled;
    // THE MEASURE IS NOT THIS EDITOR'S and is preserved by construction: `m`
    // is the live marker copied whole, and no line above writes the field. The
    // candidate parsed at accept_measure = false, so `parsed.measure` is
    // always empty and must never be assigned from — a ` //` typed into the
    // payload buffer is a grammar error the parse already red-flashed. The
    // measure has its own editor (Kind::MeasureText).

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
    viewport.invalidate_status_chain_area();
    // THE TARGET-VIEW TAIL (architect 2026-08-24). The payload editor is a
    // VALUE surface — tempo, label_def / label_ref, per-marker scale, the
    // disabled bit, the iter bracket — and never a placement one, so it is a
    // member of the warp status/value family admitted in W+target, and it owes
    // that family's contract: the contract is stated once at the head of
    // warpmarkers_ops.cpp, and the target-view re-warp inventory it joins is
    // owned by Viewport::kick_waveform_sync's declaration (viewport.h). This
    // commit is the worked case for the re-land: a renamed label_def reprices
    // every reference to it, including references EARLIER in the timeline,
    // whose spans then change duration and shift everything downstream — the
    // edited marker's own image included — so the FOCUS (which is this
    // editor's target, single-selected at the open) re-lands on its post-commit
    // image through reseat_playhead_to, a TRANSLATION that must leave the trim
    // region overlay standing (the rule at clear_region_highlight,
    // input_handler.h). SOURCE VIEW NEEDS NOTHING: identity domain, no image
    // moves.
    // AND canonical_changed IS THE SECOND TERM, which is where this site
    // differs from its three siblings: they write nothing BUT map inputs,
    // while this commit can land an ITER-BRACKET-ONLY change — session-only
    // fields, excluded from build_warp_frame_map and from the render recipe
    // alike — which moves no image and would make the kick a wasted
    // synchronous plate render and the re-land a write of the value the
    // playhead already holds. canonical_changed is exactly the map-input set
    // (tempo_inherits / tempo_cents / tempo_scale / label_def / label_ref /
    // disabled / renamed refs; position is not editable here), so it is the
    // honest gate and it is the same predicate the pre-2026-08-24 branch used
    // before that branch went unreachable and was deleted. It keeps its other
    // job as the undo push's affects_persistence gate above. The trigger keeps
    // its own store_changed gating (it re-derives identity-unchanged for an
    // iter-bracket-only commit and lands on the reuse rungs).
    if (app.active_audio_view == 'T' && canonical_changed) {
        viewport.kick_waveform_sync();
        const auto& mv_post = app.warpmarkers.markers();
        if (idx >= 0 && idx < static_cast<int>(mv_post.size())) {
            viewport.reseat_playhead_to(source_frame_to_active_domain(
                app, audio, mv_post[idx].time_frame));
        }
    }
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
// seeds an EMPTY field.
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
// current bracket text (EMPTY when blank, else `"<beats>@[<lo>,<hi>]"`) — a
// fresh open is a blank field the whole value is typed into
// (format_bpm_bracket_text, warpmarkers.h, carries that ruling).
// Reuses top_flag_editor with Kind::BpmBracket so the keyboard vocabulary
// swaps to digits + `@`/`,`/`[`/`]`; the dialog painter supplies the visible
// "BPM: " LABEL beside the field (kBpmEditorPrefix, paint_handler.h), so the
// buffer holds the bracket text alone.
void GuiFlagEditor::enter_bpm_edit(int idx) {
    if (idx < 0) return;
    if (!app.bpm_mode_enabled) return;
    const auto& mv = app.warpmarkers.markers();
    if (idx >= static_cast<int>(mv.size())) return;
    if (!bpm_popup_eligible_marker(mv[idx])) return;
    this->enter_text_edit(
        idx,
        text_editor::Kind::BpmBracket,
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
        viewport.invalidate_modal_dialog_area();
        return false;
    }
    const std::string& s = app.top_flag_editor.pending;
    int    beats = 0;
    double lo = 0.0, hi = 0.0;
    if (!parse_bpm_bracket(s, beats, lo, hi)) {
        app.top_flag_editor.red = true;
        viewport.invalidate_modal_dialog_area();
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
        // The span-end frame is the boundary marker's time when one exists
        // — the NEXT EFFECTIVELY-ENABLED marker after the last selected
        // one, which is what bpm_endpoint names (warpmarkers.h) — else the
        // song end (bpm_endpoint == n is the song-end sentinel). Guarded on
        // endpoint_idx > idx and endpoint_idx <= n.
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
                viewport.invalidate_modal_dialog_area();
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
    // TWO SURFACES, TWO OWNERS (2026-08-13, when the status chain left this
    // row for the tab row): the dialog editor just came down, which is the
    // BOTTOM row's damage, and the stamped marker is a bpm OWNER now — a
    // class the resolved readout has nothing to resolve for — so the chain's
    // lowest tier changes with it.
    viewport.invalidate_modal_dialog_area();
    viewport.invalidate_status_chain_area();
    return true;
}

// Full mode-on transition for BPM mode. Validates the activation gate, toggles
// iter mode off if active, maintains the single-owner invariant, and marks the
// FIRST selected marker as the BPM owner (mode exit wipes the bpm state, so a
// fresh entry always opens on a blank field), then flips the mode flag. The
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
    // values stay zero; format_bpm_bracket_text renders the EMPTY string for
    // that state, which seeds the BPM dialog editor. Every prior mode exit
    // wiped the bpm state, so a fresh entry always finds an untagged owner
    // and opens on a blank field.
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
// top strip and the MODAL's own lane — the bottom row, which is where the bpm
// editor's pixels are while it stands.
void GuiFlagEditor::exit_bpm_mode() {
    if (!app.bpm_mode_enabled) return;
    app.bpm_mode_enabled = false;
    wipe_bpm_state();
    viewport.invalidate_top_strip();
    viewport.invalidate_modal_dialog_area();
}
