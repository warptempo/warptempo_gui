#pragma once

// Synthesis-spectrum trig, isolated in its own translation unit so it can be
// compiled with -ffast-math (see the .cpp) without relaxing the rest of the
// engine's strict FP semantics. Fills out[k] = mag[k] * (cos theta[k], sin
// theta[k]) for k in [0, count) — the interior-bin synthesis coefficients the
// caller then IFFTs. The __restrict qualifiers are the no-aliasing guarantee
// the caller upholds (theta, mag, and out are distinct engine workspaces).
void synth_spectrum_trig(const double* __restrict theta,
                         const double* __restrict mag,
                         double (* __restrict out)[2], int count);
