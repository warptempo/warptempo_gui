#pragma once

#include <cstdint>
#include <expected>
#include <string>

enum class AudioFileKind { WavPcm16, WavPcm24, WavFloat32, Flac };

struct AudioFileInfo {
    AudioFileKind kind;
    int           sample_rate = 0;
    int           channels    = 0;
    int64_t       frames      = 0;
};

std::expected<AudioFileInfo, std::string>
audio_probe(const std::string& path);
