#include "render_cache.h"

#include "env_fingerprint.h"
#include "wav_io.h"

#include <algorithm>
#include <charconv>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <system_error>

#include <csignal>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace {

inline void put_bytes(std::vector<uint8_t>& v, const void* p, size_t n) {
    // resize-then-memcpy rather than v.insert(end, b, b + n): byte-identical
    // output, but this shape avoids a GCC -O3 -Wstringop-overflow false
    // positive through the vector reallocation-move inlined from the insert path.
    const size_t old = v.size();
    v.resize(old + n);
    std::memcpy(v.data() + old, p, n);
}
inline void put_u32(std::vector<uint8_t>& v, uint32_t x) { put_bytes(v, &x, sizeof x); }
inline void put_i32(std::vector<uint8_t>& v, int32_t  x) { put_bytes(v, &x, sizeof x); }
inline void put_i64(std::vector<uint8_t>& v, int64_t  x) { put_bytes(v, &x, sizeof x); }
inline void put_f64(std::vector<uint8_t>& v, double   x) { put_bytes(v, &x, sizeof x); }
inline void put_u8 (std::vector<uint8_t>& v, uint8_t  x) { v.push_back(x); }
inline void put_str(std::vector<uint8_t>& v, const std::string& s) {
    put_u32(v, static_cast<uint32_t>(s.size()));
    put_bytes(v, s.data(), s.size());
}

// FNV-1a, 64-bit. Used only to name files and bucket the in-memory maps; a
// collision degrades to a miss via the exact fingerprint-blob compare, never
// to a wrong hit.
uint64_t fnv1a64(const std::vector<uint8_t>& d) {
    uint64_t h = 1469598103934665603ull;
    for (uint8_t b : d) { h ^= b; h *= 1099511628211ull; }
    return h;
}

std::string hex16(uint64_t h) {
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(h));
    return std::string(buf);
}

char hex_digit(uint8_t x) {
    return static_cast<char>(x < 10 ? ('0' + x) : ('a' + (x - 10)));
}

std::string hex_encode(const std::vector<uint8_t>& bytes) {
    std::string out;
    out.reserve(bytes.size() * 2);
    for (uint8_t b : bytes) {
        out.push_back(hex_digit(static_cast<uint8_t>(b >> 4)));
        out.push_back(hex_digit(static_cast<uint8_t>(b & 0x0f)));
    }
    return out;
}

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool hex_decode(const std::string& text, std::vector<uint8_t>& out) {
    if ((text.size() & 1u) != 0) return false;
    std::vector<uint8_t> decoded;
    decoded.reserve(text.size() / 2);
    for (size_t i = 0; i < text.size(); i += 2) {
        const int hi = hex_value(text[i]);
        const int lo = hex_value(text[i + 1]);
        if (hi < 0 || lo < 0) return false;
        decoded.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    out = std::move(decoded);
    return true;
}

bool parse_u64_exact(const std::string& text, uint64_t& out) {
    if (text.empty()) return false;
    uint64_t value = 0;
    const char* first = text.data();
    const char* last = first + text.size();
    const auto r = std::from_chars(first, last, value);
    if (r.ec != std::errc{} || r.ptr != last) return false;
    out = value;
    return true;
}

bool parse_i64_exact(const std::string& text, int64_t& out) {
    if (text.empty()) return false;
    int64_t value = 0;
    const char* first = text.data();
    const char* last = first + text.size();
    const auto r = std::from_chars(first, last, value);
    if (r.ec != std::errc{} || r.ptr != last) return false;
    out = value;
    return true;
}

bool parse_prefixed_u64(const std::string& line, const char* prefix,
                        uint64_t& out) {
    const std::string_view p(prefix);
    if (!line.starts_with(p)) return false;
    return parse_u64_exact(line.substr(p.size()), out);
}

bool parse_prefixed_i64(const std::string& line, const char* prefix,
                        int64_t& out) {
    const std::string_view p(prefix);
    if (!line.starts_with(p)) return false;
    return parse_i64_exact(line.substr(p.size()), out);
}

// Canonical RENDER-IDENTITY fingerprint: the FULL recipe in this environment
// — "would a fresh render of this recipe, in this environment, produce these
// bytes". Serializes, in order: the content version; the computed
// render-environment quartet (compute_render_env_hashes() — the four library
// stat-identity digests actually mapped into THIS process, so a pre-upgrade
// artifact can never match a post-upgrade recipe); the source path plus its
// load-time source identity; the sample rate; EVERY EngineSettings field —
// the five naming/provenance fields (title, bpm, notes, url, cover) included
// by ruling (architect 2026-07-17: they change about once per movement, so a
// provenance edit forcing a fresh render is accepted; the payoff is that no
// inert-field classification exists anywhere, and a re-render refreshes the
// artifact's attested .settings provenance); the trim bounds; and the
// RESOLVED marker state — resolve_warp_markers_for_render's survivors and
// build_phase_reset_source_frames' collapsed enabled reset positions, the
// exact engine inputs, so two states normalization proves render-identical
// share a key (under unconditional triggers this is what turns an inert
// marker edit's forced re-derive into a cache hit). The key is a conservative
// over-approximation of byte identity, and that direction is the point: a
// match guarantees byte-identical output; a mismatch at worst re-renders
// redundantly.
// CALLERS OWN THE RESOLVE: render_fingerprint is pure serialization; each
// call site either threads an already-resolved product through (do_render) or
// runs its own resolve and accepts the resolver's per-resolve stderr lines
// (compute_live_render_fingerprint). GUI-only marker session scratch
// (iteration / BPM authoring) never reaches the resolver, so it is excluded
// by construction. Same inputs always produce byte-identical output; the
// result is hashed to name a cache file and stored verbatim for an
// exact-compare confirm on lookup.
constexpr uint32_t kFingerprintVersion = 17;
constexpr char     kSidecarMagic[]     = "WARPTEMPO_RENDER_FINGERPRINT";
// The sidecar_layout line versions the on-disk text container of the sidecar
// file itself. The fingerprint content version is serialized inside the
// fingerprint payload by render_fingerprint.
constexpr uint32_t kSidecarVersion     = 1;
constexpr char     kSidecarExtension[] = ".fingerprint";

bool write_bytes_to_path(const std::string& path, const std::vector<char>& bytes) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;

    bool ok = false;
    do {
        if (!bytes.empty() &&
            std::fwrite(bytes.data(), 1, bytes.size(), f) != bytes.size()) {
            break;
        }
        if (std::fflush(f) != 0) break;
        if (::fsync(::fileno(f)) != 0) break;
        ok = true;
    } while (false);

    if (std::fclose(f) != 0) ok = false;
    if (!ok) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
    return ok;
}

} // namespace

bool stat_artifact_identity(const std::string& path,
                            ArtifactStatIdentity& out) {
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0) return false;
    if (st.st_size < 0) return false;
    constexpr int64_t kNsecPerSec = 1000000000ll;
    const int64_t sec = static_cast<int64_t>(st.st_mtim.tv_sec);
    if (sec > std::numeric_limits<int64_t>::max() / kNsecPerSec ||
        sec < std::numeric_limits<int64_t>::min() / kNsecPerSec) {
        return false;
    }
    const int64_t nsec = static_cast<int64_t>(st.st_mtim.tv_nsec);
    const int64_t base = sec * kNsecPerSec;
    if (nsec < 0 || nsec >= kNsecPerSec ||
        base > std::numeric_limits<int64_t>::max() - nsec) {
        return false;
    }
    out.dev      = static_cast<uint64_t>(st.st_dev);
    out.inode    = static_cast<uint64_t>(st.st_ino);
    out.size     = static_cast<uint64_t>(st.st_size);
    out.mtime_ns = base + nsec;
    return true;
}

bool stat_file_identity(const std::string& path, RenderFileIdentity& out) {
    // The fingerprint's source trust boundary is size+mtime only (header
    // ruling); the full-identity stat above is the one stat implementation.
    ArtifactStatIdentity full;
    if (!stat_artifact_identity(path, full)) return false;
    out.size = full.size;
    out.mtime = full.mtime_ns;
    return true;
}

std::vector<uint8_t> render_fingerprint(
        const std::string& source_audio_path,
        const RenderFileIdentity& source_identity,
        int sample_rate,
        const std::vector<MarkerForRender>& resolved_warp_markers,
        const std::vector<double>& phase_reset_source_frames,
        const EngineSettings& s,
        bool has_trim_begin, int64_t trim_begin_frame,
        bool has_trim_end,   int64_t trim_end_frame) {
    std::vector<uint8_t> fp;
    fp.reserve(256 + resolved_warp_markers.size() * 64);

    put_u32(fp, kFingerprintVersion);

    // Render environment: the four library stat-identity digests actually
    // mapped into THIS process (per-process constants — one lazy computation for
    // the process lifetime), in RenderEnvHashes declaration order. The
    // COMPUTED quartet, deliberately not the .settings *_hash attestation
    // keys: the fingerprint must name the libraries that would actually
    // produce the bytes, so a pre-upgrade artifact can never match a
    // post-upgrade recipe.
    const RenderEnvHashes& env = compute_render_env_hashes();
    put_str(fp, env.libm);
    put_str(fp, env.libmvec);
    put_str(fp, env.fftw3);
    put_str(fp, env.fftw3_threads);

    put_str(fp, source_audio_path);
    put_bytes(fp, &source_identity.size, sizeof source_identity.size);
    put_bytes(fp, &source_identity.mtime, sizeof source_identity.mtime);
    put_i32(fp, static_cast<int32_t>(sample_rate));

    // Engine settings: EVERY field serializes — the full-recipe key (ruling
    // above), so no field carries an inert/live classification. The
    // exhaustive switch still forces a human decision here when the schema
    // grows: it has no default (a new EngineField enumerator draws -Wswitch),
    // and the static_assert below fails the build the moment EngineSettings
    // gains a field (the canonical-key addition recipe touches both), so a new
    // field's encoding and order must be chosen at this switch.
    static_assert(sizeof(EngineSettings) ==
                      5 * sizeof(std::string) + sizeof(double),
                  "EngineSettings changed: decide the new field's render-byte "
                  "role in render_fingerprint's per-field switch, then update "
                  "this size expression");
    constexpr EngineField kEngineFieldKeyOrder[] = {
        EngineField::Title, EngineField::Scale, EngineField::Bpm,
        EngineField::Notes, EngineField::Url,   EngineField::Cover,
    };
    for (const EngineField field : kEngineFieldKeyOrder) {
        switch (field) {  // no default: a new enumerator must be decided here
            case EngineField::Title: put_str(fp, s.title); break;
            case EngineField::Scale: put_f64(fp, s.scale); break;
            case EngineField::Bpm:   put_str(fp, s.bpm);   break;
            case EngineField::Notes: put_str(fp, s.notes); break;
            case EngineField::Url:   put_str(fp, s.url);   break;
            case EngineField::Cover: put_str(fp, s.cover); break;
        }
    }

    // Trim, with the frame values normalized to 0 when the bound is unset so
    // a stale value behind a false has-bound cannot move the key (the engine
    // ignores it in that state). Authored int64 bounds widened to the f64
    // encoding (exact — whole frames sit far below 2^53). The authored bounds
    // serialize verbatim even when plan_trim will refuse them and the render
    // falls back to untrimmed — accepted conservatism, recorded at do_render's
    // trim-plan block.
    put_u8 (fp, has_trim_begin ? 1 : 0);
    put_f64(fp, has_trim_begin ? static_cast<double>(trim_begin_frame) : 0.0);
    put_u8 (fp, has_trim_end ? 1 : 0);
    put_f64(fp, has_trim_end ? static_cast<double>(trim_end_frame) : 0.0);

    // Warp markers: the RESOLVED render list — exactly the MarkerForRender
    // fields build_warp_frame_map reads (frame, resolved owning tempo cents,
    // typed scale, label def/ref), after resolve_warp_markers_for_render's
    // filter/collapse/seed/materialize/normalize pipeline. Raw disabled
    // markers, cascade-dropped refs, collapsed duplicates, and fields
    // materialization discards never reach this list, so edits that cannot
    // change engine input cannot move the key. All survivors are enabled by
    // the resolver's output invariants — no disabled flag exists here.
    put_u32(fp, static_cast<uint32_t>(resolved_warp_markers.size()));
    for (const auto& m : resolved_warp_markers) {
        put_i64(fp, m.time_frame);
        put_i64(fp, m.tempo_cents);
        // Optional typed scale: presence flag then the value (0.0 filler
        // when absent, so an absent scale and a hypothetical 0.0 cannot
        // collide — 0.0 is unparseable as a typed scale anyway).
        put_u8 (fp, m.tempo_scale.has_value() ? 1 : 0);
        put_f64(fp, m.tempo_scale.value_or(0.0));
        put_str(fp, m.label_def);
        put_str(fp, m.label_ref);
    }

    // Phase resets: the RESOLVED authored intermediate — the collapsed
    // enabled positions build_phase_reset_source_frames emits. Disabled
    // resets and collapsed equal-frame duplicates are already gone. The
    // doubles are whole source frames by construction (int64 authored
    // positions widened exactly), so the cast back is exact — no rounding
    // occurs.
    put_u32(fp, static_cast<uint32_t>(phase_reset_source_frames.size()));
    for (const double p : phase_reset_source_frames) {
        put_i64(fp, static_cast<int64_t>(p));
    }

    return fp;
}

std::string fingerprint_sidecar_path(const std::string& wav_path) {
    std::filesystem::path p(wav_path);
    p.replace_extension(kSidecarExtension);
    return p.string();
}

bool write_fingerprint_sidecar(const std::string& wav_path,
                               const std::vector<uint8_t>& fingerprint) {
    RenderFileIdentity wav_identity;
    if (!stat_file_identity(wav_path, wav_identity)) return false;

    const std::string sidecar_path = fingerprint_sidecar_path(wav_path);
    const std::string tmp_path = sidecar_path + ".tmp";
    std::string data;
    data.reserve(128 + fingerprint.size() * 2);
    data += kSidecarMagic;
    data += '\n';
    data += "sidecar_layout=";
    data += std::to_string(kSidecarVersion);
    data += '\n';
    data += "size=";
    data += std::to_string(wav_identity.size);
    data += '\n';
    data += "mtime=";
    data += std::to_string(wav_identity.mtime);
    data += '\n';
    data += "fingerprint=";
    data += hex_encode(fingerprint);
    data += '\n';

    std::FILE* f = std::fopen(tmp_path.c_str(), "wb");
    if (!f) return false;

    bool ok = false;
    do {
        if (!data.empty() &&
            std::fwrite(data.data(), 1, data.size(), f) != data.size())
            break;
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
    std::filesystem::rename(tmp_path, sidecar_path, ec);
    if (ec) {
        std::filesystem::remove(tmp_path, ec);
        return false;
    }
    return true;
}

bool fingerprint_sidecar_matches(const std::string& wav_path,
                                 const std::vector<uint8_t>& fingerprint,
                                 ArtifactStatIdentity* out_identity) {
    // One stat serves both the sidecar's size/mtime validation and the
    // TOCTOU capture handed back through out_identity: with concurrent
    // GUI/CLI processes publishing the same title, the wav can be atomically
    // replaced between this validation and the caller's later read/copy
    // while the not-yet-replaced sidecar still validates — atomic
    // publication of each file does not make the wav/sidecar PAIR atomic.
    // The consumer therefore re-stats AFTER its read/copy and compares all
    // four fields against this capture; a mismatch discards the result as a
    // miss (no locks, no retries — a mismatch means concurrent publication,
    // and one wasted read falling back to synthesis is the whole remedy).
    ArtifactStatIdentity full_identity;
    if (!stat_artifact_identity(wav_path, full_identity)) return false;
    RenderFileIdentity wav_identity;
    wav_identity.size = full_identity.size;
    wav_identity.mtime = full_identity.mtime_ns;

    std::ifstream in(fingerprint_sidecar_path(wav_path), std::ios::binary);
    if (!in) return false;

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(std::move(line));
    }
    if (!in.eof()) return false;
    if (lines.size() != 5) return false;
    if (lines[0] != kSidecarMagic) return false;
    if (lines[1] != "sidecar_layout=1") return false;

    uint64_t recorded_size = 0;
    int64_t recorded_mtime = 0;
    if (!parse_prefixed_u64(lines[2], "size=", recorded_size)) return false;
    if (!parse_prefixed_i64(lines[3], "mtime=", recorded_mtime)) return false;
    if (recorded_size != wav_identity.size ||
        recorded_mtime != wav_identity.mtime) {
        return false;
    }

    constexpr std::string_view fp_prefix = "fingerprint=";
    if (!lines[4].starts_with(fp_prefix)) return false;
    std::vector<uint8_t> recorded_fingerprint;
    if (!hex_decode(lines[4].substr(fp_prefix.size()),
                    recorded_fingerprint)) {
        return false;
    }
    if (recorded_fingerprint != fingerprint) return false;
    if (out_identity) *out_identity = full_identity;
    return true;
}

void RenderCache::init() {
    enabled_ = false;

    std::string base;
    if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg && xdg[0]) {
        base = xdg;
    } else if (const char* home = std::getenv("HOME"); home && home[0]) {
        base = std::string(home) + "/.cache";
    } else {
        return; // no cache home; store stays disabled
    }

    parent_ = base + "/warptempo_gui";
    std::error_code ec;
    std::filesystem::create_directories(parent_, ec);
    if (ec) return;

    sweep_orphans();

    dir_ = parent_ + "/" + std::to_string(static_cast<long>(::getpid()));
    std::filesystem::create_directories(dir_, ec);
    if (ec) return;

    enabled_ = true;
}

void RenderCache::sweep_orphans() {
    std::error_code ec;
    std::filesystem::directory_iterator it(parent_, ec), end;
    if (ec) return;

    const long self = static_cast<long>(::getpid());
    for (; it != end; it.increment(ec)) {
        if (ec) break;
        if (!it->is_directory(ec)) continue;

        const std::string name = it->path().filename().string();
        if (name.empty() ||
            name.find_first_not_of("0123456789") != std::string::npos) {
            continue; // not a PID directory
        }
        long pid = 0;
        try { pid = std::stol(name); } catch (...) { continue; }
        if (pid == self) continue;

        // kill(pid, 0): 0 or EPERM means the process is alive (leave it);
        // ESRCH means it is gone (sweep its directory).
        if (::kill(static_cast<pid_t>(pid), 0) == 0) continue;
        if (errno != ESRCH) continue;

        std::error_code rmec;
        std::filesystem::remove_all(it->path(), rmec);
    }
}

void RenderCache::shutdown() {
    join_writer();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ram_.clear();
        ram_bytes_ = 0;
        disk_index_.clear();
        disk_bytes_ = 0;
    }
    if (!dir_.empty()) {
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }
    enabled_ = false;
}

std::string RenderCache::process_dir() const {
    return enabled_ ? dir_ : std::string();
}

void RenderCache::join_writer() {
    std::thread local;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        local = std::move(writer_);
    }
    if (local.joinable()) local.join();
}

bool RenderCache::lookup(const std::vector<uint8_t>& fp,
                         int channels, int sample_rate,
                         std::vector<float>& out) {
    if (!enabled_) {
        return false;
    }
    const uint64_t h = fnv1a64(fp);

    std::vector<char> ram_blob;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (auto it = ram_.find(h); it != ram_.end()) {
            RamEntry& e = it->second;
            if (e.fingerprint == fp && e.channels == channels &&
                e.sample_rate == sample_rate) {
                e.seq = ++lru_seq_;
                ram_blob = e.blob;
            }
        }
    }
    if (!ram_blob.empty()) {
        return decode_wav_blob_to_float(ram_blob, channels, sample_rate, out);
    }

    DiskEntry candidate;
    bool have_candidate = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (auto it = disk_index_.find(h); it != disk_index_.end() &&
            it->second.fingerprint == fp) {
            candidate = it->second;
            have_candidate = true;
        }
    }

    if (have_candidate) {
        const std::string path = dir_ + "/" + candidate.filename;
        // The potentially large wav read happens outside mutex_. If
        // eviction or failed validation removes the pair mid-read, the reader
        // reports a miss and the caller re-renders.
        if (read_file(path, fp, channels, sample_rate, out)) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (auto it = disk_index_.find(h); it != disk_index_.end() &&
                it->second.filename == candidate.filename &&
                it->second.fingerprint == fp) {
                it->second.seq = ++lru_seq_;
            }
            return true;
        }

        bool drop_pair = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            // Match the success-path re-check before mutating the disk index.
            if (auto it = disk_index_.find(h); it != disk_index_.end() &&
                it->second.filename == candidate.filename &&
                it->second.fingerprint == fp) {
                disk_bytes_ -= std::min(disk_bytes_, it->second.size_bytes);
                disk_index_.erase(it);
                drop_pair = true;
            }
        }
        if (drop_pair) remove_disk_pair(path);
    }

    return false;
}

bool RenderCache::publish_wav(const std::vector<uint8_t>& fp,
                              int channels, int sample_rate,
                              const std::string& staging_path) {
    if (!enabled_) return false;
    const uint64_t h = fnv1a64(fp);

    std::vector<char> ram_blob;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (auto it = ram_.find(h); it != ram_.end()) {
            RamEntry& e = it->second;
            if (e.fingerprint == fp && e.channels == channels &&
                e.sample_rate == sample_rate) {
                e.seq = ++lru_seq_;
                ram_blob = e.blob;
            }
        }
    }
    if (!ram_blob.empty()) {
        return write_bytes_to_path(staging_path, ram_blob);
    }

    DiskEntry candidate;
    bool have_candidate = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (auto it = disk_index_.find(h); it != disk_index_.end() &&
            it->second.fingerprint == fp) {
            candidate = it->second;
            have_candidate = true;
        }
    }
    if (!have_candidate) return false;

    const std::string path = dir_ + "/" + candidate.filename;
    bool copied = false;
    if (fingerprint_sidecar_matches(path, fp)) {
        std::error_code ec;
        std::filesystem::copy_file(
            path, staging_path,
            std::filesystem::copy_options::overwrite_existing, ec);
        copied = !ec;
    }
    if (copied) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (auto it = disk_index_.find(h); it != disk_index_.end() &&
            it->second.filename == candidate.filename &&
            it->second.fingerprint == fp) {
            it->second.seq = ++lru_seq_;
        }
        return true;
    }

    bool drop_pair = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Match the success-path re-check before mutating the disk index.
        if (auto it = disk_index_.find(h); it != disk_index_.end() &&
            it->second.filename == candidate.filename &&
            it->second.fingerprint == fp) {
            disk_bytes_ -= std::min(disk_bytes_, it->second.size_bytes);
            disk_index_.erase(it);
            drop_pair = true;
        }
    }
    if (drop_pair) remove_disk_pair(path);
    std::error_code ec;
    std::filesystem::remove(staging_path, ec);
    return false;
}

struct RenderCache::WriterJob {
    enum class Kind {
        WriteBlobToDisk,
        EncodeMasterThenRoute,
    };

    Kind kind = Kind::WriteBlobToDisk;
    uint64_t h = 0;
    std::vector<uint8_t> fp;
    std::string fname;
    std::string path;
    std::vector<char> blob;
    std::vector<float> samples;
    int channels = 0;
    int sample_rate = 0;
    int64_t frame_count = 0;
    // EncodeMasterThenRoute only: the dispatching render's per-dispatch
    // session cancel token (never reset after creation, so a load of it
    // names exactly that session). Null for WriteBlobToDisk jobs.
    std::shared_ptr<const std::atomic<bool>> cancel_token;
};

void RenderCache::insert(const std::vector<uint8_t>& fp,
                         const std::vector<char>& blob,
                         int channels, int sample_rate, int64_t frame_count) {
    if (!enabled_) {
        return;
    }
    if (frame_count <= 0 || blob.empty() || channels <= 0 || sample_rate <= 0) {
        return;
    }

    const uint64_t h = fnv1a64(fp);
    const uint64_t bytes = static_cast<uint64_t>(blob.size());
    const int64_t ram_tier_frames =
        static_cast<int64_t>(sample_rate) * kRamTierMaxSeconds;
    const bool to_ram =
        frame_count <= ram_tier_frames && bytes <= kRamBudgetBytes;
    if (to_ram) {
        insert_ram(h, fp, blob, channels, sample_rate);
        return;
    }

    insert_disk(h, fp, blob, frame_count);
}

void RenderCache::insert_master_floats(
        const std::vector<uint8_t>& fp,
        const std::vector<float>& samples,
        int channels, int sample_rate,
        int64_t frame_count,
        std::shared_ptr<const std::atomic<bool>> cancel_token) {
    if (!enabled_) {
        return;
    }
    if (frame_count <= 0 || samples.empty() ||
        channels <= 0 || sample_rate <= 0) {
        return;
    }
    const size_t ch = static_cast<size_t>(channels);
    if (samples.size() % ch != 0) return;

    const uint64_t h = fnv1a64(fp);
    WriterJob job;
    job.kind = WriterJob::Kind::EncodeMasterThenRoute;
    job.h = h;
    job.fp = fp;
    job.fname = hex16(h) + ".wav";
    job.path = dir_ + "/" + job.fname;
    job.samples = samples;
    job.channels = channels;
    job.sample_rate = sample_rate;
    job.frame_count = frame_count;
    job.cancel_token = std::move(cancel_token);
    start_writer_job(std::move(job));
}

bool RenderCache::insert_ram(uint64_t h, const std::vector<uint8_t>& fp,
                             const std::vector<char>& blob,
                             int channels, int sample_rate) {
    const uint64_t bytes = static_cast<uint64_t>(blob.size());

    std::lock_guard<std::mutex> lock(mutex_);
    if (auto it = ram_.find(h); it != ram_.end()) {
        ram_bytes_ -= static_cast<uint64_t>(it->second.blob.size());
        ram_.erase(it);
    }
    if (bytes > kRamBudgetBytes) return false; // pathological single entry; skip

    evict_ram_until(kRamBudgetBytes - bytes);

    RamEntry e;
    e.fingerprint = fp;
    e.blob        = blob;
    e.channels    = channels;
    e.sample_rate = sample_rate;
    e.seq         = ++lru_seq_;
    ram_[h]       = std::move(e);
    ram_bytes_   += bytes;
    return true;
}

void RenderCache::evict_ram_until(uint64_t target_max) {
    while (ram_bytes_ > target_max && !ram_.empty()) {
        auto victim = ram_.begin();
        for (auto it = ram_.begin(); it != ram_.end(); ++it)
            if (it->second.seq < victim->second.seq) victim = it;
        ram_bytes_ -= static_cast<uint64_t>(victim->second.blob.size());
        ram_.erase(victim);
    }
}

bool RenderCache::insert_disk(uint64_t h, const std::vector<uint8_t>& fp,
                              const std::vector<char>& blob,
                              int64_t frame_count) {
    WriterJob job;
    job.kind = WriterJob::Kind::WriteBlobToDisk;
    job.h = h;
    job.fp = fp;
    job.fname = hex16(h) + ".wav";
    job.path = dir_ + "/" + job.fname;
    job.blob = blob;
    job.frame_count = frame_count;
    start_writer_job(std::move(job));
    return true;
}

void RenderCache::start_writer_job(WriterJob job) {
    join_writer();

    // Drop the job if the dispatching render was cancelled while we waited
    // on the previous writer. The check sits after the potentially long
    // join and before anything becomes externally observable — the disk
    // index mutation below and the writer thread launch. A cancel can still
    // land between this load and the thread start; the writer thread's
    // post-encode re-check of the same job token catches that window, so
    // the two checks together keep a killed session from publishing cache
    // state.
    if (job.cancel_token && job.cancel_token->load()) {
        return;
    }

    if (job.kind == WriterJob::Kind::WriteBlobToDisk) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (auto it = disk_index_.find(job.h); it != disk_index_.end()) {
                disk_bytes_ -= std::min(disk_bytes_, it->second.size_bytes);
                disk_index_.erase(it);
            }
        }
        remove_disk_pair(job.path);
    }

    std::thread new_writer([this, job = std::move(job)]() mutable {
        auto write_disk_blob = [this, &job]() {
            uint64_t written = 0;
            if (!write_file(job.path, job.fp, job.blob,
                            job.frame_count, written)) {
                return;
            }

            {
                std::lock_guard<std::mutex> lock(mutex_);
                DiskEntry e;
                e.fingerprint = job.fp;
                e.filename    = job.fname;
                e.size_bytes  = written;
                e.seq         = ++lru_seq_;
                disk_index_[job.h] = std::move(e);
                disk_bytes_   += written;
                evict_disk_until(kDiskBudgetBytes);
            }
        };

        if (job.kind == WriterJob::Kind::EncodeMasterThenRoute) {
            std::vector<char> encoded;
            if (!encode_pcm24_wav_blob(job.samples, job.channels,
                                       job.sample_rate, encoded)) {
                std::fprintf(stderr,
                    "warptempo_gui: render-cache insert dropped: failed to "
                    "encode target samples as canonical PCM_24 wav\n");
                return;
            }
            job.samples.clear();
            job.samples.shrink_to_fit();
            job.blob = std::move(encoded);

            // Post-encode cancellation re-check, before anything publishes:
            // the RAM insert, the disk-index mutation, and the pair write
            // all come after this point. The token is created per dispatch
            // and never reset, so this load names exactly the dispatching
            // session; a cancel landing anywhere before this point —
            // including the window between the handoff's pre-launch check
            // and this thread's start — drops the entry silently
            // (consistent with the pre-launch drop), and a cancel landing
            // after it completes the write, which is the accepted
            // late-cancel tail, now bounded by this check.
            if (job.cancel_token && job.cancel_token->load()) {
                return;
            }

            const uint64_t bytes = static_cast<uint64_t>(job.blob.size());
            const int64_t ram_tier_frames =
                static_cast<int64_t>(job.sample_rate) * kRamTierMaxSeconds;
            const bool to_ram =
                job.frame_count <= ram_tier_frames && bytes <= kRamBudgetBytes;
            if (to_ram) {
                insert_ram(job.h, job.fp, job.blob,
                           job.channels, job.sample_rate);
                return;
            }

            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (auto it = disk_index_.find(job.h);
                    it != disk_index_.end()) {
                    disk_bytes_ -= std::min(disk_bytes_, it->second.size_bytes);
                    disk_index_.erase(it);
                }
            }
            remove_disk_pair(job.path);
            write_disk_blob();
            return;
        }

        write_disk_blob();
    });
    {
        std::lock_guard<std::mutex> lock(mutex_);
        writer_ = std::move(new_writer);
    }
}

void RenderCache::evict_disk_until(uint64_t target_max) {
    while (disk_bytes_ > target_max && disk_index_.size() > 1) {
        auto victim = disk_index_.begin();
        for (auto it = disk_index_.begin(); it != disk_index_.end(); ++it)
            if (it->second.seq < victim->second.seq) victim = it;
        remove_disk_pair(dir_ + "/" + victim->second.filename);
        disk_bytes_ -= victim->second.size_bytes;
        disk_index_.erase(victim);
    }
}

void RenderCache::remove_disk_pair(const std::string& wav_path) {
    std::error_code ec;
    std::filesystem::remove(wav_path, ec);
    std::filesystem::remove(wav_path + ".tmp", ec);
    const std::string sidecar = fingerprint_sidecar_path(wav_path);
    std::filesystem::remove(sidecar, ec);
    std::filesystem::remove(sidecar + ".tmp", ec);
}

bool RenderCache::read_file(const std::string& path,
                            const std::vector<uint8_t>& want_fp,
                            int channels, int sample_rate,
                            std::vector<float>& out) {
    if (!fingerprint_sidecar_matches(path, want_fp)) return false;
    if (read_wav_to_float(path, channels, sample_rate, out)) return true;
    out.clear();
    return false;
}

bool read_wav_to_float(const std::string& path,
                       int expected_channels, int expected_sample_rate,
                       std::vector<float>& out) {
    if (expected_channels <= 0 || expected_sample_rate <= 0) return false;
    auto info = wav_probe(path);
    if (!info) return false;
    if (info->channels != expected_channels ||
        info->sample_rate != expected_sample_rate) {
        return false;
    }
    auto tmp = wav_read_full(path);
    if (!tmp) return false;
    out = std::move(*tmp);
    return true;
}

bool read_file_bytes(const std::string& path, std::vector<char>& out) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return false;
    const std::streamoff end = in.tellg();
    if (end < 0) return false;
    if (static_cast<uint64_t>(end) > std::numeric_limits<size_t>::max()) {
        return false;
    }
    std::vector<char> tmp(static_cast<size_t>(end));
    in.seekg(0, std::ios::beg);
    if (!tmp.empty() && !in.read(tmp.data(), static_cast<std::streamsize>(tmp.size()))) {
        return false;
    }
    out = std::move(tmp);
    return true;
}

bool encode_pcm24_wav_blob(const std::vector<float>& samples,
                           int channels, int sample_rate,
                           std::vector<char>& out_blob) {
    if (channels <= 0 || sample_rate <= 0) return false;
    const size_t ch = static_cast<size_t>(channels);
    if (samples.size() % ch != 0) return false;
    const int64_t frame_count = static_cast<int64_t>(samples.size() / ch);

    std::vector<char> blob;
    auto writer = WavWriter::open_memory(blob, channels, sample_rate);
    if (!writer) return false;
    auto ok = writer->write_frames(samples.data(), frame_count);
    if (!ok) return false;
    ok = writer->close();
    if (!ok) return false;
    out_blob = std::move(blob);
    return true;
}

bool decode_wav_blob_to_float(const std::vector<char>& blob,
                              int expected_channels,
                              int expected_sample_rate,
                              std::vector<float>& out_samples) {
    if (blob.empty() || expected_channels <= 0 || expected_sample_rate <= 0) {
        return false;
    }
    const std::span<const char> bytes(blob.data(), blob.size());
    auto info = wav_probe(bytes);
    if (!info) return false;
    if (info->channels != expected_channels ||
        info->sample_rate != expected_sample_rate) {
        return false;
    }
    auto tmp = wav_read_full(bytes);
    if (!tmp) return false;
    out_samples = std::move(*tmp);
    return true;
}

bool RenderCache::write_file(const std::string& path,
                             const std::vector<uint8_t>& fp,
                             const std::vector<char>& blob,
                             int64_t frame_count, uint64_t& out_bytes) {
    if (frame_count <= 0 || blob.empty()) return false;

    const std::string tmp = path + ".tmp";
    remove_disk_pair(path);

    if (!write_bytes_to_path(tmp, blob)) {
        remove_disk_pair(path);
        return false;
    }

    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        remove_disk_pair(path);
        return false;
    }
    if (!write_fingerprint_sidecar(path, fp)) {
        remove_disk_pair(path);
        return false;
    }

    const uint64_t wav_size = std::filesystem::file_size(path, ec);
    if (ec) {
        remove_disk_pair(path);
        return false;
    }
    const uint64_t sidecar_size =
        std::filesystem::file_size(fingerprint_sidecar_path(path), ec);
    if (ec) {
        remove_disk_pair(path);
        return false;
    }
    out_bytes = wav_size + sidecar_size;
    return true;
}
