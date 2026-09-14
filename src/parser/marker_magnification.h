#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// THE MARKER MAGNIFICATION — one range, one grammar, one spelling
// (architect approval 2026-09-14, the frozen touch this header lands under).
//
// A WARP marker may carry a MAGNIFICATION: a count of waveform-picture
// doublings in [0, kMarkerMagnificationMax], or BLANK, which means INHERIT.
// It is serialized as the right half of the warp line's comment,
// ` //<measure>,<magnification>` — the split, the comma rule and the three
// comment shapes are stated once at split_marker_comment (marker_measure.h),
// the comment's one owner. Phase resets carry no magnification (and no
// measure) — architect 2026-09-14.
//
// THE GRAMMAR is a single ASCII digit `0`..`4`: no sign, no leading zero, no
// whitespace, exactly one byte, so each value has exactly one spelling. A
// blank field is not a token — it is the empty right half of the comment, and
// no validator here reads it. Anything else is refused: ADVERSARIAL at load
// (load-fatal, first error only, identically in both binaries).
//
// THE FIELD IS AUTHORED CONTENT AND NOT A RENDER INPUT: no sample, no
// playback path and no render reads it, and MarkerForRender carries it no
// more than it carries a measure, so a magnification cannot move a render
// key.
//
// While the settings key `waveform_magnification_level` still stands, its
// range owner (kWaveformMagnificationLevelMax, settings_file.h) and this one
// must be the same number — a static_assert in warpmarkers_parse.cpp pins it.
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
