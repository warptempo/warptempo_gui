#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

#include "audio_probe.h"

enum class SourceSampleCacheStatus {
    Bypassed,
    Hit,
    Miss,
    Rebuilt,
};

struct SourceSampleReadResult {
    SourceSampleCacheStatus cache_status = SourceSampleCacheStatus::Bypassed;
    bool used_cache = false;
};

bool is_source_sample_cache_path(const std::string& path);

// Private source sample cache container. It is not a user audio format:
// metadata identifies the owning sibling source and the payload is interleaved
// float32 for direct render-engine input.
std::expected<SourceSampleReadResult, std::string>
load_source_range_with_source_sample_cache(const std::string& source_path,
                                           const AudioFileInfo& source_info,
                                           size_t begin_frame,
                                           size_t end_frame,
                                           std::vector<float>& out_samples,
                                           int& out_sample_rate,
                                           int& out_channels);

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

const char* source_sample_cache_status_name(SourceSampleCacheStatus status);
