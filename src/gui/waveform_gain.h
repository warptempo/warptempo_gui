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
// ramp (1.7 % of hops over 1 dB). Of the dynamics stages weighed after the
// leveler on 2026-09-24, the DOWNWARD EXPANDER IS BUILT (THE EXPANDER below);
// the rest are RULED OUT (the list below).
//
// RULED OUT, never to be re-proposed (architect 2026-09-23 unless dated):
// classification, absorption, boundary placement, forward look-ahead,
// smoothing, hysteresis, a dead zone, a second window, a percentile of the
// column peaks (the order statistic was a key for one day, 2026-09-23, 0.90
// the marker regime's; superseded with the peak measure) — and an expander ON
// L (a threshold and a ratio, each dB of the WINDOW'S LOUDNESS under the
// threshold earning ratio dB of gain), tried and rejected 2026-09-23 after
// the architect eyeballed it against the plain leveler: the contrast he
// wanted came from the window alone at 3 s. (THE EXPANDER below is another
// stage: it reads each working column's LEVELED PEAK, with a hold and a
// release, never the window's L.) THE UPWARD COMPRESSOR (2026-09-24, one day, four keys): a
// static curve applied per column at working zoom acts on each oscillation —
// a column is half a cycle at 440 Hz — so it lifted the zero crossings of low
// notes into a solid fill, and upward compression divides the floor's
// contrast by the ratio where relief was wanted. K-WEIGHTING stays out (the
// picture is amplitude, not the ear); the RELATIVE GATE and TRUE-PEAK
// OVERSAMPLING stay out (programme-loudness machinery; the clamp is a
// sample-peak clip, right for a picture). Ruled out 2026-09-24 with the
// expander's arrival: A CLIPPER as a make-up stage (the leveler's own sparing
// clip is the right kind — about 0.1 % of columns); A DOWNWARD COMPRESSOR
// WITH MAKE-UP (measured: it flattened the swell before an onset from 4.0 to
// 1.9 dB, the opposite of the relief wanted); LOOKAHEAD and A NONZERO ATTACK
// in the expander; HYSTERESIS; a separate envelope window (Hold and Release
// ARE the envelope).
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
// THE EXPANDER (architect 2026-09-24): a DOWNWARD EXPANDER after the
// leveler, in FabFilter Pro-G's vocabulary — Threshold, Ratio, Range, Knee,
// Attack, Hold, Release — computed ONCE at load beside the gain curve, one
// reduction per working-zoom column (`expander_multiplier`). Its purpose is
// the relief before an onset: the dip in the quiet just before a note
// deepens, while nothing inside a note is expanded.
//
//   THE LEVEL a column reads: its LEVELED PEAK under the lane edge,
//   x = 20 log10(peak * g), g the leveler's gain at the column's centre
//   frame (waveform_gain_at). x <= 0 for a column at or under the edge; a
//   column the leveler clips reads > 0 and is simply above the threshold; a
//   zero peak reads minus infinity and takes the full Range.
//
//   THE GAIN COMPUTER, static, on x (T the Threshold, R the Ratio, K the
//   Knee, all dB), giving the column's TARGET REDUCTION in dB. The ratio's
//   convention is the gate's: BELOW THE THRESHOLD THE OUTPUT DROPS R dB PER
//   dB OF INPUT — at 2:1 every dB under the threshold becomes two — so the
//   reduction grows by R - 1 dB per dB, and Ratio 1.00 is the exact identity
//   (every column 0 dB, every multiplier exactly 1). With over = R - 1 and
//   hi = T + K/2:
//     x >= hi                  0
//     T - K/2 <= x < hi        over * (hi - x)^2 / (2K), the quadratic soft
//                              knee, its slope running 0 -> over across it
//     x < T - K/2              over * K/2 + over * ((T - K/2) - x), the
//                              knee's value at its lower edge plus over dB
//                              per dB under it
//   then capped at the Range, the floor: min(target, range).
//
//   THE TIME CONTROLS, walking the columns in time order with a running
//   reduction `red`:
//   - ATTACK IS 0, fixed (a ruling, not a key): a column whose x EXCEEDS T
//     sets red to 0 at once, so an onset is drawn where it happens. Under
//     attack 0 the knee's upper half [T, T + K/2) is inert for the column
//     itself — a column there opens the gate — so the smallest target the
//     release can walk toward on a column just under T is the knee's value
//     at T, over * K/8 dB.
//   - HOLD (`hold_ms`): every column that exceeds T re-arms a counter of
//     nearbyint(hold_ms * columns per second / 1000) columns; while it runs
//     red stays 0. Every crest inside a note keeps the gate open, and a gap
//     shorter than the hold is never expanded.
//   - RELEASE (`release_ms`): once the hold has run out, red rises toward
//     the column's target at the FIXED RATE Range / release_ms dB per ms —
//     the whole Range is crossed in `release_ms`, linear in dB, whatever
//     the target — and never past the target; a target under red (the level
//     rose without exceeding T) takes red down to it at once. release_ms 0
//     is the target at once.
//   - LOOKAHEAD IS OFF, fixed: no pre-open, so the dip before an onset
//     stands until the last column before it — the dip IS the picture's
//     purpose, and a pre-open would erase the very columns it deepens.
//   - HYSTERESIS IS NOT BUILT: it is Pro-G's flutter guard for a live
//     signal hovering at the threshold; here the Hold does that job, and
//     the picture is computed once, not listened to.
//   The multiplier is 10^(-red / 20), stored as a float in
//   [10^(-range / 20), 1], so THE PAINTER DOES NO POW.
//
//   THE PAINTER'S RULE: a plate column takes the LARGEST multiplier — the
//   SMALLEST reduction — over the working columns its source span covers
//   (waveform_expander_multiplier_over), and multiplies it into both tips
//   beside the gain before the one clamp. At working zoom that is the
//   column's own. Coarser, the bar is the raw min/max of every member, so
//   the member carrying the extreme is not known without a second pyramid;
//   the smallest reduction is the one choice under which the coarse bar is
//   never SHORTER than any member's own expanded bar (raw peak times the
//   largest multiplier bounds every member's peak times its own), so an
//   onset inside a coarse column is never dimmed by the dip before it and
//   the rule holds at every zoom — erring, where it errs, toward the
//   leveler's plain picture, never toward a hole. It applies exactly
//   where the gain applies (render_waveform's `gain_or_null`): target view
//   and the lit lamp stay raw.
//
//   THE CRITERION for tuning, the architect's: the SWELL before an onset —
//   the dip in the 300 ms before a local envelope maximum — roughly
//   DOUBLES from the leveler's 4.0 dB median at 2:1 with the threshold in
//   the quiet band; NOTHING inside a note or between its articulations is
//   expanded; the deep floor stays a VISIBLE LINE (the Range).
//
// THE FIVE TUNABLES ARE THE DEVICE CONFIG'S (architect 2026-09-23, a tuning
// phase; they may be hard-coded again later): `WaveformGainParams` below,
// read once at startup from `waveform_gain_*` keys (device_config.h owns their
// grammar and brackets) and handed to every derivation — and so are the
// expander's six (2026-09-24), `WaveformExpanderParams` from the
// `waveform_expander_*` keys under the same grammar — so a retune is a
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

// THE EXPANDER'S SIX TUNABLES (THE EXPANDER above), in the device config's
// writer order after the leveler's five (`waveform_expander_*` keys, the
// grammar and brackets at device_config.h). The member initializers ARE the
// defaults both first-run templates stamp (kDefaultWaveformExpanderParams;
// each default's reason is at waveform_gain.cpp) — the OFF STATE for a fresh
// device, ratio 1.00 with the other five at their seeds — construction
// state, never a load fallback: every key is required.
struct WaveformExpanderParams {
    double threshold_db = -8.0;   // T, dB of leveled column peak under the lane edge
    double ratio        = 1.0;    // R: the output drops R dB per dB under T (1 = off)
    double range_db     = 40.0;   // the floor: the largest reduction, dB
    double knee_db      = 4.0;    // K, the soft knee's width about T, dB
    double hold_ms      = 50.0;   // the gate held open after a column exceeds T
    double release_ms   = 20.0;   // the time to cross the whole Range, ms
};
inline constexpr WaveformExpanderParams kDefaultWaveformExpanderParams{};

// The picture's continuous magnification, derived from the source at load.
struct WaveformGainCurve {
    int64_t             hop_frames = 0;  // source frames between consecutive gains; gain[k] sits at frame k * hop_frames
    std::vector<double> gain;            // per hop, each in [kGainMin, gain_max]; empty for a zero-frame source
    int64_t             column_frames = 0;    // the working column's width in source frames; column k is [k * column_frames, (k + 1) * column_frames)
    std::vector<float>  expander_multiplier;  // THE EXPANDER, per working column, each in [10^(-range/20), 1]; empty is the identity
};

// `interleaved` is stereo float32, `total_frames` frames (2 * total_frames
// floats) — the decoded source buffer as GuiAudio holds it. A zero-frame
// input returns the empty curve. `params` are the device config's (the one
// struct gui_main reads at startup, reached through AppState::device_config),
// and so are `expander`'s.
WaveformGainCurve derive_waveform_gain(const float* interleaved, int64_t total_frames,
                                       int sample_rate, const WaveformGainParams& params,
                                       const WaveformExpanderParams& expander);

// The gain at one source frame: linear between the two nearest hops, the
// first and last hop's gain held beyond the ends. 1.0 for an empty curve.
double waveform_gain_at(const WaveformGainCurve& curve, int64_t frame);

// THE PAINTER'S EXPANDER RULE for a plate column spanning source frames
// [s0, s1): the LARGEST multiplier (the smallest reduction) over the working
// columns s0 / column_frames through (s1 - 1) / column_frames, both clamped
// into the array — a plain loop, one column at working zoom and a few hundred
// at full zoom-out. 1.0 for an empty curve.
float waveform_expander_multiplier_over(const WaveformGainCurve& curve, int64_t s0, int64_t s1);

// The derivation's identity for the plate fingerprint: bump on any change to
// the rule above (7 since the downward expander joined after the leveler,
// 2026-09-24; 6 was the short-term loudness measure replacing the window's
// peak and the upward compressor's deletion, the same day; 5 was the
// upward compressor's threshold, knee and range, 4 the fixed percentile and
// the ratio alone, 3 the restored leveler, 2 the expander on L's). The
// eleven tunables — the leveler's five and the expander's six — are NOT in
// the fingerprint and need not be: they are read once per
// process and never change under it, and nothing derived from the gain
// outlives the process — the curve is derived at every load (the `.peaks`
// sidecar carries no curve) and the plates live in memory only.
inline constexpr uint64_t kWaveformGainVersion = 7;
