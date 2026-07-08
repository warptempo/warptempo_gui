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
// "target_time_sec multiplier" line per entry at up to seventeen significant
// digits, the same serialization as the two sibling writers, round-tripping
// an IEEE double exactly. The midi map is a consumer export for DAW hosts,
// not an engine input, so exact round-trip is not required of it, but the
// serialization aligns with the sibling artifact writers; the consumers
// (verified in Ableton Live and REAPER) parse the values and round to their
// own tempo resolution, and the default float format's scientific notation
// for extreme values is accepted by standard float parsing on the consumer
// side.
std::expected<void, std::string> write_midi_tempo_map(
    const std::string& path, const std::vector<MidiTempoMapEntry>& entries);

// Serialize an engine query-domain phase-reset list to the canonical
// .phaseresetframemap text form: one engine query-domain double per line, in
// input order, at up to 17 significant digits (round-trips an IEEE double
// exactly, same serialization as write_warp_frame_map; whole-frame values print
// with no decimal point). The companion to write_warp_frame_map on the
// phase-reset axis — the engine-input export the GUI emits and an engine
// consumer reads as-is (read_phase_reset_frame_map in
// phase_reset_frame_map.h). The caller supplies the already-derived engine
// query-domain positions (derive_phase_reset_frame_map applies the
// anticipation offset and drops against the map shipped beside the file), so
// this writer applies no policy: it does not displace, sort, or dedupe.
std::expected<void, std::string> write_phase_reset_frame_map(
    const std::string& path, const std::vector<double>& engine_query_frames);
