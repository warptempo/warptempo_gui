#include "render_cache.h"

#include "env_fingerprint.h"

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
inline void put_u64(std::vector<uint8_t>& v, uint64_t x) { put_bytes(v, &x, sizeof x); }
inline void put_i32(std::vector<uint8_t>& v, int32_t  x) { put_bytes(v, &x, sizeof x); }
inline void put_i64(std::vector<uint8_t>& v, int64_t  x) { put_bytes(v, &x, sizeof x); }
inline void put_f64(std::vector<uint8_t>& v, double   x) { put_bytes(v, &x, sizeof x); }
inline void put_u8 (std::vector<uint8_t>& v, uint8_t  x) { v.push_back(x); }
inline void put_str(std::vector<uint8_t>& v, const std::string& s) {
    // Full uint64_t length prefix, no narrowing: the encoding stays injective
    // over the whole accepted in-memory string domain, so no oversized
    // free-text value can wrap its prefix and boundary-splice two distinct
    // field partitions into the same byte stream.
    put_u64(v, static_cast<uint64_t>(s.size()));
    put_bytes(v, s.data(), s.size());
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
// share a key. The key is a conservative over-approximation of byte identity,
// and that direction is the point: a match guarantees byte-identical output;
// a mismatch at worst re-renders redundantly.
// The sole caller is do_render, which threads an already-resolved product
// through (its own resolve produces the per-resolve stderr lines). GUI-only
// marker session scratch (iteration / BPM authoring) never reaches the
// resolver, so it is excluded by construction. Same inputs always produce
// byte-identical output; the result is hex-encoded into the .fingerprint
// sidecar and exact-compared by fingerprint_sidecar_matches.
constexpr uint32_t kFingerprintVersion = 18;
constexpr char     kSidecarMagic[]     = "WARPTEMPO_RENDER_FINGERPRINT";
// The sidecar_layout line versions the on-disk text container of the sidecar
// file itself. The fingerprint content version is serialized inside the
// fingerprint payload by render_fingerprint.
constexpr uint32_t kSidecarVersion     = 1;
constexpr char     kSidecarExtension[] = ".fingerprint";

} // namespace

bool stat_file_identity(const std::string& path, RenderFileIdentity& out) {
    // The one on-disk stat: size and mtime in nanoseconds. The fingerprint's
    // source trust boundary is exactly this pair (header ruling).
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
    out.size  = static_cast<uint64_t>(st.st_size);
    out.mtime = base + nsec;
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
                                 const std::vector<uint8_t>& fingerprint) {
    RenderFileIdentity wav_identity;
    if (!stat_file_identity(wav_path, wav_identity)) return false;

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
    return recorded_fingerprint == fingerprint;
}

void RenderCacheDir::init() {
    enabled_ = false;

    std::string base;
    if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg && xdg[0]) {
        base = xdg;
    } else if (const char* home = std::getenv("HOME"); home && home[0]) {
        base = std::string(home) + "/.cache";
    } else {
        return; // no cache home; manager stays disabled
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

void RenderCacheDir::sweep_orphans() {
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

void RenderCacheDir::shutdown() {
    if (!dir_.empty()) {
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }
    enabled_ = false;
}

std::string RenderCacheDir::process_dir() const {
    return enabled_ ? dir_ : std::string();
}
