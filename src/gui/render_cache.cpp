#include "render_cache.h"
#include "profile_util.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <system_error>

#include <csignal>
#include <sys/types.h>
#include <unistd.h>

namespace {

inline void put_bytes(std::vector<uint8_t>& v, const void* p, size_t n) {
    const auto* b = static_cast<const uint8_t*>(p);
    v.insert(v.end(), b, b + n);
}
inline void put_u32(std::vector<uint8_t>& v, uint32_t x) { put_bytes(v, &x, sizeof x); }
inline void put_i32(std::vector<uint8_t>& v, int32_t  x) { put_bytes(v, &x, sizeof x); }
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

constexpr uint32_t kFingerprintVersion = 1;
constexpr char     kMagic[4]           = {'W', 'T', 'C', '1'};
constexpr uint32_t kFileVersion        = 1;

} // namespace

std::vector<uint8_t> render_fingerprint(
        const std::string& source_audio_path, int sample_rate,
        const std::vector<GuiWarpMarker>& markers,
        const std::vector<GuiPhaseResetMarker>& phase_resets,
        const EngineSettings& s,
        bool has_trim_begin, double trim_begin_sec,
        bool has_trim_end,   double trim_end_sec) {
    std::vector<uint8_t> fp;
    fp.reserve(256 + markers.size() * 64);

    put_u32(fp, kFingerprintVersion);
    put_str(fp, source_audio_path);
    put_i32(fp, static_cast<int32_t>(sample_rate));

    // Engine settings: every field, so any settings edit the undo stack can
    // return from also returns to this exact key. Provenance fields do not
    // change audio; including them only costs a re-render on a provenance edit
    // (which the live path already does) and guarantees a hit when undone.
    put_str(fp, s.title);
    put_str(fp, s.output_format);
    put_f64(fp, s.scale);
    put_str(fp, s.bpm);
    put_str(fp, s.notes);
    put_str(fp, s.url);
    put_str(fp, s.cover);
    put_u8 (fp, s.limiter ? 1 : 0);

    // Trim, with the seconds normalized to 0 when the bound is unset so a stale
    // value behind a false has-bound cannot move the key (the engine ignores
    // it in that state).
    put_u8 (fp, has_trim_begin ? 1 : 0);
    put_f64(fp, has_trim_begin ? trim_begin_sec : 0.0);
    put_u8 (fp, has_trim_end ? 1 : 0);
    put_f64(fp, has_trim_end ? trim_end_sec : 0.0);

    // Warp markers: parser-domain base fields only (the GuiWarpMarker session
    // scratch never reaches the engine).
    put_u32(fp, static_cast<uint32_t>(markers.size()));
    for (const auto& m : markers) {
        put_f64(fp, m.time_seconds);
        put_u8 (fp, m.tempo_inherits ? 1 : 0);
        put_f64(fp, m.tempo_base);
        put_str(fp, m.tempo_scale);
        put_str(fp, m.label_def);
        put_str(fp, m.label_ref);
        put_u8 (fp, m.disabled ? 1 : 0);
    }

    // Phase reset markers: base fields. Disabled entries are kept in the key
    // because toggling disabled is a real output change.
    put_u32(fp, static_cast<uint32_t>(phase_resets.size()));
    for (const auto& p : phase_resets) {
        put_f64(fp, p.time_seconds);
        put_u8 (fp, p.disabled ? 1 : 0);
    }

    return fp;
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
    ram_.clear();
    ram_bytes_ = 0;
    disk_index_.clear();
    disk_bytes_ = 0;
    if (!dir_.empty()) {
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }
    enabled_ = false;
}

bool RenderCache::lookup(const std::vector<uint8_t>& fp,
                         int channels, int sample_rate,
                         std::vector<float>& out) {
    if (!enabled_) {
        return false;
    }
    const uint64_t h = fnv1a64(fp);

    if (auto it = ram_.find(h); it != ram_.end()) {
        RamEntry& e = it->second;
        if (e.fingerprint == fp && e.channels == channels &&
            e.sample_rate == sample_rate) {
            e.seq = ++lru_seq_;
            out = e.samples;
            return true;
        }
    }

    if (auto it = disk_index_.find(h); it != disk_index_.end()) {
        DiskEntry& e = it->second;
        if (e.fingerprint == fp) {
            if (read_file(dir_ + "/" + e.filename, fp, channels, sample_rate, out)) {
                e.seq = ++lru_seq_;
                return true;
            }
            // Missing or invalid file: drop the entry and report a miss.
            std::error_code ec;
            std::filesystem::remove(dir_ + "/" + e.filename, ec);
            disk_bytes_ -= e.size_bytes;
            disk_index_.erase(it);
        }
    }

    return false;
}

void RenderCache::insert(const std::vector<uint8_t>& fp,
                         const std::vector<float>& samples,
                         int channels, int sample_rate, int64_t frame_count) {
    const bool prof = profile::enabled();
    const auto t0 = prof ? profile::now() : profile::Clock::time_point{};
    if (!enabled_) {
        return;
    }
    if (frame_count <= 0 || samples.empty() || channels <= 0 || sample_rate <= 0) {
        return;
    }

    const uint64_t h = fnv1a64(fp);
    const bool to_ram =
        frame_count <= static_cast<int64_t>(sample_rate) * kRamMaxRenderSeconds;
    const bool inserted = to_ram
        ? insert_ram(h, fp, samples, channels, sample_rate)
        : insert_disk(h, fp, samples, channels, sample_rate, frame_count);
    if (prof && inserted) {
        const auto t1 = profile::now();
        std::fprintf(stderr,
            "[profile] cache_insert enabled=yes inserted=yes tier=%s ms=%.3f frames=%lld bytes=%llu\n",
            to_ram ? "ram" : "disk", profile::ms(t0, t1),
            static_cast<long long>(frame_count),
            static_cast<unsigned long long>(samples.size()) *
                static_cast<unsigned long long>(sizeof(float)));
    }
}

bool RenderCache::insert_ram(uint64_t h, const std::vector<uint8_t>& fp,
                             const std::vector<float>& samples,
                             int channels, int sample_rate) {
    const uint64_t bytes =
        static_cast<uint64_t>(samples.size()) * sizeof(float);

    if (auto it = ram_.find(h); it != ram_.end()) {
        ram_bytes_ -=
            static_cast<uint64_t>(it->second.samples.size()) * sizeof(float);
        ram_.erase(it);
    }
    if (bytes > kRamBudgetBytes) return false; // pathological single entry; skip

    evict_ram_until(kRamBudgetBytes - bytes);

    RamEntry e;
    e.fingerprint = fp;
    e.samples     = samples;
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
        ram_bytes_ -=
            static_cast<uint64_t>(victim->second.samples.size()) * sizeof(float);
        ram_.erase(victim);
    }
}

bool RenderCache::insert_disk(uint64_t h, const std::vector<uint8_t>& fp,
                              const std::vector<float>& samples,
                              int channels, int sample_rate,
                              int64_t frame_count) {
    const std::string fname = hex16(h) + ".f32";
    const std::string path  = dir_ + "/" + fname;

    if (auto it = disk_index_.find(h); it != disk_index_.end()) {
        disk_bytes_ -= it->second.size_bytes;
        disk_index_.erase(it);
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    uint64_t written = 0;
    if (!write_file(path, fp, samples, channels, sample_rate, frame_count, written))
        return false; // best-effort

    if (written > kDiskBudgetBytes) { // single entry exceeds the whole budget
        std::error_code ec;
        std::filesystem::remove(path, ec);
        return false;
    }

    evict_disk_until(kDiskBudgetBytes - written);

    DiskEntry e;
    e.fingerprint = fp;
    e.filename    = fname;
    e.size_bytes  = written;
    e.seq         = ++lru_seq_;
    disk_index_[h] = std::move(e);
    disk_bytes_   += written;
    return true;
}

void RenderCache::evict_disk_until(uint64_t target_max) {
    while (disk_bytes_ > target_max && !disk_index_.empty()) {
        auto victim = disk_index_.begin();
        for (auto it = disk_index_.begin(); it != disk_index_.end(); ++it)
            if (it->second.seq < victim->second.seq) victim = it;
        std::error_code ec;
        std::filesystem::remove(dir_ + "/" + victim->second.filename, ec);
        disk_bytes_ -= victim->second.size_bytes;
        disk_index_.erase(victim);
    }
}

bool RenderCache::read_file(const std::string& path,
                            const std::vector<uint8_t>& want_fp,
                            int channels, int sample_rate,
                            std::vector<float>& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;

    bool ok = false;
    do {
        char magic[4];
        if (std::fread(magic, 1, 4, f) != 4) break;
        if (std::memcmp(magic, kMagic, 4) != 0) break;

        uint32_t ver = 0, fplen = 0;
        int32_t  ch = 0, sr = 0;
        int64_t  frames = 0;
        if (std::fread(&ver,    sizeof ver,    1, f) != 1) break;
        if (ver != kFileVersion) break;
        if (std::fread(&ch,     sizeof ch,     1, f) != 1) break;
        if (std::fread(&sr,     sizeof sr,     1, f) != 1) break;
        if (std::fread(&frames, sizeof frames, 1, f) != 1) break;
        if (std::fread(&fplen,  sizeof fplen,  1, f) != 1) break;

        if (ch != channels || sr != sample_rate) break;
        if (frames < 0 || ch <= 0) break;

        std::vector<uint8_t> fp(fplen);
        if (fplen && std::fread(fp.data(), 1, fplen, f) != fplen) break;
        if (fp != want_fp) break; // exact confirm: a hash collision lands here

        const size_t n =
            static_cast<size_t>(frames) * static_cast<size_t>(ch);
        out.resize(n);
        if (n && std::fread(out.data(), sizeof(float), n, f) != n) {
            out.clear();
            break;
        }
        ok = true;
    } while (false);

    std::fclose(f);
    return ok;
}

bool RenderCache::write_file(const std::string& path,
                             const std::vector<uint8_t>& fp,
                             const std::vector<float>& samples,
                             int channels, int sample_rate,
                             int64_t frame_count, uint64_t& out_bytes) {
    (void)frame_count;
    const std::string tmp = path + ".tmp";
    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;

    bool ok = false;
    do {
        if (std::fwrite(kMagic, 1, 4, f) != 4) break;
        const uint32_t ver   = kFileVersion;
        const int32_t  ch    = channels, sr = sample_rate;
        const int64_t  fr    = frame_count;
        const uint32_t fplen = static_cast<uint32_t>(fp.size());
        if (std::fwrite(&ver,   sizeof ver,   1, f) != 1) break;
        if (std::fwrite(&ch,    sizeof ch,    1, f) != 1) break;
        if (std::fwrite(&sr,    sizeof sr,    1, f) != 1) break;
        if (std::fwrite(&fr,    sizeof fr,    1, f) != 1) break;
        if (std::fwrite(&fplen, sizeof fplen, 1, f) != 1) break;
        if (fplen && std::fwrite(fp.data(), 1, fplen, f) != fplen) break;
        if (!samples.empty() &&
            std::fwrite(samples.data(), sizeof(float), samples.size(), f)
                != samples.size())
            break;
        std::fflush(f);
        ::fsync(::fileno(f));
        ok = true;
    } while (false);

    std::fclose(f);
    if (!ok) {
        std::error_code ec;
        std::filesystem::remove(tmp, ec);
        return false;
    }

    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return false;
    }

    out_bytes = 4 + sizeof(uint32_t) + sizeof(int32_t) * 2 + sizeof(int64_t)
              + sizeof(uint32_t) + fp.size()
              + samples.size() * sizeof(float);
    return true;
}
