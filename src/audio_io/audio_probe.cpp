#include "audio_probe.h"

#include "wav_io.h"

#include <cerrno>
#include <cstdio>
#include <cstring>

std::expected<AudioFileInfo, std::string>
audio_probe(const std::string& path)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        const int err = errno;
        return std::unexpected(
            append_errno_detail("failed to open audio file", err));
    }
    unsigned char magic[4] = {};
    const size_t got = std::fread(magic, 1, sizeof(magic), f);
    std::fclose(f);
    if (got != sizeof(magic)) {
        return std::unexpected("audio file is too short");
    }

    if (std::memcmp(magic, "RIFF", 4) == 0) {
        auto info = wav_probe(path);
        if (!info) return std::unexpected(info.error());
        AudioFileInfo out;
        out.sample_rate = info->sample_rate;
        out.channels = info->channels;
        out.frames = info->frames;
        switch (info->format) {
        case WavSampleFormat::Pcm16:
            out.kind = AudioFileKind::WavPcm16;
            break;
        case WavSampleFormat::Pcm24:
            out.kind = AudioFileKind::WavPcm24;
            break;
        case WavSampleFormat::Float32:
            out.kind = AudioFileKind::WavFloat32;
            break;
        }
        return out;
    }

    return std::unexpected(std::string(kUnknownAudioMagicError));
}
