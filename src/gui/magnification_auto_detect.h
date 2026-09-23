#pragma once

#include <cstdint>
#include <vector>

// THE MAGNIFICATION LEVEL DETECTOR: proposes magnification level markers for
// a whole song by measuring THE PICTURE, not the audio. The unit it reads is
// the painted column at WORKING ZOOM — the placement-instrument zoom, where
// one column is kZoomBaseMsPerPx * 2^(kWorkingZoomLevel - 1) of source time
// (55 frames at 44.1 kHz) — and a column's height is the plate renderer's own
// min/max reduced to one number: the largest |x| over both channels. What it
// proposes is the HIGHEST level at which each stretch's typical column top
// (the kTopPercentile column height) still fits under the lane edge, 0 dBFS,
// so the magnified picture fills the lane without the body of the music
// clipping. Levels are whole doublings, so a level puts the typical top
// anywhere in the 6 dB under the edge, never at one fixed distance.
//
// THREE STAGES:
//
//   1. THE CURVE. At every analysis hop, d: the number of doublings the
//      typical column top (the kTopPercentile order statistic of the gated
//      column heights in a centred window of kWindowSeconds) sits under
//      0 dBFS, -dB(top) / kLevelDb. Columns under kGateDb are silence or tape
//      hiss and are gated out; a window with too little audible material
//      takes the d of the NEARER audible side, so the curve is the same
//      whichever way the audio runs.
//   2. THE LEVELS. Each hop takes the highest level whose typical top still
//      fits — level = floor(d), clamped to 1..4 — with NO hysteresis (it
//      made the result direction-dependent). A section shorter than the
//      window cannot be measured, so it joins the neighbour nearest in
//      level, a tie going to the longer neighbour; that is what suppresses
//      chatter, symmetrically in time.
//   3. THE BOUNDARIES, placed at column resolution with a FORWARD look: a
//      level begins at the first column whose next kLookaheadSeconds
//      measures inside it, so a loud entry is clamped slightly early and a
//      drop lands on the drop. Placement can shrink a section, so the
//      window's minimum is enforced again afterwards at column resolution.
//
// There is no lead-in rule: a crescendo that measures a level for a window's
// length is its own lead-in, and a shorter one is below the rule's
// resolution and left to hand placement.
//
// The constants are the architect's, fixed on 2026-09-22 by a stepwise
// tuning over the 40th and 41st symphonies. THE PRINCIPLE FOR ANY RETUNE:
// every number is FORCED by a criterion and never tuned to one spot — a free
// constant carries its reason, a derived one its derivation — and a passage
// the rule gets wrong is an exception for HAND PLACEMENT, not a new number.
// LEVEL 0 STAYS HAND-ONLY: the lane-edge rule would put the loudest tuttis
// there, and 0 is not for this corpus, so the detector emits 1..4.
//
// Pure: no application state, no audio object, no allocation that outlives
// the call.

struct GuiDetectedMagnificationLevel {
    int64_t frame;  // source frame where the level begins
    int     level;  // 1..4
};

// `interleaved` is stereo float32, `total_frames` frames (2 * total_frames
// floats) — the decoded source buffer as GuiAudio holds it. Returns the
// breakpoints in ascending frame order, the first always at frame 0, each
// frame < total_frames. A zero-frame input returns empty.
std::vector<GuiDetectedMagnificationLevel>
detect_magnification_levels(const float* interleaved, int64_t total_frames,
                            int sample_rate);
