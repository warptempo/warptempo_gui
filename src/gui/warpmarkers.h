#pragma once

#include "marker_store.h"
#include "value_format.h"
#include "warpmarkers_parse.h"

#include <cmath>
#include <cstdio>
#include <expected>
#include <limits>
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
    // serialized, lost on app close, populated and edited through the marker-text
    // lane — the bracket surfaces in the hover lane text and is authored in the
    // Enter flag editor (the flags themselves are textless). Signed tempo deltas
    // in integer cents —
    // the same integer-cents domain the tempo itself lives in, so the
    // sweep's per-cell base + delta is plain integer addition. nullopt
    // means "blank" (popup shows []); when set, both are set and
    // iter_start_cents <= iter_end_cents.
    std::optional<int64_t> iter_start_cents;
    std::optional<int64_t> iter_end_cents;

    // BPM mode. Session-only authoring state for basetempo-scale
    // sweeps; never serialized, lost on app close. The mode is a two-marker
    // explicit span: of the two selected markers, the earlier owns
    // (bpm_owner=true) and the later closes the span (its index held in this
    // owner's bpm_endpoint). At most one marker at a time has bpm_owner=true
    // (invariant maintained by the `m` toggle handler). "Committed" is
    // implicit: bpm_beats > 0 means the owner has authored a value (parser
    // guarantees all three of bpm_beats, bpm_lo, bpm_hi are set together).
    // The value form is "<beats>@[<lo>,<hi>]" — beats a positive integer
    // (metronomes count integer beats, capped at kBpmBeatsMax), lo/hi
    // doubles within the bpm bracket [kBpmMin, kBpmMax], lo <= hi.
    bool   bpm_owner = false;
    int    bpm_beats = 0;
    double bpm_lo    = 0.0;
    double bpm_hi    = 0.0;

    // Session-only, set with bpm_owner on the `m`-press two-marker gate.
    // Index of the span's closing marker (the later of the two selected).
    // The BPM region runs [this owner, bpm_endpoint) — the endpoint marker
    // closes the span and is not a member. -1 when unset. Not serialized.
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
    std::expected<void, std::string> load(const std::string& path);

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

// True if the marker at `idx` should render as disabled. `disabled` is allowed
// on any marker — a locally set flag always counts. For an active
// (non-locally-disabled) `label_ref`, the cascade rule applies: the ref
// inherits its target label_def's disabled state.
bool effective_disabled(const std::vector<GuiWarpMarker>& markers, int idx);

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

// Iteration mode: format the inline bracket segment spliced into
// an eligible flag after the tempo. The leading `+` is the
// "relative to base" cue; the blank (both iter values nullopt) renders as
// the zero-filled `+[+0.00,+0.00]`, set values as the same signed
// two-decimal form (no space after the comma, so the display form matches
// the typeable form). This is the single locked display form.
inline std::string format_iter_bracket_inline(const GuiWarpMarker& m) {
    if (!m.iter_start_cents.has_value() || !m.iter_end_cents.has_value()) {
        return "+[+0.00,+0.00]";
    }
    std::string out = "+[";
    out += format_signed_delta_cents(*m.iter_start_cents);
    out += ',';
    out += format_signed_delta_cents(*m.iter_end_cents);
    out += ']';
    return out;
}

// Iteration mode: an owning marker (tempo_inherits=false AND no
// label_ref) gets a persistent iteration popup. Pass markers and
// label_ref markers are excluded; disabled status does not matter.
inline bool iter_popup_eligible_marker(const GuiWarpMarker& m) {
    return !m.tempo_inherits && m.label_ref.empty();
}

// BPM mode: same eligibility shape as iter (owning marker, no
// label_ref). Defined separately so the two predicates can diverge later
// without cascading edits.
inline bool bpm_popup_eligible_marker(const GuiWarpMarker& m) {
    return !m.tempo_inherits && m.label_ref.empty();
}

// BPM mode: format the bracket-editor text for marker `m`.
// "[]" when this marker is not the BPM owner (matches iter's empty form
// exactly), or when it is the owner with bpm_beats == 0 (owner-but-blank,
// set by the `m`-toggle-on transition before any commit; bpm_beats > 0 is
// the implicit "committed" sentinel — the parser sets all three of
// bpm_beats/bpm_lo/bpm_hi together, mirroring iter's NaN convention).
// The non-empty form is the strict syntax `<beats>@[<lo>,<hi>]`.
inline std::string format_bpm_bracket_text(const GuiWarpMarker& m) {
    if (!m.bpm_owner || m.bpm_beats == 0) {
        return "[]";
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
