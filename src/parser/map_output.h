#pragma once

#include "frame_map.h"      // FrameMapSegment
#include "frame_map_build.h"   // TempomapEntry

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

// Serialize a built frame map to the canonical .warpframemap text form: one
// "src_frame tgt_frame" line per segment. When drop_zero_zero is true a
// leading (0,0) anchor is omitted. Shared by the GUI render pipeline and the
// headless map CLI so both emit byte-identical artifacts.
std::expected<void, std::string> write_standard_frame_map(
    const std::string& path, const std::vector<FrameMapSegment>& segs,
    bool drop_zero_zero);

// Serialize a MIDI tempo map to the canonical .tempomap text form: one
// "target_time_sec multiplier" line per entry at fixed 16-digit precision.
std::expected<void, std::string> write_midi_tempomap(
    const std::string& path, const std::vector<TempomapEntry>& entries);

// Serialize an undisplaced source-frame phase-reset list to the canonical
// .resetmap text form: one source-frame integer per line, in input order.
// The companion to write_standard_frame_map on the phase-reset axis — the
// frame-domain export warptempo_parser emits and the engine-only synthesis
// driver consumes (read_reset_map in frame_map.h). The caller supplies the
// already-resolved active source frames (phase_reset_source_frames drops
// disabled markers and converts time->source frame via nearbyint), so this
// writer applies no policy: it does not displace, sort, or dedupe.
std::expected<void, std::string> write_reset_map(
    const std::string& path, const std::vector<int64_t>& source_frames);
