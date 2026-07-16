#include "synth_spectrum_trig.h"

#include <cmath>

// This TU is compiled with -ffast-math (scoped to this one source in
// CMakeLists.txt, so the rest of the engine keeps strict FP semantics). Under
// that relaxation GCC vectorizes the separate cos/sin calls below to glibc's
// libmvec (_ZGVdN4v_cos / _ZGVdN4v_sin, the AVX2 four-double forms on this
// Arch/glibc host, with SSE and scalar tails compiler-generated); the
// two-output sincos form does not auto-vectorize, which is why the pair is
// written out. The __restrict qualifiers declare the caller's no-aliasing
// guarantee (distinct engine workspaces). Results are a fixed pure function of
// the inputs under one binary — bit-identical run-to-run for a given ifunc
// dispatch — and differ from scalar libm by only a few ulps; that reference
// change is carried by the fingerprint version. Inputs are finite by engine
// invariant (analysis magnitudes and propagated phases), which is what makes
// the fast-math contract safe here.
void synth_spectrum_trig(const double* __restrict theta,
                         const double* __restrict mag,
                         double (* __restrict out)[2], int count) {
    for (int k = 0; k < count; ++k) {
        out[k][0] = mag[k] * std::cos(theta[k]);
        out[k][1] = mag[k] * std::sin(theta[k]);
    }
}
