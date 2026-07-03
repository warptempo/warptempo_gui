#include "source_sample_cache.h"

#include "audio_probe.h"

#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {

constexpr int kSampleRate = 44100;
constexpr int kChannels = 2;
constexpr int kBitsPerSample = 16;
constexpr int64_t kFrames = 4;
constexpr char kCacheMagic[] = "WARPTEMPO_SOURCE_SAMPLE_CACHE";

std::string temp_path(const char* stem)
{
    return "/tmp/warptempo_source_sample_cache_test_" +
           std::to_string(getpid()) + "_" + stem + ".flac";
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

std::vector<unsigned char> standard_md5()
{
    return {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0,
            0x0f, 0xed, 0xcb, 0xa9, 0x87, 0x65, 0x43, 0x21};
}

std::vector<unsigned char> alternate_md5()
{
    return {0x21, 0x43, 0x65, 0x87, 0xa9, 0xcb, 0xed, 0x0f,
            0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12};
}

std::vector<float> standard_payload()
{
    return {-0.75f, 0.125f, 0.5f, -0.25f,
            0.875f, -0.625f, 0.25f, -0.5f};
}

std::vector<unsigned char> build_flac_fixture(int sample_rate, int channels,
                                              int bits_per_sample,
                                              int64_t frames,
                                              const std::vector<unsigned char>& md5)
{
    std::vector<unsigned char> bytes(42, 0);
    bytes[0] = 'f';
    bytes[1] = 'L';
    bytes[2] = 'a';
    bytes[3] = 'C';
    bytes[4] = 0x80;
    bytes[5] = 0;
    bytes[6] = 0;
    bytes[7] = 34;

    unsigned char* s = bytes.data() + 8;
    s[0] = 0x10;
    s[1] = 0x00;
    s[2] = 0x10;
    s[3] = 0x00;
    s[10] = static_cast<unsigned char>((sample_rate >> 12) & 0xff);
    s[11] = static_cast<unsigned char>((sample_rate >> 4) & 0xff);
    s[12] = static_cast<unsigned char>(
        ((sample_rate & 0xf) << 4) |
        (((channels - 1) & 0x7) << 1) |
        (((bits_per_sample - 1) >> 4) & 0x1));
    s[13] = static_cast<unsigned char>(
        (((bits_per_sample - 1) & 0xf) << 4) |
        ((static_cast<uint64_t>(frames) >> 32) & 0xf));
    s[14] = static_cast<unsigned char>((static_cast<uint64_t>(frames) >> 24) & 0xff);
    s[15] = static_cast<unsigned char>((static_cast<uint64_t>(frames) >> 16) & 0xff);
    s[16] = static_cast<unsigned char>((static_cast<uint64_t>(frames) >> 8) & 0xff);
    s[17] = static_cast<unsigned char>(static_cast<uint64_t>(frames) & 0xff);
    std::memcpy(s + 18, md5.data(), 16);
    return bytes;
}

bool write_bytes(const std::string& path, const std::vector<unsigned char>& bytes)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    return std::fclose(f) == 0 && ok;
}

bool read_bytes(const std::string& path, std::vector<unsigned char>& bytes)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    if (std::fseek(f, 0, SEEK_END) != 0) {
        std::fclose(f);
        return false;
    }
    const long n = std::ftell(f);
    if (n < 0 || std::fseek(f, 0, SEEK_SET) != 0) {
        std::fclose(f);
        return false;
    }
    bytes.assign(static_cast<size_t>(n), 0);
    const bool ok = bytes.empty() ||
                    std::fread(bytes.data(), 1, bytes.size(), f) == bytes.size();
    return std::fclose(f) == 0 && ok;
}

void patch_u32_le(std::vector<unsigned char>& bytes, size_t offset, uint32_t value)
{
    bytes[offset + 0] = static_cast<unsigned char>(value & 0xff);
    bytes[offset + 1] = static_cast<unsigned char>((value >> 8) & 0xff);
    bytes[offset + 2] = static_cast<unsigned char>((value >> 16) & 0xff);
    bytes[offset + 3] = static_cast<unsigned char>((value >> 24) & 0xff);
}

bool write_fixture(const std::string& path,
                   const std::vector<unsigned char>& md5 = standard_md5(),
                   int channels = kChannels)
{
    return write_bytes(path, build_flac_fixture(kSampleRate, channels,
                                                kBitsPerSample, kFrames, md5));
}

bool probe_fixture(const std::string& path, AudioFileInfo& info)
{
    auto probed = audio_probe(path);
    if (!probed) return false;
    info = *probed;
    return true;
}

bool float_payload_equal(const std::vector<float>& a, const std::vector<float>& b)
{
    return a.size() == b.size() &&
           (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0);
}

bool create_cache(const std::string& path, AudioFileInfo& info)
{
    if (!write_fixture(path) || !probe_fixture(path, info)) return false;
    const std::vector<float> payload = standard_payload();
    return ensure_source_sample_cache_from_buffer(path, info, payload.data(),
                                                  kFrames, kChannels);
}

void bump_mtime(const std::string& path)
{
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(path, ec);
    if (!ec) {
        std::filesystem::last_write_time(
            path, t + std::chrono::seconds(11), ec);
    }
}

bool test_fixture_self_validation()
{
    const std::string path = temp_path("fixture_self_validation");
    remove_pair(path);
    bool ok = false;
    do {
        if (!write_fixture(path)) break;
        AudioFileInfo info;
        if (!probe_fixture(path, info)) break;
        if (info.sample_rate != kSampleRate || info.channels != kChannels ||
            info.frames != kFrames || info.kind != AudioFileKind::Flac ||
            !info.has_content_md5 ||
            std::memcmp(info.content_md5, standard_md5().data(), 16) != 0) {
            break;
        }
        ok = true;
    } while (false);
    remove_pair(path);
    if (!ok) std::printf("selftest: FLAC header fixture self-validation failed\n");
    return ok;
}

bool test_write_and_read_back()
{
    const std::string path = temp_path("write_read_back");
    remove_pair(path);
    bool ok = false;
    do {
        AudioFileInfo info;
        if (!create_cache(path, info) || !std::filesystem::exists(cache_path_for(path)))
            break;
        std::vector<float> out;
        if (!read_full_source_from_source_sample_cache(path, info, out)) break;
        ok = float_payload_equal(out, standard_payload());
    } while (false);
    remove_pair(path);
    if (!ok) std::printf("selftest: .samples write/read-back failed\n");
    return ok;
}

bool test_range_hits()
{
    const std::string path = temp_path("range_hits");
    remove_pair(path);
    bool ok = false;
    do {
        AudioFileInfo info;
        if (!create_cache(path, info)) break;
        std::vector<float> out;
        int sample_rate = 0;
        int channels = 0;
        auto full = load_source_range_with_source_sample_cache(
            path, info, 0, 4, out, sample_rate, channels);
        if (!full || full->cache_status != SourceSampleCacheStatus::Hit ||
            !full->used_cache || sample_rate != kSampleRate ||
            channels != kChannels || !float_payload_equal(out, standard_payload())) {
            break;
        }
        auto part = load_source_range_with_source_sample_cache(
            path, info, 1, 3, out, sample_rate, channels);
        const std::vector<float> want = {0.5f, -0.25f, 0.875f, -0.625f};
        if (!part || part->cache_status != SourceSampleCacheStatus::Hit ||
            !part->used_cache || sample_rate != kSampleRate ||
            channels != kChannels || !float_payload_equal(out, want)) {
            break;
        }
        ok = true;
    } while (false);
    remove_pair(path);
    if (!ok) std::printf("selftest: .samples range hit fixtures failed\n");
    return ok;
}

bool test_md5_mismatch_preserved_size_mtime()
{
    const std::string path = temp_path("md5_preserved_mtime");
    remove_pair(path);
    bool ok = false;
    do {
        AudioFileInfo info;
        if (!create_cache(path, info)) break;
        std::error_code ec;
        const auto saved_time = std::filesystem::last_write_time(path, ec);
        if (ec) break;
        if (!write_fixture(path, alternate_md5())) break;
        std::filesystem::last_write_time(path, saved_time, ec);
        if (ec) break;
        AudioFileInfo changed;
        if (!probe_fixture(path, changed)) break;
        std::vector<float> out;
        ok = !read_full_source_from_source_sample_cache(path, changed, out);
    } while (false);
    remove_pair(path);
    if (!ok) std::printf("selftest: .samples MD5 identity mismatch failed\n");
    return ok;
}

bool test_zero_md5_metadata_fallback()
{
    const std::string path = temp_path("zero_md5_fallback");
    remove_pair(path);
    bool ok = false;
    do {
        std::vector<unsigned char> zero_md5(16, 0);
        if (!write_fixture(path, zero_md5)) break;
        AudioFileInfo info;
        if (!probe_fixture(path, info) || info.has_content_md5) break;
        const std::vector<float> payload = standard_payload();
        if (!ensure_source_sample_cache_from_buffer(path, info, payload.data(),
                                                    kFrames, kChannels)) {
            break;
        }
        std::vector<float> out;
        if (!read_full_source_from_source_sample_cache(path, info, out) ||
            !float_payload_equal(out, payload)) {
            break;
        }
        bump_mtime(path);
        AudioFileInfo changed;
        if (!probe_fixture(path, changed)) break;
        ok = !read_full_source_from_source_sample_cache(path, changed, out);
    } while (false);
    remove_pair(path);
    if (!ok) std::printf("selftest: .samples zero-MD5 metadata fallback failed\n");
    return ok;
}

bool test_nonzero_md5_staleness()
{
    const std::string path = temp_path("nonzero_md5_stale");
    remove_pair(path);
    bool ok = false;
    do {
        AudioFileInfo info;
        if (!create_cache(path, info)) break;
        bump_mtime(path);
        AudioFileInfo changed;
        if (!probe_fixture(path, changed)) break;
        std::vector<float> out;
        ok = !read_full_source_from_source_sample_cache(path, changed, out);
    } while (false);
    remove_pair(path);
    if (!ok) std::printf("selftest: .samples nonzero-MD5 staleness failed\n");
    return ok;
}

bool test_ensure_reprobe_skip()
{
    const std::string path = temp_path("ensure_reprobe_skip");
    remove_pair(path);
    bool ok = false;
    do {
        if (!write_fixture(path)) break;
        AudioFileInfo stale;
        if (!probe_fixture(path, stale)) break;
        if (!write_fixture(path, standard_md5(), 1)) break;
        const std::vector<float> payload = standard_payload();
        if (ensure_source_sample_cache_from_buffer(path, stale, payload.data(),
                                                   kFrames, kChannels)) {
            break;
        }
        ok = !std::filesystem::exists(cache_path_for(path));
    } while (false);
    remove_pair(path);
    if (!ok) std::printf("selftest: .samples ensure re-probe skip failed\n");
    return ok;
}

bool test_corrupt_oversized_string()
{
    const std::string path = temp_path("corrupt_oversized_string");
    remove_pair(path);
    bool ok = false;
    do {
        AudioFileInfo info;
        if (!create_cache(path, info)) break;
        std::vector<unsigned char> bytes;
        const std::string cache_path = cache_path_for(path);
        if (!read_bytes(cache_path, bytes)) break;
        const size_t basename_len_offset = std::strlen(kCacheMagic) + 1 + sizeof(uint32_t);
        if (bytes.size() < basename_len_offset + sizeof(uint32_t)) break;
        patch_u32_le(bytes, basename_len_offset, 0xffffffffu);
        if (!write_bytes(cache_path, bytes)) break;
        std::vector<float> out;
        const auto source_path = source_path_for_source_sample_cache(cache_path);
        ok = !read_full_source_from_source_sample_cache(path, info, out) &&
             !source_path.has_value();
    } while (false);
    remove_pair(path);
    if (!ok) std::printf("selftest: .samples oversized-string rejection failed\n");
    return ok;
}

bool test_corrupt_truncated_payload()
{
    const std::string path = temp_path("corrupt_truncated_payload");
    remove_pair(path);
    bool ok = false;
    do {
        AudioFileInfo info;
        if (!create_cache(path, info)) break;
        std::vector<unsigned char> bytes;
        const std::string cache_path = cache_path_for(path);
        if (!read_bytes(cache_path, bytes) || bytes.empty()) break;
        bytes.pop_back();
        if (!write_bytes(cache_path, bytes)) break;
        std::vector<float> out;
        ok = !read_full_source_from_source_sample_cache(path, info, out);
    } while (false);
    remove_pair(path);
    if (!ok) std::printf("selftest: .samples truncated-payload rejection failed\n");
    return ok;
}

bool test_corrupt_wrong_version()
{
    const std::string path = temp_path("corrupt_wrong_version");
    remove_pair(path);
    bool ok = false;
    do {
        AudioFileInfo info;
        if (!create_cache(path, info)) break;
        std::vector<unsigned char> bytes;
        const std::string cache_path = cache_path_for(path);
        if (!read_bytes(cache_path, bytes)) break;
        const size_t version_offset = std::strlen(kCacheMagic) + 1;
        if (bytes.size() < version_offset + sizeof(uint32_t)) break;
        patch_u32_le(bytes, version_offset, 0x7fffffffu);
        if (!write_bytes(cache_path, bytes)) break;
        std::vector<float> out;
        ok = !read_full_source_from_source_sample_cache(path, info, out);
    } while (false);
    remove_pair(path);
    if (!ok) std::printf("selftest: .samples wrong-version rejection failed\n");
    return ok;
}

bool test_source_path_for_cache()
{
    const std::string path = temp_path("source_path_for_cache");
    remove_pair(path);
    bool ok = false;
    do {
        AudioFileInfo info;
        if (!create_cache(path, info)) break;
        const std::string cache_path = cache_path_for(path);
        auto source_path = source_path_for_source_sample_cache(cache_path);
        if (!source_path || *source_path != path) break;
        std::error_code ec;
        std::filesystem::remove(path, ec);
        source_path = source_path_for_source_sample_cache(cache_path);
        ok = !source_path.has_value();
    } while (false);
    remove_pair(path);
    if (!ok) std::printf("selftest: .samples source-path lookup failed\n");
    return ok;
}

bool test_is_source_sample_cache_path()
{
    const bool ok = is_source_sample_cache_path("/tmp/a.samples") &&
                    is_source_sample_cache_path("/tmp/a.SAMPLES") &&
                    !is_source_sample_cache_path("/tmp/a.wav") &&
                    !is_source_sample_cache_path("/tmp/a.flac") &&
                    !is_source_sample_cache_path("/tmp/a");
    if (!ok) std::printf("selftest: .samples path predicate failed\n");
    return ok;
}

} // namespace

bool run_source_sample_cache_selftest()
{
    // Header-only FLAC fixtures exercise pure cache identity reads. Successful
    // rebuild decode needs real FLAC frames, and intra-call identity-skip
    // windows are not externally observable from a single-call test.
    return test_fixture_self_validation() &&
           test_write_and_read_back() &&
           test_range_hits() &&
           test_md5_mismatch_preserved_size_mtime() &&
           test_zero_md5_metadata_fallback() &&
           test_nonzero_md5_staleness() &&
           test_ensure_reprobe_skip() &&
           test_corrupt_oversized_string() &&
           test_corrupt_truncated_payload() &&
           test_corrupt_wrong_version() &&
           test_source_path_for_cache() &&
           test_is_source_sample_cache_path();
}
