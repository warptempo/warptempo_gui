#pragma once

#include <cmath>
#include <cstdio>
#include <string>

// MM:SS.mmm is a DISPLAY-ONLY rendering. Authored positions are whole
// source frames held in int64_t end to end (serialized as canonical
// integer text via frame_format.h); no file format carries timestamps.
// Display surfaces derive their text as
// format_timestamp(frame / sample_rate).
//
// WRITE-ONLY BY DESIGN, and that is why this header is <regex>-free (architect
// 2026-07-30). Nothing in the product ever READS a timestamp — the two legacy
// PARSE helpers that used to live here, is_valid_timestamp_format and
// parse_timestamp, had zero call sites in the GUI, parser, CLI and prepost, and
// their std::regex dragged that library's headers into EVERY consumer of this
// header. The inventory, derived from the compiler's own depend files rather
// than by eye: THIRTEEN translation units include this header, all of them
// DIRECTLY (no header re-exports it) — nine under src/gui, src/cli/cli_main.cpp,
// and THREE under src/parser (marker_store_validate, warp_frame_map_build,
// phase_reset_frame_map_build), the only FROZEN-directory consumers; because the
// parser sources are compiled into both products, that is sixteen object
// compilations. The helpers now live tool-locally in
// tools/migrate_sidecar_to_frames.cpp, their only consumer and the sole
// legacy-sidecar conversion route, beside the tool's other local mirrors. Do not
// reintroduce a parse direction here: an old-format sidecar is load-fatal in both
// products by ruling, so a product-side timestamp reader would have no honest
// caller.

inline std::string format_timestamp(double seconds) {
    if (seconds < 0) seconds = 0;
    // Hard cap: 59:59.999. The format carries two minute digits; movement-length
    // source never reaches this, and the cap keeps the display shape stable.
    // Display-only clamping — no persisted value flows through this function.
    if (seconds > 3599.999) seconds = 3599.999;
    long total_ms = static_cast<long>(std::nearbyint(seconds * 1000.0));
    const long m  = total_ms / 60000;
    total_ms     -= m * 60000;
    const long s  = total_ms / 1000;
    const long ms = total_ms - s * 1000;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%02ld:%02ld.%03ld", m, s, ms);
    return buf;
}
