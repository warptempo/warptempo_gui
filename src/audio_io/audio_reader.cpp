#include "audio_reader.h"

#include "wav_io.h"

#include <cstdint>
#include <memory>
#include <utility>

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
    auto reader = WavReader::open(path);
    if (!reader) return std::unexpected(reader.error());
    out.impl_ = std::make_unique<WavAudioReader>(std::move(*reader), *probed);
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
