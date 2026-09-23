#pragma once

#include <cstdint>
#include <vector>

// THE WAVEFORM GAIN: the picture's continuous magnification, derived from the
// source at load (architect 2026-09-23, "the sausage"). Every quiet passage is
// squashed graphically up to the lane edge so its transient onsets show as
// plainly as a tutti's; the loud body clipping flat at the edge is accepted.
// It is a PICTURE gain and nothing else: no sample, no render input and no
// render fingerprint field ever reads it.
//
// The measure reads THE PICTURE, not the audio. Its unit is the painted
// column at WORKING ZOOM — the placement-instrument zoom, where one column is
// kZoomBaseMsPerPx * 2^(kWorkingZoomLevel - 1) of source time (55 frames at
// 44.1 kHz) — and a column's height is the plate renderer's own min/max
// reduced to one number: the largest |x| over both channels.
//
// THE RULE, whole:
//
//   THE MEASURE. At every analysis hop, d: the number of doublings the
//   typical column top (the kTopPercentile order statistic of the gated
//   column heights in a centred window of kWindowSeconds) sits under 0 dBFS,
//   -dB(top) / kLevelDb. Columns under kGateDb are silence or tape hiss and
//   are gated out; a window with too little audible material takes the d of
//   the NEARER audible hop, so the curve is the same whichever way the audio
//   runs (up to the hop lattice and the earlier-on-a-tie choice, below). An
//   all-silent song takes d = 3 everywhere.
//
//   THE GAIN. g = 2^d, clamped to [kGainMin, kGainMax]: the gain that puts
//   the window's typical column top exactly on the lane edge. That is the
//   whole derivation — no classification, no absorption, no boundary
//   placement, no look-ahead, no smoothing and no hysteresis (each ruled out
//   by the architect, 2026-09-23). Between hops the gain is linear IN GAIN.
//
// A centred window's kTopPercentile reports loud once a tenth of it is loud,
// so the quiet before a loud entry fades down over roughly the last quarter
// of a working-zoom screen (~0.6 s) and stays down over the first quarter
// after it — symmetric in time, with no forward look-ahead (literally so only
// up to the hop lattice, whose hops sit at multiples of the hop from frame 0,
// and the nearer-known rule's earlier-on-a-tie choice for a silent hop) — and
// a lone
// accent inside a quiet passage halos its neighbours for about half a window
// on each side. Both are the rule's shape, accepted by the architect, not
// defects.
//
// The constants are the architect's. THE PRINCIPLE FOR ANY RETUNE: every
// number is FORCED by a criterion and never tuned to one spot — a free
// constant carries its reason, a derived one its derivation — and a passage
// the rule gets wrong is answered by the `[` lamp (the flat picture) or the
// A/B tabs, not by a new number.
//
// Pure: no application state, no audio object, no allocation that outlives
// the call.

// The picture's continuous magnification, derived from the source at load.
struct WaveformGainCurve {
    int64_t             hop_frames = 0;   // source frames between consecutive gains; gain[k] sits at frame k * hop_frames
    std::vector<double> gain;             // 2^d per hop, each in [kGainMin, kGainMax]; empty for a zero-frame source
};

// `interleaved` is stereo float32, `total_frames` frames (2 * total_frames
// floats) — the decoded source buffer as GuiAudio holds it. A zero-frame
// input returns the empty curve.
WaveformGainCurve derive_waveform_gain(const float* interleaved, int64_t total_frames,
                                       int sample_rate);

// The gain at one source frame: linear between the two nearest hops, the
// first and last hop's gain held beyond the ends. 1.0 for an empty curve.
double waveform_gain_at(const WaveformGainCurve& curve, int64_t frame);

// The derivation's identity for the plate fingerprint: bump on any change to
// the rule above.
inline constexpr uint64_t kWaveformGainVersion = 1;
