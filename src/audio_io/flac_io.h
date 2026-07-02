#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

// FLAC integer decode is bit-exact by specification. This wrapper converts
// miniaudio's decoded integer stream to float32 by a power-of-two division, so
// 16- and 24-bit payloads land exactly on the float32 lattice and provide
// deterministic source-decode ground truth.

struct FlacInfo {
    int     sample_rate     = 0;
    int     channels        = 0;
    int64_t frames          = 0;
    int     bits_per_sample = 0;
};

std::expected<FlacInfo, std::string> flac_probe(const std::string& path);

std::expected<std::vector<float>, std::string>
flac_read_full(const std::string& path, FlacInfo* info_out = nullptr);

std::expected<std::vector<float>, std::string>
flac_read_range(const std::string& path, int64_t begin_frame,
                int64_t end_frame, FlacInfo* info_out = nullptr);
