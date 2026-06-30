#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

#include <sndfile.h>

enum class DecodedSourceCacheStatus {
    Bypassed,
    Hit,
    Miss,
    Rebuilt,
};

struct DecodedSourceReadResult {
    DecodedSourceCacheStatus cache_status = DecodedSourceCacheStatus::Bypassed;
    bool used_cache = false;
};

bool is_decoded_source_cache_path(const std::string& path);

// Private warptempo decoded-source cache container. It is not a user audio
// format: metadata identifies the owning source and the payload is interleaved
// float32 for direct render-engine input.
std::expected<DecodedSourceReadResult, std::string>
load_source_range_with_decoded_cache(const std::string& source_path,
                                     const SF_INFO& source_info,
                                     size_t begin_frame,
                                     size_t end_frame,
                                     std::vector<float>& out_samples,
                                     int& out_sample_rate,
                                     int& out_channels);

const char* decoded_source_cache_status_name(DecodedSourceCacheStatus status);
