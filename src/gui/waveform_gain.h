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
// THE LEVELER IS A LOUDNESS NORMALIZER PER WINDOW (architect 2026-09-24): one
// named audio tool, measuring each window's short-term loudness and bringing
// it to a target. Its unit is the painted column at WORKING ZOOM — the
// placement-instrument zoom, where one column is
// kZoomBaseMsPerPx * 2^(kWorkingZoomLevel - 1) of source time (55 frames at
// 44.1 kHz) — which carries two numbers read in one pass over the samples:
// its PEAK, the plate renderer's own min/max reduced to one number (the
// largest |x| over both channels), and its MEAN SQUARE (the sum of x^2 over
// its 2 * col samples, both channels together, divided by 2 * col).
//
// THE RULE, whole:
//
//   THE MEASURE. At every analysis hop (0.1 s), L: the SHORT-TERM LOUDNESS of
//   the centred window of `window_s` seconds — 10 log10 of the mean of the
//   mean squares of the window's AUDIBLE columns. The window and the hop are
//   the EBU short-term loudness's (3 s, 100 ms); the measure is UNWEIGHTED
//   and its only gate is ours: a column whose PEAK is at or under `gate_db`
//   is silence or tape hiss and is left out of the mean, so a rest inside a
//   window does not lower the window's loudness and blow up the notes around
//   it — the loudness is that of the sounding material. A window with less
//   than `min_fraction` of its columns audible takes the L of the NEARER
//   audible hop, so the curve is the same whichever way the audio runs (up
//   to the hop lattice and the earlier-on-a-tie choice). An all-silent song
//   takes x8 everywhere.
//
//   THE GAIN. g = 10^((target - L) / 20), `target_db` the target, clamped to
//   [kGainMin = 1, `gain_max`]: the window's loudness brought to the target,
//   NEVER ATTENUATED — a window already louder than the target takes x1, its
//   raw samples being unable to exceed the edge. Between hops the gain is
//   linear IN GAIN. That is the whole derivation (the RULED-OUT list below
//   names what it deliberately is not).
//
//   THE PAINTER multiplies each plate column's raw min/max by its gain and
//   clamps the pair to [-1, 1] (render_waveform); nothing else. The clamp is
//   a sample-peak clip, which is right for a picture: a transient-rich window
//   whose peaks overshoot the edge paints them flat.
//
// WHY THE MEASURE CHANGED (architect 2026-09-24). THE PEAK LEVELER
// (2026-09-23 to 2026-09-24, a superseded record): L was the MAXIMUM of the
// gated column peaks in the window, d = -L / 6.02 doublings, g = 2^d — pure
// peak normalization per window, the window's loudest column brought to the
// edge with nothing clipping. It was a step function: the gain dropped the
// instant ONE loud column entered the window, 1.5 s before an entry, and
// 4.8 % of hops jumped by more than 1 dB on the 40th's first movement,
// drawing a line across the picture that is not in the music. The RMS gain
// moves as the entry's energy fills the window, so the same dip arrives as a
// ramp (1.7 % of hops over 1 dB). A dynamics chain after the leveler — a
// short peak detector, a clipper, a downward compressor with make-up, a
// downward expander — is PLANNED AND NOT BUILT.
//
// RULED OUT, never to be re-proposed (architect 2026-09-23 unless dated):
// classification, absorption, boundary placement, forward look-ahead,
// smoothing, hysteresis, a dead zone, a second window, a percentile of the
// column peaks (the order statistic was a key for one day, 2026-09-23, 0.90
// the marker regime's; superseded with the peak measure) — and an expander (a
// threshold and a ratio, each dB of L under the threshold earning ratio dB of
// gain), tried and rejected 2026-09-23 after the architect eyeballed it
// against the plain leveler: the contrast he wanted came from the window
// alone at 3 s. THE UPWARD COMPRESSOR (2026-09-24, one day, four keys): a
// static curve applied per column at working zoom acts on each oscillation —
// a column is half a cycle at 440 Hz — so it lifted the zero crossings of low
// notes into a solid fill, and upward compression divides the floor's
// contrast by the ratio where relief was wanted. K-WEIGHTING stays out (the
// picture is amplitude, not the ear); the RELATIVE GATE and TRUE-PEAK
// OVERSAMPLING stay out (programme-loudness machinery; the clamp is a
// sample-peak clip, right for a picture).
//
// A centred window's loudness rises as a loud entry's energy fills it, so the
// quiet before a loud entry ramps down over roughly the last half-window
// before it and stays down over the first half-window after it (1.5 s each
// side at the default 3 s window) — symmetric in time, with no forward
// look-ahead (literally so only up to the hop lattice, whose hops sit at
// multiples of the hop from frame 0, and the nearer-known rule's
// earlier-on-a-tie choice for a silent hop) — and a lone accent inside a
// quiet passage lowers its neighbours' gain for about half a window on each
// side, by its share of the window's energy. Both are the rule's shape,
// accepted by the architect, not defects: the dip before a tutti is musically
// right (the reason at the window's default, waveform_gain.cpp).
//
// THE FIVE TUNABLES ARE THE DEVICE CONFIG'S (architect 2026-09-23, a tuning
// phase; they may be hard-coded again later): `WaveformGainParams` below,
// read once at startup from `waveform_gain_*` keys (device_config.h owns their
// grammar and brackets) and handed to every derivation — so a retune is a
// file edit and a relaunch, not a recompile. The hop, the gain floor and the
// column stay fixed in waveform_gain.cpp. THE PRINCIPLE FOR ANY RETUNE: every
// number is FORCED by a criterion and never tuned to one spot — a free
// constant carries its reason, a derived one its derivation — and a passage
// the rule gets wrong is answered by the magnification lamp (the flat
// picture) or the A/B tabs, not by a new number.
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
    double target_db    = -14.0;  // the window loudness the gain brings it to, dBFS
    double gate_db      = -50.0;  // the audibility gate on a column's peak, dBFS
    double min_fraction = 0.25;   // the gated window's minimum audible share
    double gain_max     = 16.0;   // the cap
};
inline constexpr WaveformGainParams kDefaultWaveformGainParams{};

// The picture's continuous magnification, derived from the source at load.
struct WaveformGainCurve {
    int64_t             hop_frames = 0;  // source frames between consecutive gains; gain[k] sits at frame k * hop_frames
    std::vector<double> gain;            // per hop, each in [kGainMin, gain_max]; empty for a zero-frame source
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
// the rule above (6 since the short-term loudness measure replaced the
// window's peak and the upward compressor was deleted, 2026-09-24; 5 was the
// upward compressor's threshold, knee and range, 4 the fixed percentile and
// the ratio alone, 3 the restored leveler, 2 the expander's). The five
// tunables are NOT in the fingerprint and need not be: they are read once per
// process and never change under it, and nothing derived from the gain
// outlives the process — the curve is derived at every load (the `.peaks`
// sidecar carries no curve) and the plates live in memory only.
inline constexpr uint64_t kWaveformGainVersion = 6;
