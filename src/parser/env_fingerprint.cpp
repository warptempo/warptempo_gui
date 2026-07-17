#include "env_fingerprint.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include <gnu/libc-version.h>
#include <sys/stat.h>
#include <unistd.h>

// fftw3.h declares this inside extern "C"; declaring it directly keeps the
// parser TU free of the FFTW header (only the exported version string is
// read here).
extern "C" const char fftw_version[];

namespace {

// The four render-relevant libraries, in RenderEnvHashes field order.
constexpr int kLibCount = 4;
constexpr const char* kLibNames[kLibCount] = {
    "libm.so.6",
    "libmvec.so.1",
    "libfftw3.so.3",
    "libfftw3_threads.so.3",
};

// Fixed 16-hex sentinel for a library absent from /proc/self/maps. Reserved by
// convention: a real content hash of all-zero is astronomically unlikely, and
// the 64-bit non-cryptographic scheme already accepts collision risk, so a
// stored real hash reliably mismatches the sentinel (the safe direction).
constexpr char kAbsentSentinel[] = "0000000000000000";

std::string hex16(uint64_t h) {
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx",
                  static_cast<unsigned long long>(h));
    return std::string(buf);
}

bool is_hash_spelling(const std::string& s) {
    if (s.size() != 16) return false;
    for (char c : s) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

// Resolve each library's on-disk path by parsing /proc/self/maps: the FIRST
// mapping whose basename starts with the library name wins. One pass fills
// all four slots. An unresolved slot stays empty (→ the sentinel hash).
void resolve_mapped_paths(std::string (&paths)[kLibCount]) {
    std::ifstream maps("/proc/self/maps");
    if (!maps) return;
    std::string line;
    while (std::getline(maps, line)) {
        // The pathname field starts at the first '/' (library mappings
        // always carry an absolute path; anonymous and pseudo mappings
        // don't match and fall through).
        const size_t slash = line.find('/');
        if (slash == std::string::npos) continue;
        const std::string path = line.substr(slash);
        const size_t base = path.find_last_of('/');
        const std::string basename = path.substr(base + 1);
        for (int i = 0; i < kLibCount; ++i) {
            if (!paths[i].empty()) continue;
            if (basename.rfind(kLibNames[i], 0) == 0) paths[i] = path;
        }
    }
}

// Project-local FNV-style content hash over the file's full contents,
// streamed: xor-then-multiply per byte with the standard 64-bit FNV prime.
// The offset basis below (1469598103934665603) is NOT the canonical FNV-1a-64
// basis (14695981039346656037, one digit longer) — it is a project-fixed seed
// inherited from the render-cache helper. That is immaterial: this is change
// detection, not security, and a fixed seed makes identical bytes hash
// identically across processes. Do not "correct" the basis — every stored
// hash would change and fire every project's mismatch prompt once for nothing.
bool content_hash64_file(const std::string& path, uint64_t& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    uint64_t h = 1469598103934665603ull;
    unsigned char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) {
        for (size_t i = 0; i < n; ++i) {
            h ^= buf[i];
            h *= 1099511628211ull;
        }
    }
    const bool ok = (std::ferror(f) == 0);
    std::fclose(f);
    if (!ok) return false;
    out = h;
    return true;
}

// (device, size, mtime in nanoseconds, inode) — the stat identity the cache
// validates against. Inode numbers are unique only within a device, so st_dev
// joins the triple; the resolved library path (stored on CacheEntry, not here)
// completes the identity so a shared/relocated $XDG_CACHE_HOME across
// containers/chroots or a filesystem replacement cannot alias one library's
// cached hash onto another.
struct LibIdentity {
    uint64_t dev      = 0;
    uint64_t size     = 0;
    int64_t  mtime_ns = 0;
    uint64_t inode    = 0;
};

bool stat_identity(const std::string& path, LibIdentity& out) {
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0) return false;
    if (st.st_size < 0) return false;
    out.dev = static_cast<uint64_t>(st.st_dev);
    out.size = static_cast<uint64_t>(st.st_size);
    out.mtime_ns = static_cast<int64_t>(st.st_mtim.tv_sec) * 1000000000ll +
                   static_cast<int64_t>(st.st_mtim.tv_nsec);
    out.inode = static_cast<uint64_t>(st.st_ino);
    return true;
}

// XDG cache home, resolved exactly the way RenderCache::init does:
// $XDG_CACHE_HOME when set and non-empty, else $HOME/.cache, else no cache
// (the caller hashes without one). The cache FILE sits directly under
// <base>/warptempo_gui/ — a top-level file, not a per-pid directory, so the
// RenderCache dead-PID sweep (all-digit directories only) cannot touch it.
std::string cache_file_path() {
    std::string base;
    if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg && xdg[0]) {
        base = xdg;
    } else if (const char* home = std::getenv("HOME"); home && home[0]) {
        base = std::string(home) + "/.cache";
    } else {
        return {};
    }
    return base + "/warptempo_gui/env_hash_cache";
}

struct CacheEntry {
    bool        present = false;
    LibIdentity id;
    std::string hash;
    std::string path;   // resolved on-disk library path; part of identity
};

// Cache line format (one space-separated line per present library):
//
//     name dev size mtime_ns inode hash path
//
// The first six fields are whitespace-free tokens (soname, four integers, and
// the fixed 16-hex digest); the PATH IS LAST and is the whole remainder of the
// line, so a library path containing spaces round-trips unambiguously. Any
// older-format line (five fields before the hash, no dev/path) fails this
// stricter parse and takes the whole-cache-discard fallback below — the
// intended one-time migration.
//
// Read the whole cache into per-library slots keyed by library name. A
// missing, unreadable, or malformed cache is NEVER an error: any defect
// discards the whole cache silently (all four rehash and it rewrites).
void read_cache(const std::string& path, CacheEntry (&entries)[kLibCount]) {
    std::ifstream in(path);
    if (!in) return;
    CacheEntry parsed[kLibCount];
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string name, hash;
        uint64_t dev = 0, size = 0, inode = 0;
        int64_t mtime_ns = 0;
        if (!(ls >> name >> dev >> size >> mtime_ns >> inode >> hash) ||
            !is_hash_spelling(hash)) {
            return;  // malformed fixed fields: discard the whole cache
        }
        // The path is the remainder after the hash: skip the single
        // separating space, then take the rest of the line verbatim (spaces
        // included). A missing/empty remainder is malformed.
        std::string lib_path;
        std::getline(ls, lib_path);
        if (!lib_path.empty() && lib_path.front() == ' ')
            lib_path.erase(lib_path.begin());
        if (lib_path.empty()) {
            return;  // no path field: discard the whole cache
        }
        for (int i = 0; i < kLibCount; ++i) {
            if (name == kLibNames[i]) {
                parsed[i].present     = true;
                parsed[i].id.dev      = dev;
                parsed[i].id.size     = size;
                parsed[i].id.mtime_ns = mtime_ns;
                parsed[i].id.inode    = inode;
                parsed[i].hash        = hash;
                parsed[i].path        = lib_path;
            }
        }
    }
    for (int i = 0; i < kLibCount; ++i) entries[i] = parsed[i];
}

// Atomic whole-cache rewrite: unique (pid-suffixed) temp file + rename, so
// concurrent GUI/CLI writers are safe by rename semantics — last writer
// wins, and both wrote correct content. Best-effort: any failure is silent
// (the cache is a warm-path accelerator, never a correctness input).
void write_cache(const std::string& path,
                 const CacheEntry (&entries)[kLibCount]) {
    const size_t dir_end = path.find_last_of('/');
    if (dir_end != std::string::npos) {
        // Ensure <base>/warptempo_gui exists (mkdir of the leaf is enough:
        // the XDG base itself exists on any live system, and a failure here
        // just makes the open below fail silently).
        ::mkdir(path.substr(0, dir_end).c_str(), 0755);
    }
    const std::string tmp =
        path + ".tmp." + std::to_string(static_cast<long>(::getpid()));
    std::ofstream out(tmp, std::ios::trunc);
    if (!out) return;
    for (int i = 0; i < kLibCount; ++i) {
        if (!entries[i].present) continue;  // absent library: no line
        // name dev size mtime_ns inode hash path — path last (the remainder),
        // so a space-bearing library path round-trips (see read_cache).
        out << kLibNames[i] << ' ' << entries[i].id.dev << ' '
            << entries[i].id.size << ' ' << entries[i].id.mtime_ns << ' '
            << entries[i].id.inode << ' ' << entries[i].hash << ' '
            << entries[i].path << '\n';
    }
    out.close();
    if (!out) {
        ::unlink(tmp.c_str());
        return;
    }
    if (::rename(tmp.c_str(), path.c_str()) != 0) {
        ::unlink(tmp.c_str());
    }
}

RenderEnvHashes compute_impl() {
    std::string paths[kLibCount];
    resolve_mapped_paths(paths);

    const std::string cache_path = cache_file_path();
    CacheEntry cached[kLibCount];
    if (!cache_path.empty()) read_cache(cache_path, cached);

    std::string hashes[kLibCount];
    CacheEntry fresh[kLibCount];
    bool rewrite = false;
    for (int i = 0; i < kLibCount; ++i) {
        if (paths[i].empty()) {
            // Not mapped (unreachable by construction in both products):
            // the sentinel, no cache line.
            hashes[i] = kAbsentSentinel;
            continue;
        }
        LibIdentity id;
        if (!stat_identity(paths[i], id)) {
            hashes[i] = kAbsentSentinel;
            continue;
        }
        if (cached[i].present && cached[i].id.dev == id.dev &&
            cached[i].id.size == id.size &&
            cached[i].id.mtime_ns == id.mtime_ns &&
            cached[i].id.inode == id.inode &&
            cached[i].path == paths[i]) {
            // Full (dev, size, mtime, inode, path) match: the warm path, no
            // read. dev+path guard against inode aliasing across devices and a
            // shared/relocated cache dir naming a different on-disk object.
            hashes[i] = cached[i].hash;
            fresh[i] = cached[i];
            continue;
        }
        uint64_t h = 0;
        if (!content_hash64_file(paths[i], h)) {
            hashes[i] = kAbsentSentinel;  // unreadable: sentinel, no line
            rewrite = true;
            continue;
        }
        hashes[i] = hex16(h);
        fresh[i].present = true;
        fresh[i].id = id;   // pre-hash stat, revalidated on the next run
        fresh[i].hash = hashes[i];
        fresh[i].path = paths[i];
        rewrite = true;
    }

    if (rewrite && !cache_path.empty()) write_cache(cache_path, fresh);

    RenderEnvHashes out;
    out.libm          = hashes[0];
    out.libmvec       = hashes[1];
    out.fftw3         = hashes[2];
    out.fftw3_threads = hashes[3];
    return out;
}

}  // namespace

const RenderEnvHashes& compute_render_env_hashes() {
    // Once per process: the mapped libraries cannot change within a process
    // lifetime, so the first caller pays the (cached) computation and every
    // later call is a reference return.
    static const RenderEnvHashes hashes = compute_impl();
    return hashes;
}

std::string render_env_glibc_version() {
    return gnu_get_libc_version();
}

std::string render_env_fftw_version() {
    return fftw_version;
}
