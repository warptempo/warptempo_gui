#pragma once

#include <cstdint>
#include <cstdio>
#include <expected>
#include <span>
#include <string>
#include <vector>

// The WAV boundary is kept in-tree so package-level libsndfile changes cannot
// silently redefine source decode or deliverable bytes. Decode exposes
// interleaved float32; PCM_24 uses the exact lattice in pcm24.h, while Float32
// preserves source bits. Encode owns the same PCM_24 clip-and-round policy.
// The writer emits a minimal spec-conformant layout; layout parity with the
// incumbent libsndfile recipe is measured by the compare-encode test mode.

enum class WavSampleFormat { Pcm16, Pcm24, Float32 };

struct WavInfo {
    int             sample_rate = 0;
    int             channels    = 0;
    int64_t         frames      = 0;
    WavSampleFormat format      = WavSampleFormat::Float32;
};

std::expected<WavInfo, std::string> wav_probe(const std::string& path);
std::expected<WavInfo, std::string> wav_probe(std::span<const char> bytes);

std::expected<std::vector<float>, std::string>
wav_read_full(const std::string& path, WavInfo* info_out = nullptr);
std::expected<std::vector<float>, std::string>
wav_read_full(std::span<const char> bytes, WavInfo* info_out = nullptr);

std::expected<std::vector<float>, std::string>
wav_read_range(const std::string& path, int64_t begin_frame, int64_t end_frame,
               WavInfo* info_out = nullptr);
std::expected<std::vector<float>, std::string>
wav_read_range(std::span<const char> bytes, int64_t begin_frame,
               int64_t end_frame, WavInfo* info_out = nullptr);

class WavWriter {
public:
    WavWriter() = default;
    WavWriter(const WavWriter&) = delete;
    WavWriter& operator=(const WavWriter&) = delete;
    WavWriter(WavWriter&& other) noexcept;
    WavWriter& operator=(WavWriter&& other) noexcept;
    ~WavWriter();

    static std::expected<WavWriter, std::string>
    open_file(const std::string& path, WavSampleFormat format, int channels,
              int sample_rate);
    static std::expected<WavWriter, std::string>
    open_memory(std::vector<char>& out, WavSampleFormat format, int channels,
                int sample_rate);

    std::expected<void, std::string> write_frames(const float* interleaved,
                                                  int64_t frames);
    std::expected<void, std::string> close();

private:
    enum class SinkKind { None, File, Memory };

    SinkKind sink_kind_ = SinkKind::None;
    FILE* file_ = nullptr;
    std::vector<char>* memory_ = nullptr;
    WavSampleFormat format_ = WavSampleFormat::Float32;
    int channels_ = 0;
    int sample_rate_ = 0;
    uint64_t frames_written_ = 0;
    uint64_t data_bytes_ = 0;
    uint64_t riff_size_offset_ = 0;
    uint64_t fact_frames_offset_ = 0;
    uint64_t data_size_offset_ = 0;
    bool closed_ = true;

    std::expected<void, std::string> write_header();
    std::expected<void, std::string> write_bytes(const void* data, size_t size);
    std::expected<void, std::string> patch_u32(uint64_t offset, uint32_t value);
    void reset();
};
