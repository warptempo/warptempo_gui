#pragma once

// The complete playback-speed vocabulary. The GUI authors playback_speed
// through the settings editor (`:playback_speed=<value>`), the sole
// authoring surface, which red-flashes any off-preset value at commit. This
// table is the single source of truth for the on-disk vocabulary — the ten
// one-decimal speeds 0.10..1.00 — and for the .settings schema's on-disk
// acceptance check. It lives parser-side because the whole-settings schema
// (settings_file.h) enforces preset membership in BOTH products — a sidecar
// set is loadable in the GUI and the CLI, or in neither.
inline constexpr float kPlaybackSpeedPresets[] = {
    1.0f,
    0.1f,
    0.2f,
    0.3f,
    0.4f,
    0.5f,
    0.6f,
    0.7f,
    0.8f,
    0.9f,
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
