#pragma once

#include <string>

// Typed view of the five engine-relevant settings keys. Promoted onto
// AppState as the live authoring store; carried by RenderRequest as the
// only engine-settings carrier. Member-default-initialized to the values
// format_default_settings_template emits (modulo title, which defaults to
// empty and is overwritten at source load from the stem). N and the
// phase-reset lead-in are no longer fields here — both are locked to
// canonical constants in render_pipeline.cpp (N = 4096; lead-in = one
// synthesis hop).
struct EngineSettings {
    std::string title;
    std::string output_format            = "wav";
    double      scale                    = 1.0;
    std::string bpm;   // BPM render descriptor, e.g.
                       // "36 beats @ 220 bpm from 00:32.008 to 00:46.562".
                       // Empty when no BPM render is the current baseline.
                       // Informational only — no engine or GUI effect.
    bool        limiter                  = true;
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
    Limiter,
};
