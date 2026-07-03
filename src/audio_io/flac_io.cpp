#include "flac_io.h"

#include "wav_io.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdint>

namespace {

uint32_t u24be(const unsigned char* p)
{
    return (static_cast<uint32_t>(p[0]) << 16) |
           (static_cast<uint32_t>(p[1]) << 8) |
           static_cast<uint32_t>(p[2]);
}

} // namespace

std::expected<FlacInfo, std::string> flac_probe(const std::string& path)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        const int err = errno;
        return std::unexpected(
            append_errno_detail("failed to open FLAC file", err));
    }
    unsigned char header[42] = {};
    const size_t got = std::fread(header, 1, sizeof(header), f);
    std::fclose(f);
    if (got != sizeof(header)) {
        return std::unexpected("short FLAC header");
    }
    if (std::memcmp(header, "fLaC", 4) != 0) {
        return std::unexpected("not a native FLAC file");
    }
    const unsigned char block_type = header[4] & 0x7f;
    const uint32_t block_size = u24be(header + 5);
    if (block_type != 0 || block_size != 34) {
        return std::unexpected("FLAC STREAMINFO block not found");
    }

    const unsigned char* s = header + 8;
    FlacInfo info;
    info.sample_rate = (static_cast<int>(s[10]) << 12) |
                       (static_cast<int>(s[11]) << 4) |
                       (static_cast<int>(s[12]) >> 4);
    info.channels = ((s[12] >> 1) & 0x07) + 1;
    info.bits_per_sample = (((s[12] & 0x01) << 4) | (s[13] >> 4)) + 1;
    const uint64_t total_hi = static_cast<uint64_t>(s[13] & 0x0f) << 32;
    const uint64_t total_lo = (static_cast<uint64_t>(s[14]) << 24) |
                              (static_cast<uint64_t>(s[15]) << 16) |
                              (static_cast<uint64_t>(s[16]) << 8) |
                              static_cast<uint64_t>(s[17]);
    info.frames = static_cast<int64_t>(total_hi | total_lo);
    std::memcpy(info.md5, s + 18, sizeof(info.md5));
    if (info.sample_rate <= 0 || info.channels <= 0 ||
        info.bits_per_sample <= 0 || info.frames < 0) {
        return std::unexpected("invalid FLAC STREAMINFO values");
    }
    if (info.frames == 0) {
        return std::unexpected(
            "FLAC STREAMINFO reports unknown stream length; re-encode the file once at acquisition");
    }
    if (info.bits_per_sample > 24) {
        return std::unexpected("unsupported FLAC bit depth (16- or 24-bit expected)");
    }
    return info;
}
