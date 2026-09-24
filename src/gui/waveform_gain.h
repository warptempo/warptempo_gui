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
//   THE MEASURE. At every analysis hop, L: the window's typical column top in
//   dBFS — 20 log10 of the `percentile` order statistic of the gated column
//   heights in a centred window of `window_s` seconds. Columns under
//   `gate_db` are silence or tape hiss and are gated out; a window with less
//   than `min_fraction` of its columns audible takes the L of the NEARER
//   audible hop, so the curve is the same whichever way the audio runs (up
//   to the hop lattice and the earlier-on-a-tie choice, below). An
//   all-silent song takes d = 3 everywhere.
//
//   THE GAIN. d, the doublings of picture gain, puts every window's typical
//   top exactly on the lane edge,
//       d = -L / 6.02
//   (no max(0, .) is needed: L <= 0 for a decoded PCM peak, so d >= 0), and
//   g = 2^d, clamped to [kGainMin, `gain_max`]. That is the whole derivation
//   (the RULED-OUT list below names what it deliberately is not). Between
//   hops the gain is linear IN GAIN.
//
// RULED OUT, never to be re-proposed (architect 2026-09-23): classification,
// absorption, boundary placement, forward look-ahead, smoothing, hysteresis,
// a dead zone, a second window — and an expander (a threshold and a ratio,
// each dB of L under the threshold earning ratio dB of gain), tried and
// rejected 2026-09-23 after the architect eyeballed it against the plain
// leveler: the contrast he wanted came from the window alone at 3 s, which
// paints the tuttis as flat blocks while the quiet passages keep their
// texture (at -3 dBFS / 1.18 it would have dropped the corpus's tutti from
// x2.2 to x1.7 and raised the quiet-over-loud contrast from 4.1..4.7 to
// 5.3..6.2).
//
// A centred window's order-statistic top reports loud as soon as the loud
// share of it reaches the percentile's complement — at the default 1.00, as
// soon as ONE column of it is loud — so the quiet before a loud entry fades
// down over roughly the last half-window before it and stays down over the
// first half-window after it (1.5 s each side at the default 3 s window) —
// symmetric in time, with no forward look-ahead (literally so only up to the
// hop lattice, whose hops sit at multiples of the hop from frame 0, and the
// nearer-known rule's earlier-on-a-tie choice for a silent hop) — and a lone
// accent inside a quiet passage halos its neighbours for about half a window
// on each side. Both are the rule's shape, accepted by the architect, not
// defects: the dip before a tutti is musically right (the reason at the
// window's default, waveform_gain.cpp).
//
// THE FIVE TUNABLES ARE THE DEVICE CONFIG'S (architect 2026-09-23, a tuning
// phase; they may be hard-coded again later): `WaveformGainParams` below,
// read once at startup from `waveform_gain_*` keys (device_config.h owns their
// grammar and brackets) and handed to every derivation — so a retune is a
// file edit and a relaunch, not a recompile. The hop, the gain floor and the
// column stay fixed in waveform_gain.cpp. THE
// PRINCIPLE FOR ANY RETUNE: every number is FORCED by a criterion and never
// tuned to one spot — a free constant carries its reason, a derived one its
// derivation — and a passage the rule gets wrong is answered by the
// magnification lamp (the flat picture) or the A/B tabs, not by a new number.
//
// Pure: no application state, no audio object, no allocation that outlives
// the call.

// THE RULE'S FIVE TUNABLES, in the device config's writer order. The member
// initializers ARE the defaults both backends' first-run templates stamp
// (kDefaultWaveformGainParams; each default's reason is at waveform_gain.cpp)
// — construction state, never a load fallback: every key is required. The
// derivation takes the values as given; their brackets are the config
// reader's (device_config.h), the one producer.
struct WaveformGainParams {
    double window_s     = 3.0;    // the centred window, seconds
    double percentile   = 1.0;    // the typical top's order statistic
    double gate_db      = -50.0;  // the audibility gate, dBFS
    double min_fraction = 0.25;   // the gated window's minimum audible share
    double gain_max     = 16.0;   // the cap
};
inline constexpr WaveformGainParams kDefaultWaveformGainParams{};

// The picture's continuous magnification, derived from the source at load.
struct WaveformGainCurve {
    int64_t             hop_frames = 0;   // source frames between consecutive gains; gain[k] sits at frame k * hop_frames
    std::vector<double> gain;             // 2^d per hop, each in [kGainMin, gain_max]; empty for a zero-frame source
};

// `interleaved` is stereo float32, `total_frames` frames (2 * total_frames
// floats) — the decoded source buffer as GuiAudio holds it. A zero-frame
// input returns the empty curve. `params` are the device config's (the one
// struct gui_main reads at startup, reached through AppState::device_config).
WaveformGainCurve derive_waveform_gain(const float* interleaved, int64_t total_frames,
                                       int sample_rate, const WaveformGainParams& params);

// The gain at one source frame: linear between the two nearest hops, the
// first and last hop's gain held beyond the ends. 1.0 for an empty curve.
double waveform_gain_at(const WaveformGainCurve& curve, int64_t frame);

// The derivation's identity for the plate fingerprint: bump on any change to
// the rule above (3 since the expander was deleted and the leveler restored,
// 2026-09-23; 2 was the expander's). The five tunables are
// NOT in the fingerprint and need not be: they are read once per process and
// never change under it, and nothing derived from the gain outlives the
// process — the curve is derived at every load (the `.peaks` sidecar carries
// no curve) and the plates live in memory only.
inline constexpr uint64_t kWaveformGainVersion = 3;
