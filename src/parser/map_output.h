#pragma once

// Dead include removed under grant (architect approval 2026-08-02).
#include "warp_frame_map.h"      // WarpFrameMapSegment

#include <expected>
#include <string>
#include <vector>

// The framemap pair writers (warp column + phase reset column). Together the
// two files are exactly the engine's input for a full render. The GUI's
// archival render pipeline drops the pair into the RenderCache per-process dir
// as future-proofing, and the writers are shared with the headless parser CLI
// so both emit byte-identical artifacts.

// Serialize a built frame map to the canonical .warpframemap text form: one
// "src_frame tgt_frame" line per segment, every segment included — the
// readers require a first-target-zero pair, so the leading anchor is part of
// the contract.
std::expected<void, std::string> write_warp_frame_map(
    const std::string& path, const std::vector<WarpFrameMapSegment>& segs);

// Serialize an engine query-domain phase-reset list to the canonical
// .phaseresetframemap text form: one engine query-domain double per line, in
// input order, at up to 17 significant digits (round-trips an IEEE double
// exactly, same serialization as write_warp_frame_map; whole-frame values print
// with no decimal point). The companion to write_warp_frame_map on the
// phase-reset axis — the engine-input export the GUI emits and an external
// engine consumer reads as-is. The caller supplies the already-derived engine
// query-domain positions (derive_phase_reset_frame_map drops
// non-participants against the map shipped beside the file), so
// this writer applies no policy: it does not displace, sort, or dedupe.
std::expected<void, std::string> write_phase_reset_frame_map(
    const std::string& path, const std::vector<double>& engine_query_frames);

// Write both columns of the pair into `dir`, named `<stem>.warpframemap` /
// `<stem>.phaseresetframemap` — the single write-or-clean home shared by the
// GUI render pipeline and the headless CLI so neither drifts. The pair is
// all-or-nothing: on ANY writer failure (a first-writer failure included, so an
// old complete pair can never pose as the new render's) BOTH names are removed
// (std::filesystem::remove, errors swallowed — these are cache files), leaving
// the observable state as exactly a current complete pair or no pair, never a
// mixed generation. Callers compose `dir` (a source-dir single writes the flat
// per-process dir; a batch cell writes a per-batch-folder subdir it created);
// the byte serializers above are untouched. Returns the writer's error string
// on failure — the caller prints it as one non-fatal stderr line and the render
// proceeds — and {} on success.
std::expected<void, std::string> write_frame_map_pair(
    const std::string& dir, const std::string& stem,
    const std::vector<WarpFrameMapSegment>& warp_segs,
    const std::vector<double>& phase_reset_engine_query_frames);
