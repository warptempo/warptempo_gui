#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

// The exact error audio_probe returns when the file's leading magic bytes
// match no recognized container (as opposed to a malformed but recognized
// WAV, which yields that owner's concrete diagnostic). Callers compare
// a probe failure against this to tell "unrecognized format" apart from
// "corrupt supported container", and append the convert-once acquisition
// hint only for the former.
//
// Its sentence-initial capital is part of the 2026-08-02 terminal
// capitalization pass (architect approval 2026-08-02) — text-only, otherwise
// byte-identical output; the constant is the ONE definition both the message
// and the equality compare read, so the compare is unaffected. The fuller
// record, including why this vocabulary moves together with wav_io.cpp's,
// lives at audio_probe.cpp's head.
inline constexpr std::string_view kUnknownAudioMagicError =
    "Unknown audio file magic";

struct AudioFileInfo {
    int           sample_rate = 0;
    int           channels    = 0;
    int64_t       frames      = 0;
};

std::expected<AudioFileInfo, std::string>
audio_probe(const std::string& path);
