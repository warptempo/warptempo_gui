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
// header. THE INVENTORY, RE-GREPED 2026-09-17 (it read "thirteen translation
// units, nine under src/gui plus src/cli/cli_main.cpp" from 2026-07-30, and
// the src/gui consumers were consolidated away under it): SEVEN translation
// units include this header, all of them DIRECTLY (no header re-exports it) —
// FOUR under src/gui (paint_handler, input_render_dispatch,
// phase_reset_propagate and, since 2026-09-17, car_transport, which spells the
// head unit's trim-span line) and THREE under src/parser
// (marker_store_validate, warp_frame_map_build,
// phase_reset_frame_map_build), the only FROZEN-directory consumers; the CLI's
// own main no longer includes it at all. Because the
// parser sources are compiled into both products, that is ten object
// compilations. The helpers now live tool-locally in
// tools/migrate_sidecar_to_frames.cpp, their only consumer and the sole
// legacy-sidecar conversion route, beside the tool's other local mirrors. Do not
// reintroduce a parse direction here: an old-format sidecar is load-fatal in both
// products by ruling, so a product-side timestamp reader would have no honest
// caller.

inline std::string format_timestamp(double seconds) {
    if (seconds < 0) seconds = 0;
    // Hard cap: 59:59.999. The format carries EXACTLY TWO minute digits, and a
    // longer source is TRUNCATED rather than given a third (architect
    // 2026-08-01: "ok to truncate larger — kdenlive also truncates long msgs").
    //
    // THE THIRD DIGIT EXISTED FOR ONE COMMIT and is recorded here so it is not
    // rediscovered as an oversight: the container does allow a longer source (a
    // RIFF data chunk tops out near 4 GiB, ~16232 s = 270:32 at 44.1 kHz stereo
    // 24-bit), and this cap does read wrong past an hour. The architect ruled
    // that cost preferable to a wider clock — the bottom row's timestamp section
    // is a fixed width and the third digit spends it on a case his material
    // never reaches. Display-only clamping; no persisted value flows through
    // this function, and nothing but the display is affected either way.
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
