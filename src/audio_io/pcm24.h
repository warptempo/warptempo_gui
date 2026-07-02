#pragma once

#include <cstdint>

// The PCM_24 boundary policy is chosen for exactness over mirror symmetry.
// Signed 24-bit is inherently lopsided: 2^24 codes is an even count and zero
// must be a code, so the negative side carries one extra code
// ([-8388608, +8388607]). No full-range encoder can be mirror-symmetric.
// Decode here is c / 2^23, so the unique encode that exactly inverts it is
// round(x * 2^23) with a clamp. The symmetric alternative scales by
// 2^23 - 1 instead. That is mirror-perfect, but it cannot invert this decode,
// breaking roundtrip identity on loud samples, and it gives every sample a
// uniform sub-LSB gain. This project's gates are roundtrip gates: bit-identity
// and null tests. Inverse exactness therefore outranks mirror symmetry at the
// single input, -1.0, where the two conflict. The clamp makes out-of-range
// input saturate instead of wrapping to the opposite polarity under 3-byte
// truncation. Scaling and rounding run in double via llrint, the project's
// round-half-to-even convention, which is sign-symmetric by parity; NaN maps
// to code zero.
int32_t pcm24_code_from_float(float x);

// Every signed 24-bit code divided by 8388608.0f is exactly representable in
// float32, making the decode direction exact by construction.
float pcm24_float_from_code(int32_t c);

// Snap a float onto the signed 24-bit lattice. The exhaustive codec self-test
// proves quantize/dequantize identity over all 2^24 codes.
float pcm24_quantize(float x);
