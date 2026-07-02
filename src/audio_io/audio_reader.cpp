#include "audio_reader.h"

#include "miniaudio.h"
#include "wav_io.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>

namespace {

constexpr float kFlacS32Scale = 1.0f / 2147483648.0f;

void flac_s32_to_float(const ma_int32* in, float* out, size_t samples)
{
    for (size_t i = 0; i < samples; ++i) {
        out[i] = static_cast<float>(in[i]) * kFlacS32Scale;
    }
}

struct FlacDecoder {
    ma_decoder decoder{};
    bool initialized = false;

    FlacDecoder() = default;
    FlacDecoder(const FlacDecoder&) = delete;
    FlacDecoder& operator=(const FlacDecoder&) = delete;
    FlacDecoder(FlacDecoder&& other) noexcept
    {
        decoder = other.decoder;
        initialized = other.initialized;
        other.decoder = {};
        other.initialized = false;
    }
    FlacDecoder& operator=(FlacDecoder&& other) noexcept
    {
        if (this == &other) return *this;
        if (initialized) ma_decoder_uninit(&decoder);
        decoder = other.decoder;
        initialized = other.initialized;
        other.decoder = {};
        other.initialized = false;
        return *this;
    }

    ~FlacDecoder()
    {
        if (initialized) ma_decoder_uninit(&decoder);
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
        if (ma_decoder_seek_to_pcm_frame(
                &decoder_.decoder, static_cast<ma_uint64>(frame)) != MA_SUCCESS) {
            return std::unexpected("failed to seek FLAC file");
        }
        return {};
    }

    std::expected<int64_t, std::string> read_frames(float* out,
                                                    int64_t frames) override
    {
        if (frames < 0 || (frames > 0 && out == nullptr)) {
            return std::unexpected("invalid FLAC frame read");
        }
        if (frames == 0) return int64_t{0};

        const uint64_t samples =
            static_cast<uint64_t>(frames) * static_cast<uint64_t>(info_.channels);
        if (samples > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
            return std::unexpected("FLAC read is too large");
        }

        std::vector<ma_int32> s32(static_cast<size_t>(samples));
        ma_uint64 got = 0;
        const ma_result rc = ma_decoder_read_pcm_frames(
            &decoder_.decoder, s32.data(), static_cast<ma_uint64>(frames), &got);
        if (rc != MA_SUCCESS && rc != MA_AT_END) {
            return std::unexpected("failed to read FLAC data");
        }
        const size_t got_samples =
            static_cast<size_t>(got) * static_cast<size_t>(info_.channels);
        flac_s32_to_float(s32.data(), out, got_samples);
        return static_cast<int64_t>(got);
    }

private:
    FlacDecoder decoder_;
    AudioFileInfo info_;
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
        ma_decoder_config config = ma_decoder_config_init(ma_format_s32, 0, 0);
        config.encodingFormat = ma_encoding_format_flac;

        FlacDecoder decoder;
        const ma_result rc =
            ma_decoder_init_file(path.c_str(), &config, &decoder.decoder);
        if (rc != MA_SUCCESS) return std::unexpected("failed to open FLAC file");
        decoder.initialized = true;
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

std::expected<std::vector<float>, std::string>
audio_read_full(const std::string& path, AudioFileInfo* info_out)
{
    auto reader = AudioReader::open(path);
    if (!reader) return std::unexpected(reader.error());
    const AudioFileInfo info = reader->info();
    if (info.frames < 0 || info.channels <= 0) {
        return std::unexpected("invalid audio stream info");
    }
    const uint64_t samples =
        static_cast<uint64_t>(info.frames) * static_cast<uint64_t>(info.channels);
    if (samples > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        return std::unexpected("audio file is too large");
    }
    std::vector<float> out(static_cast<size_t>(samples));
    auto got = reader->read_frames(out.data(), info.frames);
    if (!got) return std::unexpected(got.error());
    if (*got != info.frames) return std::unexpected("short audio read");
    if (info_out) *info_out = info;
    return out;
}
