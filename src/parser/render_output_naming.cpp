#include "render_output_naming.h"

std::vector<std::string> render_output_extensions(
    const std::string& output_format) {
    if (output_format == "warptempo_maps")
        return {".warpframemap", ".phaseresetframemap"};
    if (output_format == "generic_map")
        return {".warpframemap"};
    if (output_format == "midi_map")
        return {".miditempomap"};
    return {".wav"};
}

std::filesystem::path render_output_directory(
    const std::string& source_audio_path) {
    std::filesystem::path dir =
        std::filesystem::path(source_audio_path).parent_path();
    if (dir.empty()) dir = std::filesystem::path(".");
    return dir;
}

std::string render_output_stem(const EngineSettings& es,
                               const std::string& source_stem) {
    // Map artifacts are project files named by the source stem, like the
    // authoring sidecars beside the source. Only the wav deliverable is
    // title-named, so the clean-float prefix is wav-scoped by construction.
    if (es.output_format != "wav") return source_stem;
    return es.limiter ? es.title : ("limiter=false;" + es.title);
}

std::vector<std::filesystem::path> compose_render_output_paths(
    const std::filesystem::path& dir,
    const std::string& stem,
    const std::string& output_format) {
    std::vector<std::filesystem::path> paths;
    for (const std::string& ext : render_output_extensions(output_format))
        paths.push_back(dir / (stem + ext));
    return paths;
}
