#pragma once

#include <cstdint>
#include <vector>

// THE WAVEFORM GAIN: the picture's continuous magnification, derived from the
// source once per load, on a thread of its own that the load starts and does
// not wait for (GuiAudio::gain_curve; architect 2026-09-23, "the sausage").
// Every quiet passage is squashed graphically up to the lane edge so its
// transient onsets show as plainly as a tutti's; the loud body clipping flat
// at the edge is accepted.
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
//   the centred window of kWindowSeconds (3 s) — 10 log10 of the mean of the
//   mean squares of the window's AUDIBLE columns. The window and the hop are
//   the EBU short-term loudness's (3 s, 100 ms); the measure is UNWEIGHTED
//   and its only gate is ours: a column whose PEAK is at or under kGateDb
//   (-50 dB) is silence or tape hiss and is left out of the mean, so a rest inside a
//   window does not lower the window's loudness and blow up the notes around
//   it — the loudness is that of the sounding material. A window with less
//   than kMinFraction (0.25) of its columns audible takes the L of the NEARER
//   audible hop, so the curve is the same whichever way the audio runs (up
//   to the hop lattice and the earlier-on-a-tie choice). An all-silent song
//   takes x8 everywhere.
//
//   THE GAIN. g = 10^((target - L) / 20), kTargetDb (-14 dBFS) the target,
//   clamped to [kGainMin = 1, kGainMax = 16]: the window's loudness brought to the target,
//   NEVER ATTENUATED — a window already louder than the target takes x1, its
//   raw samples being unable to exceed the edge. Between hops the gain is
//   linear IN GAIN. That is the whole derivation (the RULED-OUT list below
//   names what it deliberately is not).
//
//   THE PAINTER multiplies each plate column's raw min/max by its gain and
//   clamps the pair to [-1, 1] (render_waveform); nothing else. The product
//   is THE GHOST BAR (architect 2026-09-24): the lit lamp paints it BEHIND
//   the column's raw bar, which is always present, so the magnified picture
//   never replaces the raw one. Both are flat colours, the WaveformPalette's
//   ghost ink and lit ink (render.h, tunable for a tuning phase, architect
//   2026-09-25); the gain decides the ghost's height and never its colour,
//   and the ghost and the raw bar each take the palette's own flat level
//   on top (architect 2026-09-25, the background's and the foreground's),
//   so the raw bar covers the ghost wherever the gain here is at or under
//   the separation between the two levels.
//   The clamp is a sample-peak clip, which is right for a
//   picture: a transient-rich window whose peaks overshoot the edge paints
//   its ghost flat.
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
// stage: it reads each working column's LEVELED PEAK, never the window's L.)
// THE UPWARD COMPRESSOR (2026-09-24, one day, four keys): a
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
// were the envelope; since their deletion the expander has none). Ruled out
// the same day by the architect's eye on the 40th, each set to zero and
// preferred there, each small alone and adding up: THE RANGE (a floor steadied
// the sustained body; a truly quiet column is drawn quiet); THE KNEE (it
// softened the contrast between the peaks and the valleys it should sharpen);
// THE HOLD (it extended the body of a sustained note, and onsets, not
// sustained notes, are what the picture is for); THE RELEASE (its fade
// steadied the body after every crest). With Hold and Release gone the attack
// and lookahead rulings have nothing left to rule on.
//
// A centred window's loudness rises as a loud entry's energy fills it, so the
// quiet before a loud entry ramps down over roughly the last half-window
// before it and stays down over the first half-window after it (1.5 s each
// side at the 3 s window) — symmetric in time, with no forward
// look-ahead (literally so only up to the hop lattice, whose hops sit at
// multiples of the hop from frame 0, and the nearer-known rule's
// earlier-on-a-tie choice for a silent hop) — and a lone accent inside a
// quiet passage lowers its neighbours' gain for about half a window on each
// side, by its share of the window's energy. Both are the rule's shape,
// accepted by the architect, not defects: the dip before a tutti is musically
// right (the reason at kWindowSeconds, waveform_gain.cpp).
//
// THE EXPANDER (architect 2026-09-24): a DOWNWARD EXPANDER after the
// leveler, in FabFilter Pro-G's vocabulary, TWO NUMBERS — the Threshold and
// the Ratio (kExpanderThresholdDb -8 dB, kExpanderRatio 2, waveform_gain.cpp)
// — a STATIC CURVE computed ONCE per load beside the gain curve, one reduction
// per working-zoom column (`expander_multiplier`). Its purpose is the relief
// before an onset: the dip in the quiet just before a note deepens, while
// nothing inside a note is expanded.
//
//   THE LEVEL a column reads: its LEVELED PEAK under the lane edge,
//   x = 20 log10(peak * g), g the leveler's gain at the column's centre
//   frame (waveform_gain_at). x <= 0 for a column at or under the edge; a
//   column the leveler clips reads > 0 and is simply above the threshold.
//
//   THE CURVE, on x (T the Threshold, R the Ratio, both fixed): at or above
//   T, no reduction; under it, (R - 1) dB of reduction per dB under T. The
//   ratio's convention is the gate's: BELOW THE THRESHOLD THE OUTPUT DROPS
//   R dB PER dB OF INPUT — at 2:1 every dB under the threshold becomes two —
//   so Ratio 1 would be the identity. UNCAPPED: there is no floor, so a
//   truly quiet column is drawn quiet, and a zero peak reads minus infinity,
//   takes multiplier 0 and its column paints nothing (the painter's >=1px
//   floor leaves it the centre row every silent column keeps). The
//   multiplier is 10^(-reduction / 20), stored as a float in [0, 1], so
//   THE PAINTER DOES NO POW.
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
//   where the gain applies (render_waveform's `gain_or_null`): the dark
//   lamp stays raw, in both audio views.
//
//   THE CRITERION, the architect's: the SWELL before an onset — the dip in
//   the 300 ms before a local envelope maximum — roughly DOUBLES from the
//   leveler's 4.0 dB median at 2:1; NOTHING inside a note or between its
//   articulations is expanded.
//
// THE SEVEN VALUES ARE HARD-CODED (architect 2026-09-24): the leveler's five
// and the expander's two are constexpr constants in waveform_gain.cpp, each
// with its reason, beside the hop, the gain floor and the column. They were
// the device config's `waveform_gain_*` (2026-09-23) and `waveform_expander_*`
// (2026-09-24) keys for a tuning phase; the phase is closed because every
// project should share ONE FRAME OF REFERENCE — the values are set once and
// left, and a retune is a recompile, by design. THE PRINCIPLE FOR ANY RETUNE:
// every number is FORCED by a criterion and never tuned to one spot — a free
// constant carries its reason, a derived one its derivation — and a passage
// the rule gets wrong is answered by the magnification lamp (dark, the raw
// picture) or the A/B tabs, not by a new number.
//
// Pure: no application state, no audio object, no allocation that outlives
// the call.

// The picture's continuous magnification, derived from the source once per
// load (off the load path — GuiAudio::gain_curve).
struct WaveformGainCurve {
    int64_t             hop_frames = 0;  // source frames between consecutive gains; gain[k] sits at frame k * hop_frames
    std::vector<double> gain;            // per hop, each in [kGainMin, kGainMax]; empty for a zero-frame source
    int64_t             column_frames = 0;    // the working column's width in source frames; column k is [k * column_frames, (k + 1) * column_frames)
    std::vector<float>  expander_multiplier;  // THE EXPANDER, per working column, each in [0, 1]; empty is the identity
};

// `interleaved` is stereo float32, `total_frames` frames (2 * total_frames
// floats) — the decoded source buffer as GuiAudio holds it. A zero-frame
// input returns the empty curve.
WaveformGainCurve derive_waveform_gain(const float* interleaved, int64_t total_frames,
                                       int sample_rate);

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
// the rule above (8 since the expander's Range, Knee, Hold and Release were
// deleted and the seven values hard-coded, 2026-09-24; 7 was the downward
// expander joining after the leveler, the same day; 6 was the short-term loudness measure replacing the window's
// peak and the upward compressor's deletion, the same day; 5 was the
// upward compressor's threshold, knee and range, 4 the fixed percentile and
// the ratio alone, 3 the restored leveler, 2 the expander on L's). The seven
// constants are part of the rule, so a change to any of them bumps it too.
// Nothing derived from the gain outlives the process — the curve is derived
// at every load (the `.peaks` sidecar carries no curve) and the plates live
// in memory only.
inline constexpr uint64_t kWaveformGainVersion = 8;
