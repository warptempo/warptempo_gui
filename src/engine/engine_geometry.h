#pragma once

// Locked warptempo driver geometry. The engine core takes N as a runtime
// parameter; these are the canonical values every driver (GUI render pipeline,
// render CLI, engine CLI) hands it. N is the canonical PGHI window length;
// the phase-reset lead-in is one synthesis hop. Neither is authoring-tunable --
// not a flag, not a settings field, not a variable. Single source of truth so
// the three drivers cannot drift (a mismatch would only surface as a cmp
// failure at render time).
constexpr int    kCanonicalN           = 4096;
constexpr double kPhaseResetOffsetHops = 1.0;
