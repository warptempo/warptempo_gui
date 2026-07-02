#include "pcm24.h"
#include "wav_io.h"

#include <unistd.h>

#include <bit>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <limits>
#include <random>
#include <span>
#include <string>
#include <vector>

namespace {

bool same_float_bits(float a, float b)
{
    return std::bit_cast<uint32_t>(a) == std::bit_cast<uint32_t>(b);
}

std::string temp_path(const char* stem)
{
    return "/tmp/warptempo_audio_io_test_" + std::to_string(getpid()) + "_" +
           stem + ".wav";
}

std::span<const char> bytes_span(const std::vector<char>& v)
{
    return std::span<const char>(v.data(), v.size());
}

bool write_memory_roundtrip(WavSampleFormat fmt, const std::vector<float>& in,
                            int channels, int sample_rate)
{
    std::vector<char> blob;
    auto writer = WavWriter::open_memory(blob, fmt, channels, sample_rate);
    if (!writer) {
        std::cout << "selftest: memory writer open failed: "
                  << writer.error() << "\n";
        return false;
    }
    auto ok = writer->write_frames(in.data(),
                                   static_cast<int64_t>(in.size() / channels));
    if (!ok) {
        std::cout << "selftest: memory write failed: " << ok.error() << "\n";
        return false;
    }
    ok = writer->close();
    if (!ok) {
        std::cout << "selftest: memory close failed: " << ok.error() << "\n";
        return false;
    }
    WavInfo info;
    auto out = wav_read_full(bytes_span(blob), &info);
    if (!out) {
        std::cout << "selftest: memory read failed: " << out.error() << "\n";
        return false;
    }
    if (info.channels != channels || info.sample_rate != sample_rate ||
        out->size() != in.size()) {
        std::cout << "selftest: memory metadata mismatch\n";
        return false;
    }
    for (size_t i = 0; i < in.size(); ++i) {
        const float want = fmt == WavSampleFormat::Pcm24
                               ? pcm24_quantize(in[i])
                               : in[i];
        if (!same_float_bits((*out)[i], want)) {
            std::cout << "selftest: memory sample mismatch at " << i << "\n";
            return false;
        }
    }
    return true;
}

bool write_file_roundtrip(WavSampleFormat fmt, const std::vector<float>& in,
                          int channels, int sample_rate, const char* stem)
{
    const std::string path = temp_path(stem);
    auto writer = WavWriter::open_file(path, fmt, channels, sample_rate);
    if (!writer) {
        std::cout << "selftest: file writer open failed: " << writer.error()
                  << "\n";
        return false;
    }
    auto ok = writer->write_frames(in.data(),
                                   static_cast<int64_t>(in.size() / channels));
    if (!ok) {
        std::cout << "selftest: file write failed: " << ok.error() << "\n";
        std::remove(path.c_str());
        return false;
    }
    ok = writer->close();
    if (!ok) {
        std::cout << "selftest: file close failed: " << ok.error() << "\n";
        std::remove(path.c_str());
        return false;
    }
    WavInfo info;
    auto out = wav_read_full(path, &info);
    std::remove(path.c_str());
    if (!out) {
        std::cout << "selftest: file read failed: " << out.error() << "\n";
        return false;
    }
    if (info.channels != channels || info.sample_rate != sample_rate ||
        out->size() != in.size()) {
        std::cout << "selftest: file metadata mismatch\n";
        return false;
    }
    for (size_t i = 0; i < in.size(); ++i) {
        const float want = fmt == WavSampleFormat::Pcm24
                               ? pcm24_quantize(in[i])
                               : in[i];
        if (!same_float_bits((*out)[i], want)) {
            std::cout << "selftest: file sample mismatch at " << i << "\n";
            return false;
        }
    }
    return true;
}

int run_selftest()
{
    uint64_t checked = 0;
    for (int32_t c = -8388608;; ++c) {
        const int32_t got = pcm24_code_from_float(pcm24_float_from_code(c));
        if (got != c) {
            std::cout << "selftest: pcm24 exhaustive failed at code " << c
                      << " got " << got << "\n";
            return 1;
        }
        ++checked;
        if (c == 8388607) break;
    }
    std::cout << "selftest: pcm24 exhaustive roundtrip passed: " << checked
              << " codes\n";

    std::vector<float> policy = {
        0.0f,
        -0.0f,
        1.0f,
        -1.0f,
        std::numeric_limits<float>::denorm_min(),
        -std::numeric_limits<float>::denorm_min(),
        std::numeric_limits<float>::quiet_NaN(),
    };
    std::mt19937 rng(0x5eed1234u);
    std::uniform_real_distribution<float> dist(-2.0f, 2.0f);
    for (int i = 0; i < 200000; ++i) policy.push_back(dist(rng));
    for (float x : policy) {
        if (std::isnan(x) && pcm24_code_from_float(x) != 0) {
            std::cout << "selftest: NaN did not map to code 0\n";
            return 1;
        }
        const float once = pcm24_quantize(x);
        const float twice = pcm24_quantize(once);
        if (!same_float_bits(once, twice)) {
            std::cout << "selftest: quantize idempotence failed\n";
            return 1;
        }
    }
    if (pcm24_code_from_float(1.0f) != 8388607 ||
        pcm24_code_from_float(-1.0f) != -8388608) {
        std::cout << "selftest: clamp policy failed\n";
        return 1;
    }
    std::cout << "selftest: pcm24 policy/idempotence passed\n";

    std::vector<float> audio(8192 * 2);
    std::uniform_real_distribution<float> audiodist(-1.5f, 1.5f);
    for (float& x : audio) x = audiodist(rng);
    if (!write_memory_roundtrip(WavSampleFormat::Pcm24, audio, 2, 48000) ||
        !write_memory_roundtrip(WavSampleFormat::Float32, audio, 2, 48000) ||
        !write_file_roundtrip(WavSampleFormat::Pcm24, audio, 2, 48000,
                              "pcm24") ||
        !write_file_roundtrip(WavSampleFormat::Float32, audio, 2, 48000,
                              "float32")) {
        return 1;
    }
    std::cout << "selftest: WAV memory/file roundtrips passed\n";
    std::cout << "selftest: all passed\n";
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cout << "usage: warptempo_audio_io_test selftest\n";
        return 1;
    }
    const std::string mode = argv[1];
    if (mode == "selftest") {
        if (argc != 2) {
            std::cout << "selftest: unexpected arguments\n";
            return 1;
        }
        return run_selftest();
    }
    std::cout << "unknown mode: " << mode << "\n";
    return 1;
}
