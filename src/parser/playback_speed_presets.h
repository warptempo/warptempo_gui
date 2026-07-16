#pragma once

// The complete playback-speed vocabulary. The GUI authors playback_speed
// through the settings editor (`:playback_speed=<value>`), the sole
// authoring surface, which red-flashes any off-preset value at commit. This
// table is the single source of truth for the on-disk vocabulary — the ten
// one-decimal speeds 0.1..1.0 — and for the .settings schema's on-disk
// acceptance check. It lives parser-side because the whole-settings schema
// (settings_file.h) enforces preset membership in BOTH products — a sidecar
// set is loadable in the GUI and the CLI, or in neither.
//
// Membership is by exact TEXT: each pair carries the one-decimal spelling the
// writer emits (%.1f: "0.1".."0.9", "1.0") alongside the nearest-float value
// the live store takes. The schema matches the on-disk byte string against
// `text` and, on a hit, adopts `value` — so "0.7" is the one accepted
// spelling of that speed, while ".7", "0.70", and "00.7" are not.
struct PlaybackSpeedPreset {
    const char* text;
    float       value;
};

inline constexpr PlaybackSpeedPreset kPlaybackSpeedPresets[] = {
    { "0.1", 0.1f },
    { "0.2", 0.2f },
    { "0.3", 0.3f },
    { "0.4", 0.4f },
    { "0.5", 0.5f },
    { "0.6", 0.6f },
    { "0.7", 0.7f },
    { "0.8", 0.8f },
    { "0.9", 0.9f },
    { "1.0", 1.0f },
};
