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
    // STREAMINFO MD5 of unencoded audio for FLAC when present; WAV has no embedded content hash.
    unsigned char content_md5[16] = {};
    bool          has_content_md5 = false;
};

std::expected<AudioFileInfo, std::string>
audio_probe(const std::string& path);
