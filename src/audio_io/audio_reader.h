#pragma once

#include "audio_probe.h"

#include <expected>
#include <memory>
#include <string>
#include <vector>

// The audio I/O surface is WAV-only: probe, full read, range read, this
// streaming reader, and writer APIs. audio_probe dispatches on file magic
// alone, so anything that is not a RIFF/WAVE container refuses with the
// unknown-magic error.
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
    // The reader clamps to frames remaining, so reading past the end returns
    // a short count with exact position bookkeeping. A short count inside the
    // clamped request means mid-stream decode loss or truncation. Inside a
    // probed known-length range, use read_frames_exact.
    std::expected<int64_t, std::string> read_frames(float* out,
                                                    int64_t frames);

private:
    std::unique_ptr<Impl> impl_;
};

// Read exactly `frames` frames or fail. For use inside a probed,
// known-length stream where the caller has already validated the range against
// info().frames, so a short read inside the clamped request can only mean the
// stream is truncated or lost frames mid-stream. This contract detects
// truncation; it cannot detect a content splice inside a read that still
// returns the full count. A short return is terminal, so there is no retry
// loop.
std::expected<void, std::string>
read_frames_exact(AudioReader& reader, float* out, int64_t frames);

std::expected<std::vector<float>, std::string>
audio_read_full(const std::string& path, AudioFileInfo* info_out = nullptr);
