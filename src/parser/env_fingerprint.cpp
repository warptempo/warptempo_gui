#include "env_fingerprint.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>

#include <gnu/libc-version.h>
#include <sys/stat.h>

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
// convention: a real stat-identity fold landing on all-zero is astronomically
// unlikely, and the 64-bit non-cryptographic scheme already accepts collision
// risk, so a stored real digest reliably mismatches the sentinel (the safe
// direction).
constexpr char kAbsentSentinel[] = "0000000000000000";

std::string hex16(uint64_t h) {
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx",
                  static_cast<unsigned long long>(h));
    return std::string(buf);
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

// (device, size, mtime in nanoseconds, inode) — the stat identity each digest
// folds. Inode numbers are unique only within a device, so st_dev joins the
// triple; a real library upgrade moves at least one of these fields, so the
// fold changes and the render environment reads as changed.
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

// Project-local FNV-style 64-bit fold of a library's stat identity: the 32
// bytes of the four integer fields (dev, size, mtime_ns, inode) in that fixed
// order, xor-then-multiply per byte, seeded with the offset basis. Native byte
// order is fine — the single-host `-march=native` build never ships a digest
// off the machine that produced it, and a fixed field order plus a fixed seed
// make the same stat identity fold identically across processes. The offset
// basis below (1469598103934665603) is NOT the canonical FNV-1a-64 basis
// (14695981039346656037, one digit longer) — it is a project-fixed seed shared
// with the render-cache helper. That is immaterial: this is change detection,
// not security. Do not "correct" the basis — every stored hash would change and
// fire every project's mismatch prompt once for nothing.
uint64_t fold_identity(const LibIdentity& id) {
    const uint64_t fields[4] = {
        id.dev,
        id.size,
        static_cast<uint64_t>(id.mtime_ns),
        id.inode,
    };
    const unsigned char* bytes =
        reinterpret_cast<const unsigned char*>(fields);
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < sizeof fields; ++i) {
        h ^= bytes[i];
        h *= 1099511628211ull;
    }
    return h;
}

RenderEnvHashes compute_impl() {
    std::string paths[kLibCount];
    resolve_mapped_paths(paths);

    std::string hashes[kLibCount];
    for (int i = 0; i < kLibCount; ++i) {
        if (paths[i].empty()) {
            // Not mapped (unreachable by construction in both products): the
            // sentinel.
            hashes[i] = kAbsentSentinel;
            continue;
        }
        LibIdentity id;
        if (!stat_identity(paths[i], id)) {
            hashes[i] = kAbsentSentinel;  // unstattable: sentinel
            continue;
        }
        hashes[i] = hex16(fold_identity(id));
    }

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
    // lifetime, so the first caller runs the computation and every later call
    // is a reference return.
    static const RenderEnvHashes hashes = compute_impl();
    return hashes;
}

std::string render_env_glibc_version() {
    return gnu_get_libc_version();
}

std::string render_env_fftw_version() {
    return fftw_version;
}
