#pragma once

#include "audio_probe.h"

#include <expected>
#include <memory>
#include <string>
#include <vector>

// The audio I/O surface is deliberately asymmetric. WAV exposes probe, full
// read, range read, streaming reader, and writer APIs; FLAC exposes probe plus
// this streaming reader only. audio_probe dispatches on file magic alone, and
// FLAC means native FLAC, not another container carrying FLAC frames.
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
    // WAV clamps to frames remaining, so a short count is clean end-of-stream
    // with exact position bookkeeping. FLAC returns the decoder's count; a
    // short read means end-of-stream or mid-stream decode loss. Inside a probed
    // known-length range, use read_frames_exact.
    std::expected<int64_t, std::string> read_frames(float* out,
                                                    int64_t frames);

private:
    std::unique_ptr<Impl> impl_;
};

// Read exactly `frames` frames or fail. For use inside a probed,
// known-length stream where the caller has already validated the
// range against info().frames, so a short underlying read can only
// mean the stream is truncated or lost frames mid-stream (dr_flac
// silently skips FLAC frames whose CRC mismatches, shortening the
// stream). This contract detects truncation; it cannot detect a
// content splice inside a read that still returns the full count.
// Both backends treat a short return as terminal, so there is no
// retry loop.
std::expected<void, std::string>
read_frames_exact(AudioReader& reader, float* out, int64_t frames);

std::expected<std::vector<float>, std::string>
audio_read_full(const std::string& path, AudioFileInfo* info_out = nullptr);
