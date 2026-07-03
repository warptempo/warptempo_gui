#include "audio_probe.h"

#include "flac_io.h"
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

    if (std::memcmp(magic, "fLaC", 4) == 0) {
        auto info = flac_probe(path);
        if (!info) return std::unexpected(info.error());
        AudioFileInfo out;
        out.kind = AudioFileKind::Flac;
        out.sample_rate = info->sample_rate;
        out.channels = info->channels;
        out.frames = info->frames;
        std::memcpy(out.content_md5, info->md5, sizeof(out.content_md5));
        out.has_content_md5 = false;
        for (unsigned char b : out.content_md5) {
            if (b != 0) {
                out.has_content_md5 = true;
                break;
            }
        }
        return out;
    }

    return std::unexpected("unknown audio file magic");
}
