#pragma once

#include <string>

// Render-environment identity: per-library digests of the four render-relevant
// shared libraries as actually mapped into the running process — libm.so.6,
// libmvec.so.1, libfftw3.so.3, libfftw3_threads.so.3. The app records these in
// the `.settings` sidecar at save time and warns at load when they changed
// (detection, not prevention: the system rolls freely, and a mismatch render is
// fully valid at parser/engine level — the warning never blocks or invalidates
// anything).
//
// Each digest is a project-local FNV-style 64-bit fold of the library's STAT
// IDENTITY — device, size, mtime in nanoseconds, and inode of the file as
// actually mapped into this process — rendered as exactly 16 lowercase hex
// digits, the same canonical spelling the settings schema enforces for the four
// *_hash keys. A real library upgrade moves at least one of those fields, so it
// is detected; a benign metadata-only change (a same-version reinstall that
// only touches mtime) reads as changed and re-renders — the safe (conservative)
// direction. There is NO on-disk cache: the four stats are the whole cost, and
// no file is opened or hashed by its bytes.
//
// A library absent from /proc/self/maps (never the case by construction in
// either product) takes the fixed sentinel `0000000000000000` — reserved by
// convention, not mathematically disjoint from a real digest; the 64-bit
// non-cryptographic scheme already accepts collision risk, so a stored real
// digest reliably (not certainly) mismatches the sentinel, the safe direction.
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
