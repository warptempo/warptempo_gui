#pragma once

// The complete playback-speed vocabulary. The GUI authors playback_speed
// exclusively through the Shift+0..9 presets: Shift+0 is 1.00 (normal speed)
// and Shift+1..9 are 0.10..0.90, the slow-down precision-finetune tool.
// Indexed by the pressed digit, so kPlaybackSpeedPresets[d] is the speed for
// Shift+d. This table is the single source of truth for both the Shift+digit
// dispatch and the .settings schema's on-disk acceptance check. It lives
// parser-side because the whole-settings schema (settings_file.h) enforces
// preset membership in BOTH products — a sidecar set is loadable in the GUI
// and the CLI, or in neither.
inline constexpr float kPlaybackSpeedPresets[] = {
    1.0f,  // Shift+0
    0.1f,  // Shift+1
    0.2f,  // Shift+2
    0.3f,  // Shift+3
    0.4f,  // Shift+4
    0.5f,  // Shift+5
    0.6f,  // Shift+6
    0.7f,  // Shift+7
    0.8f,  // Shift+8
    0.9f,  // Shift+9
};

// True iff `v` is bit-exactly one of the preset speeds. Used by the .settings
// reader to reject any off-preset hand-edited value. Exact float equality is
// sound: each preset is the nearest float of a short decimal, and the writer
// emits that same short decimal (%.1f — every preset is one-decimal), so
// parsing the written text back
// yields the identical float — no tolerance is needed or wanted.
inline bool is_playback_speed_preset(float v) {
    for (const float preset : kPlaybackSpeedPresets) {
        if (preset == v) return true;
    }
    return false;
}
