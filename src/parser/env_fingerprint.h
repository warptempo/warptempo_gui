#pragma once

#include <string>

// Render-environment identity: per-library content digests of the four
// render-relevant shared libraries as actually mapped into the running
// process — libm.so.6, libmvec.so.1, libfftw3.so.3, libfftw3_threads.so.3.
// The app records these in the `.settings` sidecar at save time and warns at
// load when they changed (detection, not prevention: the system rolls freely,
// and a mismatch render is fully valid at parser/engine level — the warning
// never blocks or invalidates anything). Content hashing rather than version
// strings is deliberate: Arch rebuilds the same glibc version with a new
// toolchain and only bytes tell the truth.
//
// Each digest is FNV-1a 64-bit over the library file's full contents (change
// detection, not security), rendered as exactly 16 lowercase hex digits —
// the same canonical spelling the settings schema enforces for the four
// *_hash keys. A library absent from /proc/self/maps (never the case by
// construction in either product) takes the fixed sentinel
// `0000000000000000`, the safe direction: it differs from any real hash.
//
// The warm path is <1 ms via a stat-validated text cache at
// `<XDG cache>/warptempo_gui/env_hash_cache` (one line per resolved library:
// name, size, mtime in nanoseconds, inode, hash). A missing, unreadable, or
// malformed cache is never an error — all four libraries are silently
// rehashed and the cache rewritten atomically (unique temp file + rename, so
// concurrent GUI/CLI writers last-writer-win with correct content). The
// RenderCache dead-PID sweep removes only all-digit per-pid DIRECTORIES
// under the same parent and cannot touch this top-level file.
struct RenderEnvHashes {
    std::string libm;
    std::string libmvec;
    std::string fftw3;
    std::string fftw3_threads;
};

// Computed once per process (function-local static — the mapped libraries
// cannot change within a process lifetime).
const RenderEnvHashes& compute_render_env_hashes();

// Human-readable library versions for the CLI's mismatch stderr line.
std::string render_env_glibc_version();  // gnu_get_libc_version()
std::string render_env_fftw_version();   // fftw's exported fftw_version string
