#pragma once

#include "audio_probe.h"

#include <expected>
#include <memory>
#include <string>
#include <vector>

class AudioReader {
public:
    struct Impl;

    AudioReader();
    AudioReader(const AudioReader&) = delete;
    AudioReader& operator=(const AudioReader&) = delete;
    AudioReader(AudioReader&&) noexcept;
    AudioReader& operator=(AudioReader&&) noexcept;
    ~AudioReader();

    static std::expected<AudioReader, std::string>
    open(const std::string& path);

    const AudioFileInfo& info() const;
    std::expected<void, std::string> seek_to_frame(int64_t frame);
    std::expected<int64_t, std::string> read_frames(float* out,
                                                    int64_t frames);

private:
    std::unique_ptr<Impl> impl_;
};

std::expected<std::vector<float>, std::string>
audio_read_full(const std::string& path, AudioFileInfo* info_out = nullptr);
