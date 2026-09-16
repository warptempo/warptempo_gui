#pragma once

#include "propagate_clipboard.h"

#include <cstdint>

// Session-only clipboard for the W-mode MAGNIFICATION LEVEL propagate
// (Ctrl+M copy / Ctrl+Alt+M paste / Ctrl+Alt+Shift+M state paste; architect
// 2026-09-15) — the phase reset clipboard's shape (phase_reset_clipboard.h)
// over this column's placement. A copy captures a sequence of named warp
// blocks and, inside each, the magnification level markers that fall in it;
// paste walks a destination anchor's named-block sequence in lockstep with the
// clipboard, materializing markers at the destination's actual durations. ITS
// OWN SLOT beside the phase reset clipboard's (AppState): a copy of one column
// never overwrites the other's.
//
// WHAT IS CAPTURED AND SCALED IS THE MARKER'S OWN FRAME: a magnification level
// is a PICTURE BOUNDARY at the frame it stands on, not a synthesis event
// seeded ahead of the point it protects, so THE FRAME IS ITS OWN ANCHOR and
// there is no lead-in to carry forward or re-derive (the phase reset
// placement's anchor, the ONE delta between the two families, is stated at the
// anchor block in phase_reset_propagate.cpp; this family has no such block
// because it has no such quantity). The section fraction below is the frame's
// directly.
struct MagnificationLevelClipboardPlacement {
    // The marker's position inside the capturing block, (frame - start) /
    // duration. NOT clamped to [0, 1], the phase family's ruling read across:
    // a marker may fall before a block's start (nudged off the boundary by
    // hand, inside the guard), and the fraction is scaled as it stands; the
    // paste walls the materialized FRAME at [0, total - 1] and nothing else.
    double  fractional_position = 0.0;
    // The frame this placement was captured by, in capture-time source frames
    // (whole, widened: an authored position). Held because it is what the
    // COPY decided membership by: the state paste re-buckets the flat
    // placement list against its own windows and must bucket by the same
    // quantity the capture did, or the two acts disagree about which block a
    // marker belongs to.
    double  source_frame        = 0.0;
    // THE STATE, both halves: the level the marker sets from its frame on and
    // its disabled bit. The placement paste copies both onto what it
    // materializes; the state paste aligns both on the destination's existing
    // markers (architect 2026-09-15: a magnification level marker's state is
    // its disabled bit AND its level — the whole of its non-positional
    // content, where a phase reset's is its disabled bit alone because it
    // carries nothing else).
    uint8_t level               = 0;
    bool    disabled            = false;
};

using MagnificationLevelClipboardBlock =
    PropagateClipboardBlock<MagnificationLevelClipboardPlacement>;
using MagnificationLevelClipboard =
    PropagateClipboard<MagnificationLevelClipboardBlock>;
