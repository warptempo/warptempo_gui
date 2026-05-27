#pragma once

#include <string>

// Typed view of the ten engine-relevant settings keys. Promoted onto
// AppState as the live authoring store; carried by RenderRequest as the
// only engine-settings carrier. Member-default-initialized to the values
// format_default_settings_template emits (modulo title, which defaults to
// empty and is overwritten at source load from the stem).
struct EngineSettings {
    std::string title;
    std::string output_format            = "wav";
    double      scale                    = 1.0;
    int         bpm                      = 0;     // informational only; unused by engine/GUI
    int         N                        = 4096;
    int         fftw_threads             = 16;
    bool        limiter_enabled_on_render = true;
    double      phase_reset_offset_hops  = 1.0;
    double      limiter_ceiling          = -0.3;     // dBFS, expected <= 0
    double      limiter_attack_ms        = 0.25;
    double      limiter_release_ms       = 0.5;
};

// Identifier for one field of EngineSettings. Stored on each
// EnginePassthrough entry of kSettingsOrder (settings_io.cpp) so the
// serializer can switch over the typed field at write time without a
// string-key lookup. Order is incidental — only the value is read.
enum class EngineField {
    Title,
    Scale,
    Bpm,
    OutputFormat,
    N,
    FftwThreads,
    LimiterEnabledOnRender,
    PhaseResetOffsetHops,
    LimiterCeiling,
    LimiterAttackMs,
    LimiterReleaseMs,
};
