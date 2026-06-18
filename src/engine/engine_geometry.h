#pragma once

#include <cmath>
#include <cstdint>

// warptempo's locked engine geometry. The engine core takes N as a runtime
// parameter; these are the fixed values every driver (GUI render pipeline,
// render CLI, engine CLI) hands it. N = 4096 matches the LTFAT released
// reference implementation (github.com/ltfat/pvdoneright) with M = 2N = 8192
// and R_s = N/4 = 1024; the PGHI paper text states a 4092-sample window -- a
// figure absent from the released code -- and this engine follows the
// implementation. The phase-reset lead-in is one synthesis hop. None of these
// are authoring-tunable -- not a flag, not a settings field, not a variable.
//
// Single source of truth for both the primitive constants and the derived
// geometry (the synthesis hop R_s and the integer phase-reset offset in
// samples). Every driver pulls these here rather than recomputing them, so the
// drivers cannot drift (a mismatch would only surface as a cmp failure at
// render time).
constexpr int    kN                    = 4096;
constexpr double kPhaseResetOffsetHops = 1.0;

// Synthesis hop: one quarter of the window. The /4 relationship is the
// invariant, so it is written out rather than as a literal.
constexpr int    kRs                   = kN / 4;

// Phase-reset lead-in expressed in samples: the offset-in-hops scaled by the
// synthesis hop, banker's-rounded to the integer sample domain.
inline const int64_t phase_reset_offset_samples = static_cast<int64_t>(
    std::nearbyint(kPhaseResetOffsetHops * static_cast<double>(kRs)));
