#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// THE MAGNIFICATION LEVEL — one range, one grammar, one spelling
// (architect approval 2026-09-14, the frozen touch this header landed under;
// retold under architect approval 2026-09-15).
//
// A magnification LEVEL is a count of waveform-picture doublings in
// [0, kMarkerMagnificationMax]. No marker field carries one: the warp
// marker's comment is the measure alone.
//
// THE GRAMMAR is a single ASCII digit `0`..`4`: no sign, no leading zero, no
// whitespace, exactly one byte, so each value has exactly one spelling.
// Anything else is refused: ADVERSARIAL at load (load-fatal, first error
// only, identically in both binaries).
//
// A LEVEL IS DISPLAY-ONLY AND NOT A RENDER INPUT: no sample, no playback path
// and no render reads it, so a level cannot move a render key.
//
// THIS IS THE ONE RANGE OWNER: the waveform magnification level settings key
// and its range left the schema 2026-09-14 (architect approval 2026-09-14).
inline constexpr int kMarkerMagnificationMax = 4;

// The one byte bound: a single digit.
inline constexpr size_t kMaxMarkerMagnificationBytes = 1;

inline constexpr bool is_marker_magnification(int64_t v) {
    return v >= 0 && v <= kMarkerMagnificationMax;
}

// Parse one magnification token. Returns true on success; on failure returns
// false and sets `error_out` to a one-line diagnostic in the readers' voice.
// The grammar's one implementation.
inline bool parse_marker_magnification(std::string_view text, uint8_t& out,
                                       std::string& error_out) {
    if (text.size() != kMaxMarkerMagnificationBytes || text[0] < '0' ||
        text[0] > '9' || !is_marker_magnification(text[0] - '0')) {
        error_out = "magnification must be a single digit 0.." +
                    std::to_string(kMarkerMagnificationMax);
        return false;
    }
    out = static_cast<uint8_t>(text[0] - '0');
    return true;
}

// The canonical spelling — the writer half, round-tripping byte for byte.
inline std::string format_marker_magnification(uint8_t v) {
    return std::string(1, static_cast<char>('0' + v));
}
