#pragma once

#include <string>

// Typed view of the seven engine-relevant settings keys. Promoted onto
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
    bool        limiter                  = true;
    double      phase_reset_offset_hops  = 1.0;
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
    Limiter,
    PhaseResetOffsetHops,
};
