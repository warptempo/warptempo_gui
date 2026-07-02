#include "audio_probe.h"
#include "flac_io.h"
#include "pcm24.h"
#include "wav_io.h"

#include <sndfile.h>
#include <unistd.h>

#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <expected>
#include <iomanip>
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

std::expected<std::vector<float>, std::string>
read_with_ours(const std::string& path, AudioFileInfo* info_out)
{
    auto probe = audio_probe(path);
    if (!probe) return std::unexpected(probe.error());
    *info_out = *probe;
    if (probe->kind == AudioFileKind::Flac) {
        FlacInfo fi;
        auto data = flac_read_full(path, &fi);
        if (!data) return std::unexpected(data.error());
        info_out->sample_rate = fi.sample_rate;
        info_out->channels = fi.channels;
        info_out->frames = fi.frames;
        return data;
    }
    WavInfo wi;
    auto data = wav_read_full(path, &wi);
    if (!data) return std::unexpected(data.error());
    info_out->sample_rate = wi.sample_rate;
    info_out->channels = wi.channels;
    info_out->frames = wi.frames;
    return data;
}

std::expected<std::vector<float>, std::string>
read_with_sndfile(const std::string& path, AudioFileInfo* info_out)
{
    SF_INFO sfinfo{};
    SNDFILE* sf = sf_open(path.c_str(), SFM_READ, &sfinfo);
    if (!sf) return std::unexpected(sf_strerror(nullptr));
    info_out->sample_rate = sfinfo.samplerate;
    info_out->channels = sfinfo.channels;
    info_out->frames = sfinfo.frames;
    std::vector<float> out(static_cast<size_t>(sfinfo.frames) *
                           static_cast<size_t>(sfinfo.channels));
    const sf_count_t got = sf_readf_float(sf, out.data(), sfinfo.frames);
    sf_close(sf);
    if (got != sfinfo.frames) {
        return std::unexpected("libsndfile returned a short read");
    }
    return out;
}

int run_compare_decode(int argc, char** argv)
{
    if (argc < 3) {
        std::cout << "compare-decode: expected one or more paths\n";
        return 1;
    }
    bool failed = false;
    for (int i = 2; i < argc; ++i) {
        const std::string path = argv[i];
        AudioFileInfo sfmeta{};
        AudioFileInfo ourmeta{};
        auto sfdata = read_with_sndfile(path, &sfmeta);
        auto ourdata = read_with_ours(path, &ourmeta);
        if (!sfdata) {
            std::cout << path << ": libsndfile error: " << sfdata.error()
                      << "\n";
            failed = true;
            continue;
        }
        if (!ourdata) {
            std::cout << path << ": our decoder error: " << ourdata.error()
                      << "\n";
            failed = true;
            continue;
        }
        if (sfmeta.sample_rate != ourmeta.sample_rate ||
            sfmeta.channels != ourmeta.channels ||
            sfmeta.frames != ourmeta.frames) {
            std::cout << path << ": metadata mismatch libsndfile sr="
                      << sfmeta.sample_rate << " ch=" << sfmeta.channels
                      << " frames=" << sfmeta.frames << " ours sr="
                      << ourmeta.sample_rate << " ch=" << ourmeta.channels
                      << " frames=" << ourmeta.frames << "\n";
            failed = true;
            continue;
        }
        size_t diff = sfdata->size();
        for (size_t j = 0; j < sfdata->size(); ++j) {
            if (!same_float_bits((*sfdata)[j], (*ourdata)[j])) {
                diff = j;
                break;
            }
        }
        if (diff == sfdata->size()) {
            std::cout << path << ": identical\n";
        } else {
            std::cout << path << ": first divergent sample " << diff
                      << " libsndfile=" << std::setprecision(9)
                      << (*sfdata)[diff] << " ours=" << (*ourdata)[diff]
                      << "\n";
            failed = true;
        }
    }
    return failed ? 1 : 0;
}

struct SfMemory {
    std::vector<char> data;
    sf_count_t pos = 0;
};

sf_count_t vio_get_filelen(void* user_data)
{
    auto* m = static_cast<SfMemory*>(user_data);
    return static_cast<sf_count_t>(m->data.size());
}

sf_count_t vio_seek(sf_count_t offset, int whence, void* user_data)
{
    auto* m = static_cast<SfMemory*>(user_data);
    sf_count_t base = 0;
    if (whence == SEEK_SET) base = 0;
    else if (whence == SEEK_CUR) base = m->pos;
    else if (whence == SEEK_END) base = static_cast<sf_count_t>(m->data.size());
    else return -1;
    const sf_count_t next = base + offset;
    if (next < 0) return -1;
    m->pos = next;
    return m->pos;
}

sf_count_t vio_read(void* ptr, sf_count_t count, void* user_data)
{
    auto* m = static_cast<SfMemory*>(user_data);
    if (m->pos >= static_cast<sf_count_t>(m->data.size())) return 0;
    const sf_count_t avail =
        static_cast<sf_count_t>(m->data.size()) - m->pos;
    const sf_count_t n = std::min(count, avail);
    std::memcpy(ptr, m->data.data() + m->pos, static_cast<size_t>(n));
    m->pos += n;
    return n;
}

sf_count_t vio_write(const void* ptr, sf_count_t count, void* user_data)
{
    auto* m = static_cast<SfMemory*>(user_data);
    const sf_count_t end = m->pos + count;
    if (end < m->pos) return 0;
    if (end > static_cast<sf_count_t>(m->data.size())) {
        m->data.resize(static_cast<size_t>(end));
    }
    std::memcpy(m->data.data() + m->pos, ptr, static_cast<size_t>(count));
    m->pos = end;
    return count;
}

sf_count_t vio_tell(void* user_data)
{
    auto* m = static_cast<SfMemory*>(user_data);
    return m->pos;
}

std::expected<std::vector<char>, std::string>
encode_ours_pcm24(const std::vector<float>& samples)
{
    std::vector<char> blob;
    auto writer = WavWriter::open_memory(blob, WavSampleFormat::Pcm24, 1,
                                         48000);
    if (!writer) return std::unexpected(writer.error());
    auto ok = writer->write_frames(samples.data(),
                                   static_cast<int64_t>(samples.size()));
    if (!ok) return std::unexpected(ok.error());
    ok = writer->close();
    if (!ok) return std::unexpected(ok.error());
    return blob;
}

std::expected<std::vector<char>, std::string>
encode_sndfile_pcm24(const std::vector<float>& samples)
{
    SF_VIRTUAL_IO vio{};
    vio.get_filelen = vio_get_filelen;
    vio.seek = vio_seek;
    vio.read = vio_read;
    vio.write = vio_write;
    vio.tell = vio_tell;
    SfMemory mem;
    SF_INFO info{};
    info.samplerate = 48000;
    info.channels = 1;
    info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_24;
    SNDFILE* sf = sf_open_virtual(&vio, SFM_WRITE, &info, &mem);
    if (!sf) return std::unexpected(sf_strerror(nullptr));
    const sf_count_t wrote =
        sf_writef_float(sf, samples.data(),
                        static_cast<sf_count_t>(samples.size()));
    const int close_rc = sf_close(sf);
    if (wrote != static_cast<sf_count_t>(samples.size()) || close_rc != 0) {
        return std::unexpected("libsndfile encode failed");
    }
    return mem.data;
}

std::expected<std::pair<size_t, size_t>, std::string>
find_data_chunk(const std::vector<char>& blob)
{
    if (blob.size() < 12 || std::memcmp(blob.data(), "RIFF", 4) != 0 ||
        std::memcmp(blob.data() + 8, "WAVE", 4) != 0) {
        return std::unexpected("not a RIFF/WAVE blob");
    }
    size_t pos = 12;
    while (pos + 8 <= blob.size()) {
        const char* id = blob.data() + pos;
        const unsigned char* sz =
            reinterpret_cast<const unsigned char*>(blob.data() + pos + 4);
        const uint32_t n = static_cast<uint32_t>(sz[0]) |
                           (static_cast<uint32_t>(sz[1]) << 8) |
                           (static_cast<uint32_t>(sz[2]) << 16) |
                           (static_cast<uint32_t>(sz[3]) << 24);
        const size_t payload = pos + 8;
        if (payload + n > blob.size()) {
            return std::unexpected("chunk overruns blob");
        }
        if (std::memcmp(id, "data", 4) == 0) return {{payload, n}};
        pos = payload + n + (n & 1u);
    }
    return std::unexpected("data chunk not found");
}

int pcm24_code_at(const std::vector<char>& blob, size_t data_offset,
                  size_t frame)
{
    const unsigned char* p =
        reinterpret_cast<const unsigned char*>(blob.data() + data_offset +
                                               frame * 3);
    int32_t v = static_cast<int32_t>(p[0]) |
                (static_cast<int32_t>(p[1]) << 8) |
                (static_cast<int32_t>(p[2]) << 16);
    if (v & 0x00800000) v |= static_cast<int32_t>(0xff000000);
    return v;
}

bool compare_blobs(const std::string& label, const std::vector<char>& ours,
                   const std::vector<char>& snd)
{
    const size_t n = std::min(ours.size(), snd.size());
    size_t diff = n;
    for (size_t i = 0; i < n; ++i) {
        if (ours[i] != snd[i]) {
            diff = i;
            break;
        }
    }
    if (diff == n && ours.size() == snd.size()) {
        std::cout << "compare-encode: " << label << ": byte-identical\n";
        return true;
    }
    if (diff == n) diff = n;
    auto our_data = find_data_chunk(ours);
    auto sf_data = find_data_chunk(snd);
    const bool payload =
        our_data && sf_data && diff >= our_data->first &&
        diff < our_data->first + our_data->second && diff >= sf_data->first &&
        diff < sf_data->first + sf_data->second;
    std::cout << "compare-encode: " << label << ": first differing offset "
              << diff << " (" << (payload ? "sample payload" : "header")
              << "), ours_size=" << ours.size()
              << " libsndfile_size=" << snd.size();
    if (payload) {
        const size_t frame = (diff - our_data->first) / 3;
        std::cout << ", frame=" << frame
                  << " ours_code=" << pcm24_code_at(ours, our_data->first, frame)
                  << " libsndfile_code="
                  << pcm24_code_at(snd, sf_data->first, frame);
    }
    std::cout << "\n";
    return false;
}

int compare_sequence(const std::string& label, const std::vector<float>& data)
{
    auto ours = encode_ours_pcm24(data);
    auto snd = encode_sndfile_pcm24(data);
    if (!ours) {
        std::cout << "compare-encode: " << label
                  << ": our encode failed: " << ours.error() << "\n";
        return 1;
    }
    if (!snd) {
        std::cout << "compare-encode: " << label
                  << ": libsndfile encode failed: " << snd.error() << "\n";
        return 1;
    }
    return compare_blobs(label, *ours, *snd) ? 0 : 1;
}

int run_compare_encode()
{
    std::vector<float> lattice;
    lattice.reserve(1u << 24);
    for (int32_t c = -8388608;; ++c) {
        lattice.push_back(pcm24_float_from_code(c));
        if (c == 8388607) break;
    }
    int failures = compare_sequence("full-lattice", lattice);

    std::vector<float> stress(1u << 20);
    std::mt19937 rng(0x9e3779b9u);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (float& x : stress) x = dist(rng);
    failures += compare_sequence("stress", stress);
    return failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cout << "usage: warptempo_audio_io_test "
                     "selftest|compare-decode|compare-encode [paths...]\n";
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
    if (mode == "compare-decode") return run_compare_decode(argc, argv);
    if (mode == "compare-encode") {
        if (argc != 2) {
            std::cout << "compare-encode: unexpected arguments\n";
            return 1;
        }
        return run_compare_encode();
    }
    std::cout << "unknown mode: " << mode << "\n";
    return 1;
}
