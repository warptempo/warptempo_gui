#pragma once

#include <clocale>
#include <cstdio>
#include <cstring>

// The .warpmarkers / .phaseresetmarkers / .settings readers and writers,
// and the map artifact writers, format and parse numbers via the C numeric
// locale and require it to be "C". Nothing in this program calls setlocale;
// this guard refuses to run if a linked library ever changes that, so the
// failure is loud instead of a silent misparse. Call first thing in every
// main. Returns true when safe to proceed.
inline bool verify_c_numeric_locale(const char* program_name) {
    const char* lc = std::setlocale(LC_NUMERIC, nullptr);
    if (lc && std::strcmp(lc, "C") == 0) return true;
    std::fprintf(stderr,
        "%s: Numeric locale is '%s', not 'C'; number parsing and "
        "formatting would be corrupted; refusing to run\n",
        program_name, lc ? lc : "(null)");
    return false;
}
