#include "audio_probe.h"
#include "pcm24.h"
#include "wav_io.h"

#include <unistd.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <limits>
#include <random>
#include <span>
#include <string>
#include <vector>

bool run_source_sample_cache_selftest();

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

void append_fourcc(std::vector<char>& v, const char* s)
{
    v.insert(v.end(), s, s + 4);
}

void append_u16(std::vector<char>& v, uint16_t x)
{
    v.push_back(static_cast<char>(x & 0xff));
    v.push_back(static_cast<char>((x >> 8) & 0xff));
}

void append_u32(std::vector<char>& v, uint32_t x)
{
    v.push_back(static_cast<char>(x & 0xff));
    v.push_back(static_cast<char>((x >> 8) & 0xff));
    v.push_back(static_cast<char>((x >> 16) & 0xff));
    v.push_back(static_cast<char>((x >> 24) & 0xff));
}

void patch_u32(std::vector<char>& v, size_t off, uint32_t x)
{
    v[off + 0] = static_cast<char>(x & 0xff);
    v[off + 1] = static_cast<char>((x >> 8) & 0xff);
    v[off + 2] = static_cast<char>((x >> 16) & 0xff);
    v[off + 3] = static_cast<char>((x >> 24) & 0xff);
}

void append_i16(std::vector<char>& v, int16_t x)
{
    append_u16(v, static_cast<uint16_t>(x));
}

void append_float32(std::vector<char>& v, float x)
{
    append_u32(v, std::bit_cast<uint32_t>(x));
}

void append_pcm16_fmt(std::vector<char>& v, uint16_t channels,
                      uint32_t sample_rate)
{
    append_fourcc(v, "fmt ");
    append_u32(v, 16);
    append_u16(v, 1);
    append_u16(v, channels);
    append_u32(v, sample_rate);
    append_u32(v, sample_rate * channels * 2u);
    append_u16(v, static_cast<uint16_t>(channels * 2u));
    append_u16(v, 16);
}

void append_extensible_fmt(std::vector<char>& v, uint16_t channels,
                           uint32_t sample_rate, uint16_t bits,
                           uint16_t valid_bits, uint32_t subformat_tag)
{
    const uint16_t bytes_per_sample = static_cast<uint16_t>(bits / 8);
    const uint16_t block_align =
        static_cast<uint16_t>(channels * bytes_per_sample);
    const unsigned char guid_tail[12] = {
        0x00, 0x00, 0x10, 0x00, 0x80, 0x00,
        0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71
    };

    append_fourcc(v, "fmt ");
    append_u32(v, 40);
    append_u16(v, 0xfffe);
    append_u16(v, channels);
    append_u32(v, sample_rate);
    append_u32(v, sample_rate * block_align);
    append_u16(v, block_align);
    append_u16(v, bits);
    append_u16(v, 22);
    append_u16(v, valid_bits);
    append_u32(v, 0);
    append_u32(v, subformat_tag);
    for (unsigned char b : guid_tail) v.push_back(static_cast<char>(b));
}

std::vector<char> riff_prefix()
{
    std::vector<char> v;
    append_fourcc(v, "RIFF");
    append_u32(v, 0);
    append_fourcc(v, "WAVE");
    return v;
}

void finalize_riff_size(std::vector<char>& v)
{
    patch_u32(v, 4, static_cast<uint32_t>(v.size() - 8));
}

size_t find_chunk(const std::vector<char>& v, const char* id)
{
    auto it = std::search(v.begin(), v.end(), id, id + 4);
    return it == v.end() ? v.size() : static_cast<size_t>(it - v.begin());
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

bool test_riff_limit_predicate()
{
    constexpr uint64_t u32max = std::numeric_limits<uint32_t>::max();
    constexpr uint64_t header_span = 44;
    if (wav_exceeds_riff_limits(header_span, u32max - 36, 1) ||
        !wav_exceeds_riff_limits(header_span, u32max - 35, 1)) {
        std::cout << "selftest: RIFF size limit predicate failed\n";
        return false;
    }
    if (wav_exceeds_riff_limits(8, u32max, 1) ||
        !wav_exceeds_riff_limits(8, u32max + 1, 1)) {
        std::cout << "selftest: data chunk limit predicate failed\n";
        return false;
    }
    if (wav_exceeds_riff_limits(8, 0, u32max) ||
        !wav_exceeds_riff_limits(8, 0, u32max + 1)) {
        std::cout << "selftest: fact frame limit predicate failed\n";
        return false;
    }
    return true;
}

bool test_data_before_fmt_parses()
{
    std::vector<char> blob = riff_prefix();
    append_fourcc(blob, "data");
    append_u32(blob, 2);
    append_u16(blob, 0x4000);
    append_pcm16_fmt(blob, 1, 48000);
    finalize_riff_size(blob);

    WavInfo info;
    auto out = wav_read_full(bytes_span(blob), &info);
    if (!out || info.channels != 1 || info.sample_rate != 48000 ||
        info.frames != 1 || out->size() != 1 || (*out)[0] != 0.5f) {
        std::cout << "selftest: data-before-fmt parse failed";
        if (!out) std::cout << ": " << out.error();
        std::cout << "\n";
        return false;
    }
    return true;
}

bool test_data_before_fmt_overrun_fails_cleanly()
{
    std::vector<char> blob = riff_prefix();
    append_fourcc(blob, "data");
    append_u32(blob, 100);
    finalize_riff_size(blob);

    // An ordinary declared size extending past EOF is refused outright —
    // only the 0xffffffff streamed placeholder gets the clamp-to-present
    // tolerance (test_streamed_unpatched_data_size_fixture).
    auto out = wav_read_full(bytes_span(blob));
    if (out || out.error() != "WAV chunk extends past end of file") {
        std::cout << "selftest: data-before-fmt overrun failure mismatch";
        if (!out) std::cout << ": " << out.error();
        std::cout << "\n";
        return false;
    }
    return true;
}

bool test_odd_junk_chunk_before_data()
{
    const std::vector<float> in = {-0.25f, 0.75f, 1.25f, -1.25f};
    std::vector<char> blob;
    auto writer = WavWriter::open_memory(blob, WavSampleFormat::Pcm24, 2, 44100);
    if (!writer) {
        std::cout << "selftest: odd-junk writer open failed: "
                  << writer.error() << "\n";
        return false;
    }
    auto ok = writer->write_frames(in.data(), 2);
    if (!ok) {
        std::cout << "selftest: odd-junk write failed: " << ok.error()
                  << "\n";
        return false;
    }
    ok = writer->close();
    if (!ok) {
        std::cout << "selftest: odd-junk close failed: " << ok.error()
                  << "\n";
        return false;
    }

    const size_t data_pos = find_chunk(blob, "data");
    if (data_pos == blob.size()) {
        std::cout << "selftest: odd-junk data chunk not found\n";
        return false;
    }
    std::vector<char> junk;
    append_fourcc(junk, "JUNK");
    append_u32(junk, 5);
    junk.insert(junk.end(), {'a', 'b', 'c', 'd', 'e', '\0'});
    blob.insert(blob.begin() + static_cast<std::ptrdiff_t>(data_pos),
                junk.begin(), junk.end());
    finalize_riff_size(blob);

    auto out = wav_read_full(bytes_span(blob));
    if (!out || out->size() != in.size()) {
        std::cout << "selftest: odd-junk read failed";
        if (!out) std::cout << ": " << out.error();
        std::cout << "\n";
        return false;
    }
    for (size_t i = 0; i < in.size(); ++i) {
        if (!same_float_bits((*out)[i], pcm24_quantize(in[i]))) {
            std::cout << "selftest: odd-junk sample mismatch at " << i
                      << "\n";
            return false;
        }
    }
    return true;
}

bool test_wave_format_extensible_fixtures()
{
    {
        const std::vector<int16_t> samples = {
            -32768, -16384, 0, 16384, 32767, -1};
        std::vector<char> blob = riff_prefix();
        append_extensible_fmt(blob, 2, 44100, 16, 16, 1);
        append_fourcc(blob, "data");
        append_u32(blob, static_cast<uint32_t>(samples.size() * 2));
        for (int16_t x : samples) append_i16(blob, x);
        finalize_riff_size(blob);

        WavInfo info;
        auto out = wav_read_full(bytes_span(blob), &info);
        if (!out || info.format != WavSampleFormat::Pcm16 ||
            info.channels != 2 || info.sample_rate != 44100 ||
            info.frames != 3 || out->size() != samples.size()) {
            std::cout << "selftest: extensible PCM16 read failed";
            if (!out) std::cout << ": " << out.error();
            std::cout << "\n";
            return false;
        }
        for (size_t i = 0; i < samples.size(); ++i) {
            const float want = static_cast<float>(samples[i]) / 32768.0f;
            if (!same_float_bits((*out)[i], want)) {
                std::cout << "selftest: extensible PCM16 sample mismatch at "
                          << i << "\n";
                return false;
            }
        }
    }

    {
        const std::vector<float> samples = {-1.0f, -0.0f, 0.25f, 1.5f};
        std::vector<char> blob = riff_prefix();
        append_extensible_fmt(blob, 2, 48000, 32, 32, 3);
        append_fourcc(blob, "data");
        append_u32(blob, static_cast<uint32_t>(samples.size() * 4));
        for (float x : samples) append_float32(blob, x);
        finalize_riff_size(blob);

        WavInfo info;
        auto out = wav_read_full(bytes_span(blob), &info);
        if (!out || info.format != WavSampleFormat::Float32 ||
            info.channels != 2 || info.sample_rate != 48000 ||
            info.frames != 2 || out->size() != samples.size()) {
            std::cout << "selftest: extensible Float32 read failed";
            if (!out) std::cout << ": " << out.error();
            std::cout << "\n";
            return false;
        }
        for (size_t i = 0; i < samples.size(); ++i) {
            if (!same_float_bits((*out)[i], samples[i])) {
                std::cout << "selftest: extensible Float32 sample mismatch at "
                          << i << "\n";
                return false;
            }
        }
    }

    {
        std::vector<char> blob = riff_prefix();
        append_extensible_fmt(blob, 1, 44100, 16, 16, 1);
        const size_t fmt_pos = find_chunk(blob, "fmt ");
        if (fmt_pos == blob.size()) {
            std::cout << "selftest: extensible negative fmt not found\n";
            return false;
        }
        blob[fmt_pos + 8 + 24 + 4] ^= 0x01;
        append_fourcc(blob, "data");
        append_u32(blob, 0);
        finalize_riff_size(blob);

        auto out = wav_read_full(bytes_span(blob));
        if (out ||
            out.error() != "unsupported WAVE_FORMAT_EXTENSIBLE subformat") {
            std::cout << "selftest: extensible subformat failure mismatch";
            if (!out) std::cout << ": " << out.error();
            std::cout << "\n";
            return false;
        }
    }

    {
        std::vector<char> blob = riff_prefix();
        append_extensible_fmt(blob, 1, 44100, 16, 16, 1);
        const size_t fmt_pos = find_chunk(blob, "fmt ");
        if (fmt_pos == blob.size()) {
            std::cout << "selftest: extensible cbSize fmt not found\n";
            return false;
        }
        blob[fmt_pos + 8 + 16] = 20;
        blob[fmt_pos + 8 + 17] = 0;
        append_fourcc(blob, "data");
        append_u32(blob, 0);
        finalize_riff_size(blob);

        auto out = wav_read_full(bytes_span(blob));
        if (out || out.error() != "WAVE_FORMAT_EXTENSIBLE cbSize is too short") {
            std::cout << "selftest: extensible cbSize failure mismatch";
            if (!out) std::cout << ": " << out.error();
            std::cout << "\n";
            return false;
        }
    }

    return true;
}

bool test_streamed_unpatched_data_size_fixture()
{
    constexpr int frames = 3;
    const std::vector<int16_t> samples = {
        -32768, -8192, 0, 8192, 16384, 32767};
    std::vector<char> blob = riff_prefix();
    append_pcm16_fmt(blob, 2, 44100);
    append_fourcc(blob, "data");
    append_u32(blob, 0xffffffffu);
    for (int16_t x : samples) append_i16(blob, x);
    // The unpatched-size tolerance only fires when the declared size exceeds
    // the bytes present; a data size left at zero currently parses as zero
    // frames, and whether to trust those bytes is still an architect question.
    blob.push_back(static_cast<char>(0x7f));
    // A streamed encoder that leaves the data size at the placeholder leaves
    // the RIFF size there too; model the real container so the placeholder
    // sibling in the RIFF-size guard is exercised.
    patch_u32(blob, 4, 0xffffffffu);

    WavInfo info;
    auto out = wav_read_full(bytes_span(blob), &info);
    if (!out || info.format != WavSampleFormat::Pcm16 || info.channels != 2 ||
        info.sample_rate != 44100 || info.frames != frames ||
        out->size() != samples.size()) {
        std::cout << "selftest: streamed unpatched data-size read failed";
        if (!out) std::cout << ": " << out.error();
        std::cout << "\n";
        return false;
    }
    for (size_t i = 0; i < samples.size(); ++i) {
        const float want = static_cast<float>(samples[i]) / 32768.0f;
        if (!same_float_bits((*out)[i], want)) {
            std::cout << "selftest: streamed unpatched sample mismatch at "
                      << i << "\n";
            return false;
        }
    }
    return true;
}

bool test_unknown_magic_probe_rejection()
{
    const std::string path = temp_path("unknown_magic");
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        std::cout << "selftest: unknown-magic fixture create failed\n";
        return false;
    }
    const char blob[8] = {'N', 'O', 'P', 'E', 'd', 'a', 't', 'a'};
    const bool wrote = std::fwrite(blob, 1, sizeof(blob), f) == sizeof(blob);
    std::fclose(f);
    if (!wrote) {
        std::remove(path.c_str());
        std::cout << "selftest: unknown-magic fixture write failed\n";
        return false;
    }
    auto probed = audio_probe(path);
    std::remove(path.c_str());
    if (probed || probed.error() != "unknown audio file magic") {
        std::cout << "selftest: unknown magic probe mismatch";
        if (!probed) std::cout << ": " << probed.error();
        std::cout << "\n";
        return false;
    }
    return true;
}

bool test_allocation_and_riff_projection_boundaries()
{
    auto count = checked_audio_sample_count(0, 2);
    if (!count || *count != 0) {
        std::cout << "selftest: zero-frame allocation boundary failed\n";
        return false;
    }
    if (checked_audio_sample_count(-1, 2) ||
        checked_audio_sample_count(1, 0) ||
        checked_audio_sample_count(1, -1)) {
        std::cout << "selftest: invalid audio shape boundary failed\n";
        return false;
    }
    if (checked_audio_sample_count(std::numeric_limits<int64_t>::max(), 2)) {
        std::cout << "selftest: allocation multiplication guard failed\n";
        return false;
    }

    constexpr int64_t byte_cap_frames = 1ll << 30;
    count = checked_audio_sample_count(byte_cap_frames, 2);
    if (!count || *count != static_cast<size_t>(byte_cap_frames * 2)) {
        std::cout << "selftest: allocation byte-cap boundary failed\n";
        return false;
    }
    auto over_cap = checked_audio_sample_count(byte_cap_frames + 1, 2);
    if (over_cap ||
        over_cap.error().find("implausibly large audio allocation") ==
            std::string::npos) {
        std::cout << "selftest: implausible allocation boundary failed\n";
        return false;
    }

    if (wav_projected_exceeds_riff_limits(WavSampleFormat::Pcm24, 2, 16) ||
        wav_projected_exceeds_riff_limits(WavSampleFormat::Float32, 2, 16)) {
        std::cout << "selftest: small RIFF projection boundary failed\n";
        return false;
    }
    constexpr uint64_t u32max = std::numeric_limits<uint32_t>::max();
    if (!wav_projected_exceeds_riff_limits(WavSampleFormat::Pcm24, 2, u32max) ||
        !wav_projected_exceeds_riff_limits(WavSampleFormat::Float32, 2, u32max) ||
        !wav_projected_exceeds_riff_limits(
            WavSampleFormat::Float32, 2,
            std::numeric_limits<uint64_t>::max())) {
        std::cout << "selftest: large RIFF projection boundary failed\n";
        return false;
    }

    const std::vector<float> samples = {
        -0.25f, 0.125f, 0.5f, -0.5f, 0.75f, -0.75f};
    for (WavSampleFormat fmt : {WavSampleFormat::Float32,
                                WavSampleFormat::Pcm24}) {
        const char* stem = fmt == WavSampleFormat::Float32
                               ? "projection_float32"
                               : "projection_pcm24";
        const std::string path = temp_path(stem);
        auto writer = WavWriter::open_file(path, fmt, 2, 48000);
        if (!writer) {
            std::cout << "selftest: projection writer open failed: "
                      << writer.error() << "\n";
            return false;
        }
        auto ok = writer->write_frames(samples.data(), 3);
        if (!ok) {
            std::cout << "selftest: projection write failed: " << ok.error()
                      << "\n";
            std::remove(path.c_str());
            return false;
        }
        ok = writer->close();
        if (!ok) {
            std::cout << "selftest: projection close failed: " << ok.error()
                      << "\n";
            std::remove(path.c_str());
            return false;
        }
        const uint64_t size = std::filesystem::file_size(path);
        std::remove(path.c_str());
        const uint64_t header_span =
            fmt == WavSampleFormat::Float32 ? 56 : 44;
        const uint64_t bytes_per_sample =
            fmt == WavSampleFormat::Float32 ? 4 : 3;
        const uint64_t want = header_span + 3 * 2 * bytes_per_sample;
        if (size != want) {
            std::cout << "selftest: RIFF projection span mismatch\n";
            return false;
        }
    }
    return true;
}

int run_selftest()
{
    if (!test_riff_limit_predicate() || !test_data_before_fmt_parses() ||
        !test_data_before_fmt_overrun_fails_cleanly() ||
        !test_odd_junk_chunk_before_data() ||
        !test_unknown_magic_probe_rejection()) {
        return 1;
    }
    std::cout << "selftest: WAV parser hardening fixtures passed\n";

    if (!test_wave_format_extensible_fixtures()) {
        return 1;
    }
    std::cout << "selftest: WAVE_FORMAT_EXTENSIBLE fixtures passed\n";

    if (!test_streamed_unpatched_data_size_fixture()) {
        return 1;
    }
    std::cout << "selftest: streamed-size fixture passed\n";

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

    if (!test_allocation_and_riff_projection_boundaries()) {
        return 1;
    }
    std::cout << "selftest: allocation and RIFF projection boundaries passed\n";

    if (!run_source_sample_cache_selftest()) {
        return 1;
    }
    std::cout << "selftest: .samples container and identity fixtures passed\n";

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
