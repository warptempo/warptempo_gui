#pragma once

#include "warp_frame_map.h"      // WarpFrameMapSegment
#include "warp_frame_map_build.h"   // MidiTempoMapEntry

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

// Serialize a built frame map to the canonical .warpframemap text form: one
// "src_frame tgt_frame" line per segment, every segment included — the
// readers require a first-target-zero pair, so the leading anchor is part of
// the contract. Shared by the GUI render pipeline and the headless parser
// CLI so both emit byte-identical artifacts.
std::expected<void, std::string> write_warp_frame_map(
    const std::string& path, const std::vector<WarpFrameMapSegment>& segs);

// Serialize a tempo map to the canonical .miditempomap text form: one
// "target_time_sec multiplier" line per entry at fixed 16-digit precision.
std::expected<void, std::string> write_midi_tempo_map(
    const std::string& path, const std::vector<MidiTempoMapEntry>& entries);

// Serialize an engine query-domain phase-reset list to the canonical
// .phaseresetframemap text form: one engine query-domain double per line, in
// input order, at up to 17 significant digits (round-trips an IEEE double
// exactly, same serialization as write_warp_frame_map; whole-frame values print
// with no decimal point). The companion to write_warp_frame_map on the
// phase-reset axis — the engine-input export warptempo_parser emits and the
// engine-only synthesis driver consumes as-is (read_phase_reset_frame_map in
// phase_reset_frame_map.h). The caller supplies the already-derived engine
// query-domain positions (derive_phase_reset_frame_map applies the
// anticipation offset and drops against the map shipped beside the file), so
// this writer applies no policy: it does not displace, sort, or dedupe.
std::expected<void, std::string> write_phase_reset_frame_map(
    const std::string& path, const std::vector<double>& engine_query_frames);
