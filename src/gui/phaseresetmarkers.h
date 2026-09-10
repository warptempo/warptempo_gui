#pragma once

#include "marker_store.h"
#include "phaseresetmarkers_parse.h"
#include "warpmarkers.h"   // MarkerCell and format_signed_hops — the cell
                           // vocabulary and the hop composer are one product's,
                           // and warpmarkers.h is where both live

#include <expected>
#include <optional>
#include <string>
#include <vector>

// The GUI's authoring view: a PhaseResetMarker plus the session-only iteration
// bracket. The base exists so the GUI marker store has its own type and the
// parser can fill the PhaseResetMarker base by reference (a
// GuiPhaseResetMarker binds to PhaseResetMarker& by upcast), mirroring the
// WarpMarker / GuiWarpMarker split.
struct GuiPhaseResetMarker : PhaseResetMarker {
    // Iteration mode on the PHASE-RESET column (architect 2026-09-09): "just
    // like the existing grid iterations, aside from the domain". Session-only
    // render-parameter scratchpad — NEVER SERIALIZED (there is no sidecar
    // grammar for it and no parser knows it; the slice below drops it by
    // copy-construction), lost on app close, out of every commit — because
    // "iterations are transitory; the tmp/ file names are what I remember;
    // loading in place recalls one". Authored on the flag's two BOUND CELLS
    // and nowhere else, exactly as the warp bracket is: stepped a hop at a
    // time by the vertical arrows on the addressed cell, or typed into that
    // cell's own editor (GuiFlagEditor::enter_iter_bound_edit, which serves
    // both columns).
    //
    // THE DOMAIN IS HOPS, NOT CENTS. A phase reset carries no value to sweep,
    // only a POSITION, and the position that matters is quantized: the engine
    // seeds a reset at the last schedule window whose centre is at or before
    // the authored frame, so a reset nudged inside one hop cell renders the
    // same bytes. One step of this bracket is therefore ONE HOP OF THE
    // ANALYSIS LATTICE, and a cell at +k displaces the reset by THE SMALLEST
    // AMOUNT that seeds k hops away (phase_reset_hop_cell_frame,
    // warp_frame_map_view.h — his "the amount of change added by each hop
    // should be the MINIMUM amount to get to that hop, so that we stay as
    // close to the original starting point of the phase reset as possible").
    //
    // A SINGLE DIGIT EACH WAY (kIterHopMax below, his "past nine it is no
    // longer the phase reset it was"). nullopt means "blank" (both cells read
    // `+0`); when set, both are set and iter_start_hops <= iter_end_hops. A
    // pair of two zeroes is THE BLANK on every road, not a zero-width sweep:
    // both authoring roads write through the one site that clears it
    // (phase_iter_bound_step_write, app_state.h — the arrows' step and the
    // cell editor's commit alike), so two cells reading `+0` always mean the
    // same thing. A disabled reset's bracket is DORMANT — kept, off the flag,
    // out of the sweep and reachable by no editor until re-enabled
    // (phase_reset_iter_eligible_marker below).
    //
    // THERE IS NO RETROACTIVE CLAMP HERE and that is a RULING rather than a
    // gap: the warp bracket rides its base through one owner because its
    // window has two authoring surfaces, while this window is a fact about the
    // whole store AND the live warp map (phase_reset_hop_window,
    // warp_frame_map_view.h) with a dozen writers between them, so nothing
    // clamps and the SWEEP'S PLAN re-verifies every bracket on every read
    // instead (IterationSweepRefusal::PhaseBracketOffWall, app_state.h).
    std::optional<int> iter_start_hops;
    std::optional<int> iter_end_hops;
};

// THE SINGLE DIGIT (architect 2026-09-09): a bracket bound is at most nine
// hops from the resting seed in either direction, because "past nine it is no
// longer the phase reset it was". It is the range of the authored value, not
// of the displacement — how far nine hops move a reset in FRAMES depends on
// the map (phase_reset_hop_cell_frame). The bound editor's byte cap is derived
// from it (kMaxPendingCharsIterHop, text_editor.h, with the assert that pins
// the derivation).
inline constexpr int kIterHopMax = 9;

// Slice a GUI phase-reset vector down to the serialized base, mirroring
// slice_to_warp_markers. Each element is copy-constructed as a
// PhaseResetMarker from its GuiPhaseResetMarker, so THE ITERATION BRACKET IS
// DROPPED HERE — that is the whole of what keeps the session-only field out of
// the render boundary and off every sidecar the pipeline writes. Used at the
// render boundary so the parser-domain phase-reset assembly
// (build_phase_reset_source_frames) never sees the GUI type.
inline std::vector<PhaseResetMarker> slice_to_phase_reset_markers(
    const std::vector<GuiPhaseResetMarker>& src) {
    return std::vector<PhaseResetMarker>(src.begin(), src.end());
}

// Iteration mode: the text of ONE bound cell on the phase-reset column — the
// bound in the signed integer-hop form, `+0` for a blank bracket on either
// side. THE BLANK RULE HAS THIS ONE HOME for this column: the flag's two cells
// and the bound editor's seed (GuiFlagEditor::enter_iter_bound_edit) both read
// it, so what a cell shows and what its editor opens with are one spelling of
// one value. `side` is Lower or Upper; any other member answers the lower
// bound, the harmless reading, since neither the payload nor the measure
// carries a bound. The warp twin is format_iter_bound_cell (warpmarkers.h);
// the two differ in their composer alone, and THE ABSENT DECIMALS ARE WHAT
// TELL THE COLUMNS APART wherever the two meet — on the flag and in a sweep
// cell's file name.
inline std::string format_phase_iter_bound_cell(const GuiPhaseResetMarker& m,
                                                MarkerCell side) {
    const bool blank = !m.iter_start_hops.has_value() ||
                       !m.iter_end_hops.has_value();
    if (blank) return format_signed_hops(0);
    return format_signed_hops(side == MarkerCell::Upper ? *m.iter_end_hops
                                                        : *m.iter_start_hops);
}

// Iteration mode: which phase resets THE SWEEP READS. EVERY PHASE RESET IS A
// CARRIER (architect 2026-09-09's domain answer, read structurally): the
// bracket displaces a POSITION, and every reset has one — there is no pass, no
// label ref and no tempo to own, so the warp column's structural half
// (iter_bracket_carrier) has no counterpart here and the verdict is the
// disabled bit alone. Nor is there a cascade to ask: this column carries no
// labels, so the flag pass reads the plain bool and so does this.
//
// A DISABLED RESET'S BRACKET IS DORMANT, exactly as a disabled owner's is —
// kept, off the flag, out of the product, reachable by no editor, back the
// moment it is re-enabled. FIVE READERS, the warp predicate's own inventory in
// this column's terms (greped 2026-09-09): the sweep's dispatch
// (run_iteration_sweep_render, input_key_dispatch.cpp) and its face's plan
// (iteration_sweep_plan, app_state.h) skip the reset, so its bracket neither
// multiplies the cell count nor names a cell; the flag painter
// (render_phase_reset_flags, render.cpp) paints the two bound cells on exactly
// these resets and none on a disabled flag; the bound step
// (GuiPhaseResetMarkersOps::adjust_iter_bound_hops) skips an ineligible member
// in a group and refuses an ineligible singleton on a card; and the bound
// editor's open (GuiFlagEditor::enter_iter_bound_edit) refuses where no cell
// paints — no cell, no editor.
inline bool phase_reset_iter_eligible_marker(
    const std::vector<GuiPhaseResetMarker>& pv, int idx) {
    if (idx < 0 || idx >= static_cast<int>(pv.size())) return false;
    return !pv[static_cast<size_t>(idx)].disabled;
}

// The store mechanics (sorted vector, generation token, insert/remove/mut
// accessors) are the shared GuiMarkerStore base (marker_store.h); this
// class carries the phase reset column's parse and serializer surfaces.
class GuiPhaseResetMarkers : public GuiMarkerStore<GuiPhaseResetMarker> {
public:
    // Parses `path`. On success, populates markers() and returns the parsed
    // markers. The first malformed line aborts the parse and returns a
    // one-line error; a missing/unopenable file is a failure (the sidecar is
    // created at source load and required at every load boundary; the empty
    // file is the no-resets form). No throw.
    //
    // `path_free_reason` is the parser's own out-parameter, passed through
    // unread — the warp column's contract exactly
    // (parse_phaseresetmarkers_file, phaseresetmarkers_parse.h).
    std::expected<void, std::string> load(
        const std::string& path,
        std::optional<std::string>* path_free_reason = nullptr);

    // Writes the canonical form to `path`. Atomic: writes to <path>.tmp,
    // fsyncs, then renames. Preserves existing permissions or uses 0644 if
    // the file is new. Returns true on success. Save writes every row with
    // no dedup or ordering validation; the serializer contract (the store
    // is sorted by construction, equal-time rows are legal and reload,
    // and the render boundary — not the serializer — collapses an
    // exact-equal group to one event) is documented at
    // format_phaseresetmarkers_text in phaseresetmarkers.cpp.
    bool save(const std::string& path) const;

    // Static variant for callers that hold a raw GuiPhaseResetMarker vector
    // (e.g. the render pipeline writing the authored .phaseresetmarkers copy
    // beside a batch render). Same on-disk format as the instance method:
    // authored positions, whole source frames as plain integer text.
    static bool save(const std::string& path,
                     const std::vector<GuiPhaseResetMarker>& markers);
};

// The `.phaseresetmarkers` file's exact bytes for `markers`, built and
// returned without touching disk — the string half both save() overloads hand
// to the atomic writer, so the two can never diverge. Its other consumer is
// the GitHub recheck's "now" side (history_diff.h), which diffs the live store
// against a committed snapshot and needs precisely what a Ctrl+S would land at
// this instant, with no file anywhere. The serializer contract is at the
// definition.
std::string format_phaseresetmarkers_text(
    const std::vector<GuiPhaseResetMarker>& markers);
