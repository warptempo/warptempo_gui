// The spike's OWN minimal WAV reader.
//
// src/audio_io is under permanent hard freeze and is not involved in the spike at
// all -- not even by inclusion. This reader exists so the spike can prove the
// AAudio callback path without borrowing a single frozen line, and it is
// deliberately dumber than the product's: canonical RIFF/WAVE, PCM 16- or 24-bit,
// stereo, any rate. Anything else is refused with a message that lands on screen.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

struct SpikeWav {
    int sample_rate = 0;
    int channels = 0;
    int bits = 0;
    // Interleaved float in [-1, 1], channels as stated. AAudio's PCM_FLOAT is the
    // same layout and the same range, so no conversion happens after this point.
    std::vector<float> samples;

    size_t frames() const { return channels > 0 ? samples.size() / static_cast<size_t>(channels) : 0; }
};

inline uint32_t spike_wav_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

inline uint16_t spike_wav_u16(const uint8_t* p) {
    return static_cast<uint16_t>(static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8));
}

inline bool spike_wav_parse(const uint8_t* data, size_t size, SpikeWav& out, std::string& err) {
    out = SpikeWav{};
    if (!data || size < 44) { err = "wav: too small"; return false; }
    if (std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0) {
        err = "wav: not RIFF/WAVE";
        return false;
    }

    size_t pos = 12;
    bool have_fmt = false;
    uint16_t fmt_tag = 0;
    const uint8_t* pcm = nullptr;
    size_t pcm_bytes = 0;

    while (pos + 8 <= size) {
        const char* id = reinterpret_cast<const char*>(data + pos);
        const uint32_t chunk = spike_wav_u32(data + pos + 4);
        const size_t body = pos + 8;
        if (body + chunk > size) break;

        if (std::memcmp(id, "fmt ", 4) == 0 && chunk >= 16) {
            fmt_tag = spike_wav_u16(data + body);
            out.channels = spike_wav_u16(data + body + 2);
            out.sample_rate = static_cast<int>(spike_wav_u32(data + body + 4));
            out.bits = spike_wav_u16(data + body + 14);
            have_fmt = true;
        } else if (std::memcmp(id, "data", 4) == 0) {
            pcm = data + body;
            pcm_bytes = chunk;
        }
        pos = body + chunk + (chunk & 1u);  // RIFF chunks are word-aligned
    }

    if (!have_fmt) { err = "wav: no fmt chunk"; return false; }
    if (!pcm) { err = "wav: no data chunk"; return false; }
    if (fmt_tag != 1) { err = "wav: not PCM (fmt " + std::to_string(fmt_tag) + ")"; return false; }
    if (out.channels != 2) { err = "wav: not stereo (" + std::to_string(out.channels) + " ch)"; return false; }
    if (out.bits != 16 && out.bits != 24) { err = "wav: bits " + std::to_string(out.bits); return false; }

    const size_t bytes_per_sample = static_cast<size_t>(out.bits) / 8;
    const size_t total = pcm_bytes / bytes_per_sample;
    out.samples.resize(total);
    if (out.bits == 16) {
        for (size_t i = 0; i < total; ++i) {
            const int16_t v = static_cast<int16_t>(spike_wav_u16(pcm + i * 2));
            out.samples[i] = static_cast<float>(v) / 32768.0f;
        }
    } else {
        for (size_t i = 0; i < total; ++i) {
            const uint8_t* p = pcm + i * 3;
            int32_t v = static_cast<int32_t>(static_cast<uint32_t>(p[0]) |
                                             (static_cast<uint32_t>(p[1]) << 8) |
                                             (static_cast<uint32_t>(p[2]) << 16));
            if (v & 0x800000) v -= 0x1000000;
            out.samples[i] = static_cast<float>(v) / 8388608.0f;
        }
    }
    return true;
}
