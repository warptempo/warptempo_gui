#include "decoded_source_cache.h"

#include "frame_map_build.h"

#include <algorithm>
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

constexpr char kMagic[] = "WARPTEMPO_DECODED_SOURCE_CACHE";
constexpr uint32_t kFileVersion = 1;
constexpr uint32_t kHashAlgorithmNone = 0;
constexpr char kPayloadType[] = "float32_interleaved";
constexpr char kDecodedExtension[] = ".decoded";

struct SourceMetadata {
    std::string basename;
    std::string extension;
    std::string canonical_path;
    uint64_t source_size = 0;
    int64_t mtime_ticks = 0;
    int sample_rate = 0;
    int channels = 0;
    int64_t frame_count = 0;
    int format = 0;
};

bool put_bytes(std::FILE* f, const void* p, size_t n) {
    return n == 0 || std::fwrite(p, 1, n, f) == n;
}

bool put_u32(std::FILE* f, uint32_t x) { return std::fwrite(&x, sizeof x, 1, f) == 1; }
bool put_u64(std::FILE* f, uint64_t x) { return std::fwrite(&x, sizeof x, 1, f) == 1; }
bool put_i64(std::FILE* f, int64_t x)  { return std::fwrite(&x, sizeof x, 1, f) == 1; }
bool put_i32(std::FILE* f, int32_t x)  { return std::fwrite(&x, sizeof x, 1, f) == 1; }

bool put_str(std::FILE* f, const std::string& s) {
    if (s.size() > std::numeric_limits<uint32_t>::max()) return false;
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
    s.assign(n, '\0');
    return n == 0 || get_bytes(f, s.data(), n);
}

std::string lowercase(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

bool is_cacheable_source(const std::string& source_path, int format) {
    const std::filesystem::path p(source_path);
    const std::string ext = lowercase(p.extension().string());
    if (ext == ".wav" || ext == ".wave") return false;

    const int type = format & SF_FORMAT_TYPEMASK;
    if (type == SF_FORMAT_WAV) return false;

    return ext == ".flac" || ext == ".ogg" || ext == ".opus" ||
           ext == ".mp3" || ext == ".oga";
}

std::filesystem::path cache_path_for_source(const std::string& source_path) {
    std::filesystem::path p(source_path);
    p.replace_extension(kDecodedExtension);
    return p;
}

std::string canonical_or_absolute(const std::filesystem::path& p) {
    std::error_code ec;
    auto c = std::filesystem::weakly_canonical(p, ec);
    if (!ec) return c.string();
    auto a = std::filesystem::absolute(p, ec);
    return ec ? p.string() : a.string();
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
                               const SF_INFO& source_info) {
    const std::filesystem::path p(source_path);
    SourceMetadata m;
    m.basename = p.stem().string();
    m.extension = p.extension().string();
    m.canonical_path = canonical_or_absolute(p);
    std::error_code ec;
    m.source_size = std::filesystem::file_size(p, ec);
    if (ec) m.source_size = 0;
    m.mtime_ticks = file_time_ticks(p);
    m.sample_rate = source_info.samplerate;
    m.channels = source_info.channels;
    m.frame_count = static_cast<int64_t>(source_info.frames);
    m.format = source_info.format;
    return m;
}

bool metadata_matches(const SourceMetadata& have, const SourceMetadata& want) {
    return have.basename == want.basename &&
           have.extension == want.extension &&
           have.canonical_path == want.canonical_path &&
           have.source_size == want.source_size &&
           have.mtime_ticks == want.mtime_ticks &&
           have.sample_rate == want.sample_rate &&
           have.channels == want.channels &&
           have.frame_count == want.frame_count &&
           have.format == want.format;
}

bool read_header(std::FILE* f, const SourceMetadata& want,
                 int64_t& payload_frames, uint64_t& payload_bytes,
                 long& payload_offset) {
    char magic[sizeof(kMagic)]{};
    if (!get_bytes(f, magic, sizeof(kMagic))) return false;
    if (std::memcmp(magic, kMagic, sizeof(kMagic)) != 0) return false;

    uint32_t version = 0;
    if (!get_u32(f, version) || version != kFileVersion) return false;

    SourceMetadata have;
    int32_t sr = 0, ch = 0, fmt = 0;
    if (!get_str(f, have.basename)) return false;
    if (!get_str(f, have.extension)) return false;
    if (!get_str(f, have.canonical_path)) return false;
    if (!get_u64(f, have.source_size)) return false;
    if (!get_i64(f, have.mtime_ticks)) return false;
    if (!get_i32(f, sr)) return false;
    if (!get_i32(f, ch)) return false;
    if (!get_i64(f, have.frame_count)) return false;
    if (!get_i32(f, fmt)) return false;
    have.sample_rate = sr;
    have.channels = ch;
    have.format = fmt;

    std::string payload_type;
    if (!get_str(f, payload_type) || payload_type != kPayloadType) return false;
    if (!get_i64(f, payload_frames)) return false;
    if (!get_u64(f, payload_bytes)) return false;

    uint32_t hash_algorithm = 0, hash_len = 0;
    if (!get_u32(f, hash_algorithm)) return false;
    if (!get_u32(f, hash_len)) return false;
    if (hash_algorithm != kHashAlgorithmNone || hash_len != 0) return false;

    if (!metadata_matches(have, want)) return false;
    if (payload_frames != want.frame_count) return false;
    if (payload_frames < 0 || want.channels <= 0) return false;

    const uint64_t want_bytes =
        static_cast<uint64_t>(payload_frames) *
        static_cast<uint64_t>(want.channels) *
        static_cast<uint64_t>(sizeof(float));
    if (payload_bytes != want_bytes) return false;

    payload_offset = std::ftell(f);
    return payload_offset >= 0;
}

bool read_cache_range(const std::filesystem::path& cache_path,
                      const SourceMetadata& want,
                      size_t begin_frame,
                      size_t end_frame,
                      std::vector<float>& out_samples) {
    std::FILE* f = std::fopen(cache_path.c_str(), "rb");
    if (!f) return false;

    bool ok = false;
    do {
        int64_t payload_frames = 0;
        uint64_t payload_bytes = 0;
        long payload_offset = 0;
        if (!read_header(f, want, payload_frames, payload_bytes, payload_offset)) break;
        if (end_frame > static_cast<size_t>(payload_frames) ||
            end_frame <= begin_frame) {
            break;
        }

        const size_t channels = static_cast<size_t>(want.channels);
        const size_t frames = end_frame - begin_frame;
        const uint64_t sample_offset =
            static_cast<uint64_t>(begin_frame) * static_cast<uint64_t>(channels);
        const uint64_t byte_offset =
            static_cast<uint64_t>(payload_offset) + sample_offset * sizeof(float);
        if (byte_offset > static_cast<uint64_t>(std::numeric_limits<long>::max()))
            break;
        if (std::fseek(f, static_cast<long>(byte_offset), SEEK_SET) != 0) break;

        out_samples.assign(frames * channels, 0.0f);
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

bool write_header(std::FILE* f, const SourceMetadata& m, uint64_t payload_bytes) {
    return put_bytes(f, kMagic, sizeof(kMagic)) &&
           put_u32(f, kFileVersion) &&
           put_str(f, m.basename) &&
           put_str(f, m.extension) &&
           put_str(f, m.canonical_path) &&
           put_u64(f, m.source_size) &&
           put_i64(f, m.mtime_ticks) &&
           put_i32(f, m.sample_rate) &&
           put_i32(f, m.channels) &&
           put_i64(f, m.frame_count) &&
           put_i32(f, m.format) &&
           put_str(f, kPayloadType) &&
           put_i64(f, m.frame_count) &&
           put_u64(f, payload_bytes) &&
           put_u32(f, kHashAlgorithmNone) &&
           put_u32(f, 0);
}

bool rebuild_cache(const std::string& source_path,
                   const std::filesystem::path& cache_path,
                   const SourceMetadata& meta) {
    SF_INFO info{};
    info.format = 0;
    SNDFILE* snd = sf_open(source_path.c_str(), SFM_READ, &info);
    if (!snd) return false;
    if (info.samplerate != meta.sample_rate || info.channels != meta.channels ||
        info.frames != meta.frame_count || info.format != meta.format) {
        sf_close(snd);
        return false;
    }

    const std::filesystem::path tmp_path = cache_path.string() + ".tmp";
    std::FILE* f = std::fopen(tmp_path.c_str(), "wb");
    if (!f) {
        sf_close(snd);
        return false;
    }

    bool ok = false;
    do {
        const uint64_t payload_bytes =
            static_cast<uint64_t>(meta.frame_count) *
            static_cast<uint64_t>(meta.channels) *
            static_cast<uint64_t>(sizeof(float));
        if (!write_header(f, meta, payload_bytes)) break;

        constexpr sf_count_t kChunkFrames = 65536;
        std::vector<float> buffer(static_cast<size_t>(kChunkFrames) *
                                  static_cast<size_t>(meta.channels));
        int64_t remaining = meta.frame_count;
        while (remaining > 0) {
            const sf_count_t want = static_cast<sf_count_t>(
                std::min<int64_t>(remaining, kChunkFrames));
            const sf_count_t got = sf_readf_float(snd, buffer.data(), want);
            if (got != want) break;
            const size_t sample_count =
                static_cast<size_t>(got) * static_cast<size_t>(meta.channels);
            if (sample_count > 0 &&
                std::fwrite(buffer.data(), sizeof(float), sample_count, f) !=
                    sample_count) {
                break;
            }
            remaining -= static_cast<int64_t>(got);
        }
        if (remaining != 0) break;

        if (std::fflush(f) != 0) break;
        if (::fsync(::fileno(f)) != 0) break;
        ok = true;
    } while (false);

    sf_close(snd);
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

bool is_decoded_source_cache_path(const std::string& path) {
    return lowercase(std::filesystem::path(path).extension().string()) ==
           kDecodedExtension;
}

std::expected<DecodedSourceReadResult, std::string>
load_source_range_with_decoded_cache(const std::string& source_path,
                                     const SF_INFO& source_info,
                                     size_t begin_frame,
                                     size_t end_frame,
                                     std::vector<float>& out_samples,
                                     int& out_sample_rate,
                                     int& out_channels) {
    if (!is_cacheable_source(source_path, source_info.format)) {
        if (auto r = load_source_range_to_buffer(source_path, begin_frame, end_frame,
                                                out_samples, out_sample_rate,
                                                out_channels); !r) {
            return std::unexpected(r.error());
        }
        return DecodedSourceReadResult{DecodedSourceCacheStatus::Bypassed, false};
    }

    const SourceMetadata meta = source_metadata(source_path, source_info);
    const std::filesystem::path cache_path = cache_path_for_source(source_path);

    DecodedSourceReadResult result;
    if (read_cache_range(cache_path, meta, begin_frame, end_frame, out_samples)) {
        out_sample_rate = meta.sample_rate;
        out_channels = meta.channels;
        result.cache_status = DecodedSourceCacheStatus::Hit;
        result.used_cache = true;
        return result;
    }

    result.cache_status = std::filesystem::exists(cache_path)
        ? DecodedSourceCacheStatus::Rebuilt
        : DecodedSourceCacheStatus::Miss;

    if (!rebuild_cache(source_path, cache_path, meta)) {
        if (auto r = load_source_range_to_buffer(source_path, begin_frame, end_frame,
                                                out_samples, out_sample_rate,
                                                out_channels); !r) {
            return std::unexpected(r.error());
        }
        result.used_cache = false;
        return result;
    }

    if (!read_cache_range(cache_path, meta, begin_frame, end_frame, out_samples)) {
        if (auto r = load_source_range_to_buffer(source_path, begin_frame, end_frame,
                                                out_samples, out_sample_rate,
                                                out_channels); !r) {
            return std::unexpected(r.error());
        }
        result.used_cache = false;
        return result;
    }

    out_sample_rate = meta.sample_rate;
    out_channels = meta.channels;
    result.used_cache = true;
    if (result.cache_status == DecodedSourceCacheStatus::Miss)
        result.cache_status = DecodedSourceCacheStatus::Rebuilt;
    return result;
}

const char* decoded_source_cache_status_name(DecodedSourceCacheStatus status) {
    switch (status) {
        case DecodedSourceCacheStatus::Bypassed: return "bypassed";
        case DecodedSourceCacheStatus::Hit:      return "hit";
        case DecodedSourceCacheStatus::Miss:     return "miss";
        case DecodedSourceCacheStatus::Rebuilt:  return "rebuilt";
    }
    return "bypassed";
}
