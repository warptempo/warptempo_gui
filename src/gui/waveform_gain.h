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
// placement-instrument zoom, where one column is the device's WORKING COLUMN
// (working_column_frames, app_state.h, handed in by the caller: 46 frames on
// the tablet at 44.1 kHz; the constants were tuned at a 55-frame column) —
// which carries two numbers read
// in one pass over the samples:
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
//   THE PAINTER multiplies each working column's samples by THE COLUMN'S
//   TWO SCALES (waveform_column_scales below: the gain and the inner scale
//   at the column's centre frame, each times the column's own expander
//   multiplier) and clamps them to [-1, 1]; a plate bar's tips are the least
//   and the greatest of those magnified samples over its span, whatever
//   working columns it crosses (architect 2026-11-15, "Agree to
//   all."; render_waveform_columns' contract, THE EXACT FORM, owns the
//   definition): THE OUTER BAR, the levelled, expanded one, painted
//   first in the plate's own ink (the `waveform_ink` tone). Over it the lit
//   lamp paints
//   THE INNER BAR, the source's own bar through the same expander, THE
//   COMPRESSOR and THE FOREGROUND GAIN below instead of the gain, raw x c x
//   1/2 x E, a solid fill in the `waveform_core` tone over the outer's ink
//   (architect 2026-11-04, render.h's THE CORE) — both bars from the same
//   reads (architect 2026-09-25), the inks render.h's THE WAVEFORM block's. The
//   gain decides
//   the outer's height and
//   never a colour.
//   The clamp is a sample-peak clip, which is right for a
//   picture: a transient-rich window whose peaks overshoot the edge paints
//   its outer flat.
//
// WHY LOUDNESS AND NOT PEAK (architect 2026-09-24). A PEAK LEVELER — L the
// MAXIMUM of the gated column peaks in the window, g = 2^(-L / 6.02), the
// window's loudest column brought to the edge with nothing clipping — is a
// step function: the gain drops the instant ONE loud column enters the
// window, 1.5 s before an entry, and 4.8 % of hops jump by more than 1 dB on
// the 40th's first movement, drawing a line across the picture that is not
// in the music. The RMS gain moves as the entry's energy fills the window,
// so the same dip arrives as a ramp (1.7 % of hops over 1 dB). Of the
// dynamics stages weighed beside the leveler (2026-09-24), the DOWNWARD
// EXPANDER IS BUILT (THE EXPANDER below); the rest are RULED OUT (the list
// below).
//
// RULED OUT, never to be re-proposed (architect 2026-09-23 unless dated):
// classification, absorption, boundary placement, forward look-ahead,
// smoothing, hysteresis, a dead zone, a second window, a percentile of the
// column peaks — and an expander ON
// L (a threshold and a ratio, each dB of the WINDOW'S LOUDNESS under the
// threshold earning ratio dB of gain), rejected by the architect's eye
// against the plain leveler: the contrast he wants comes from the window
// alone at 3 s. (THE EXPANDER below is another
// stage: it reads each working column's LEVELED PEAK, never the window's L.)
// THE UPWARD COMPRESSOR (2026-09-24): a
// static curve applied per column at working zoom acts on each oscillation —
// a column is half a cycle at 440 Hz — so it lifts the zero crossings of low
// notes into a solid fill, and upward compression divides the floor's
// contrast by the ratio where relief is wanted. K-WEIGHTING stays out (the
// picture is amplitude, not the ear); the RELATIVE GATE and TRUE-PEAK
// OVERSAMPLING stay out (programme-loudness machinery; the clamp is a
// sample-peak clip, right for a picture). Ruled out 2026-09-24: A CLIPPER
// as a make-up stage (the leveler's own sparing
// clip is the right kind — about 0.1 % of columns); A DOWNWARD COMPRESSOR
// WITH MAKE-UP ON THE MAGNIFIED BAR, a per-column stage (measured: it
// flattened the swell before an onset from 4.0 to 1.9 dB, the opposite of
// the relief wanted) — the ruling stands for the OUTER bar; THE COMPRESSOR
// below (2026-09-25) is a different stage, slow on the window's L, on the
// INNER bar alone, with no make-up, and reopens nothing else here: smoothing,
// hysteresis and a second window stay out (the inner is one more per-hop
// multiplier on the same reads, as the expander is one more per-column one)
// — the gained second pyramid is built at the working column as THE LIT
// SUMMARY below (architect 2026-11-15) — and the upward compressor's
// per-oscillation fault cannot arise, c reading L and never the column;
// LOOKAHEAD and A NONZERO ATTACK
// in the expander; HYSTERESIS; a separate envelope window (the expander has
// no envelope). Ruled out
// 2026-09-24 by the architect's eye on the 40th, each set to zero and
// preferred there, each small alone and adding up: THE RANGE (a floor steadied
// the sustained body; a truly quiet column is drawn quiet); THE KNEE (it
// softened the contrast between the peaks and the valleys it should sharpen);
// THE HOLD (it extended the body of a sustained note, and onsets, not
// sustained notes, are what the picture is for); THE RELEASE (its fade
// steadied the body after every crest). With no Hold and no Release the
// attack and lookahead rulings have nothing to rule on. A PER-COLUMN SHADE ON
// EITHER BAR is out, and with it DRAWING THE GAIN AS A TINT or a plotted
// curve: a shade by the leveler's gain is a second reading laid over the
// picture and fighting it, and A CORE SHADE BY THE BAR GAP (the inner's ink
// blended per hop toward a teal by the dB of g / c) is out — "it doesn't
// work" (architect 2026-09-26). Both lit inks are flat. A
// SUGGESTED-MARKER FINDER on the magnification (troughs of the leveled peak
// as grey suggestion triangles) is PARKED (architect 2026-11-04,
// closed_questions.md): his warp markers sit on envelope minima but the dips
// are too shallow for any prominence rule, 7–20 suggestions per marker of
// his at every setting.
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
//   THE PAINTER'S RULE (architect 2026-11-15, "Agree to all."):
//   EVERY WORKING COLUMN TAKES ITS OWN MULTIPLIER, into both tips of BOTH
//   lit bars — beside the gain on the outer, beside the compressor on the
//   inner (architect 2026-09-25: both stages on both bars) — before each
//   bar's one clamp: one factor of the column's two scales
//   (waveform_column_scales). A plate bar over several working columns is
//   the hull of their own magnified samples, each column at its own
//   multiplier, so a coarse bar is the outline of the working zoom's own
//   bars: an onset inside it keeps the height the working zoom gives it and
//   the dip before it keeps its own depth, at every zoom and in both views
//   (THE LIT SUMMARY below stores the whole columns' hull; the definition is
//   render_waveform_columns', THE EXACT FORM). At working zoom in source view
//   a bar is one working column (the plate's columns stand on the column's
//   multiples, the frame-0 lattice — render.h's THE PLATE'S BARS). It
//   applies exactly where the gain applies (render_waveform_columns'
//   `gain_or_null`): the dark lamp stays raw, in both audio views.
//
//   THE CRITERION, the architect's: the SWELL before an onset — the dip in
//   the 300 ms before a local envelope maximum — roughly DOUBLES from the
//   leveler's 4.0 dB median at 2:1; NOTHING inside a note or between its
//   articulations is expanded.
//
// THE COMPRESSOR (architect 2026-09-25): the INNER bar's reduction, a
// DOWNWARD COMPRESSOR on the leveler's own window loudness L — the same
// per-hop measure the gain comes from, "RMS type, slow" — in the expander's
// Pro-C vocabulary, TWO NUMBERS, the Threshold T (dBFS, on L) and the Ratio
// R (kCompressorThresholdDb -24 dBFS, kCompressorRatio 2, waveform_gain.cpp).
// At or under T no reduction, the inner
// being the raw bar (x the expander), the quiet parts exactly as the source;
// over it (1 - 1/R) dB of reduction per dB of L over T, and
// c = 10^(-reduction / 20), in (0, 1]. No make-up, no knee, no attack,
// release or hold, no envelope of its own: L already is the envelope (the
// 3 s window, the 0.1 s hop, the half-window ramps).
//
//   DERIVED FROM THE UNCLAMPED L, never from the gain: above the target the
//   gain is clamped to x1 and L is not readable from it. A hop whose
//   window is too quiet to measure takes the L of the NEARER audible hop, as
//   its gain does (one picked L feeds both), and an all-silent song takes
//   c = 1 everywhere beside its x8 (times the half below, as every hop is).
//
//   STORED PER HOP beside `gain` (WaveformGainCurve::inner_scale, c x 1/2
//   with THE FOREGROUND GAIN below folded in), linear IN SCALE between hops
//   (waveform_inner_scale_at, the sibling of waveform_gain_at). THE
//   PAINTER'S INNER BAR is raw x c x 1/2 x the expander's multiplier, the
//   one clamp, the >=1px floor; with the expander on BOTH bars the gap
//   between them is g / (c x 1/2), a pure function of L, and since
//   c x 1/2 <= 1/2 < 1 <= g the inner cannot exceed the outer in
//   height about the centre row (a column wholly on one side of zero has its
//   outer pushed outward past the inner — render_waveform_columns' declaration). The
//   reading is the source's bar over a levelled bar behind it, the core's
//   relative thickness the loudness.
//
//   THE CRITERION: the gap between the inner and the outer >= 4.5 dB at the
//   loudest 5 % of hops, the quiet parts untouched, monotone by
//   construction. T -24 dBFS and R 2 are the gentlest ratio that holds it in
//   all three K550 movements with margin (the measured map is at the two
//   constants, waveform_gain.cpp, at unit gain), settled by eye (architect
//   2026-09-27); the half below widens every gap by 6.02 dB.
//
//   RULED OUT with the stage, never to be re-proposed (architect
//   2026-09-25..27): three bars; the inner without the expander; a
//   compressor reading the column's peak (c reads L, never an oscillation);
//   a gate on either bar (it would draw a line where the gain crosses it);
//   and any flat level on the inner but the settled half below.
//
// THE FOREGROUND GAIN (architect 2026-09-27): a FLAT LINEAR gain of exactly
// ONE HALF (kForegroundGain 0.5, waveform_gain.cpp) on the INNER bar alone,
// folded into the inner's per-hop scale at the derivation; the outer is
// untouched. At unit gain the inner stands too close to the lane top in the
// tuttis; the architect picked -6.02 dB by eye and asked for the "actual
// half", so the constant is 0.5 itself, never a decibel power. It is the
// inner's one flat level, SETTLED at one half; the outer takes none (its
// peak sits at the lane edge in every window).
//
// THE TEN VALUES ARE HARD-CODED: the leveler's five, the expander's two,
// the compressor's two and the foreground gain are constexpr constants in
// waveform_gain.cpp, each with its reason, beside the hop, the gain floor and
// the column (architect 2026-09-27). No key: every project shares ONE FRAME
// OF REFERENCE — the values are set once and left, and a retune is a
// recompile, by design. THE PRINCIPLE FOR ANY RETUNE:
// every number is FORCED by a criterion and never tuned to one spot — a free
// constant carries its reason, a derived one its derivation — and a passage
// the rule gets wrong is answered by the magnification lamp (dark, the raw
// picture) or the A/B tabs, not by a new number.
//
// Pure: no application state, no audio object, no allocation that outlives
// the call.

// THE LIT SUMMARY (architect 2026-11-15, "Agree to all." — the gained
// second pyramid, built at the working column): THE LIT PICTURE OF EVERY
// WORKING COLUMN, STORED, with a range structure over it, so a plate bar over
// many working columns reads
// their hull in a bounded number of entries instead of a read and two scales
// a column (render_waveform_columns' contract, THE EXACT FORM, owns what a
// bar is and when the summary is read).
//
//   THE BASE, per channel per working column, FOUR NUMBERS: the outer's
//   least and greatest magnified sample and the inner's — the column's exact
//   float sample extremes of that channel (the derivation's own column pass,
//   which reads every sample once for the peaks and the mean squares and
//   keeps these beside them), each times THE COLUMN'S TWO SCALES
//   (waveform_column_scales: the one function the producer reads too, so the
//   arithmetic cannot fork) in double, clamped to [-1, 1] and quantized as
//   the peak pyramid quantizes (audio.cpp's quantize_unit: nearbyint of
//   v x 32767, in double).
//   THE LEVELS, FACTOR 4 (the peak pyramid's shape): level j's entry i is the
//   hull — the least of the minima, the greatest of the maxima — of level
//   j - 1's entries [4i, 4i + 4) that exist, up to the level of one entry.
//   THE QUERY (waveform_lit_summary_hull) walks bottom-up over a run of
//   whole working columns, at most three entries a side a level, and is
//   EXACT: every entry it takes lies inside the run, they cover it, and min
//   and max are idempotent and order-free on int16.
//   BUILT INSIDE derive_waveform_gain after the expander (the scales need
//   it), a member of the curve: pure, immutable with it, standing before the
//   derivation's `ready` (GuiAudio::gain_curve), so the lamp cannot be lit
//   without it. NOTHING REBUILDS IT: it is a function of the source and the
//   ten constants; one per GuiAudio, both views reading it (the target view
//   draws the source through the map), the lamp choosing whether it is read.
//   WHY int16: a step is 1/32767 of the lane's half height, about 0.016 of a
//   quarter row on the tablet's channel band, under the pyramid's own slack.
//   MEMORY: 8 bytes a channel a working column at the base, about 4/3 of
//   that with the levels — measured 2026-11-15 on the Mac at the tablet's
//   46-frame column, 12.3 MB for a ten-minute piece at 44.1 kHz (575 218
//   columns; its decoded samples are 212 MB, its peak pyramid about 18 MB)
//   and 3.7 MB for three minutes. THE BUILD, on the gain thread
//   off the load path: as a separate pass on the Mac it cost what the
//   derivation itself costs (94 against 95 ms for ten minutes); inside the
//   column pass the sample read is shared (not measured apart). The column
//   pass's transient extremes (16 bytes a working column) are freed with the
//   call.
struct WaveformLitEntry {
    int16_t outer_min = 0, outer_max = 0;
    int16_t inner_min = 0, inner_max = 0;
};
inline constexpr int64_t kWaveformLitSummaryFactor = 4;
struct WaveformLitSummary {
    std::vector<int64_t>          level_begin;  // level j is entries [level_begin[j], level_begin[j + 1]); level 0 one entry per working column
    std::vector<WaveformLitEntry> entries[2];   // per channel, every level, level 0 first
};

// The picture's continuous magnification, derived from the source once per
// load (off the load path — GuiAudio::gain_curve).
struct WaveformGainCurve {
    int64_t             hop_frames = 0;  // source frames between consecutive gains; gain[k] sits at frame k * hop_frames
    std::vector<double> gain;            // per hop, each in [kGainMin, kGainMax]; empty for a zero-frame source
    std::vector<double> inner_scale;     // THE COMPRESSOR x THE FOREGROUND GAIN, per hop beside `gain`, each in (0, 1/2]
    int64_t             column_frames = 0;    // the working column's width in source frames; column k is [k * column_frames, (k + 1) * column_frames)
    std::vector<float>  expander_multiplier;  // THE EXPANDER, per working column, each in [0, 1]; empty for a zero-frame source
    WaveformLitSummary  lit_summary;          // THE LIT SUMMARY over the working columns (its head above); empty for a zero-frame source
};

// `interleaved` is stereo float32, `total_frames` frames (2 * total_frames
// floats) — the decoded source buffer as GuiAudio holds it. A zero-frame
// input returns the empty curve. `column_frames` is the analysis column, the
// device's working column (>= 1; GuiAudio::load's, fixed for the audio's
// lifetime, so the plate fingerprint carries no column term).
WaveformGainCurve derive_waveform_gain(const float* interleaved, int64_t total_frames,
                                       int sample_rate, int64_t column_frames);

// The gain at one source frame: linear between the two nearest hops, the
// first and last hop's gain held beyond the ends. 1.0 for an empty curve.
double waveform_gain_at(const WaveformGainCurve& curve, int64_t frame);

// THE INNER BAR'S SCALE (c x 1/2) at one source frame, the sibling of
// waveform_gain_at over `inner_scale`: linear in scale between the two
// nearest hops, the ends held. 1.0 for an empty curve.
double waveform_inner_scale_at(const WaveformGainCurve& curve, int64_t frame);

// THE COLUMN'S TWO SCALES (architect 2026-11-15; THE PAINTER'S RULE
// above): working column k's outer scale, the gain at its centre times its
// own expander multiplier, and its inner scale, the inner bar's scale at the
// same centre times the same multiplier — the centre k x column_frames +
// column_frames / 2 (integer division, NOT clamped to the source's end:
// the bar [k x col, (k + 1) x col)'s own (s0 + s1) / 2, so the working zoom
// reads the scale it always read, the short last column included; the
// expander's own centre, clamped, differs on that column alone), in that
// expression order (render_waveform_columns' arithmetic). The one function
// the producer and THE LIT SUMMARY's build share. `k` is a working column of
// a curve that has one (0 <= k < expander_multiplier.size()).
struct WaveformColumnScales {
    double outer = 1.0;
    double inner = 1.0;
};
WaveformColumnScales waveform_column_scales(const WaveformGainCurve& curve, int64_t k);

// THE LIT SUMMARY'S QUERY (its head above): the hull of whole working
// columns [ka, kb] (both inclusive, 0 <= ka <= kb < the column count) of
// `channel` (0 or 1) — the least of the outer's and the inner's minima and
// the greatest of their maxima — each dequantized as double(q) / 32767.0.
struct WaveformLitHull {
    double outer_min = 0.0, outer_max = 0.0;
    double inner_min = 0.0, inner_max = 0.0;
};
WaveformLitHull waveform_lit_summary_hull(const WaveformGainCurve& curve, int channel,
                                          int64_t ka, int64_t kb);

// The derivation's identity for the plate fingerprint: bump on any change to
// the rule above (the lit rule's exact form, architect 2026-11-15: every
// working column at its own scales, a bar the hull of its columns' magnified
// samples — THE PAINTER'S RULE). The fingerprint is the version alone: the
// analysis column, the device's working column handed in by the load, is no
// fingerprint term, since the plates live in memory, the cache is per
// project and the column is fixed for its lifetime. The ten
// constants are part of the rule, so a change to any of them bumps it too.
// Nothing derived from the gain outlives the process — the curve is derived
// at every load (the `.peaks` sidecar carries no curve) and the plates live
// in memory only.
inline constexpr uint64_t kWaveformGainVersion = 16;
