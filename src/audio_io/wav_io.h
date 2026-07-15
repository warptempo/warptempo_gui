#pragma once

#include <cstdint>
#include <cstdio>
#include <expected>
#include <span>
#include <string>
#include <vector>

// The WAV boundary is kept in-tree so package-level codec changes cannot
// silently redefine source decode or deliverable bytes. Decode exposes
// interleaved float32 from PCM 16 or PCM 24 sources; PCM_24 uses the exact
// lattice in pcm24.h. The writer emits a minimal spec-conformant PCM_24
// layout — the sole deliverable format — with the same clip-and-round policy.

enum class WavSampleFormat { Pcm16, Pcm24 };

// Refusal policy for absurd header claims, not a memory manager. This leaves
// generous headroom for real sources while rejecting malformed terabyte shapes.
inline constexpr uint64_t kMaxPlausibleAudioAllocBytes =
    8ull * 1024ull * 1024ull * 1024ull;

// Validates that an interleaved float32 buffer of frames-times-channels samples
// is a plausible allocation: shape sanity, multiplication overflow, the
// kMaxPlausibleAudioAllocBytes refusal, and size_t fit. Returns the element
// count for the allocation so every read path applies one policy and produces
// one message.
std::expected<size_t, std::string>
checked_audio_sample_count(int64_t frames, int channels);

// Appends the OS description for a nonzero errno to a failure message via std::error_code with the generic category, so file-open failures across the audio surface report the same thread-safe detail.
std::string append_errno_detail(const char* message, int err);

struct WavInfo {
    int             sample_rate = 0;
    int             channels    = 0;
    int64_t         frames      = 0;
    WavSampleFormat format      = WavSampleFormat::Pcm16;
};

// True when a WAV of this shape can no longer be finalized inside RIFF's
// 32-bit size fields. header_span is the byte length of everything before the
// data payload, data_bytes and frames_written are the running totals.
bool wav_exceeds_riff_limits(uint64_t header_span, uint64_t data_bytes,
                             uint64_t frames_written);

// Projects the writer's minimal PCM_24 layout for a WAV of this shape -
// header span plus data bytes - onto the existing RIFF-limit predicate so a
// render can be refused before synthesis rather than at the write that
// crosses the limit. Keep this in step with WavWriter::write_header().
bool wav_projected_exceeds_riff_limits(int channels, uint64_t frames);

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

// Call close() explicitly and check its result. The destructor closes only as a
// last resort and swallows errors by design.
class WavWriter {
public:
    WavWriter() = default;
    WavWriter(const WavWriter&) = delete;
    WavWriter& operator=(const WavWriter&) = delete;
    WavWriter(WavWriter&& other) noexcept;
    WavWriter& operator=(WavWriter&& other) noexcept;
    ~WavWriter();

    // open_file and open_memory write a PCM_24 layout and trust the caller's
    // channels and sample_rate, narrowing them into RIFF fmt's 16-bit and
    // 32-bit fields as written. Sources admitted by the in-tree probes are
    // structurally within those fields' ranges; geometry beyond them is not
    // refused, the header simply carries the narrowed values.
    static std::expected<WavWriter, std::string>
    open_file(const std::string& path, int channels, int sample_rate);
    static std::expected<WavWriter, std::string>
    open_memory(std::vector<char>& out, int channels, int sample_rate);

    std::expected<void, std::string> write_frames(const float* interleaved,
                                                  int64_t frames);
    std::expected<void, std::string> close();

private:
    enum class SinkKind { None, File, Memory };

    SinkKind sink_kind_ = SinkKind::None;
    FILE* file_ = nullptr;
    std::vector<char>* memory_ = nullptr;
    int channels_ = 0;
    int sample_rate_ = 0;
    uint64_t frames_written_ = 0;
    uint64_t data_bytes_ = 0;
    uint64_t riff_size_offset_ = 0;
    uint64_t data_size_offset_ = 0;
    bool closed_ = true;
    // Writer objects are single-threaded; this scratch is reused across writes.
    std::vector<unsigned char> scratch_;

    std::expected<void, std::string> write_header();
    std::expected<void, std::string> write_bytes(const void* data, size_t size);
    std::expected<void, std::string> patch_u32(uint64_t offset, uint32_t value);
    void reset();
};

class WavReader {
public:
    WavReader() = default;
    WavReader(const WavReader&) = delete;
    WavReader& operator=(const WavReader&) = delete;
    WavReader(WavReader&& other) noexcept;
    WavReader& operator=(WavReader&& other) noexcept;
    ~WavReader();

    static std::expected<WavReader, std::string>
    open(const std::string& path);

    const WavInfo& info() const { return info_; }
    std::expected<void, std::string> seek_to_frame(int64_t frame);
    std::expected<int64_t, std::string> read_frames(float* out,
                                                    int64_t frames);

private:
    FILE* file_ = nullptr;
    WavInfo info_;
    uint64_t data_offset_ = 0;
    uint16_t block_align_ = 0;
    int64_t cursor_frame_ = 0;
    // Reader objects are single-threaded; this scratch is reused across reads.
    std::vector<unsigned char> scratch_;

    void reset();
};
