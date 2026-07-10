#include "source_sample_cache.h"

#include "wav_io.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <system_error>
#include <type_traits>

#include <unistd.h>

namespace {

constexpr char kMagic[] = "WARPTEMPO_SOURCE_SAMPLE_CACHE";
constexpr uint32_t kFileVersion = 2;
constexpr uint32_t kHashAlgorithmNone = 0;
constexpr uint32_t kHashAlgorithmFlacStreaminfoMd5 = 1;
// Header strings are bounded so a corrupt cache is a cheap miss rather than a
// memory-pressure event. The writer enforces the same bound as a format rule.
constexpr uint32_t kMaxHeaderStringBytes = 4096;
constexpr char kPayloadType[] = "float32_interleaved";
constexpr char kSampleCacheExtension[] = ".samples";
std::atomic<uint64_t> g_cache_tmp_counter{0};

// For FLAC sources the identity includes the STREAMINFO MD5 of the unencoded
// audio, read for free by the probe, so content replacement invalidates the
// cache even when size and mtime are preserved. WAV sources deliberately trust
// this metadata tuple alone because hashing a WAV costs the full read this
// cache exists to avoid. An all-zero FLAC MD5 means the encoder never set it
// and falls back to the same metadata-only trust.
struct SourceMetadata {
    std::string basename;
    std::string extension;
    uint64_t source_size = 0;
    int64_t mtime_ticks = 0;
    int sample_rate = 0;
    int channels = 0;
    int64_t frame_count = 0;
    int kind_code = 0;
    uint32_t hash_algorithm = kHashAlgorithmNone;
    unsigned char hash[16] = {};
};

bool put_bytes(std::FILE* f, const void* p, size_t n) {
    return n == 0 || std::fwrite(p, 1, n, f) == n;
}

bool put_u32(std::FILE* f, uint32_t x) { return std::fwrite(&x, sizeof x, 1, f) == 1; }
bool put_u64(std::FILE* f, uint64_t x) { return std::fwrite(&x, sizeof x, 1, f) == 1; }
bool put_i64(std::FILE* f, int64_t x)  { return std::fwrite(&x, sizeof x, 1, f) == 1; }
bool put_i32(std::FILE* f, int32_t x)  { return std::fwrite(&x, sizeof x, 1, f) == 1; }

bool put_str(std::FILE* f, const std::string& s) {
    if (s.size() > kMaxHeaderStringBytes) return false;
    return put_u32(f, static_cast<uint32_t>(s.size())) &&
           put_bytes(f, s.data(), s.size());
}

bool get_bytes(std::FILE* f, void* p, size_t n) {
    return n == 0 || std::fread(p, 1, n, f) == n;
}

bool get_u32(std::FILE* f, uint32_t& x) { return std::fread(&x, sizeof x, 1, f) == 1; }
bool get_u64(std::FILE* f, uint64_t& x) { return std::fread(&x, sizeof x, 1, f) == 1; }
bool get_i64(std::FILE* f, int64_t& x)  { return std::fread(&x, sizeof x, 1, f) == 1; }
bool get_i32(std::FILE* f, int32_t& x)  { return std::fread(&x, sizeof x, 1, f) == 1; }

bool get_str(std::FILE* f, std::string& s) {
    uint32_t n = 0;
    if (!get_u32(f, n)) return false;
    if (n > kMaxHeaderStringBytes) return false;
    s.assign(n, '\0');
    return n == 0 || get_bytes(f, s.data(), n);
}

std::string lowercase(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

int audio_kind_code(AudioFileKind kind) {
    switch (kind) {
        case AudioFileKind::WavPcm16:   return 1;
        case AudioFileKind::WavPcm24:   return 2;
        case AudioFileKind::WavFloat32: return 3;
        case AudioFileKind::Flac:       return 4;
    }
    return 0;
}

bool is_cacheable_source(const AudioFileInfo& source_info) {
    return source_info.kind == AudioFileKind::Flac;
}

std::filesystem::path cache_path_for_source(const std::string& source_path) {
    std::filesystem::path p(source_path);
    p.replace_extension(kSampleCacheExtension);
    return p;
}

int64_t file_time_ticks(const std::filesystem::path& p) {
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(p, ec);
    if (ec) return 0;
    const auto ticks = t.time_since_epoch().count();
    using TickCount = std::remove_cv_t<decltype(ticks)>;
    if (ticks > static_cast<TickCount>(std::numeric_limits<int64_t>::max()))
        return std::numeric_limits<int64_t>::max();
    if (ticks < static_cast<TickCount>(std::numeric_limits<int64_t>::min()))
        return std::numeric_limits<int64_t>::min();
    return static_cast<int64_t>(ticks);
}

SourceMetadata source_metadata(const std::string& source_path,
                               const AudioFileInfo& source_info) {
    const std::filesystem::path p(source_path);
    SourceMetadata m;
    m.basename = p.stem().string();
    m.extension = p.extension().string();
    std::error_code ec;
    m.source_size = std::filesystem::file_size(p, ec);
    if (ec) m.source_size = 0;
    m.mtime_ticks = file_time_ticks(p);
    m.sample_rate = source_info.sample_rate;
    m.channels = source_info.channels;
    m.frame_count = static_cast<int64_t>(source_info.frames);
    m.kind_code = audio_kind_code(source_info.kind);
    if (source_info.has_content_md5) {
        m.hash_algorithm = kHashAlgorithmFlacStreaminfoMd5;
        std::memcpy(m.hash, source_info.content_md5, sizeof(m.hash));
    }
    return m;
}

bool metadata_matches(const SourceMetadata& have, const SourceMetadata& want) {
    return have.basename == want.basename &&
           have.extension == want.extension &&
           have.source_size == want.source_size &&
           have.mtime_ticks == want.mtime_ticks &&
           have.sample_rate == want.sample_rate &&
           have.channels == want.channels &&
           have.frame_count == want.frame_count &&
           have.kind_code == want.kind_code &&
           have.hash_algorithm == want.hash_algorithm &&
           (want.hash_algorithm != kHashAlgorithmFlacStreaminfoMd5 ||
            std::memcmp(have.hash, want.hash, sizeof(want.hash)) == 0);
}

bool read_header_metadata(std::FILE* f, SourceMetadata& have,
                          std::string& payload_type,
                          int64_t& payload_frames,
                          uint64_t& payload_bytes,
                          long& payload_offset) {
    char magic[sizeof(kMagic)]{};
    if (!get_bytes(f, magic, sizeof(kMagic))) return false;
    if (std::memcmp(magic, kMagic, sizeof(kMagic)) != 0) return false;

    uint32_t version = 0;
    if (!get_u32(f, version) || version != kFileVersion) return false;

    int32_t sr = 0, ch = 0, kind = 0;
    if (!get_str(f, have.basename)) return false;
    if (!get_str(f, have.extension)) return false;
    if (!get_u64(f, have.source_size)) return false;
    if (!get_i64(f, have.mtime_ticks)) return false;
    if (!get_i32(f, sr)) return false;
    if (!get_i32(f, ch)) return false;
    if (!get_i64(f, have.frame_count)) return false;
    if (!get_i32(f, kind)) return false;
    have.sample_rate = sr;
    have.channels = ch;
    have.kind_code = kind;

    if (!get_str(f, payload_type) || payload_type != kPayloadType) return false;
    if (!get_i64(f, payload_frames)) return false;
    if (!get_u64(f, payload_bytes)) return false;

    uint32_t hash_len = 0;
    if (!get_u32(f, have.hash_algorithm)) return false;
    if (!get_u32(f, hash_len)) return false;
    if (have.hash_algorithm == kHashAlgorithmNone) {
        if (hash_len != 0) return false;
    } else if (have.hash_algorithm == kHashAlgorithmFlacStreaminfoMd5) {
        if (hash_len != sizeof(have.hash)) return false;
        if (!get_bytes(f, have.hash, sizeof(have.hash))) return false;
    } else {
        return false;
    }

    payload_offset = std::ftell(f);
    return payload_offset >= 0;
}

bool read_header(std::FILE* f, const SourceMetadata& want,
                 int64_t& payload_frames, uint64_t& payload_bytes,
                 long& payload_offset) {
    SourceMetadata have;
    std::string payload_type;
    if (!read_header_metadata(f, have, payload_type, payload_frames,
                              payload_bytes, payload_offset)) {
        return false;
    }
    if (!metadata_matches(have, want)) return false;
    if (payload_frames != want.frame_count) return false;
    if (payload_frames < 0 || want.channels <= 0) return false;

    const uint64_t want_bytes =
        static_cast<uint64_t>(payload_frames) *
        static_cast<uint64_t>(want.channels) *
        static_cast<uint64_t>(sizeof(float));
    if (payload_bytes != want_bytes) return false;
    return true;
}

bool read_cache_full(const std::filesystem::path& cache_path,
                     const SourceMetadata& want,
                     std::vector<float>& out_samples) {
    std::FILE* f = std::fopen(cache_path.c_str(), "rb");
    if (!f) return false;

    bool ok = false;
    do {
        int64_t payload_frames = 0;
        uint64_t payload_bytes = 0;
        long payload_offset = 0;
        if (!read_header(f, want, payload_frames, payload_bytes, payload_offset)) break;

        // The header read leaves the stream at payload_offset, so the whole
        // interleaved payload reads back from here.
        auto sample_count =
            checked_audio_sample_count(payload_frames, want.channels);
        if (!sample_count) break;
        out_samples.assign(*sample_count, 0.0f);
        if (!out_samples.empty() &&
            std::fread(out_samples.data(), sizeof(float), out_samples.size(), f) !=
                out_samples.size()) {
            out_samples.clear();
            break;
        }

        std::error_code ec;
        const auto actual_size = std::filesystem::file_size(cache_path, ec);
        if (ec) break;
        if (actual_size != static_cast<uint64_t>(payload_offset) + payload_bytes)
            break;

        ok = true;
    } while (false);

    std::fclose(f);
    return ok;
}

bool cache_file_matches(const std::filesystem::path& cache_path,
                        const SourceMetadata& want) {
    std::FILE* f = std::fopen(cache_path.c_str(), "rb");
    if (!f) return false;

    bool ok = false;
    int64_t payload_frames = 0;
    uint64_t payload_bytes = 0;
    long payload_offset = 0;
    if (read_header(f, want, payload_frames, payload_bytes, payload_offset)) {
        std::error_code ec;
        const auto actual_size = std::filesystem::file_size(cache_path, ec);
        ok = !ec &&
             actual_size == static_cast<uint64_t>(payload_offset) + payload_bytes;
    }
    std::fclose(f);
    return ok;
}

bool write_header(std::FILE* f, const SourceMetadata& m, uint64_t payload_bytes) {
    const uint32_t hash_len =
        m.hash_algorithm == kHashAlgorithmFlacStreaminfoMd5
            ? static_cast<uint32_t>(sizeof(m.hash))
            : 0;
    return put_bytes(f, kMagic, sizeof(kMagic)) &&
           put_u32(f, kFileVersion) &&
           put_str(f, m.basename) &&
           put_str(f, m.extension) &&
           put_u64(f, m.source_size) &&
           put_i64(f, m.mtime_ticks) &&
           put_i32(f, m.sample_rate) &&
           put_i32(f, m.channels) &&
           put_i64(f, m.frame_count) &&
           put_i32(f, m.kind_code) &&
           put_str(f, kPayloadType) &&
           put_i64(f, m.frame_count) &&
           put_u64(f, payload_bytes) &&
           put_u32(f, m.hash_algorithm) &&
           put_u32(f, hash_len) &&
           put_bytes(f, m.hash, hash_len);
}

bool write_cache_samples(const std::filesystem::path& cache_path,
                         const SourceMetadata& meta,
                         const float* samples,
                         uint64_t sample_count) {
    // Unique staging names let the GUI-load background writer overlap the
    // render worker's rebuild path. Both writers produce identical bytes for
    // the same source metadata, and atomic rename makes concurrent publishes
    // benign.
    const std::filesystem::path tmp_path =
        cache_path.string() + ".tmp" +
        std::to_string(g_cache_tmp_counter.fetch_add(1));
    std::FILE* f = std::fopen(tmp_path.c_str(), "wb");
    if (!f) return false;

    bool ok = false;
    do {
        const uint64_t payload_bytes =
            static_cast<uint64_t>(meta.frame_count) *
            static_cast<uint64_t>(meta.channels) *
            static_cast<uint64_t>(sizeof(float));
        if (!write_header(f, meta, payload_bytes)) break;
        if (sample_count > std::numeric_limits<size_t>::max()) break;
        if (sample_count > 0 &&
            std::fwrite(samples, sizeof(float), static_cast<size_t>(sample_count), f) !=
                static_cast<size_t>(sample_count)) {
            break;
        }

        if (std::fflush(f) != 0) break;
        if (::fsync(::fileno(f)) != 0) break;
        ok = true;
    } while (false);

    if (std::fclose(f) != 0) ok = false;

    if (!ok) {
        std::error_code ec;
        std::filesystem::remove(tmp_path, ec);
        return false;
    }

    std::error_code ec;
    std::filesystem::rename(tmp_path, cache_path, ec);
    if (ec) {
        std::filesystem::remove(tmp_path, ec);
        return false;
    }
    return true;
}

} // namespace

bool is_source_sample_cache_path(const std::string& path) {
    return lowercase(std::filesystem::path(path).extension().string()) ==
           kSampleCacheExtension;
}

bool read_full_source_from_source_sample_cache(const std::string& source_path,
                                               const AudioFileInfo& source_info,
                                               std::vector<float>& out_samples) {
    if (!is_cacheable_source(source_info)) return false;

    const SourceMetadata meta = source_metadata(source_path, source_info);
    if (meta.frame_count <= 0) return false;

    return read_cache_full(cache_path_for_source(source_path), meta, out_samples);
}

bool ensure_source_sample_cache_from_buffer(const std::string& source_path,
                                            const AudioFileInfo& source_info,
                                            const float* samples,
                                            int64_t frames,
                                            int channels) {
    if (!samples || frames <= 0 || channels <= 0 ||
        !is_cacheable_source(source_info)) {
        return true;
    }
    if (source_info.sample_rate <= 0 || source_info.channels != channels ||
        source_info.frames != frames) {
        return false;
    }

    // The background writer proves the on-disk file still matches the load it
    // decoded before publishing a cache for it, and skips silently if identity
    // has moved.
    auto fresh_probe = audio_probe(source_path);
    if (!fresh_probe) return false;
    if (fresh_probe->sample_rate != source_info.sample_rate ||
        fresh_probe->channels != source_info.channels ||
        fresh_probe->frames != source_info.frames ||
        fresh_probe->kind != source_info.kind ||
        fresh_probe->has_content_md5 != source_info.has_content_md5 ||
        (fresh_probe->has_content_md5 &&
         std::memcmp(fresh_probe->content_md5, source_info.content_md5,
                     sizeof(source_info.content_md5)) != 0)) {
        return false;
    }

    const SourceMetadata meta = source_metadata(source_path, *fresh_probe);
    const std::filesystem::path cache_path = cache_path_for_source(source_path);

    if (cache_file_matches(cache_path, meta)) return true;

    const uint64_t sample_count =
        static_cast<uint64_t>(frames) * static_cast<uint64_t>(channels);
    return write_cache_samples(cache_path, meta, samples, sample_count);
}

