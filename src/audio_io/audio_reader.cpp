#include "audio_reader.h"

#include "dr_flac.h"
#include "wav_io.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>

namespace {

// dr_flac emits sample codes shifted into the top of s32; scaling by 2^-31
// reduces that to code / 2^(bits - 1). The float cast is exact because
// flac_probe admits at most 24 significant bits.
constexpr float kFlacS32Scale = 1.0f / 2147483648.0f;

void flac_s32_to_float(const drflac_int32* in, float* out, size_t samples)
{
    for (size_t i = 0; i < samples; ++i) {
        out[i] = static_cast<float>(in[i]) * kFlacS32Scale;
    }
}

struct FlacDecoder {
    drflac* handle = nullptr;

    FlacDecoder() = default;
    FlacDecoder(const FlacDecoder&) = delete;
    FlacDecoder& operator=(const FlacDecoder&) = delete;
    FlacDecoder(FlacDecoder&& other) noexcept
        : handle(other.handle)
    {
        other.handle = nullptr;
    }
    FlacDecoder& operator=(FlacDecoder&& other) noexcept
    {
        if (this == &other) return *this;
        if (handle) drflac_close(handle);
        handle = other.handle;
        other.handle = nullptr;
        return *this;
    }

    ~FlacDecoder()
    {
        if (handle) drflac_close(handle);
    }
};

} // namespace

struct AudioReader::Impl {
    virtual ~Impl() = default;
    virtual const AudioFileInfo& info() const = 0;
    virtual std::expected<void, std::string> seek_to_frame(int64_t frame) = 0;
    virtual std::expected<int64_t, std::string> read_frames(float* out,
                                                            int64_t frames) = 0;
};

namespace {

class WavAudioReader final : public AudioReader::Impl {
public:
    WavAudioReader(WavReader&& reader, AudioFileInfo info)
        : reader_(std::move(reader)), info_(info)
    {}

    const AudioFileInfo& info() const override { return info_; }

    std::expected<void, std::string> seek_to_frame(int64_t frame) override
    {
        return reader_.seek_to_frame(frame);
    }

    std::expected<int64_t, std::string> read_frames(float* out,
                                                    int64_t frames) override
    {
        return reader_.read_frames(out, frames);
    }

private:
    WavReader reader_;
    AudioFileInfo info_;
};

class FlacAudioReader final : public AudioReader::Impl {
public:
    FlacAudioReader(FlacDecoder&& decoder, AudioFileInfo info)
        : decoder_(std::move(decoder)), info_(info)
    {}

    const AudioFileInfo& info() const override { return info_; }

    std::expected<void, std::string> seek_to_frame(int64_t frame) override
    {
        if (frame < 0 || frame > info_.frames) {
            return std::unexpected("invalid FLAC frame range");
        }
        if (!drflac_seek_to_pcm_frame(
                decoder_.handle, static_cast<drflac_uint64>(frame))) {
            return std::unexpected("failed to seek FLAC file");
        }
        cursor_frame_ = frame;
        return {};
    }

    std::expected<int64_t, std::string> read_frames(float* out,
                                                    int64_t frames) override
    {
        if (frames < 0 || (frames > 0 && out == nullptr)) {
            return std::unexpected("invalid FLAC frame read");
        }
        const int64_t remaining = info_.frames - cursor_frame_;
        const int64_t to_read = std::min(frames, remaining);
        if (to_read <= 0) return int64_t{0};

        const uint64_t samples =
            static_cast<uint64_t>(to_read) *
            static_cast<uint64_t>(info_.channels);
        if (samples > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
            return std::unexpected("FLAC read is too large");
        }

        scratch_.resize(static_cast<size_t>(samples));
        // The request is clamped to frames remaining per the probed stream
        // length, so a short return within the clamped request means
        // end-of-stream arrived early: mid-stream decode loss or truncation.
        const drflac_uint64 got = drflac_read_pcm_frames_s32(
            decoder_.handle, static_cast<drflac_uint64>(to_read),
            scratch_.data());
        cursor_frame_ += static_cast<int64_t>(got);
        const size_t got_samples =
            static_cast<size_t>(got) * static_cast<size_t>(info_.channels);
        flac_s32_to_float(scratch_.data(), out, got_samples);
        return static_cast<int64_t>(got);
    }

private:
    FlacDecoder decoder_;
    AudioFileInfo info_;
    int64_t cursor_frame_ = 0;
    // Reader objects are single-threaded; this scratch is reused across reads.
    std::vector<drflac_int32> scratch_;
};

} // namespace

AudioReader::AudioReader() = default;
AudioReader::AudioReader(AudioReader&&) noexcept = default;
AudioReader& AudioReader::operator=(AudioReader&&) noexcept = default;
AudioReader::~AudioReader() = default;

std::expected<AudioReader, std::string>
AudioReader::open(const std::string& path)
{
    auto probed = audio_probe(path);
    if (!probed) return std::unexpected(probed.error());

    AudioReader out;
    if (probed->kind == AudioFileKind::Flac) {
        FlacDecoder decoder;
        decoder.handle = drflac_open_file(path.c_str(), nullptr);
        if (!decoder.handle) return std::unexpected("failed to open FLAC file");
        // The probe hand-parses STREAMINFO and enforces project policy (known
        // length, exactly 16 or 24 bits). Re-check the fields the decoder parsed so a
        // malformed or inconsistent header cannot slip a different geometry past
        // the probe, and re-verify the bit-depth gate the float-exactness
        // argument depends on against the decoder's own view.
        if (static_cast<int64_t>(decoder.handle->totalPCMFrameCount) !=
                probed->frames ||
            static_cast<int>(decoder.handle->channels) != probed->channels ||
            static_cast<int>(decoder.handle->sampleRate) != probed->sample_rate) {
            return std::unexpected(
                "FLAC decoder disagrees with STREAMINFO probe");
        }
        if (decoder.handle->bitsPerSample != 16 &&
            decoder.handle->bitsPerSample != 24) {
            return std::unexpected(
                "unsupported FLAC bit depth (16- or 24-bit expected)");
        }
        out.impl_ = std::make_unique<FlacAudioReader>(std::move(decoder), *probed);
    } else {
        auto reader = WavReader::open(path);
        if (!reader) return std::unexpected(reader.error());
        out.impl_ = std::make_unique<WavAudioReader>(std::move(*reader), *probed);
    }
    return out;
}

const AudioFileInfo& AudioReader::info() const
{
    return impl_->info();
}

std::expected<void, std::string> AudioReader::seek_to_frame(int64_t frame)
{
    return impl_->seek_to_frame(frame);
}

std::expected<int64_t, std::string> AudioReader::read_frames(float* out,
                                                             int64_t frames)
{
    return impl_->read_frames(out, frames);
}

std::expected<void, std::string>
read_frames_exact(AudioReader& reader, float* out, int64_t frames)
{
    auto got = reader.read_frames(out, frames);
    if (!got) return std::unexpected(got.error());
    if (*got != frames) {
        return std::unexpected("short audio read (got " +
                               std::to_string(*got) + " of " +
                               std::to_string(frames) + " frames)");
    }
    return {};
}

std::expected<std::vector<float>, std::string>
audio_read_full(const std::string& path, AudioFileInfo* info_out)
{
    auto reader = AudioReader::open(path);
    if (!reader) return std::unexpected(reader.error());
    const AudioFileInfo info = reader->info();
    auto sample_count = checked_audio_sample_count(info.frames, info.channels);
    if (!sample_count) return std::unexpected(sample_count.error());
    std::vector<float> out(*sample_count);
    auto read = read_frames_exact(*reader, out.data(), info.frames);
    if (!read) return std::unexpected(read.error());
    if (info_out) *info_out = info;
    return out;
}
