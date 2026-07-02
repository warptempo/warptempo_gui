#include "flac_io.h"

#include "audio_reader.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <limits>

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
    if (!f) return std::unexpected("failed to open FLAC file");
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
    if (info.sample_rate <= 0 || info.channels <= 0 ||
        info.bits_per_sample <= 0 || info.frames < 0) {
        return std::unexpected("invalid FLAC STREAMINFO values");
    }
    return info;
}

std::expected<std::vector<float>, std::string>
flac_read_full(const std::string& path, FlacInfo* info_out)
{
    auto info = flac_probe(path);
    if (!info) return std::unexpected(info.error());
    return flac_read_range(path, 0, info->frames, info_out);
}

std::expected<std::vector<float>, std::string>
flac_read_range(const std::string& path, int64_t begin_frame,
                int64_t end_frame, FlacInfo* info_out)
{
    auto info_probe = flac_probe(path);
    if (!info_probe) return std::unexpected(info_probe.error());
    FlacInfo info = *info_probe;
    if (begin_frame < 0 || end_frame < begin_frame || end_frame > info.frames) {
        return std::unexpected("invalid FLAC frame range");
    }
    if (info.channels <= 0 || info.sample_rate <= 0 || info.bits_per_sample <= 0 ||
        info.bits_per_sample > 32) {
        return std::unexpected("invalid FLAC stream info");
    }
    if (info_out) *info_out = info;

    const int64_t frames = end_frame - begin_frame;
    const int64_t samples = frames * info.channels;
    std::vector<float> out(static_cast<size_t>(samples));
    if (samples == 0) return out;

    auto reader = AudioReader::open(path);
    if (!reader) return std::unexpected(reader.error());
    auto seeked = reader->seek_to_frame(begin_frame);
    if (!seeked) return std::unexpected(seeked.error());
    auto got = reader->read_frames(out.data(), frames);
    if (!got) return std::unexpected(got.error());
    if (*got != frames) {
        return std::unexpected("truncated FLAC data");
    }
    return out;
}
