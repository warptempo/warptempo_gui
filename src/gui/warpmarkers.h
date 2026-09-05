#pragma once

#include "marker_store.h"
#include "value_format.h"
#include "warpmarkers_parse.h"

#include <algorithm>
#include <expected>
#include <optional>
#include <string>
#include <vector>

// The GUI's authoring view: a WarpMarker plus the session-only authoring
// scratchpad. The scratchpad is never serialized and never crosses the
// render boundary. The canonical per-line parser fills the WarpMarker base
// by reference; a GuiWarpMarker binds to that WarpMarker& by upcast (no
// slicing). At the render boundary the GUI slices a GuiWarpMarker vector
// down to a std::vector<WarpMarker> via slice_to_warp_markers below, so the
// resolver and the engine path never see these GUI-only fields.
struct GuiWarpMarker : WarpMarker {
    // Iteration mode. Session-only render-parameter scratchpad: never
    // serialized, lost on app close, authored on the flag's two BOUND CELLS
    // and nowhere else (architect 2026-09-04, the road's authoring surface:
    // the bracket is the one thing the mode needed a keyboard for, and since
    // 2026-09-05 each cell is a mini flag with its own editor) — stepped a
    // cent at a time by the vertical arrows on the addressed cell, or typed
    // into that cell's own editor (GuiFlagEditor::enter_iter_bound_edit) —
    // and surfaced ON THE FLAG ITSELF while iteration mode is on and the
    // owner is not effectively disabled: the flag keeps its plain text and
    // grows two cells to its right, the lower bound then the upper, each
    // painted as another flag payload (render_flags, render.cpp). The flag
    // editor never carries the bracket: its line is the plain canonical
    // payload. A disabled owner's bracket is DORMANT — kept, off the flag,
    // out of the sweep and reachable by no editor until re-enabled; the
    // eligibility pair below. Signed tempo deltas in integer cents — the same
    // integer-cents domain the tempo itself lives in, so the sweep's per-cell
    // base + delta is plain integer addition. nullopt means "blank" (both
    // cells read `+0.00` — the one blank rule at format_iter_bound_cell
    // below); when set, both are set and iter_start_cents <= iter_end_cents.
    // A pair of two zeroes is the blank, not a zero-width sweep: both
    // authoring roads write through the one site that clears it
    // (iter_bound_step_write, app_state.h — the arrows' step and the cell
    // editor's commit alike), so two cells reading +0.00 always mean the same
    // thing. The retroactive clamp below is the one writer that does not ask,
    // and its own comment says why.
    std::optional<int64_t> iter_start_cents;
    std::optional<int64_t> iter_end_cents;

    // BPM mode. Session-only authoring state for basetempo-scale
    // sweeps; never serialized, lost on app close. The mode is SECTION-based
    // (architect 2026-07-23): a contiguous run of selected markers is chosen;
    // the FIRST owns (bpm_owner=true) and the sweep tempo covers the run's
    // whole extent — the sections every selected marker owns, the LAST
    // marker's section INCLUDED. bpm_endpoint holds the index that CLOSES
    // that last section (== store size means the span runs to the song end).
    // The map OUTSIDE the span keeps its shape: every effectively ENABLED
    // owning marker there is rescaled by the owner's own change in each cell
    // (bpm_cell_warp_markers, input_handler.h, the cell rewrite's one owner,
    // which since 2026-08-29 leaves a disabled marker untouched wherever it
    // sits; the `m` gate's one-tempo
    // rule over the selected run is what makes the owner's change the span's).
    // At most one marker at a time has bpm_owner=true (invariant
    // maintained by the `m` toggle handler). "Committed" is
    // implicit: bpm_beats > 0 means the owner has authored a value (parser
    // guarantees all three of bpm_beats, bpm_lo, bpm_hi are set together).
    // The value form is "<beats>@[<lo>,<hi>]" — beats a positive integer
    // (metronomes count integer beats, capped at kBpmBeatsMax), lo/hi
    // doubles within the bpm bracket [kBpmMin, kBpmMax], lo <= hi.
    bool   bpm_owner = false;
    int    bpm_beats = 0;
    double bpm_lo    = 0.0;
    double bpm_hi    = 0.0;

    // Session-only, set with bpm_owner on the `m`-press section gate.
    // THE INDEX OF THE FIRST EFFECTIVELY-ENABLED MARKER STRICTLY AFTER THE
    // LAST SELECTED ONE (architect 2026-08-24; it was the raw index one past
    // the last selected marker until then, which measured the span short
    // whenever a disabled marker sat at its edge): section_end_index below,
    // called on the last selected marker, is the one owner of that walk and
    // the phase-reset propagate's too. The BPM region runs
    // [this owner, bpm_endpoint) over the store, and the tempo covers the
    // sections owned by every marker in that half-open range — which is every
    // SELECTED marker plus any disabled markers trailing them, the disabled
    // ones taking no part in the render and so being swept in without being
    // written at all. bpm_endpoint == store size is the SONG-END sentinel: no
    // enabled marker follows, so the last section runs to total_frames and
    // there is no closing marker. When bpm_endpoint < size, the marker at that
    // index is the boundary: it is effectively enabled, it owns the FOLLOWING
    // section (outside the span), and the sweep treats it exactly as it
    // treats every other marker outside the span — rescaled when it owns
    // its tempo, left as it is when it is a pass or a ref. -1 when unset.
    // Not serialized.
    int  bpm_endpoint = -1;
};

// Slice a GUI authoring vector down to the serialized base. Each element is
// copy-constructed as a WarpMarker from its GuiWarpMarker (the derived
// iter_*/bpm_* state is dropped). Used at the render boundary so the
// parser-domain resolver and the engine-bound pipeline never see
// GUI-only fields. O(n) and called only on marker/scale change, not per
// paint frame.
inline std::vector<WarpMarker> slice_to_warp_markers(
    const std::vector<GuiWarpMarker>& src) {
    return std::vector<WarpMarker>(src.begin(), src.end());
}

// The store mechanics (sorted vector, generation token, insert/remove/mut
// accessors) are the shared GuiMarkerStore base (marker_store.h); this
// class carries the warp column's parse and serializer surfaces.
class GuiWarpMarkers : public GuiMarkerStore<GuiWarpMarker> {
public:
    // Parses `path`. On success, populates markers() and returns the parsed
    // markers. The first malformed line aborts the parse and returns a
    // one-line error; a missing/unopenable file is a failure (callers that
    // treat absence as "no markers" check existence first). No throw.
    //
    // `path_free_reason` is the parser's own out-parameter, passed through
    // unread: an unopenable or unreadable file writes its words with no path
    // in them there, so a caller composing a card names the file once, its
    // own way (parse_warpmarkers_file, warpmarkers_parse.h).
    std::expected<void, std::string> load(
        const std::string& path,
        std::optional<std::string>* path_free_reason = nullptr);

    // Writes the canonical form to `path`. Atomic: writes to
    // <path>.tmp, fsyncs, then renames. Preserves existing permissions or
    // uses 0644 if the file is new. Returns true on success.
    bool save(const std::string& path) const;

    // Static variant for callers that hold a raw GuiWarpMarker vector (e.g.
    // the render pipeline writing the authored .warpmarkers copy beside a
    // batch render). Same on-disk format as the instance method: authored
    // positions, whole source frames as plain integer text.
    static bool save(const std::string& path,
                     const std::vector<GuiWarpMarker>& markers);
};

// The `.warpmarkers` file's exact bytes for `markers`, built and returned
// without touching disk — the string half both save() overloads hand to the
// atomic writer, so the two can never diverge. Its other consumer is the
// GitHub recheck's "now" side (history_diff.h), which diffs the live store
// against a committed snapshot and needs precisely what a Ctrl+S would land at
// this instant, with no file anywhere.
std::string format_warpmarkers_text(const std::vector<GuiWarpMarker>& markers);

// True if the marker at `idx` should render as disabled. `disabled` is allowed
// on any marker — a locally set flag always counts. For an active
// (non-locally-disabled) `label_ref`, the cascade rule applies: the ref
// inherits its target label_def's disabled state.
bool effective_disabled(const std::vector<GuiWarpMarker>& markers, int idx);

// THE SECTION RULE'S ONE OWNER (architect 2026-07-23; in its EFFECTIVE-
// PARTICIPATION form for the propagate spans since 2026-08-01 and for the BPM
// sweep since 2026-08-24, so the product now states the rule once). Index of
// the marker that CLOSES the section owned by `i`: the first marker after it
// that PARTICIPATES IN THE RENDER (the next effectively-enabled one), or
// mv.size() when none follows — the SONG-END sentinel, the section running to
// the end of the piece. A disabled marker is dropped before the warp map is
// built (warp_markers_render_keep_mask, the participation verdict's one owner
// — "as if the marker were not present"), so it is not a section boundary at
// all: its span belongs to the preceding enabled marker, and a duration
// measured to it would be short by the whole remainder. An unlabeled ENABLED
// marker IS a boundary — it warps its own section, and is only excluded from
// the label sequence.
//
// TWO READERS, ONE WALK. The phase-reset propagate takes the FRAME through
// section_end_frame below (its copy loop and its destination walk both, so the
// clipboard's blocks and the paste's cannot be measured differently, and the
// resets lying under a disabled marker are captured, cleared and pasted with
// the section they musically belong to). The `m` BPM sweep takes the INDEX: it
// records the boundary marker's store slot on the owner's bpm_endpoint, bounds
// the per-cell pass rewrite with it, and reads the span-end frame off it — a
// raw next-in-store boundary made the span duration, and so every derived cell
// tempo, wrong whenever a disabled marker sat at the edge. The index form is
// the owner and the frame form calls it, so the two cannot drift.
//
// effective_disabled re-scans the store for a disabled def on every label-ref
// query, so this forward scan is worst-case O(n^2) across a whole walk. That is
// the deliberate choice over a per-walk keep-mask: both readers are discrete
// commands over tens-to-hundreds of markers, and one shared expression is worth
// more here than callers each carrying their own cached mask.
inline int section_end_index(const std::vector<GuiWarpMarker>& mv, int i) {
    const int n = static_cast<int>(mv.size());
    for (int j = i + 1; j < n; ++j) {
        if (!effective_disabled(mv, j)) return j;
    }
    return n;
}

// The FRAME form of section_end_index above, for callers that want the extent
// rather than the boundary's store slot: the closing marker's authored frame,
// or `song_end_frame` when the section runs to the song end.
inline int64_t section_end_frame(const std::vector<GuiWarpMarker>& mv, int i,
                                 int64_t song_end_frame) {
    const int j = section_end_index(mv, i);
    return (j < static_cast<int>(mv.size())) ? mv[j].time_frame
                                             : song_end_frame;
}

// (THERE IS NO LABEL-CASCADE RESOLVER FOR THE MEASURE FIELD, and the absence
// is a RULING rather than a gap — architect 2026-08-20, reversing his own
// ruling of the day before. The field INHERITED down the label cascade for one
// day: a ref carrying no value of its own displayed the definition's, the way
// it takes that definition's tempo. He drove it and reversed it on what the
// field SAYS: it is a POSITION IN THE SCORE, true where the definition sits and
// FALSE at a reference sitting bars later, so rippling it puts a confident
// wrong bar number on every copy. EVERY MEASURE IS ITS OWN, hand-authored,
// inherited from nothing down this axis. The flag painters read the plain field
// on both columns, which is why the phase column needed no counterpart and the
// warp one no longer has any. Do not re-derive one from the label cascade; the
// cascade carries tempo and the disabled bit, and those are values that a copy
// genuinely shares.
//
// THIS RULING IS ABOUT THE CASCADE AXIS ALONE and says nothing about the
// measure grammar's own '+' OFFSET FORM, which is a different axis entirely:
// it runs PREDECESSOR TO SUCCESSOR down the store, never definition to ref, it
// is owned by marker_measure.h where its semantics are stated once, and it is
// resolved at ACT time by the consumer rather than at display time here. A
// resolver for it is not the resolver this ruling forbids.)

// (parse_single_canonical_line is declared in warpmarkers_parse.h, included
// above; flag_editor.cpp sees it transitively through this header.)

// Signed tempo-delta cents -> the explicit-sign two-decimal text ("+1.50",
// "-0.50", "+0.00"). The signed session-only sibling of format_tempo_cents
// (value_format.h): iter deltas live in integer cents, so their display
// text formats directly from cents with no double round-trip. Used by the
// inline iter bracket below and the iteration sweep's delta CSV filenames.
inline std::string format_signed_delta_cents(int64_t cents) {
    std::string s(1, cents < 0 ? '-' : '+');
    const int64_t a = cents < 0 ? -cents : cents;
    s += std::to_string(a / 100);
    s += '.';
    s += static_cast<char>('0' + (a % 100) / 10);
    s += static_cast<char>('0' + a % 10);
    return s;
}

// The cells of a marker's flag run, in painted order (architect 2026-09-04,
// the bound cells; one enum for the press, the axis and the editors since
// 2026-09-05). Payload is the flag box itself — the composed line on a warp
// marker, the display token on a phase reset — Lower and Upper are the two
// bound cells iteration mode paints to its right on an eligible warp marker,
// and Measure is the blue box that follows. It answers three questions with
// one value: WHICH BOX a press landed on (hit_test_flag_cell, app_state.cpp,
// off the painter's published boundaries), WHICH CELL OF THE FOCUS IS
// ADDRESSED (AppState::addressed_cell — the bright cell, the cell the vertical
// arrows step, the cell Enter opens) and WHICH EDITOR a double-click or Enter
// opens (the Payload editor, the bound editor on Lower or Upper, the measure
// editor). The type lives here, beside the bracket two of its members
// address, because the painter (render.h) needs it and render.h cannot see
// AppState.
enum class MarkerCell { Payload, Lower, Upper, Measure };

// Iteration mode: the text of ONE bound cell — the bound in the signed
// two-decimal form, `+0.00` for a blank bracket on either side. THE BLANK
// RULE HAS THIS ONE HOME: the flag's two cells and the bound editor's seed
// (GuiFlagEditor::enter_iter_bound_edit) both read it, so what a cell shows
// and what its editor opens with are one spelling of one value. `side` is
// Lower or Upper; any other member answers the lower bound, the harmless
// reading, since neither the payload nor the measure carries a delta.
inline std::string format_iter_bound_cell(const GuiWarpMarker& m,
                                          MarkerCell side) {
    const bool blank = !m.iter_start_cents.has_value() ||
                       !m.iter_end_cents.has_value();
    if (blank) return format_signed_delta_cents(0);
    return format_signed_delta_cents(side == MarkerCell::Upper
                                         ? *m.iter_end_cents
                                         : *m.iter_start_cents);
}

// Iteration mode: which markers CAN CARRY a bracket — the STRUCTURAL half of
// the eligibility. An owning marker (tempo_inherits=false AND no label_ref)
// has a base tempo for the sweep's per-cell delta to ride; a pass and a
// label_ref marker have none, so neither ever carries a bracket: every route
// that turns a carrier into a non-carrier clears both bounds (the flag
// editor's commit, Ctrl+N's owner->pass and ref->pass conversions —
// toggle_inherits, warpmarkers_ops.cpp). DISABLEMENT IS NOT SUCH A LOSS
// (architect 2026-09-02, the four-tier review's R-12 — "a disabled marker is
// invisible to the act", the `m` BPM sweep's own rule asked of the iteration
// sweep): a disabled owner keeps its bracket DORMANT, never cleared, so the
// bracket returns to the flag and to the sweep the moment the marker is
// re-enabled. A dormant bracket is reachable by no editor while the marker
// is disabled: the cells are its only authoring surface and a disabled owner
// paints none, so it waits, kept, for the re-enable. THE READERS: the flag
// editor's commit's carrier-loss clear (a marker that stops owning its tempo
// loses its bracket — the store's rule, not any grammar's), the bound step's
// kind refusal (iter_bound_step_kind_refusal), and the retroactive clamp
// below.
inline bool iter_bracket_carrier(const GuiWarpMarker& m) {
    return !m.tempo_inherits && m.label_ref.empty();
}

// Iteration mode: which markers THE SWEEP READS — a carrier that is not
// effectively disabled. The disabled verdict is asked of the ONE cascade owner
// (effective_disabled above, the resolver's own keep mask) so the sweep can
// never count a marker the render drops: for a carrier the cascade reduces to
// the marker's own bit today (a label ref, the only marker the cascade
// reaches, is no carrier), and it is asked through the vector/index form all
// the same so that a change to the cascade lands here for free and no caller
// can hand a bare marker and lose it — which is why this is the ONE spelling
// and the single-marker form above carries a different name. FIVE READERS,
// and a disabled owner's bracket is dormant at all five: the sweep's
// dispatch (run_iteration_sweep_render, input_key_dispatch.cpp) and its face's
// plan (iteration_sweep_plan, app_state.h) skip the marker, so its bracket
// neither multiplies the cell count nor names a byte-identical cell — the
// marker's tempo is render-filtered whatever value a cell wrote into it; the
// flag painter (render_flags, render.cpp) paints the two bound cells on
// exactly these markers and no cells on a disabled flag; the bound step
// (GuiWarpMarkersOps::adjust_iter_bound_cents) skips an ineligible member in
// a group and refuses an ineligible singleton on a card; and the bound
// editor's open (GuiFlagEditor::enter_iter_bound_edit) refuses where no cell
// paints — no cell, no editor. (The name is the retired hover popup's — the
// eligibility rule outlived the surface that first displayed it.)
inline bool iter_popup_eligible_marker(const std::vector<GuiWarpMarker>& mv,
                                       int idx) {
    if (idx < 0 || idx >= static_cast<int>(mv.size())) return false;
    return iter_bracket_carrier(mv[static_cast<size_t>(idx)]) &&
           !effective_disabled(mv, idx);
}

// Iteration mode: THE ITER BRACKET RIDES ITS BASE (architect 2026-08-02).
// The one owner of the retroactive clamp — every site that moves a marker's
// BASE tempo while a live bracket rests on it calls this right after the new
// base is written. Every sweep cell renders base + delta and the deltas run
// from iter_start_cents to iter_end_cents inclusive, so the two endpoints
// bound every cell; without this, a base walking toward an edge drags cells
// out of [kTempoMinCents, kTempoMaxCents] and the cell RENDERS (the frame-map
// build refuses only a non-positive tempo) into a render-entry sidecar whose
// strict tempo parse hard-rejects on load — the `'` load-in-place refuses it
// and the
// CLI insurance path is dead for that entry. Folding the deltas into
// [kTempoMinCents - base, kTempoMaxCents - base] makes the sweep's cell
// vocabulary closed by CONSTRUCTION rather than by discipline.
//
// The callers are the two base-tempo authoring surfaces: the bare Up/Down
// cent step (both arms, warpmarkers_ops.cpp) and the flag editor's commit
// (flag_editor.cpp, which types no bound and folds whatever bracket rests on
// the base it just moved). The labour is divided by which value is being
// authored: a bound TYPED into its cell's editor gates LOUD at that commit
// (red flash, a card naming the wall) because it is authored input arriving
// at its own surface; later base motion clamps SILENTLY, because there the
// base is what is being authored and the bracket is the passenger. Neither
// bound road is a caller: the bound STEP (the arrows on a bound cell,
// adjust_iter_bound_cents) lands through iter_bound_step_landing
// (app_state.h), which clamps into this same window as it steps, and the
// bound EDITOR's commit refuses outside it, so nothing either writes needs
// folding after the fact.
//
// A blank bracket (either bound nullopt) is untouched — no cells, nothing to
// bound. A bracket that lands FULLY outside degenerates to a zero-width delta
// at the window edge: a valid one-cell sweep and the accepted result, never
// cleared to nullopt (a clear would silently drop the marker from the sweep's
// delta CSV and change the product's shape). That edge is [0, 0] only when the
// base rests exactly on a tempo wall (kTempoMinCents or kTempoMaxCents, where
// one limit is zero) and the whole bracket lies outside it, and this owner
// still does not clear there: the ruling above is the base's passenger rule,
// and it outranks the blank rule the two authoring roads keep (the field's own
// comment). The accepted residue is that one degenerate bracket reading +0.00
// in both cells — a cell the user authored elsewhere and the base then walked
// onto, not a bracket this file invented. Clamping both bounds into the
// SAME interval is monotone, so lo <= hi survives. The result also stays
// inside the session delta bracket [-kIterDeltaMaxCents, +kIterDeltaMaxCents]
// for free: an in-bracket base bounds either limit by
// kTempoMaxCents - kTempoMinCents = 375.
//
// Undo needs nothing of its own and none is invented: warp undo entries
// snapshot the WHOLE GuiWarpMarker vector, session-only iter fields included
// (the row-identity compare in undo.cpp says so), so an undo of the tempo
// step restores the pre-clamp bracket together with the pre-step base — the
// clamp is exactly as undoable as the gesture that caused it.
inline void clamp_iter_bracket_to_tempo_bracket(GuiWarpMarker& m) {
    if (!m.iter_start_cents.has_value() || !m.iter_end_cents.has_value()) {
        return;
    }
    // A bracket rests only on a carrier (iter_bracket_carrier above — every
    // carrier loss clears both bounds; a disabled owner keeps its dormant one
    // and is an owner still), and an owner's tempo_cents is in-bracket at
    // every input surface, so lo_limit <= 0 <= hi_limit: the clamp window
    // always contains the zero delta and can never be empty. Exact integer
    // cents throughout, the domain the deltas live in.
    const int64_t lo_limit = kTempoMinCents - m.tempo_cents;
    const int64_t hi_limit = kTempoMaxCents - m.tempo_cents;
    m.iter_start_cents = std::clamp(*m.iter_start_cents, lo_limit, hi_limit);
    m.iter_end_cents   = std::clamp(*m.iter_end_cents,   lo_limit, hi_limit);
}

// BPM mode: an owning, enabled marker (owning = !tempo_inherits AND no
// label_ref). Defined separately from iter so the two predicates can
// diverge without cascading edits. They diverged first on the disabled term
// — the bpm owner had to be enabled (!m.disabled) while iter's did not,
// because a disabled bpm owner was a render-inert rewrite (the sweep
// authored a tempo onto a marker whose disabled state drops it from the
// resolved map) — and since 2026-09-02 (R-12) both carry it, the iteration
// sweep's through the vector/index form above. THIS ONE KEEPS THE PLAIN
// FLAG, the recorded asymmetry: for an OWNING marker (the other two
// conjuncts) raw disabled equals effective disabled — the effective-disabled
// cascade only reaches refs, which are excluded here — so no marker-vector/
// index threading is owed, and the bpm owner is asked of a marker the `m`
// gate already holds by value.
inline bool bpm_popup_eligible_marker(const GuiWarpMarker& m) {
    return !m.tempo_inherits && m.label_ref.empty() && !m.disabled;
}

// BPM mode: format the bracket-editor text for marker `m`.
// EMPTY when this marker is not the BPM owner, or when it is the owner with
// bpm_beats == 0 (owner-but-blank, set by the `m`-toggle-on transition before
// any commit; bpm_beats > 0 is the implicit "committed" sentinel — the parser
// sets all three of bpm_beats/bpm_lo/bpm_hi together, mirroring iter's NaN
// convention). THE BLANK FORM IS THE EMPTY STRING RATHER THAN "[]" (architect
// 2026-08-24): there is no bracket to fill here — the whole value
// `<beats>@[<lo>,<hi>]` is typed from nothing, so a seeded "[]" is a fragment
// the user has to edit around. Iter's "[]" is a different thing entirely: a
// real token of its own grammar, and the two forms deliberately no longer
// match. The non-empty form is the strict syntax `<beats>@[<lo>,<hi>]`.
inline std::string format_bpm_bracket_text(const GuiWarpMarker& m) {
    if (!m.bpm_owner || m.bpm_beats == 0) {
        return "";
    }
    // lo/hi print as bpm values: plain shortest round-trip form
    // (min_decimals 0 — "72" stays "72", "72.5" stays "72.5").
    std::string out = std::to_string(m.bpm_beats);
    out += "@[";
    out += format_value_double(m.bpm_lo, 0);
    out += ',';
    out += format_value_double(m.bpm_hi, 0);
    out += ']';
    return out;
}

// BPM mode: strict parser for "<beats>@[<lo>,<hi>]". beats must be a
// positive integer (metronomes count integer beats) capped at
// kBpmBeatsMax; lo/hi are doubles (parse_value_double strictness) within
// the bpm bracket [kBpmMin, kBpmMax] (value_format.h) with lo <= hi
// (degenerate lo=hi is valid), each pinned to ONE canonical spelling — the
// bpm writer's form (format_value_double, min 0 decimals), so "210" and
// "210.5" commit while "210.0" and "210.50" refuse; no whitespace, no
// missing fields, no alternate forms. On failure returns false and leaves
// out-params unchanged.
inline bool parse_bpm_bracket(const std::string& s,
                              int& beats, double& lo, double& hi) {
    if (s.empty()) return false;
    const auto at_pos = s.find('@');
    if (at_pos == std::string::npos) return false;
    if (s.find('@', at_pos + 1) != std::string::npos) return false;
    const std::string left  = s.substr(0, at_pos);
    const std::string right = s.substr(at_pos + 1);
    if (left.empty() || right.empty()) return false;
    if (right.front() != '[' || right.back() != ']') return false;
    const std::string inner = right.substr(1, right.size() - 2);
    if (inner.empty()) return false;
    const auto comma = inner.find(',');
    if (comma == std::string::npos) return false;
    if (inner.find(',', comma + 1) != std::string::npos) return false;
    const std::string lo_s = inner.substr(0, comma);
    const std::string hi_s = inner.substr(comma + 1);
    if (lo_s.empty() || hi_s.empty()) return false;
    auto digits_only = [](const std::string& v) {
        for (char c : v) {
            if (c < '0' || c > '9') return false;
        }
        return !v.empty();
    };
    if (!digits_only(left)) return false;
    auto parse_pos_int = [](const std::string& v, int& out) -> bool {
        long long acc = 0;
        for (char c : v) {
            acc = acc * 10 + (c - '0');
            if (acc > kBpmBeatsMax) return false;
        }
        if (acc <= 0) return false;
        out = static_cast<int>(acc);
        return true;
    };
    int    b = 0;
    double l = 0.0, h = 0.0;
    if (!parse_pos_int(left, b))                  return false;
    if (!parse_value_double(lo_s, l) || l < kBpmMin || l > kBpmMax ||
        format_value_double(l, 0) != lo_s) return false;
    if (!parse_value_double(hi_s, h) || h < kBpmMin || h > kBpmMax ||
        format_value_double(h, 0) != hi_s) return false;
    if (l > h) return false;
    beats = b;
    lo    = l;
    hi    = h;
    return true;
}
