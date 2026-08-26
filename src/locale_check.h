#pragma once

#include <clocale>
#include <cstdio>
#include <cstring>

// The .warpmarkers / .phaseresetmarkers / .settings readers and writers,
// and the map artifact writers, format and parse numbers via the C numeric
// locale and require it to be "C". On the Linux path nothing calls
// setlocale, and this guard refuses to run if a linked library ever changes
// that, so the failure is loud instead of a silent misparse. The one caller
// is the Android backend's android_main (platform_android.cpp), which pins
// LC_ALL to "C" before this guard runs: bionic starts a process in
// "C.UTF-8" rather than "C", numerically identical but a different name, so
// this guard would otherwise refuse at every launch; the pin makes the name
// match the truth instead of widening what the guard accepts, so the
// tripwire's job survives on Android too. Call first thing in every main.
// Returns true when safe to proceed.
inline bool verify_c_numeric_locale(const char* program_name) {
    const char* lc = std::setlocale(LC_NUMERIC, nullptr);
    if (lc && std::strcmp(lc, "C") == 0) return true;
    std::fprintf(stderr,
        "%s: Numeric locale is '%s', not 'C'; number parsing and "
        "formatting would be corrupted; refusing to run\n",
        program_name, lc ? lc : "(null)");
    return false;
}
