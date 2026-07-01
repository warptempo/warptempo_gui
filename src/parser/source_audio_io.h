#pragma once

#include <cstddef>
#include <expected>
#include <string>
#include <vector>

// libsndfile-based slice: reads src_path samples [begin_frame, end_frame)
// and writes them to out_path as 32-bit float WAV preserving channel count
// and sample rate. Returns {} on success; returns the error to the caller on
// any sndfile error. No sox dependency.
std::expected<void, std::string> write_trimmed_wav(const std::string& src_path,
                       const std::string& out_path,
                       size_t begin_frame,
                       size_t end_frame);

// Reads samples in [begin_frame, end_frame) from src_path into out_samples
// as interleaved 32-bit float, and reports the source sample rate and
// channel count. Used by render_pipeline.cpp to populate the warptempo
// engine's in-memory source buffer (replaces the wav-on-disk trim shim).
// Returns {} on success; returns the error to the caller on any sndfile error.
std::expected<void, std::string> load_source_range_to_buffer(const std::string& src_path,
                                 size_t begin_frame,
                                 size_t end_frame,
                                 std::vector<float>& out_samples,
                                 int& out_sample_rate,
                                 int& out_channels);
