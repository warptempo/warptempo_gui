#pragma once

#include "frame_map.h"      // FrameMapSegment
#include "timemap_core.h"   // TempomapEntry

#include <string>
#include <vector>

// Serialize a built frame map to the canonical .warpframemap text form: one
// "src_frame tgt_frame" line per segment. When drop_zero_zero is true a
// leading (0,0) anchor is omitted. Shared by the GUI render pipeline and the
// headless recipe CLI so both emit byte-identical artifacts.
bool write_standard_frame_map(const std::string& path,
                              const std::vector<FrameMapSegment>& segs,
                              bool drop_zero_zero);

// Serialize a MIDI tempo map to the canonical .tempomap text form: one
// "target_time_sec multiplier" line per entry at fixed 16-digit precision.
bool write_midi_tempomap(const std::string& path,
                         const std::vector<TempomapEntry>& entries);
