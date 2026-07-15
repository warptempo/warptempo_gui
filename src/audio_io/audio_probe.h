#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

enum class AudioFileKind { WavPcm16, WavPcm24, WavFloat32 };

// The exact error audio_probe returns when the file's leading magic bytes
// match no recognized container (as opposed to a malformed but recognized
// WAV, which yields that owner's concrete diagnostic). Callers compare
// a probe failure against this to tell "unrecognized format" apart from
// "corrupt supported container", and append the convert-once acquisition
// hint only for the former.
inline constexpr std::string_view kUnknownAudioMagicError =
    "unknown audio file magic";

struct AudioFileInfo {
    AudioFileKind kind;
    int           sample_rate = 0;
    int           channels    = 0;
    int64_t       frames      = 0;
};

std::expected<AudioFileInfo, std::string>
audio_probe(const std::string& path);
