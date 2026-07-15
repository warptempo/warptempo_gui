#pragma once

#include <cstddef>
#include <expected>
#include <string>
#include <vector>

// Reads samples in [begin_frame, end_frame) from src_path into out_samples
// as interleaved 32-bit float, and reports the source sample rate and
// channel count. Used by warptempo_cli to populate the warptempo engine's
// in-memory source buffer.
// Uses the in-tree source reader. Returns {} on success; returns the error to
// the caller on any read error.
std::expected<void, std::string> load_source_range_to_buffer(const std::string& src_path,
                                 size_t begin_frame,
                                 size_t end_frame,
                                 std::vector<float>& out_samples,
                                 int& out_sample_rate,
                                 int& out_channels);
