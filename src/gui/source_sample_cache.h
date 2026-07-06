#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

#include "audio_probe.h"

bool is_source_sample_cache_path(const std::string& path);

// Hit-only full-source read from the private sample cache. A validated hit
// fills out_samples with the full interleaved float32 payload at
// source_info.channels channels and source_info.frames frames.
bool read_full_source_from_source_sample_cache(const std::string& source_path,
                                               const AudioFileInfo& source_info,
                                               std::vector<float>& out_samples);

bool ensure_source_sample_cache_from_buffer(const std::string& source_path,
                                            const AudioFileInfo& source_info,
                                            const float* samples,
                                            int64_t frames,
                                            int channels);

std::expected<std::string, std::string>
source_path_for_source_sample_cache(const std::string& cache_path);
