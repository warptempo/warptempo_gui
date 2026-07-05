#pragma once

#include "frame_map.h"      // FrameMapSegment
#include "frame_map_build.h"   // TempoMapEntry

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

// Serialize a built frame map to the canonical .warpframemap text form: one
// "src_frame tgt_frame" line per segment, every segment included — the
// readers require a first-target-zero pair, so the leading anchor is part of
// the contract. Shared by the GUI render pipeline and the headless parser
// CLI so both emit byte-identical artifacts.
std::expected<void, std::string> write_frame_map(
    const std::string& path, const std::vector<FrameMapSegment>& segs);

// Serialize a tempo map to the canonical .tempomap text form: one
// "target_time_sec multiplier" line per entry at fixed 16-digit precision.
std::expected<void, std::string> write_tempo_map(
    const std::string& path, const std::vector<TempoMapEntry>& entries);

// Serialize an undisplaced source-frame phase-reset list to the canonical
// .resetmap text form: one source-frame integer per line, in input order.
// The companion to write_frame_map on the phase-reset axis — the
// frame-domain export warptempo_parser emits and the engine-only synthesis
// driver consumes (read_reset_map in frame_map.h). The caller supplies the
// already-resolved active source frames (phase_reset_source_frames drops
// disabled markers and converts time->source frame via nearbyint), so this
// writer applies no policy: it does not displace, sort, or dedupe.
std::expected<void, std::string> write_reset_map(
    const std::string& path, const std::vector<int64_t>& source_frames);
