#pragma once

#include <cstdint>

// The project's rendered PCM_24 bytes currently come from system libsndfile,
// so a package upgrade can silently move the bit-identity baseline. This
// in-tree policy freezes the float-to-code boundary explicitly: NaN maps to
// zero; finite values are scaled by 8388608.0, rounded by llrint under the
// default round-half-to-even FP environment, then clipped to signed 24-bit.
int32_t pcm24_code_from_float(float x);

// Every signed 24-bit code divided by 8388608.0f is exactly representable in
// float32, making the decode direction exact by construction.
float pcm24_float_from_code(int32_t c);

// Snap a float onto the signed 24-bit lattice. The exhaustive codec self-test
// proves quantize/dequantize identity over all 2^24 codes.
float pcm24_quantize(float x);
