#include "audio_probe.h"

#include "wav_io.h"

#include <cerrno>
#include <cstdio>
#include <cstring>

// Terminal message strings in this file carry sentence-initial capitals
// (architect approval 2026-08-02, the terminal capitalization pass —
// text-only, otherwise byte-identical output). They share the printed slot
// with wav_io.cpp's vocabulary (a probe failure forwards the WAV owner's
// diagnostic verbatim), so the two files move together.

std::expected<AudioFileInfo, std::string>
audio_probe(const std::string& path)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        const int err = errno;
        return std::unexpected(
            append_errno_detail("Failed to open audio file", err));
    }
    unsigned char magic[4] = {};
    const size_t got = std::fread(magic, 1, sizeof(magic), f);
    std::fclose(f);
    if (got != sizeof(magic)) {
        return std::unexpected("Audio file is too short");
    }

    if (std::memcmp(magic, "RIFF", 4) == 0) {
        auto info = wav_probe(path);
        if (!info) return std::unexpected(info.error());
        AudioFileInfo out;
        out.sample_rate = info->sample_rate;
        out.channels = info->channels;
        out.frames = info->frames;
        return out;
    }

    return std::unexpected(std::string(kUnknownAudioMagicError));
}
