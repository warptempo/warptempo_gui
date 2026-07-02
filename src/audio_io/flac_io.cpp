#include "flac_io.h"

#include "miniaudio.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <limits>

namespace {

struct DecoderHandle {
    ma_decoder decoder{};
    bool initialized = false;

    DecoderHandle() = default;
    DecoderHandle(const DecoderHandle&) = delete;
    DecoderHandle& operator=(const DecoderHandle&) = delete;
    DecoderHandle(DecoderHandle&&) = delete;
    DecoderHandle& operator=(DecoderHandle&&) = delete;

    ~DecoderHandle()
    {
        if (initialized) ma_decoder_uninit(&decoder);
    }
};

uint32_t u24be(const unsigned char* p)
{
    return (static_cast<uint32_t>(p[0]) << 16) |
           (static_cast<uint32_t>(p[1]) << 8) |
           static_cast<uint32_t>(p[2]);
}

std::expected<void, std::string> open_flac_decoder(DecoderHandle& h,
                                                   const std::string& path)
{
    ma_decoder_config config = ma_decoder_config_init(ma_format_s32, 0, 0);
    config.encodingFormat = ma_encoding_format_flac;
    const ma_result rc = ma_decoder_init_file(path.c_str(), &config, &h.decoder);
    if (rc != MA_SUCCESS) return std::unexpected("failed to open FLAC file");
    h.initialized = true;
    return {};
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
    DecoderHandle h;
    auto opened = open_flac_decoder(h, path);
    if (!opened) return std::unexpected(opened.error());
    if (begin_frame < 0 || end_frame < begin_frame || end_frame > info.frames) {
        return std::unexpected("invalid FLAC frame range");
    }
    if (info.channels <= 0 || info.sample_rate <= 0 || info.bits_per_sample <= 0 ||
        info.bits_per_sample > 32) {
        return std::unexpected("invalid FLAC stream info");
    }
    if (info_out) *info_out = info;
    if (ma_decoder_seek_to_pcm_frame(&h.decoder,
                                     static_cast<ma_uint64>(begin_frame)) !=
        MA_SUCCESS) {
        return std::unexpected("failed to seek FLAC file");
    }

    const int64_t frames = end_frame - begin_frame;
    const int64_t samples = frames * info.channels;
    std::vector<float> out(static_cast<size_t>(samples));
    if (samples == 0) return out;

    std::vector<ma_int32> s32(static_cast<size_t>(samples));
    ma_uint64 got = 0;
    const ma_result read_rc = ma_decoder_read_pcm_frames(
        &h.decoder, s32.data(), static_cast<ma_uint64>(frames), &got);
    if (read_rc != MA_SUCCESS) {
        return std::unexpected("failed to read FLAC data");
    }
    if (got != static_cast<ma_uint64>(frames)) {
        return std::unexpected("truncated FLAC data");
    }

    // ma_decoder is configured for ma_format_s32; miniaudio's FLAC backend
    // reaches ma_dr_flac_read_pcm_frames_s32, which left-shifts decoded samples
    // by 32 - bitsPerSample before returning, so divide by full-scale s32.
    constexpr float kScale = 1.0f / 2147483648.0f;
    for (size_t i = 0; i < s32.size(); ++i) {
        out[i] = static_cast<float>(s32[i]) * kScale;
    }
    return out;
}
