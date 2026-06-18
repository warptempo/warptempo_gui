#pragma once

#include <cmath>
#include <cstdint>

// Locked warptempo driver geometry. The engine core takes N as a runtime
// parameter; these are the canonical values every driver (GUI render pipeline,
// render CLI, engine CLI) hands it. N is the canonical PGHI window length;
// the phase-reset lead-in is one synthesis hop. Neither is authoring-tunable --
// not a flag, not a settings field, not a variable.
//
// Single source of truth for both the primitive constants and the derived
// geometry (the synthesis hop R_s and the integer phase-reset offset in
// samples). Every driver pulls these here rather than recomputing them, so the
// drivers cannot drift (a mismatch would only surface as a cmp failure at
// render time).
constexpr int    kCanonicalN           = 4096;
constexpr double kPhaseResetOffsetHops = 1.0;

// Synthesis hop: one quarter of the window. The /4 relationship is the
// invariant, so it is written out rather than as a literal.
constexpr int    kCanonicalRs          = kCanonicalN / 4;

// Phase-reset lead-in expressed in samples: the offset-in-hops scaled by the
// synthesis hop, banker's-rounded to the integer sample domain.
inline int64_t canonical_phase_reset_offset_samples() {
    return static_cast<int64_t>(
        std::nearbyint(kPhaseResetOffsetHops *
                       static_cast<double>(kCanonicalRs)));
}
