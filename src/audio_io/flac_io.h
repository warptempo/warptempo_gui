#pragma once

#include <cstdint>
#include <expected>
#include <string>

// Hand-parsed native FLAC STREAMINFO probe over the fixed-offset first metadata
// block. The probe admits only known-length, <=24-bit streams so downstream
// decoding can keep exact integer-to-float conversion semantics.

struct FlacInfo {
    int     sample_rate     = 0;
    int     channels        = 0;
    int64_t frames          = 0;
    int     bits_per_sample = 0;
    // STREAMINFO MD5 of the unencoded audio; all zero when the encoder left it unset.
    unsigned char md5[16]   = {};
};

std::expected<FlacInfo, std::string> flac_probe(const std::string& path);
