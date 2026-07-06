#pragma once

// warptempo's locked engine geometry. The engine core takes N as a runtime
// parameter; these are the fixed values every driver (GUI render pipeline,
// render CLI, engine CLI) hands it. N = 4096 matches the LTFAT released
// reference implementation (github.com/ltfat/pvdoneright) with M = 2N = 8192
// and R_s = N/4 = 1024; the PGHI paper text states a 4092-sample window -- a
// figure absent from the released code -- and this engine follows the
// implementation. Neither value is authoring-tunable -- not a flag, not a
// settings field, not a variable.
//
// Single source of truth for the primitive constant and the derived
// synthesis hop. Every driver pulls these here rather than recomputing them,
// so the drivers cannot drift (a mismatch would only surface as a cmp
// failure at render time).
constexpr int kN = 4096;

// Synthesis hop: one quarter of the window. The /4 relationship is the
// invariant, so it is written out rather than as a literal.
constexpr int kRs = kN / 4;
