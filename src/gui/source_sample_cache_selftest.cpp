#include "source_sample_cache.h"

#include "audio_probe.h"
#include "wav_io.h"

#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

constexpr int kSampleRate = 44100;
constexpr int kChannels = 2;
constexpr int64_t kFrames = 4;

std::string temp_path(const char* stem)
{
    return "/tmp/warptempo_source_sample_cache_test_" +
           std::to_string(getpid()) + "_" + stem + ".wav";
}

std::string cache_path_for(const std::string& source_path)
{
    std::filesystem::path p(source_path);
    p.replace_extension(".samples");
    return p.string();
}

void remove_pair(const std::string& source_path)
{
    std::error_code ec;
    std::filesystem::remove(source_path, ec);
    std::filesystem::remove(cache_path_for(source_path), ec);
}

std::vector<float> standard_payload()
{
    return {-0.75f, 0.125f, 0.5f, -0.25f,
            0.875f, -0.625f, 0.25f, -0.5f};
}

bool write_wav_fixture(const std::string& path)
{
    auto writer = WavWriter::open_file(path, WavSampleFormat::Float32,
                                       kChannels, kSampleRate);
    if (!writer) return false;
    const std::vector<float> payload = standard_payload();
    if (!writer->write_frames(payload.data(), kFrames)) return false;
    return static_cast<bool>(writer->close());
}

// No surviving source kind is admitted to the sample cache (the cache existed
// to skip the FLAC decode on reload, and FLAC support is removed), so both
// public entry points must stay inert for a WAV source: the ensure call skips
// silently without creating a file, and the read call misses.
bool test_wav_source_never_caches()
{
    const std::string path = temp_path("wav_never_caches");
    remove_pair(path);
    bool ok = false;
    do {
        if (!write_wav_fixture(path)) break;
        auto probed = audio_probe(path);
        if (!probed) break;
        const std::vector<float> payload = standard_payload();
        if (!ensure_source_sample_cache_from_buffer(path, *probed,
                                                    payload.data(), kFrames,
                                                    kChannels)) {
            break;
        }
        if (std::filesystem::exists(cache_path_for(path))) break;
        std::vector<float> out;
        ok = !read_full_source_from_source_sample_cache(path, *probed, out);
    } while (false);
    remove_pair(path);
    if (!ok) std::printf("selftest: .samples WAV never-cache invariant failed\n");
    return ok;
}

bool test_is_source_sample_cache_path()
{
    const bool ok = is_source_sample_cache_path("/tmp/a.samples") &&
                    is_source_sample_cache_path("/tmp/a.SAMPLES") &&
                    !is_source_sample_cache_path("/tmp/a.wav") &&
                    !is_source_sample_cache_path("/tmp/a.peaks") &&
                    !is_source_sample_cache_path("/tmp/a");
    if (!ok) std::printf("selftest: .samples path predicate failed\n");
    return ok;
}

} // namespace

bool run_source_sample_cache_selftest()
{
    return test_wav_source_never_caches() &&
           test_is_source_sample_cache_path();
}
